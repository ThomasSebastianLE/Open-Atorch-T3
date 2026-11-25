#ifndef T3_BSP_H
#define T3_BSP_H

#include "typedef.h"

typedef struct {
    u8  ina226_ok;
    u16 ina226_die_id;
    u8  sc7a20_ok;
    u8  sc7a20_whoami;
    u8  bmp280_ok;
    u8  bmp280_id;
    u8  ens220_ok;
    u8  ens220_status;
} t3_sensor_inventory_t;

typedef struct {
    u8  valid;
    s32 vbus_mv;
    s32 ibus_ma;
    s32 power_mw;
    u16 raw_bus;
    s16 raw_shunt;
} t3_meter_sample_t;

typedef struct {
    u8  valid;
    s16 ax;
    s16 ay;
    s16 az;
} t3_accel_sample_t;

typedef struct {
    u8  valid;
    u8  is_ens220;
    u32 pressure_pa;
    s16 temp_centi_c;
    s32 altitude_dm; /* 0.1 m resolution */
} t3_baro_sample_t;

void t3_bsp_init(void);
void t3_bsp_probe_sensors(t3_sensor_inventory_t *out);
const t3_sensor_inventory_t *t3_bsp_get_inventory(void);

int  t3_bsp_read_ina226(t3_meter_sample_t *out);
int  t3_bsp_read_sc7a20(t3_accel_sample_t *out);
int  t3_bsp_read_baro(t3_baro_sample_t *out, u32 sea_level_pa);
int  t3_bsp_read_usb_dp_dm(s32 *dp_mv, s32 *dm_mv);
int  t3_bsp_read_ntc_deci_c(s16 *temp_deci_c);
int  t3_bsp_capture_ripple(u16 *samples_mv, u16 count, u32 *vpp_mv, u32 *freq_hz);
void t3_bsp_get_touch(s16 *x, s16 *y, u8 *pressed);
void t3_bsp_get_touch_diag(u8 *probe_addr, u8 *int_lvl, u16 *ack_cnt, u8 *raw5, s16 *last_x, s16 *last_y);
void t3_bsp_get_usb_diag(u8 out16[16]);
u32  t3_bsp_scan_keys(const char **first_pin_name);
void t3_bsp_motor_buzz(u16 duration_ms, u8 duty_pct);
void t3_bsp_backlight_set(u8 percent);
void t3_bsp_enter_uboot(void);
void t3_ble_beacon_update(u8 enable, const u8 *payload16);
int  t3_ble_gatt_notify(const u8 *data, u16 len);
void t3_ble_on_rx(const u8 *data, u16 len);

#endif /* T3_BSP_H */
