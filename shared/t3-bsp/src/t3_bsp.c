#include "app_config.h"
#include "system/includes.h"
#include "gpio.h"
#include "gpadc.h"
#include "asm/power/power_gate.h"
#include <math.h>

#include "t3_board.h"
#include "t3_bsp.h"

extern void go_mask_usb_updata(void);
extern int lcd_drv_backlight_ctrl(u8 percent);

static t3_sensor_inventory_t g_inventory;
static u8 g_bsp_initialized = 0;
static u8 g_motor_pwm_ok = 0;
static u8 g_keys_initialized = 0;
static s16 g_tp_last_x = 0;
static s16 g_tp_last_y = 0;
static u8  g_tp_probe_addr = 0;
static u8  g_tp_chip_id = 0;
static u16 g_tp_ack_cnt = 0;
static u8  g_tp_raw5[5] = {0};

/* --- Bit-banged open-drain Soft-I2C helper (shared by I2C0 touch & I2C1 sensors) --- */

typedef struct {
    u32 scl_io;
    u32 sda_io;
} t3_soft_i2c_bus_t;

static const t3_soft_i2c_bus_t s_i2c0 = { T3_TP_SCL_IO, T3_TP_SDA_IO };
static const t3_soft_i2c_bus_t s_i2c1 = { T3_I2C1_SCL_IO, T3_I2C1_SDA_IO };

static inline void soft_i2c_delay(void)
{
    udelay(5);
}

static inline void soft_i2c_scl_high(const t3_soft_i2c_bus_t *bus)
{
    gpio_set_mode(IO_PORT_SPILT(bus->scl_io), PORT_INPUT_PULLUP_10K);
}

static inline void soft_i2c_scl_low(const t3_soft_i2c_bus_t *bus)
{
    gpio_set_mode(IO_PORT_SPILT(bus->scl_io), PORT_OUTPUT_LOW);
}

static inline void soft_i2c_sda_high(const t3_soft_i2c_bus_t *bus)
{
    gpio_set_mode(IO_PORT_SPILT(bus->sda_io), PORT_INPUT_PULLUP_10K);
}

static inline void soft_i2c_sda_low(const t3_soft_i2c_bus_t *bus)
{
    gpio_set_mode(IO_PORT_SPILT(bus->sda_io), PORT_OUTPUT_LOW);
}

static inline int soft_i2c_sda_read(const t3_soft_i2c_bus_t *bus)
{
    return gpio_read(bus->sda_io) ? 1 : 0;
}

static void soft_i2c_bus_init(const t3_soft_i2c_bus_t *bus)
{
    soft_i2c_sda_high(bus);
    soft_i2c_scl_high(bus);
    soft_i2c_delay();
}

static void soft_i2c_start(const t3_soft_i2c_bus_t *bus)
{
    soft_i2c_sda_high(bus);
    soft_i2c_scl_high(bus);
    soft_i2c_delay();
    soft_i2c_sda_low(bus);
    soft_i2c_delay();
    soft_i2c_scl_low(bus);
    soft_i2c_delay();
}

static void soft_i2c_stop(const t3_soft_i2c_bus_t *bus)
{
    soft_i2c_sda_low(bus);
    soft_i2c_delay();
    soft_i2c_scl_high(bus);
    soft_i2c_delay();
    soft_i2c_sda_high(bus);
    soft_i2c_delay();
}

static int soft_i2c_write_byte(const t3_soft_i2c_bus_t *bus, u8 byte)
{
    for (int i = 7; i >= 0; i--) {
        if ((byte >> i) & 1U) {
            soft_i2c_sda_high(bus);
        } else {
            soft_i2c_sda_low(bus);
        }
        soft_i2c_delay();
        soft_i2c_scl_high(bus);
        soft_i2c_delay();
        soft_i2c_scl_low(bus);
    }
    soft_i2c_sda_high(bus);
    soft_i2c_delay();
    soft_i2c_scl_high(bus);
    soft_i2c_delay();
    int nack = soft_i2c_sda_read(bus);
    soft_i2c_scl_low(bus);
    soft_i2c_delay();
    return (nack == 0) ? 0 : -1;
}

static u8 soft_i2c_read_byte(const t3_soft_i2c_bus_t *bus, int send_ack)
{
    u8 val = 0;
    soft_i2c_sda_high(bus);
    for (int i = 7; i >= 0; i--) {
        soft_i2c_delay();
        soft_i2c_scl_high(bus);
        soft_i2c_delay();
        if (soft_i2c_sda_read(bus)) {
            val |= (u8)(1U << i);
        }
        soft_i2c_scl_low(bus);
    }
    if (send_ack) {
        soft_i2c_sda_low(bus);
    } else {
        soft_i2c_sda_high(bus);
    }
    soft_i2c_delay();
    soft_i2c_scl_high(bus);
    soft_i2c_delay();
    soft_i2c_scl_low(bus);
    soft_i2c_sda_high(bus);
    soft_i2c_delay();
    return val;
}

static int soft_i2c_read_reg(const t3_soft_i2c_bus_t *bus, u8 addr7, u8 reg, u8 *buf, u8 len)
{
    soft_i2c_start(bus);
    if (soft_i2c_write_byte(bus, (u8)(addr7 << 1)) != 0) {
        soft_i2c_stop(bus);
        return -1;
    }
    if (soft_i2c_write_byte(bus, reg) != 0) {
        soft_i2c_stop(bus);
        return -1;
    }
    soft_i2c_start(bus);
    if (soft_i2c_write_byte(bus, (u8)((addr7 << 1) | 1U)) != 0) {
        soft_i2c_stop(bus);
        return -1;
    }
    for (u8 i = 0; i < len; i++) {
        buf[i] = soft_i2c_read_byte(bus, i + 1U < len);
    }
    soft_i2c_stop(bus);
    return 0;
}

static int t3_i2c1_probe(u8 addr7)
{
    soft_i2c_start(&s_i2c1);
    int rc = soft_i2c_write_byte(&s_i2c1, (u8)(addr7 << 1));
    soft_i2c_stop(&s_i2c1);
    return rc;
}

static int soft_i2c_write_reg(const t3_soft_i2c_bus_t *bus, u8 addr7, u8 reg, const u8 *buf, u8 len)
{
    soft_i2c_start(bus);
    if (soft_i2c_write_byte(bus, (u8)(addr7 << 1)) != 0) {
        soft_i2c_stop(bus);
        return -1;
    }
    if (soft_i2c_write_byte(bus, reg) != 0) {
        soft_i2c_stop(bus);
        return -1;
    }
    for (u8 i = 0; i < len; i++) {
        if (soft_i2c_write_byte(bus, buf[i]) != 0) {
            soft_i2c_stop(bus);
            return -1;
        }
    }
    soft_i2c_stop(bus);
    return 0;
}

static int t3_i2c1_write_reg(u8 addr7, u8 reg, const u8 *buf, u8 len)
{
    return soft_i2c_write_reg(&s_i2c1, addr7, reg, buf, len);
}

static int t3_i2c1_read_reg(u8 addr7, u8 reg, u8 *buf, u8 len)
{
    return soft_i2c_read_reg(&s_i2c1, addr7, reg, buf, len);
}

void t3_bsp_get_touch(s16 *x, s16 *y, u8 *pressed)
{
    u8 buf[5] = {0};
    u8 st = 0;
    u8 addr = g_tp_probe_addr ? g_tp_probe_addr : T3_TP_I2C_ADDR;
    if (soft_i2c_read_reg(&s_i2c0, addr, 0x02, buf, sizeof(buf)) == 0) {
        g_tp_ack_cnt++;
        memcpy(g_tp_raw5, buf, 5);
        static u8 s_tp_was_pressed = 0;
        u8 fingers = buf[0] & 0x0FU;
        s16 rx = (s16)(((u16)(buf[1] & 0x0FU) << 8) | buf[2]);
        s16 ry = (s16)(((u16)(buf[3] & 0x0FU) << 8) | buf[4]);
        if (fingers == 1U && (rx != 0 || ry != 0)) {
            s16 sx = (s16)(((s32)rx * (s32)T3_LCD_WIDTH) / 320);
            s16 sy = (s16)((((s32)ry - 230) * (s32)T3_LCD_HEIGHT) / 138);
            if (sx < 0) sx = 0;
            if (sx >= T3_LCD_WIDTH) sx = T3_LCD_WIDTH - 1;
            if (sy < 0) sy = 0;
            if (sy >= T3_LCD_HEIGHT) sy = T3_LCD_HEIGHT - 1;
            if (s_tp_was_pressed) {
                sx = (s16)((g_tp_last_x + sx) / 2);
                sy = (s16)((g_tp_last_y + sy) / 2);
            }
            s_tp_was_pressed = 1;
            g_tp_last_x = sx;
            g_tp_last_y = sy;
            st = 1U;
        } else {
            s_tp_was_pressed = 0;
        }
    }
    if (x) *x = g_tp_last_x;
    if (y) *y = g_tp_last_y;
    if (pressed) *pressed = st;
}

void t3_bsp_get_touch_diag(u8 *probe_addr, u8 *int_lvl, u16 *ack_cnt, u8 *raw5, s16 *last_x, s16 *last_y)
{
    if (probe_addr) *probe_addr = g_tp_probe_addr;
    if (int_lvl) *int_lvl = gpio_read(T3_TP_INT_IO) ? 1U : 0U;
    if (ack_cnt) *ack_cnt = g_tp_ack_cnt;
    if (raw5) memcpy(raw5, g_tp_raw5, 5);
    if (last_x) *last_x = g_tp_last_x;
    if (last_y) *last_y = g_tp_last_y;
}

void t3_bsp_get_usb_diag(u8 out16[16])
{
    if (!out16) {
        return;
    }
    memcpy(&out16[0], g_tp_raw5, 5);
    out16[5] = g_tp_probe_addr;
    out16[6] = gpio_read(T3_TP_INT_IO) ? 1U : 0U;
    out16[7] = g_tp_chip_id;
    out16[8] = (u8)(g_tp_ack_cnt & 0xFFU);
    out16[9] = (u8)((g_tp_ack_cnt >> 8) & 0xFFU);
    out16[10] = (u8)(g_tp_last_x & 0xFF);
    out16[11] = (u8)((g_tp_last_x >> 8) & 0xFF);
    out16[12] = (u8)(g_tp_last_y & 0xFF);
    out16[13] = (u8)((g_tp_last_y >> 8) & 0xFF);
    out16[14] = (u8)(t3_bsp_scan_keys(NULL) & 0xFFU);
    out16[15] = 0xA5U;
}

void get_touch_x_y_status(u16 *x, u16 *y, u8 *status)
{
    s16 tx = 0, ty = 0;
    u8 st = 0;
    t3_bsp_get_touch(&tx, &ty, &st);
    if (x) *x = (u16)tx;
    if (y) *y = (u16)ty;
    if (status) *status = st;
}

static int t3_i2c1_read_u16_be(u8 addr7, u8 reg, u16 *out)
{
    u8 buf[2] = {0, 0};
    if (t3_i2c1_read_reg(addr7, reg, buf, 2) != 0) {
        return -1;
    }
    *out = (u16)(((u16)buf[0] << 8) | (u16)buf[1]);
    return 0;
}

static int t3_i2c1_write_u16_be(u8 addr7, u8 reg, u16 val)
{
    u8 buf[2] = {(u8)(val >> 8), (u8)(val & 0xFFU)};
    return t3_i2c1_write_reg(addr7, reg, buf, 2);
}

/* --- Public T3 BSP API --- */

void t3_bsp_probe_sensors(t3_sensor_inventory_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));

    u16 die_id = 0;
    if (t3_i2c1_read_u16_be(T3_INA226_ADDR, T3_INA226_REG_DIE_ID, &die_id) == 0) {
        out->ina226_die_id = die_id;
        out->ina226_ok = (die_id == T3_INA226_DIE_ID_VAL) ? 1 : 0;
        if (out->ina226_ok) {
            t3_i2c1_write_u16_be(T3_INA226_ADDR, T3_INA226_REG_CONFIG, T3_INA226_CFG_DEFAULT);
        }
    }

    u8 whoami = 0;
    if (t3_i2c1_read_reg(T3_SC7A20_ADDR, T3_SC7A20_REG_WHOAMI, &whoami, 1) == 0) {
        out->sc7a20_whoami = whoami;
        out->sc7a20_ok = (whoami == T3_SC7A20_WHOAMI_VAL) ? 1 : 0;
    }

    u8 bmp_id = 0;
    if (t3_i2c1_read_reg(T3_BMP280_ADDR, T3_BMP280_REG_ID, &bmp_id, 1) == 0) {
        out->bmp280_id = bmp_id;
        out->bmp280_ok = (bmp_id == T3_BMP280_ID_VAL) ? 1 : 0;
    }

    if (t3_i2c1_probe(T3_ENS220_ADDR) == 0) {
        u8 st = 0;
        if (t3_i2c1_read_reg(T3_ENS220_ADDR, T3_ENS220_REG_STATUS, &st, 1) == 0) {
            out->ens220_status = st;
        }
        out->ens220_ok = 1;
    }
}

void t3_bsp_init(void)
{
    if (g_bsp_initialized) {
        return;
    }
    g_bsp_initialized = 1;

    /* Ensure LCD panel power / CS-control line PA07 is driven high */
    gpio_set_mode(IO_PORT_SPILT(T3_LCD_PWR_IO), PORT_OUTPUT_HIGH);

    /* Haptic motor on IO_MT_PG @ 10 kHz, off initially */
    g_motor_pwm_ok = (power_gate_pwm_init(T3_MOTOR_PWM_IO, T3_MOTOR_PWM_FREQ, 0) == 0) ? 1 : 0;

    /* Reset CST816D capacitive touch (PA02) & init Soft-I2C0 (PA06/PA05) */
    gpio_set_mode(IO_PORT_SPILT(T3_TP_INT_IO), PORT_INPUT_PULLUP_10K);
    gpio_set_mode(IO_PORT_SPILT(T3_TP_RST_IO), PORT_OUTPUT_HIGH);
    mdelay(5);
    gpio_set_mode(IO_PORT_SPILT(T3_TP_RST_IO), PORT_OUTPUT_LOW);
    mdelay(10);
    gpio_set_mode(IO_PORT_SPILT(T3_TP_RST_IO), PORT_OUTPUT_HIGH);
    mdelay(150);
    soft_i2c_bus_init(&s_i2c0);
    {
        static const u8 tp_addrs[] = { 0x15, 0x38, 0x5D, 0x14 };
        for (u32 i = 0; i < sizeof(tp_addrs); i++) {
            soft_i2c_start(&s_i2c0);
            int rc = soft_i2c_write_byte(&s_i2c0, (u8)(tp_addrs[i] << 1));
            soft_i2c_stop(&s_i2c0);
            if (rc == 0) {
                g_tp_probe_addr = tp_addrs[i];
                break;
            }
        }
        if (g_tp_probe_addr == 0x15) {
            soft_i2c_read_reg(&s_i2c0, 0x15, 0xA7, &g_tp_chip_id, 1);
        }
    }

    soft_i2c_bus_init(&s_i2c1);
    t3_bsp_probe_sensors(&g_inventory);

    if (g_inventory.sc7a20_ok) {
        /* 100 Hz normal mode, X/Y/Z enabled (0x20 = 0x57); BDU + HR (0x23 = 0x88) */
        u8 sc7_ctrl1 = 0x57;
        u8 sc7_ctrl4 = 0x88;
        t3_i2c1_write_reg(T3_SC7A20_ADDR, 0x20, &sc7_ctrl1, 1);
        t3_i2c1_write_reg(T3_SC7A20_ADDR, 0x23, &sc7_ctrl4, 1);
    }
}

const t3_sensor_inventory_t *t3_bsp_get_inventory(void)
{
    return &g_inventory;
}

int t3_bsp_read_ina226(t3_meter_sample_t *out)
{
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (!g_inventory.ina226_ok) {
        return -1;
    }

    u16 raw_bus = 0;
    u16 raw_shunt_u = 0;
    if (t3_i2c1_read_u16_be(T3_INA226_ADDR, T3_INA226_REG_BUS, &raw_bus) != 0) {
        return -1;
    }
    if (t3_i2c1_read_u16_be(T3_INA226_ADDR, T3_INA226_REG_SHUNT, &raw_shunt_u) != 0) {
        return -1;
    }

    s16 raw_shunt = (s16)raw_shunt_u;
    s32 vbus_mv = (s32)(((s64)raw_bus * 16123LL) / 10000LL);
    s32 ibus_ma = ((s32)raw_shunt) / 2; /* 0.5 mA/LSB */
    s32 abs_ma = (ibus_ma >= 0) ? ibus_ma : -ibus_ma;
    s32 power_mw = (s32)(((s64)vbus_mv * (s64)abs_ma) / 1000LL);

    out->valid = 1;
    out->raw_bus = raw_bus;
    out->raw_shunt = raw_shunt;
    out->vbus_mv = vbus_mv;
    out->ibus_ma = ibus_ma;
    out->power_mw = power_mw;
    return 0;
}

int t3_bsp_read_sc7a20(t3_accel_sample_t *out)
{
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (!g_inventory.sc7a20_ok) {
        return -1;
    }
    u8 raw[6] = {0};
    if (t3_i2c1_read_reg(T3_SC7A20_ADDR, 0xA8, raw, sizeof(raw)) != 0) {
        return -1;
    }
    out->valid = 1;
    out->ax = (s16)(((u16)raw[1] << 8) | raw[0]) >> 4;
    out->ay = (s16)(((u16)raw[3] << 8) | raw[2]) >> 4;
    out->az = (s16)(((u16)raw[5] << 8) | raw[4]) >> 4;
    return 0;
}

static u8 g_k2_mode[19] = {0}; /* 0=unused, 1=active-LOW, 2=active-HIGH */

u32 t3_bsp_scan_keys(const char **first_pin_name)
{
    /* Bit 0: verified hardware button PB07 (RESET=PB07_08_0 in isd_config.ini).
       Bit 1: safe auto-probe across all unused GPIOs (active-LOW or active-HIGH). */
    static const u32 k2_pins[19] = {
        IO_PORTA_00, IO_PORTA_01, IO_PORTA_03, IO_PORTA_04,
        IO_PORTA_08, IO_PORTA_09, IO_PORTA_11, IO_PORTA_12,
        IO_PORTA_13, IO_PORTA_14, IO_PORTA_15,
        IO_PORTB_03, IO_PORTB_04, IO_PORTB_05, IO_PORTB_06, IO_PORTB_09,
        IO_PORTC_01, IO_PORTC_04, IO_PORTC_05
    };
    static const char *const k2_names[19] = {
        "PA00", "PA01", "PA03", "PA04",
        "PA08", "PA09", "PA11", "PA12",
        "PA13", "PA14", "PA15",
        "PB03", "PB04", "PB05", "PB06", "PB09",
        "PC01", "PC04", "PC05"
    };

    if (!g_keys_initialized) {
        gpio_set_mode(IO_PORT_SPILT(IO_PORTB_07), PORT_INPUT_PULLUP_10K);
        for (int i = 0; i < 19; i++) {
            g_k2_mode[i] = 0;
            gpio_set_mode(IO_PORT_SPILT(k2_pins[i]), PORT_INPUT_PULLUP_10K);
        }
        udelay(50);
        for (int i = 0; i < 19; i++) {
            if (gpio_read(k2_pins[i]) != 0) {
                g_k2_mode[i] = 1; /* idle-HIGH -> active-LOW button */
            } else {
                gpio_set_mode(IO_PORT_SPILT(k2_pins[i]), PORT_INPUT_PULLDOWN_10K);
                udelay(20);
                if (gpio_read(k2_pins[i]) == 0) {
                    g_k2_mode[i] = 2; /* idle-LOW -> active-HIGH button */
                }
            }
        }
        g_keys_initialized = 1;
    }

    u32 mask = 0;
    const char *name = NULL;
    if (gpio_read(IO_PORTB_07) == 0) {
        mask |= (1U << 0);
        name = "PB07";
    }
    for (int i = 0; i < 19; i++) {
        if (g_k2_mode[i] == 1 && gpio_read(k2_pins[i]) == 0) {
            mask |= (1U << 1);
            if (!name) name = k2_names[i];
            break;
        }
        if (g_k2_mode[i] == 2 && gpio_read(k2_pins[i]) != 0) {
            mask |= (1U << 1);
            if (!name) name = k2_names[i];
            break;
        }
    }
    if (first_pin_name) {
        *first_pin_name = name;
    }
    return mask;
}

void t3_bsp_motor_buzz(u16 duration_ms, u8 duty_pct)
{
    if (duty_pct > 100U) {
        duty_pct = 100U;
    }
    if (!g_motor_pwm_ok || duty_pct >= 95U) {
        power_gate_open_drain_output(T3_MOTOR_PWM_IO, 0U);
        if (duration_ms >= 20U) {
            os_time_dly((duration_ms + 9U) / 10U);
        } else if (duration_ms > 0U) {
            mdelay(duration_ms);
        }
        power_gate_open_drain_output(T3_MOTOR_PWM_IO, 1U);
        return;
    }
    power_gate_pwm_set_duty(T3_MOTOR_PWM_IO, (u32)duty_pct * 100U);
    if (duration_ms >= 20U) {
        os_time_dly((duration_ms + 9U) / 10U);
    } else if (duration_ms > 0U) {
        mdelay(duration_ms);
    }
    power_gate_pwm_set_duty(T3_MOTOR_PWM_IO, 0U);
}

void t3_bsp_backlight_set(u8 percent)
{
    if (percent > 100U) {
        percent = 100U;
    }
    lcd_drv_backlight_ctrl(percent);
}

void t3_bsp_enter_uboot(void)
{
    go_mask_usb_updata();
}

/* --- Barometer (BMP280 / ENS220) + GPADC (D+/D-, NTC, AC Ripple) --- */

typedef struct {
    u8  inited;
    u16 dig_T1;
    s16 dig_T2, dig_T3;
    u16 dig_P1;
    s16 dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
} bmp280_calib_t;

static bmp280_calib_t s_bmp_cal;
static u8 s_ens220_inited = 0;

static s32 baro_calc_altitude_dm(u32 pressure_pa, u32 sea_level_pa)
{
    if (pressure_pa == 0U || sea_level_pa == 0U) {
        return 0;
    }
    double ratio = (double)sea_level_pa / (double)pressure_pa;
    double alt_m = (298.15 / 0.0065) * (pow(ratio, 0.190263) - 1.0);
    return (s32)(alt_m * 10.0);
}

int t3_bsp_read_baro(t3_baro_sample_t *out, u32 sea_level_pa)
{
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (sea_level_pa == 0U) {
        sea_level_pa = 101325U;
    }

    if (g_inventory.bmp280_ok) {
        if (!s_bmp_cal.inited) {
            u8 c[24] = {0};
            if (t3_i2c1_read_reg(T3_BMP280_ADDR, 0x88, c, sizeof(c)) != 0) {
                return -1;
            }
            s_bmp_cal.dig_T1 = (u16)(c[0] | ((u16)c[1] << 8));
            s_bmp_cal.dig_T2 = (s16)(c[2] | ((u16)c[3] << 8));
            s_bmp_cal.dig_T3 = (s16)(c[4] | ((u16)c[5] << 8));
            s_bmp_cal.dig_P1 = (u16)(c[6] | ((u16)c[7] << 8));
            s_bmp_cal.dig_P2 = (s16)(c[8] | ((u16)c[9] << 8));
            s_bmp_cal.dig_P3 = (s16)(c[10] | ((u16)c[11] << 8));
            s_bmp_cal.dig_P4 = (s16)(c[12] | ((u16)c[13] << 8));
            s_bmp_cal.dig_P5 = (s16)(c[14] | ((u16)c[15] << 8));
            s_bmp_cal.dig_P6 = (s16)(c[16] | ((u16)c[17] << 8));
            s_bmp_cal.dig_P7 = (s16)(c[18] | ((u16)c[19] << 8));
            s_bmp_cal.dig_P8 = (s16)(c[20] | ((u16)c[21] << 8));
            s_bmp_cal.dig_P9 = (s16)(c[22] | ((u16)c[23] << 8));

            u8 rst = 0xB6, ctrl = 0xB3, cfg = 0x14;
            t3_i2c1_write_reg(T3_BMP280_ADDR, 0xE0, &rst, 1);
            mdelay(5);
            t3_i2c1_write_reg(T3_BMP280_ADDR, 0xF4, &ctrl, 1);
            t3_i2c1_write_reg(T3_BMP280_ADDR, 0xF5, &cfg, 1);
            s_bmp_cal.inited = 1;
            mdelay(20);
        }

        u8 raw[6] = {0};
        if (t3_i2c1_read_reg(T3_BMP280_ADDR, 0xF7, raw, sizeof(raw)) != 0) {
            return -1;
        }
        s32 adc_P = (s32)(((u32)raw[0] << 12) | ((u32)raw[1] << 4) | ((u32)raw[2] >> 4));
        s32 adc_T = (s32)(((u32)raw[3] << 12) | ((u32)raw[4] << 4) | ((u32)raw[5] >> 4));

        s32 var1 = ((((adc_T >> 3) - ((s32)s_bmp_cal.dig_T1 << 1))) * ((s32)s_bmp_cal.dig_T2)) >> 11;
        s32 var2 = (((((adc_T >> 4) - ((s32)s_bmp_cal.dig_T1)) *
                      ((adc_T >> 4) - ((s32)s_bmp_cal.dig_T1))) >> 12) *
                    ((s32)s_bmp_cal.dig_T3)) >> 14;
        s32 t_fine = var1 + var2;
        s32 temp_c = (t_fine * 5 + 128) >> 8;

        s64 pvar1 = (s64)t_fine - 128000;
        s64 pvar2 = pvar1 * pvar1 * (s64)s_bmp_cal.dig_P6;
        pvar2 = pvar2 + ((pvar1 * (s64)s_bmp_cal.dig_P5) << 17);
        pvar2 = pvar2 + (((s64)s_bmp_cal.dig_P4) << 35);
        pvar1 = ((pvar1 * pvar1 * (s64)s_bmp_cal.dig_P3) >> 8) + ((pvar1 * (s64)s_bmp_cal.dig_P2) << 12);
        pvar1 = (((((s64)1) << 47) + pvar1)) * ((s64)s_bmp_cal.dig_P1) >> 33;
        u32 press_pa = 0;
        if (pvar1 != 0) {
            s64 p = 1048576 - adc_P;
            p = (((p << 31) - pvar2) * 3125) / pvar1;
            pvar1 = (((s64)s_bmp_cal.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
            pvar2 = (((s64)s_bmp_cal.dig_P8) * p) >> 19;
            p = ((p + pvar1 + pvar2) >> 8) + (((s64)s_bmp_cal.dig_P7) << 4);
            press_pa = (u32)(p >> 8);
        }

        out->valid = 1;
        out->is_ens220 = 0;
        out->pressure_pa = press_pa;
        out->temp_centi_c = (s16)temp_c;
        out->altitude_dm = baro_calc_altitude_dm(press_pa, sea_level_pa);
        return 0;
    }

    if (g_inventory.ens220_ok) {
        if (!s_ens220_inited) {
            u8 v = 0x08;
            t3_i2c1_write_reg(T3_ENS220_ADDR, 0x06, &v, 1);
            mdelay(5);
            v = 0x80; t3_i2c1_write_reg(T3_ENS220_ADDR, 0x06, &v, 1);
            v = 0x08; t3_i2c1_write_reg(T3_ENS220_ADDR, 0x07, &v, 1);
            v = 0x00; t3_i2c1_write_reg(T3_ENS220_ADDR, 0x08, &v, 1);
            v = 0x22; t3_i2c1_write_reg(T3_ENS220_ADDR, 0x09, &v, 1);
            v = 0x13; t3_i2c1_write_reg(T3_ENS220_ADDR, 0x06, &v, 1);
            s_ens220_inited = 1;
            mdelay(20);
        }

        u8 raw_p[3] = {0};
        if (t3_i2c1_read_reg(T3_ENS220_ADDR, 0x17, raw_p, 3) != 0) {
            return -1;
        }
        u32 p_raw = (u32)raw_p[0] | ((u32)raw_p[1] << 8) | ((u32)raw_p[2] << 16);
        u32 press_pa = p_raw / 64U; /* 64 LSB/Pa */

        u8 raw_t[2] = {0};
        s16 temp_c = 2500;
        if (t3_i2c1_read_reg(T3_ENS220_ADDR, 0x15, raw_t, 2) == 0) {
            s16 tr = (s16)((u16)raw_t[0] | ((u16)raw_t[1] << 8));
            temp_c = (s16)(((s32)tr * 100) / 64 - 27315);
        }

        out->valid = 1;
        out->is_ens220 = 1;
        out->pressure_pa = press_pa;
        out->temp_centi_c = temp_c;
        out->altitude_dm = baro_calc_altitude_dm(press_pa, sea_level_pa);
        return 0;
    }

    return -1;
}

int t3_bsp_read_usb_dp_dm(s32 *dp_mv, s32 *dm_mv)
{
    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_02), PORT_HIGHZ);
    gpio_set_mode(IO_PORT_SPILT(IO_PORTC_03), PORT_HIGHZ);
    u32 dm = adc_get_voltage_blocking(T3_USB_DM_ADC_CH);
    u32 dp = adc_get_voltage_blocking(T3_USB_DP_ADC_CH);
    if (dm_mv) *dm_mv = (s32)dm;
    if (dp_mv) *dp_mv = (s32)dp;
    return 0;
}

int t3_bsp_read_ntc_deci_c(s16 *temp_deci_c)
{
    /* Subset of OEM 150-entry NTC LUT (kOhm @ -30C..+110C in 5C steps for compact lookup) */
    static const float ntc_kohm_5c[29] = {
        1368.34f, 1010.15f, 753.67f, 568.18f, 432.62f, 332.51f, 257.87f, 201.66f,
        158.95f,  126.22f,  100.94f,  80.40f,  65.34f,  53.05f,  43.35f,  35.63f,
        29.45f,   24.47f,   20.43f,   17.14f,  14.44f,  12.22f,  10.38f,  8.86f,
        7.59f,    6.53f,    5.64f,    4.89f,   4.25f
    };
    gpio_set_mode(IO_PORT_SPILT(T3_NTC_IO), PORT_INPUT_PULLUP_10K);
    u32 raw = adc_get_value_blocking(T3_NTC_ADC_CH);
    if (raw >= 4090U) {
        if (temp_deci_c) *temp_deci_c = -300;
        return -1;
    }
    if (raw < 10U) {
        raw = 10U;
    }
    /* Internal pullup ~10 kOhm -> R_ntc = 10 * raw / (4095 - raw) */
    float rk = (10.0f * (float)raw) / (float)(4095U - raw);
    s16 deci_c = 1100;
    if (rk >= ntc_kohm_5c[0]) {
        deci_c = -300;
    } else {
        for (int i = 0; i < 28; i++) {
            if (rk >= ntc_kohm_5c[i + 1]) {
                float span = ntc_kohm_5c[i] - ntc_kohm_5c[i + 1];
                float frac = (span > 0.01f) ? ((ntc_kohm_5c[i] - rk) / span) : 0.0f;
                deci_c = (s16)((-30 + i * 5) * 10 + (int)(frac * 50.0f));
                break;
            }
        }
    }
    if (temp_deci_c) *temp_deci_c = deci_c;
    return 0;
}

int t3_bsp_capture_ripple(u16 *samples_mv, u16 count, u32 *vpp_mv, u32 *freq_hz)
{
    if (!samples_mv || count < 16U) {
        return -1;
    }
    gpio_set_mode(IO_PORT_SPILT(IO_PORTA_10), PORT_HIGHZ);
    u32 t_start_ms = timer_get_ms();
    u32 sum = 0;
    u16 vmin = 0xFFFFU;
    u16 vmax = 0U;
    for (u16 i = 0; i < count; i++) {
        u32 raw = adc_get_value_blocking(T3_RIPPLE_ADC_CH);
        /* 0.781738 mV/LSB (~3200 mV / 4096) */
        u16 mv = (u16)((raw * 782U) / 1000U);
        samples_mv[i] = mv;
        sum += mv;
        if (mv < vmin) vmin = mv;
        if (mv > vmax) vmax = mv;
    }
    u32 elapsed_ms = timer_get_ms() - t_start_ms;
    u16 mean = (u16)(sum / count);
    u32 crossings = 0;
    for (u16 i = 1; i < count; i++) {
        if ((samples_mv[i - 1] < mean && samples_mv[i] >= mean) ||
            (samples_mv[i - 1] > mean && samples_mv[i] <= mean)) {
            crossings++;
        }
    }
    if (vpp_mv) *vpp_mv = (u32)(vmax - vmin);
    if (freq_hz) {
        if (elapsed_ms > 0U && crossings >= 2U) {
            *freq_hz = (crossings * 500U) / elapsed_ms;
        } else {
            *freq_hz = 0U;
        }
    }
    return 0;
}

/* --- SDK compatibility stubs & LTO keep-alives --- */

#if (!TCFG_SYS_LVD_EN)
void check_power_on_voltage(void)
{
}

u16 get_vbat_value(void)
{
    return 420;
}

u8 get_vbat_percent(void)
{
    return 100;
}

u8 get_self_battery_level(void)
{
    return 9;
}
#endif

#if (!TCFG_LCD_TE_USED_PEND)
u32 lcd_get_te_frame_period_us(void)
{
    return 16666;
}

u8 lcd_get_te_line_period_us(void)
{
    return 69;
}

u32 lcd_get_spi_frame_period_us(void)
{
    return 16666;
}
#endif

void *lv_res_fopen(const char *path, const char *mode)
{
    (void)path;
    (void)mode;
    return NULL;
}

int lv_res_fget_attrs(void *file, void *attr)
{
    (void)file;
    (void)attr;
    return -1;
}

int lv_res_fclose(void *file)
{
    (void)file;
    return 0;
}

int jl_wallpaper_sharing_recv(u8 *data, int length)
{
    (void)data;
    (void)length;
    return 0;
}

__attribute__((weak))
void t3_ble_beacon_update(u8 enable, const u8 *payload16)
{
    (void)enable;
    (void)payload16;
}

__attribute__((weak))
int t3_ble_gatt_notify(const u8 *data, u16 len)
{
    (void)data;
    (void)len;
    return -1;
}

__attribute__((weak))
void t3_ble_on_rx(const u8 *data, u16 len)
{
    (void)data;
    (void)len;
}

__attribute__((weak))
char bt_tws_get_local_channel(void)
{
    return 'L';
}

__attribute__((weak))
bool get_tws_sibling_connect_state(void)
{
    return 0;
}

__attribute__((weak))
u8 get_tws_sibling_bat_persent(void)
{
    return 0xFF;
}

