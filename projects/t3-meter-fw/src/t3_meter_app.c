#include "lvgl.h"
#include "system/includes.h"

#include "t3_board.h"
#include "t3_bsp.h"
#include "t3_meter_app.h"

#define T3_METER_VM_ID    48U
#define T3_METER_VM_MAGIC 0x54334D34UL /* 'T3M4' */
#define T3_BANK_COUNT     3U

/* --- Pages --- */
typedef enum {
    PAGE_METER = 0,
    PAGE_CURVE,
    PAGE_RIPPLE,
    PAGE_CABLE,
    PAGE_SENSORS,
    PAGE_SETUP,
    PAGE_COUNT
} meter_page_t;

static const char *const PAGE_TITLES[PAGE_COUNT] = {
    "LIVE", "CURVE", "RIPPLE", "CABLE", "PROTO", "SETUP"
};

typedef struct {
    u64 mah_in_uahs;
    u64 wh_in_uwhs;
    u64 mah_out_uahs;
    u64 wh_out_uwhs;
    u32 uptime_ticks;
} meter_bank_t;

typedef struct {
    u32 magic;
    u8  brightness;
    u8  motor_on;
    u8  tilt_nav_on;
    u8  ble_beacon_on;
    u8  alarms_on;
    u8  big_mode;
    u8  active_bank;
    u8  reserved;
    s32 ibus_zero_ma;
    s32 v_gain_permille;
    s32 i_gain_permille;
    u32 sea_level_pa;
    s32 alarm_ov_mv;
    s32 alarm_oc_ma;
    s16 alarm_ot_deci_c;
    s16 alarm_full_ma;
} t3_meter_vm_cfg_t;

/* --- Blackbox Ring Buffer (10 s interval, 60 entries = 10 min) --- */
#define BLACKBOX_LEN 60
typedef struct {
    u16 v_mv;
    s16 i_ma;
    u16 p_cw;
    s16 temp_dc;
} blackbox_entry_t;

static blackbox_entry_t s_blackbox[BLACKBOX_LEN];
static u16 s_bb_head = 0;
static u16 s_bb_count = 0;

static meter_page_t s_page = PAGE_METER;

/* --- Config & Energy Banks --- */
static t3_meter_vm_cfg_t s_cfg;
static meter_bank_t s_banks[T3_BANK_COUNT];

/* --- Live & Peak Measurements --- */
static s32 s_v_min_mv     = 0x7FFFFFFF;
static s32 s_v_max_mv     = 0;
static s32 s_i_max_ma     = 0;
static s32 s_p_max_mw     = 0;
static u16 s_brownout_cnt = 0;
static u8  s_brownout_latched = 0;

static s32 s_last_v_mv    = 0;
static s32 s_last_i_ma    = 0;
static s32 s_last_p_mw    = 0;
static s32 s_last_dp_mv   = 0;
static s32 s_last_dm_mv   = 0;
static s16 s_last_temp_dc = 250;
static u8  s_last_proto_id = 0;
static u8  s_alarm_latched = 0;
static u8  s_full_seen_load = 0;

/* --- Cable Test State --- */
static s32 s_cable_v0_mv  = 0;
static s32 s_cable_mohm   = -1;

/* --- Lightweight Bar-Plot Buffers --- */
#define PLOT_BARS      28
#define RIPPLE_SAMPLES 56
static u8  s_curve_v_hist[PLOT_BARS];
static u8  s_curve_i_hist[PLOT_BARS];
static lv_obj_t *s_curve_v_bars[PLOT_BARS];
static lv_obj_t *s_curve_i_bars[PLOT_BARS];

static u16 s_ripple_buf[RIPPLE_SAMPLES];
static lv_obj_t *s_ripple_bars[PLOT_BARS];
static u32 s_ripple_avg_mv = 0;

/* --- Hardware Keys & SC7A20 Tilt/Knock Nav --- */
static u16 s_tick_div      = 0;
static u32 s_prev_key_mask = 0;
static u16 s_k1_hold_ticks = 0;
static u16 s_k2_hold_ticks = 0;
static u8  s_k1_long_fired = 0;
static u8  s_k2_long_fired = 0;

static u8  s_tilt_armed    = 1;
static u8  s_tilt_cooldown = 0;

/* --- UI Objects --- */
static lv_obj_t *s_pages[PAGE_COUNT];
static lv_obj_t *s_hdr_title;
static lv_obj_t *s_hdr_badge;
static lv_obj_t *s_hdr_dots[PAGE_COUNT];

/* Page 0: LIVE */
static lv_obj_t *s_alarm_banner;
static lv_obj_t *s_lbl_alarm;
static lv_obj_t *s_lbl_vbus;
static lv_obj_t *s_lbl_ibus;
static lv_obj_t *s_lbl_pwr;
static lv_obj_t *s_lbl_energy;
static lv_obj_t *s_lbl_peak_time;
static lv_obj_t *s_lbl_proto_temp;
static lv_obj_t *s_lbl_bank_btn;

/* Page 1: CURVE */
static lv_obj_t *s_lbl_curve_info;

/* Page 2: RIPPLE */
static lv_obj_t *s_lbl_ripple_info;

/* Page 3: CABLE */
static lv_obj_t *s_lbl_cable_v0;
static lv_obj_t *s_lbl_cable_res;

/* Page 4: PROTO & SENSORS */
static lv_obj_t *s_lbl_sns_usb;
static lv_obj_t *s_lbl_sns_baro;
static lv_obj_t *s_lbl_sns_acc;
static lv_obj_t *s_obj_bubble;

/* Page 5: SETUP (8 focusable items for 1/2-button control) */
#define SETUP_ITEM_COUNT 8
static u8 s_setup_idx = 0;
static lv_obj_t *s_setup_objs[SETUP_ITEM_COUNT];
static lv_obj_t *s_lbl_set_bright;
static lv_obj_t *s_lbl_set_motor;
static lv_obj_t *s_lbl_set_tilt;
static lv_obj_t *s_lbl_set_ble;
static lv_obj_t *s_lbl_set_alarm;
static lv_obj_t *s_lbl_set_bank;
static lv_obj_t *s_lbl_set_cal;

/* --- Helpers --- */
static void buzz_ms(u16 ms)
{
    if (s_cfg.motor_on) {
        t3_bsp_motor_buzz(ms, 70);
    }
}

static void vm_load_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    memset(s_banks, 0, sizeof(s_banks));
    s_cfg.magic           = T3_METER_VM_MAGIC;
    s_cfg.brightness      = 90;
    s_cfg.motor_on        = 1;
    s_cfg.tilt_nav_on     = 1;
    s_cfg.ble_beacon_on   = 1;
    s_cfg.alarms_on       = 1;
    s_cfg.big_mode        = 0;
    s_cfg.active_bank     = 0;
    s_cfg.ibus_zero_ma    = 0;
    s_cfg.v_gain_permille = 1000;
    s_cfg.i_gain_permille = 1000;
    s_cfg.sea_level_pa    = 101325U;
    s_cfg.alarm_ov_mv     = 21000;
    s_cfg.alarm_oc_ma     = 5000;
    s_cfg.alarm_ot_deci_c = 600;
    s_cfg.alarm_full_ma   = 50;
}

static void vm_load(void)
{
    memset(s_banks, 0, sizeof(s_banks));
    t3_meter_vm_cfg_t tmp;
    int r = syscfg_read(T3_METER_VM_ID, &tmp, sizeof(tmp));
    if (r == (int)sizeof(tmp) && tmp.magic == T3_METER_VM_MAGIC) {
        s_cfg = tmp;
        if (s_cfg.brightness < 10 || s_cfg.brightness > 100) s_cfg.brightness = 90;
        if (s_cfg.active_bank >= T3_BANK_COUNT) s_cfg.active_bank = 0;
        if (s_cfg.v_gain_permille < 900 || s_cfg.v_gain_permille > 1100) s_cfg.v_gain_permille = 1000;
        if (s_cfg.i_gain_permille < 900 || s_cfg.i_gain_permille > 1100) s_cfg.i_gain_permille = 1000;
        if (s_cfg.sea_level_pa < 80000U || s_cfg.sea_level_pa > 110000U) s_cfg.sea_level_pa = 101325U;
    } else {
        vm_load_defaults();
    }
}

static void vm_save_now(void)
{
    s_cfg.magic = T3_METER_VM_MAGIC;
    syscfg_write(T3_METER_VM_ID, &s_cfg, sizeof(s_cfg));
}

static void fmt_fixed(char *dst, size_t sz, s32 milli, const char *unit, int decimals)
{
    const char *sign = "";
    s32 av = milli;
    if (av < 0) {
        sign = "-";
        av = -av;
    }
    if (decimals == 3) {
        lv_snprintf(dst, sz, "%s%ld.%03ld%s", sign, (long)(av / 1000), (long)(av % 1000), unit);
    } else if (decimals == 2) {
        s32 c = (av + 5) / 10;
        lv_snprintf(dst, sz, "%s%ld.%02ld%s", sign, (long)(c / 100), (long)(c % 100), unit);
    } else {
        s32 d = (av + 50) / 100;
        lv_snprintf(dst, sz, "%s%ld.%ld%s", sign, (long)(d / 10), (long)(d % 10), unit);
    }
}

static const char *detect_usb_proto(s32 vbus_mv, s32 dp_mv, s32 dm_mv, u8 *out_id)
{
    u8 id = 0;
    const char *name = "USB 5V";

    if (vbus_mv > 16500) {
        id = 10; name = "PD/QC 20V";
    } else if (vbus_mv > 13500) {
        id = 9; name = "PD 15V";
    } else if (vbus_mv > 10800) {
        id = 8; name = "PD/QC 12V";
    } else if (vbus_mv > 7800) {
        id = 7; name = "PD/QC 9V";
    } else if (vbus_mv > 5600) {
        id = 6; name = "QC3.0/PPS";
    } else if (dp_mv > 2500 && dm_mv > 2500) {
        id = 3; name = "Apple 2.4A";
    } else if (dp_mv > 2500 && dm_mv > 1800) {
        id = 2; name = "Apple 1/2A";
    } else if (dp_mv > 1800 && dm_mv < 400) {
        id = 5; name = "AFC / FCP";
    } else if (dp_mv > 400 && dm_mv > 400 && (dp_mv - dm_mv) < 200 && (dm_mv - dp_mv) < 200) {
        id = 1; name = "DCP 1.5A";
    } else if (dp_mv > 400 && dm_mv < 200) {
        id = 4; name = "QC2.0 5V";
    }

    if (out_id) *out_id = id;
    return name;
}

static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h, u32 bg_hex,
                          const char *txt, lv_event_cb_t cb, void *udata, lv_obj_t **out_lbl)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, udata);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(lbl, txt);
    lv_obj_center(lbl);
    if (out_lbl) *out_lbl = lbl;
    return btn;
}

static lv_obj_t *make_lbl(lv_obj_t *parent, int x, int y, u32 hex_col, const lv_font_t *font, const char *txt)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(lbl, lv_color_hex(hex_col), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_label_set_text(lbl, txt);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

static void update_header_ui(void)
{
    if (s_hdr_title) {
        lv_label_set_text(s_hdr_title, PAGE_TITLES[s_page]);
    }
    if (s_hdr_badge) {
        char bdg[20];
        lv_snprintf(bdg, sizeof(bdg), "%c | %s",
                    'A' + s_cfg.active_bank,
                    s_cfg.ble_beacon_on ? "BLE" : "---");
        lv_label_set_text(s_hdr_badge, bdg);
    }
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (s_hdr_dots[i]) {
            lv_obj_set_style_bg_color(
                s_hdr_dots[i],
                lv_color_hex((i == (int)s_page) ? 0x38BDF8 : 0x334155),
                LV_PART_MAIN
            );
        }
    }
}

static void update_setup_focus(void)
{
    for (int i = 0; i < SETUP_ITEM_COUNT; i++) {
        if (s_setup_objs[i]) {
            if (i == (int)s_setup_idx) {
                lv_obj_set_style_border_color(s_setup_objs[i], lv_color_hex(0x38BDF8), LV_PART_MAIN);
                lv_obj_set_style_border_width(s_setup_objs[i], 2, LV_PART_MAIN);
            } else {
                lv_obj_set_style_border_width(s_setup_objs[i], 0, LV_PART_MAIN);
            }
        }
    }
}

static void apply_big_mode_layout(void)
{
    if (!s_lbl_vbus || !s_lbl_ibus || !s_lbl_pwr) return;
    if (s_cfg.big_mode) {
        lv_obj_set_pos(s_lbl_vbus, 18, 36);
        lv_obj_set_pos(s_lbl_ibus, 18, 94);
        lv_obj_add_flag(s_lbl_pwr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_energy, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_peak_time, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_proto_temp, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_pos(s_lbl_vbus, 10, 6);
        lv_obj_set_pos(s_lbl_ibus, 10, 40);
        lv_obj_set_pos(s_lbl_pwr,  10, 74);
        lv_obj_clear_flag(s_lbl_pwr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_lbl_energy, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_lbl_peak_time, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_lbl_proto_temp, LV_OBJ_FLAG_HIDDEN);
    }
}

static void toggle_big_mode(void)
{
    s_cfg.big_mode ^= 1U;
    apply_big_mode_layout();
    buzz_ms(15);
}

static void big_btn_cb(lv_event_t *e) { (void)e; toggle_big_mode(); }

static void switch_page(meter_page_t next)
{
    if (next >= PAGE_COUNT) next = PAGE_METER;
    s_page = next;
    if (s_page == PAGE_SETUP) {
        s_setup_idx = 0;
        update_setup_focus();
    }
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (s_pages[i]) {
            if (i == (int)next) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    update_header_ui();
}

static void hdr_click_cb(lv_event_t *e)
{
    (void)e;
    buzz_ms(15);
    switch_page((meter_page_t)((s_page + 1) % PAGE_COUNT));
}

static void reset_active_bank(void)
{
    meter_bank_t *b = &s_banks[s_cfg.active_bank];
    b->mah_in_uahs = 0;
    b->wh_in_uwhs = 0;
    b->mah_out_uahs = 0;
    b->wh_out_uwhs = 0;
    b->uptime_ticks = 0;
    s_v_min_mv = 0x7FFFFFFF;
    s_v_max_mv = 0;
    s_i_max_ma = 0;
    s_p_max_mw = 0;
    s_brownout_cnt = 0;
    s_full_seen_load = 0;
}

static void reset_counters_cb(lv_event_t *e) { (void)e; reset_active_bank(); buzz_ms(30); }

static void refresh_setup_labels(void)
{
    if (s_lbl_set_bright) {
        char buf[28];
        lv_snprintf(buf, sizeof(buf), "Bright: %u%%", (unsigned)s_cfg.brightness);
        lv_label_set_text(s_lbl_set_bright, buf);
    }
    if (s_lbl_set_motor) {
        lv_label_set_text(s_lbl_set_motor, s_cfg.motor_on ? "Haptics: ON" : "Haptics: OFF");
    }
    if (s_lbl_set_tilt) {
        lv_label_set_text(s_lbl_set_tilt, s_cfg.tilt_nav_on ? "Tilt-Nav: ON" : "Tilt-Nav: OFF");
    }
    if (s_lbl_set_ble) {
        lv_label_set_text(s_lbl_set_ble, s_cfg.ble_beacon_on ? "BLE Beacon: ON" : "BLE Beacon: OFF");
    }
    if (s_lbl_set_alarm) {
        lv_label_set_text(s_lbl_set_alarm, s_cfg.alarms_on ? "Alarms: ON" : "Alarms: OFF");
    }
    if (s_lbl_set_bank) {
        char btxt[16];
        lv_snprintf(btxt, sizeof(btxt), "Bank: %c", 'A' + s_cfg.active_bank);
        lv_label_set_text(s_lbl_set_bank, btxt);
    }
    if (s_lbl_set_cal) {
        char buf[44];
        lv_snprintf(buf, sizeof(buf), "Cal V:%ld%% I:%ld%% Z:%ldmA",
                    (long)(s_cfg.v_gain_permille / 10),
                    (long)(s_cfg.i_gain_permille / 10),
                    (long)s_cfg.ibus_zero_ma);
        lv_label_set_text(s_lbl_set_cal, buf);
    }
    if (s_lbl_bank_btn) {
        char btxt[12];
        lv_snprintf(btxt, sizeof(btxt), "BNK %c", 'A' + s_cfg.active_bank);
        lv_label_set_text(s_lbl_bank_btn, btxt);
    }
    update_header_ui();
}

static void bank_cycle(void)
{
    s_cfg.active_bank = (u8)((s_cfg.active_bank + 1U) % T3_BANK_COUNT);
    refresh_setup_labels();
    buzz_ms(20);
}

static void bank_cycle_cb(lv_event_t *e) { (void)e; bank_cycle(); }

static void do_cable_v0(void)
{
    s_cable_v0_mv = s_last_v_mv;
    s_cable_mohm = -1;
    buzz_ms(20);
    if (s_lbl_cable_v0) {
        char buf[36], vs[16];
        fmt_fixed(vs, sizeof(vs), s_cable_v0_mv, "V", 3);
        lv_snprintf(buf, sizeof(buf), "V0 (No-Load): %s", vs);
        lv_label_set_text(s_lbl_cable_v0, buf);
    }
}

static void do_cable_meas(void)
{
    buzz_ms(25);
    if (!s_lbl_cable_res) return;
    if (s_cable_v0_mv <= 0) {
        lv_label_set_text(s_lbl_cable_res, "Run Step 1 (Save V0) first!");
        return;
    }
    s32 abs_i = (s_last_i_ma >= 0) ? s_last_i_ma : -s_last_i_ma;
    if (abs_i < 100) {
        lv_label_set_text(s_lbl_cable_res, "Load too low (<0.10A)!");
        return;
    }
    s32 dv_mv = s_cable_v0_mv - s_last_v_mv;
    if (dv_mv < 0) dv_mv = 0;
    s_cable_mohm = (dv_mv * 1000) / abs_i;
    s32 droop_pct10 = (s_cable_v0_mv > 0) ? ((dv_mv * 1000) / s_cable_v0_mv) : 0;
    const char *qual = (s_cable_mohm < 120) ? "EXCELLENT" : ((s_cable_mohm < 250) ? "OK" : "HIGH RES");
    char buf[72], vs[16], is[16];
    fmt_fixed(vs, sizeof(vs), s_last_v_mv, "V", 3);
    fmt_fixed(is, sizeof(is), abs_i, "A", 2);
    lv_snprintf(buf, sizeof(buf), "R=%ldmOhm (%s)\nDroop:%ldmV (%ld.%ld%%) @ %s/%s",
                (long)s_cable_mohm, qual, (long)dv_mv,
                (long)(droop_pct10 / 10), (long)(droop_pct10 % 10), vs, is);
    lv_label_set_text(s_lbl_cable_res, buf);
}

static void cable_v0_cb(lv_event_t *e)  { (void)e; do_cable_v0(); }
static void cable_meas_cb(lv_event_t *e){ (void)e; do_cable_meas(); }

static void do_baro_zero(void)
{
    t3_baro_sample_t b;
    if (t3_bsp_read_baro(&b, s_cfg.sea_level_pa) == 0 && b.valid) {
        s_cfg.sea_level_pa = b.pressure_pa;
        buzz_ms(25);
    }
}

static void baro_zero_cb(lv_event_t *e) { (void)e; do_baro_zero(); }

static void do_zero_curr(void)
{
    t3_meter_sample_t s;
    if (t3_bsp_read_ina226(&s) == 0 && s.valid) {
        if (s.ibus_ma > -200 && s.ibus_ma < 200) {
            s_cfg.ibus_zero_ma = s.ibus_ma;
            buzz_ms(30);
            refresh_setup_labels();
        }
    }
}

static void cycle_brightness(void)
{
    int nxt = (int)s_cfg.brightness + 20;
    if (nxt > 100) nxt = 20;
    s_cfg.brightness = (u8)nxt;
    t3_bsp_backlight_set(s_cfg.brightness);
    buzz_ms(15);
    refresh_setup_labels();
}

static void set_bright_cb(lv_event_t *e)      { (void)e; cycle_brightness(); }
static void set_toggle_motor_cb(lv_event_t *e){ (void)e; s_cfg.motor_on ^= 1U; buzz_ms(20); refresh_setup_labels(); }
static void set_toggle_tilt_cb(lv_event_t *e) { (void)e; s_cfg.tilt_nav_on ^= 1U; buzz_ms(20); refresh_setup_labels(); }
static void set_toggle_ble_cb(lv_event_t *e)  { (void)e; s_cfg.ble_beacon_on ^= 1U; if (!s_cfg.ble_beacon_on) t3_ble_beacon_update(0, NULL); buzz_ms(20); refresh_setup_labels(); }
static void set_toggle_alarm_cb(lv_event_t *e){ (void)e; s_cfg.alarms_on ^= 1U; s_alarm_latched = 0; buzz_ms(20); refresh_setup_labels(); }
static void set_zero_curr_cb(lv_event_t *e)   { (void)e; do_zero_curr(); }
static void btn_save_vm_cb(lv_event_t *e)     { (void)e; vm_save_now(); buzz_ms(35); }
static void btn_uboot_hold_cb(lv_event_t *e)  { (void)e; buzz_ms(80); t3_bsp_enter_uboot(); }

static void execute_setup_item(u8 idx)
{
    switch (idx) {
    case 0: cycle_brightness(); break;
    case 1: s_cfg.motor_on ^= 1U; buzz_ms(20); refresh_setup_labels(); break;
    case 2: s_cfg.tilt_nav_on ^= 1U; buzz_ms(20); refresh_setup_labels(); break;
    case 3: s_cfg.ble_beacon_on ^= 1U; if (!s_cfg.ble_beacon_on) t3_ble_beacon_update(0, NULL); buzz_ms(20); refresh_setup_labels(); break;
    case 4: s_cfg.alarms_on ^= 1U; s_alarm_latched = 0; buzz_ms(20); refresh_setup_labels(); break;
    case 5: bank_cycle(); break;
    case 6: do_zero_curr(); break;
    case 7: vm_save_now(); buzz_ms(35); break;
    default: break;
    }
}

/* --- Touch Driver (optional passive read, no cursor dot) --- */
static void meter_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    s16 tx = 0, ty = 0;
    u8 pressed = 0;
    t3_bsp_get_touch(&tx, &ty, &pressed);
    data->point.x = (lv_coord_t)tx;
    data->point.y = (lv_coord_t)ty;
    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static void handle_key2_short(void)
{
    switch (s_page) {
    case PAGE_METER:
        toggle_big_mode();
        break;
    case PAGE_CURVE:
    case PAGE_RIPPLE:
        bank_cycle();
        break;
    case PAGE_CABLE:
        if (s_cable_v0_mv <= 0) do_cable_v0();
        else do_cable_meas();
        break;
    case PAGE_SENSORS:
        do_baro_zero();
        break;
    case PAGE_SETUP:
        execute_setup_item(s_setup_idx);
        break;
    default:
        break;
    }
}

static void update_ble_beacon(u8 alarm_flag, u8 full_flag)
{
    if (!s_cfg.ble_beacon_on) return;

    u8 payload[16];
    meter_bank_t *b = &s_banks[s_cfg.active_bank];
    u16 v_mv = (s_last_v_mv > 0) ? (u16)s_last_v_mv : 0U;
    s16 i_ma = (s16)s_last_i_ma;
    u32 p_mw = (s_last_p_mw > 0) ? (u32)s_last_p_mw : 0U;
    u32 mah_tot = (u32)((b->mah_out_uahs + b->mah_in_uahs) / 3600000ULL);
    u8 flags = (u8)((alarm_flag ? 0x01U : 0U) |
                    (full_flag ? 0x02U : 0U) |
                    (s_brownout_cnt > 0 ? 0x04U : 0U));

    payload[0]  = (u8)(v_mv & 0xFFU);
    payload[1]  = (u8)((v_mv >> 8) & 0xFFU);
    payload[2]  = (u8)((u16)i_ma & 0xFFU);
    payload[3]  = (u8)(((u16)i_ma >> 8) & 0xFFU);
    payload[4]  = (u8)(p_mw & 0xFFU);
    payload[5]  = (u8)((p_mw >> 8) & 0xFFU);
    payload[6]  = (u8)((p_mw >> 16) & 0xFFU);
    payload[7]  = (u8)((p_mw >> 24) & 0xFFU);
    payload[8]  = (u8)(mah_tot & 0xFFU);
    payload[9]  = (u8)((mah_tot >> 8) & 0xFFU);
    payload[10] = (u8)((mah_tot >> 16) & 0xFFU);
    payload[11] = (u8)((mah_tot >> 24) & 0xFFU);
    payload[12] = (u8)((u16)s_last_temp_dc & 0xFFU);
    payload[13] = (u8)(((u16)s_last_temp_dc >> 8) & 0xFFU);
    payload[14] = s_last_proto_id;
    payload[15] = flags;

    t3_ble_beacon_update(1, payload);

    /* Send 36-byte ATorch GATT telemetry packet on 0xFFE1 if WebBLE is connected */
    u8 pkt[36];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0xFF; pkt[1] = 0x55; pkt[2] = 0x01; pkt[3] = 0x03;
    u32 v_c = (u32)((s_last_v_mv > 0 ? s_last_v_mv : 0) / 10);
    u32 i_c = (u32)((s_last_i_ma >= 0 ? s_last_i_ma : -s_last_i_ma) / 10);
    u32 wh_c = (u32)((b->wh_out_uwhs + b->wh_in_uwhs) / 36000000ULL);
    u16 dm_c = (u16)((s_last_dm_mv > 0 ? s_last_dm_mv : 0) / 10);
    u16 dp_c = (u16)((s_last_dp_mv > 0 ? s_last_dp_mv : 0) / 10);
    s16 temp_c = (s16)(s_last_temp_dc / 10);
    u32 sec = b->uptime_ticks / 10U;
    u16 hrs = (u16)(sec / 3600U);
    u8 mins = (u8)((sec % 3600U) / 60U);
    u8 secs = (u8)(sec % 60U);
    pkt[4] = (u8)(v_c >> 16); pkt[5] = (u8)(v_c >> 8); pkt[6] = (u8)v_c;
    pkt[7] = (u8)(i_c >> 16); pkt[8] = (u8)(i_c >> 8); pkt[9] = (u8)i_c;
    pkt[10] = (u8)(mah_tot >> 16); pkt[11] = (u8)(mah_tot >> 8); pkt[12] = (u8)mah_tot;
    pkt[13] = (u8)(wh_c >> 24); pkt[14] = (u8)(wh_c >> 16); pkt[15] = (u8)(wh_c >> 8); pkt[16] = (u8)wh_c;
    pkt[17] = (u8)(dm_c >> 8); pkt[18] = (u8)dm_c;
    pkt[19] = (u8)(dp_c >> 8); pkt[20] = (u8)dp_c;
    pkt[21] = (u8)((u16)temp_c >> 8); pkt[22] = (u8)((u16)temp_c & 0xFFU);
    pkt[23] = (u8)(hrs >> 8); pkt[24] = (u8)hrs;
    pkt[25] = mins; pkt[26] = secs;
    pkt[27] = s_cfg.brightness;
    pkt[28] = 0x13; pkt[29] = 0x88;
    pkt[32] = 0x02; pkt[33] = 0x8A;
    u8 sum = 0;
    for (int k = 2; k < 35; k++) sum += pkt[k];
    pkt[35] = (u8)(sum ^ 0x44U);
    t3_ble_gatt_notify(pkt, sizeof(pkt));
}

void t3_ble_on_rx(const u8 *data, u16 len)
{
    if (!data || len < 10 || data[0] != 0xFF || data[1] != 0x55) return;
    u8 cmd = data[4];
    if (cmd == 0x01 || cmd == 0x02 || cmd == 0x03) {
        reset_active_bank();
        buzz_ms(40);
    } else if (cmd == 0x10) {
        bank_cycle();
    } else if (cmd == 0x11) {
        do_zero_curr();
    }
}

/* --- 100 ms Periodic Sampling & UI Refresh --- */
static void meter_tick_cb(lv_timer_t *tmr)
{
    (void)tmr;
    s_tick_div++;
    meter_bank_t *bank = &s_banks[s_cfg.active_bank];

    /* 0. Both physical buttons:
       - Key 1 (PB07):
         * On SETUP: short = next item (wraps to LIVE), long (0.8s) = toggle/execute item
         * Elsewhere: short = next page, long (1.0s) = reset active bank
       - Key 2 (PB04..06):
         * Short = context action (BIG / BANK / CABLE / ALT=0 / SETUP execute)
         * Long (1.0s) = ZERO I */
    u32 keys = t3_bsp_scan_keys(NULL);
    if (keys & 0x01U) {
        s_k1_hold_ticks++;
        u16 thresh = (s_page == PAGE_SETUP) ? 8U : 10U;
        if (!s_k1_long_fired && s_k1_hold_ticks >= thresh) {
            s_k1_long_fired = 1;
            if (s_page == PAGE_SETUP) {
                execute_setup_item(s_setup_idx);
            } else {
                reset_active_bank();
                buzz_ms(60);
            }
        }
    } else {
        if ((s_prev_key_mask & 0x01U) && !s_k1_long_fired && s_k1_hold_ticks > 0) {
            buzz_ms(15);
            if (s_page == PAGE_SETUP) {
                s_setup_idx++;
                if (s_setup_idx >= SETUP_ITEM_COUNT) {
                    switch_page(PAGE_METER);
                } else {
                    update_setup_focus();
                }
            } else {
                switch_page((meter_page_t)((s_page + 1) % PAGE_COUNT));
            }
        }
        s_k1_hold_ticks = 0;
        s_k1_long_fired = 0;
    }

    if (keys & 0x02U) {
        s_k2_hold_ticks++;
        if (!s_k2_long_fired && s_k2_hold_ticks >= 10) {
            s_k2_long_fired = 1;
            do_zero_curr();
        }
    } else {
        if ((s_prev_key_mask & 0x02U) && !s_k2_long_fired && s_k2_hold_ticks > 0) {
            handle_key2_short();
        }
        s_k2_hold_ticks = 0;
        s_k2_long_fired = 0;
    }
    s_prev_key_mask = keys;

    /* 1. Fast INA226 sampling (100 ms) */
    t3_meter_sample_t m;
    if (t3_bsp_read_ina226(&m) == 0 && m.valid) {
        s32 v_mv = (m.vbus_mv * s_cfg.v_gain_permille) / 1000;
        s32 raw_i = m.ibus_ma - s_cfg.ibus_zero_ma;
        s32 i_ma = (raw_i * s_cfg.i_gain_permille) / 1000;
        if (i_ma > -2 && i_ma < 2) i_ma = 0;

        s32 abs_i = (i_ma >= 0) ? i_ma : -i_ma;
        s32 p_mw = (v_mv * abs_i) / 1000;

        s_last_v_mv = v_mv;
        s_last_i_ma = i_ma;
        s_last_p_mw = p_mw;

        if (v_mv < s_v_min_mv) s_v_min_mv = v_mv;
        if (v_mv > s_v_max_mv) s_v_max_mv = v_mv;
        if (abs_i > s_i_max_ma) s_i_max_ma = abs_i;
        if (p_mw > s_p_max_mw) s_p_max_mw = p_mw;

        if (abs_i > 100 && v_mv < 4500) {
            if (!s_brownout_latched) {
                s_brownout_cnt++;
                s_brownout_latched = 1;
            }
        } else if (v_mv > 4650) {
            s_brownout_latched = 0;
        }

        if (abs_i > 150) s_full_seen_load = 1;

        if (abs_i > 0) {
            bank->uptime_ticks++;
            if (i_ma >= 0) {
                bank->mah_out_uahs += (u64)abs_i * 100ULL;
                bank->wh_out_uwhs  += (u64)p_mw * 100ULL;
            } else {
                bank->mah_in_uahs  += (u64)abs_i * 100ULL;
                bank->wh_in_uwhs   += (u64)p_mw * 100ULL;
            }
        }

        /* Alarms check */
        u8 alarm_hit = 0;
        u8 full_hit = 0;
        const char *alarm_txt = NULL;
        u32 alarm_col = 0x991B1B;
        if (s_cfg.alarms_on) {
            if (v_mv > s_cfg.alarm_ov_mv) {
                alarm_hit = 1; alarm_txt = "ALARM: OVERVOLTAGE!";
            } else if (abs_i > s_cfg.alarm_oc_ma) {
                alarm_hit = 1; alarm_txt = "ALARM: OVERCURRENT!";
            } else if (s_last_temp_dc > s_cfg.alarm_ot_deci_c) {
                alarm_hit = 1; alarm_txt = "ALARM: OVERTEMP!";
            } else if (s_full_seen_load && s_cfg.alarm_full_ma > 0 && abs_i > 2 && abs_i < s_cfg.alarm_full_ma) {
                full_hit = 1; alarm_txt = "CHARGE FULL (<50mA)"; alarm_col = 0x166534;
            }
        }

        if (s_alarm_banner && s_lbl_alarm) {
            if (alarm_txt) {
                lv_obj_set_style_bg_color(s_alarm_banner, lv_color_hex(alarm_col), LV_PART_MAIN);
                lv_label_set_text(s_lbl_alarm, alarm_txt);
                lv_obj_clear_flag(s_alarm_banner, LV_OBJ_FLAG_HIDDEN);
                if (!s_alarm_latched) {
                    buzz_ms(120);
                    s_alarm_latched = 1;
                }
            } else {
                lv_obj_add_flag(s_alarm_banner, LV_OBJ_FLAG_HIDDEN);
                s_alarm_latched = 0;
            }
        }

        if (s_page == PAGE_METER) {
            char vs[16], is[16], ps[16], ebuf[56], pbuf[56];
            fmt_fixed(vs, sizeof(vs), v_mv, " V", 3);
            fmt_fixed(is, sizeof(is), i_ma, " A", 3);
            fmt_fixed(ps, sizeof(ps), p_mw / 10, " W", 2);
            lv_label_set_text(s_lbl_vbus, vs);
            lv_label_set_text(s_lbl_ibus, is);
            lv_label_set_text(s_lbl_pwr,  ps);

            if (!s_cfg.big_mode) {
                u32 mah_out = (u32)(bank->mah_out_uahs / 3600000ULL);
                u32 mah_in  = (u32)(bank->mah_in_uahs  / 3600000ULL);
                u32 wh_tot  = (u32)((bank->wh_out_uwhs + bank->wh_in_uwhs) / 36000000ULL);
                u32 mah_3v7 = (u32)(((bank->wh_out_uwhs + bank->wh_in_uwhs) * 1000ULL) / (3600000ULL * 3700ULL));
                lv_snprintf(ebuf, sizeof(ebuf), "+%lu/-%lumAh  3.7V:%lumAh  %lu.%02luWh",
                            (unsigned long)mah_out, (unsigned long)mah_in,
                            (unsigned long)mah_3v7,
                            (unsigned long)(wh_tot / 100U), (unsigned long)(wh_tot % 100U));
                lv_label_set_text(s_lbl_energy, ebuf);

                u32 sec = bank->uptime_ticks / 10U;
                u32 hh = sec / 3600U;
                u32 mm = (sec % 3600U) / 60U;
                u32 ss = sec % 60U;
                s32 pmax_cw = s_p_max_mw / 10;
                lv_snprintf(pbuf, sizeof(pbuf), "Pk:%ld.%02ldW Ip:%ld.%02ldA  %02lu:%02lu:%02lu",
                            (long)(pmax_cw / 100), (long)(pmax_cw % 100),
                            (long)(s_i_max_ma / 1000), (long)((s_i_max_ma % 1000) / 10),
                            (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
                lv_label_set_text(s_lbl_peak_time, pbuf);
            }
        }

        if ((s_tick_div % 10U) == 0U) {
            update_ble_beacon(alarm_hit, full_hit);
        }

        /* Update CURVE bar history every 200 ms */
        if ((s_tick_div & 1U) == 0U) {
            for (int k = 0; k < PLOT_BARS - 1; k++) {
                s_curve_v_hist[k] = s_curve_v_hist[k + 1];
                s_curve_i_hist[k] = s_curve_i_hist[k + 1];
            }
            s32 vp = (v_mv * 140) / 24000;
            s32 ip = (abs_i * 140) / 6000;
            if (vp < 2) vp = 2;
            if (vp > 140) vp = 140;
            if (ip < 2) ip = 2;
            if (ip > 140) ip = 140;
            s_curve_v_hist[PLOT_BARS - 1] = (u8)vp;
            s_curve_i_hist[PLOT_BARS - 1] = (u8)ip;

            if (s_page == PAGE_CURVE) {
                for (int k = 0; k < PLOT_BARS; k++) {
                    u8 vh = s_curve_v_hist[k];
                    u8 ih = s_curve_i_hist[k];
                    lv_obj_set_size(s_curve_v_bars[k], 5, vh);
                    lv_obj_set_pos(s_curve_v_bars[k], 2 + k * 8, 160 - vh);
                    lv_obj_set_size(s_curve_i_bars[k], 3, ih);
                    lv_obj_set_pos(s_curve_i_bars[k], 4 + k * 8, 160 - ih);
                }
                char cbuf[52];
                s32 vmin_c = (s_v_min_mv == 0x7FFFFFFF) ? 0 : (s_v_min_mv / 10);
                s32 vmax_c = s_v_max_mv / 10;
                lv_snprintf(cbuf, sizeof(cbuf), "V:%ld.%02ld..%ld.%02ldV Ip:%ld.%02ldA Drp:%u",
                            (long)(vmin_c / 100), (long)(vmin_c % 100),
                            (long)(vmax_c / 100), (long)(vmax_c % 100),
                            (long)(s_i_max_ma / 1000), (long)((s_i_max_ma % 1000) / 10),
                            (unsigned)s_brownout_cnt);
                lv_label_set_text(s_lbl_curve_info, cbuf);
            }
        }
    } else if (s_page == PAGE_METER && s_lbl_vbus) {
        lv_label_set_text(s_lbl_vbus, "INA226 ERR");
    }

    /* 2. PA10 AC-Ripple oscilloscope (when Ripple page active, every 400 ms) */
    if (s_page == PAGE_RIPPLE && (s_tick_div % 4U) == 0U) {
        u32 vpp = 0, freq_hz = 0;
        if (t3_bsp_capture_ripple(s_ripple_buf, RIPPLE_SAMPLES, &vpp, &freq_hz) == 0) {
            s_ripple_avg_mv = (s_ripple_avg_mv == 0) ? vpp : ((s_ripple_avg_mv * 3U + vpp) / 4U);
            u32 scale_max = (vpp < 40U) ? 50U : (vpp + 20U);
            for (int k = 0; k < PLOT_BARS; k++) {
                u32 mv = s_ripple_buf[k * 2];
                s32 h = (s32)((mv * 136U) / scale_max);
                if (h < 2) h = 2;
                if (h > 136) h = 136;
                lv_obj_set_size(s_ripple_bars[k], 6, h);
                lv_obj_set_pos(s_ripple_bars[k], 2 + k * 8, 146 - h);
            }

            const char *qual = "VERY CLEAN";
            u32 qcol = 0x4ADE80;
            if (s_ripple_avg_mv >= 80U) {
                qual = "HIGH NOISE";
                qcol = 0xEF4444;
            } else if (s_ripple_avg_mv >= 30U) {
                qual = "OK";
                qcol = 0xFACC15;
            }
            lv_obj_set_style_text_color(s_lbl_ripple_info, lv_color_hex(qcol), LV_PART_MAIN);
            char rbuf[64];
            if (freq_hz >= 1000U) {
                lv_snprintf(rbuf, sizeof(rbuf), "Vpp:%lumV Avg:%lumV %lu.%lukHz\nPSU: %s",
                            (unsigned long)vpp, (unsigned long)s_ripple_avg_mv,
                            (unsigned long)(freq_hz / 1000U), (unsigned long)((freq_hz % 1000U) / 100U), qual);
            } else {
                lv_snprintf(rbuf, sizeof(rbuf), "Vpp:%lumV Avg:%lumV %luHz\nPSU: %s",
                            (unsigned long)vpp, (unsigned long)s_ripple_avg_mv,
                            (unsigned long)freq_hz, qual);
            }
            lv_label_set_text(s_lbl_ripple_info, rbuf);
        }
    }

    /* 3. D+/D-, NTC & Barometer every 500 ms */
    if ((s_tick_div % 5U) == 0U) {
        t3_bsp_read_usb_dp_dm(&s_last_dp_mv, &s_last_dm_mv);
        t3_bsp_read_ntc_deci_c(&s_last_temp_dc);
        const char *proto = detect_usb_proto(s_last_v_mv, s_last_dp_mv, s_last_dm_mv, &s_last_proto_id);

        if (s_page == PAGE_METER && !s_cfg.big_mode) {
            char pbuf[52];
            s16 td = s_last_temp_dc;
            const char *ts = (td < 0) ? "-" : "";
            s16 ta = (td < 0) ? -td : td;
            lv_snprintf(pbuf, sizeof(pbuf), "%s  NTC:%s%d.%dC",
                        proto, ts, (int)(ta / 10), (int)(ta % 10));
            lv_label_set_text(s_lbl_proto_temp, pbuf);
        }

        if (s_page == PAGE_SENSORS) {
            char ubuf[64], dps[12], dms[12];
            fmt_fixed(dps, sizeof(dps), s_last_dp_mv, "V", 2);
            fmt_fixed(dms, sizeof(dms), s_last_dm_mv, "V", 2);
            lv_snprintf(ubuf, sizeof(ubuf), "%s\nD+:%s  D-:%s  NTC:%d.%dC",
                        proto, dps, dms,
                        (int)(s_last_temp_dc / 10),
                        (int)(s_last_temp_dc < 0 ? (-s_last_temp_dc) % 10 : s_last_temp_dc % 10));
            lv_label_set_text(s_lbl_sns_usb, ubuf);

            t3_baro_sample_t b;
            if (t3_bsp_read_baro(&b, s_cfg.sea_level_pa) == 0 && b.valid) {
                char bbuf[48];
                lv_snprintf(bbuf, sizeof(bbuf), "%s %lu.%02luhPa  %ld.%ldm",
                            b.is_ens220 ? "ENS" : "BMP",
                            (unsigned long)(b.pressure_pa / 100U),
                            (unsigned long)(b.pressure_pa % 100U),
                            (long)(b.altitude_dm / 10),
                            (long)(b.altitude_dm < 0 ? (-b.altitude_dm) % 10 : b.altitude_dm % 10));
                lv_label_set_text(s_lbl_sns_baro, bbuf);
            } else {
                lv_label_set_text(s_lbl_sns_baro, "Baro: not detected");
            }
        }
    }

    /* 4. SC7A20 Tilt/Knock page navigation & level bubble every 100 ms */
    if (s_tilt_cooldown > 0) s_tilt_cooldown--;
    if (s_cfg.tilt_nav_on || s_page == PAGE_SENSORS) {
        t3_accel_sample_t acc;
        if (t3_bsp_read_sc7a20(&acc) == 0 && acc.valid) {
            if (s_page == PAGE_SENSORS && s_obj_bubble) {
                int bx = 27 + (acc.ax / 45);
                int by = 27 + (acc.ay / 45);
                if (bx < 4) bx = 4;
                if (bx > 50) bx = 50;
                if (by < 4) by = 4;
                if (by > 50) by = 50;
                lv_obj_set_pos(s_obj_bubble, bx - 5, by - 5);
                char abuf[32];
                lv_snprintf(abuf, sizeof(abuf), "G: X=%d Y=%d", (int)(acc.ax / 11), (int)(acc.ay / 11));
                lv_label_set_text(s_lbl_sns_acc, abuf);
            }
            if (s_cfg.tilt_nav_on) {
                if (acc.ax > -250 && acc.ax < 250) {
                    s_tilt_armed = 1;
                } else if (s_tilt_armed && s_tilt_cooldown == 0) {
                    if (acc.ax > 550) {
                        s_tilt_armed = 0;
                        s_tilt_cooldown = 6;
                        buzz_ms(15);
                        switch_page((meter_page_t)((s_page + 1) % PAGE_COUNT));
                    } else if (acc.ax < -550) {
                        s_tilt_armed = 0;
                        s_tilt_cooldown = 6;
                        buzz_ms(15);
                        switch_page((meter_page_t)((s_page + PAGE_COUNT - 1) % PAGE_COUNT));
                    }
                }
            }
        }
    }

    /* 5. Blackbox log every 10 s */
    if ((s_tick_div % 100U) == 0U) {
        s_blackbox[s_bb_head].v_mv = (s_last_v_mv > 0) ? (u16)s_last_v_mv : 0U;
        s_blackbox[s_bb_head].i_ma = (s16)s_last_i_ma;
        s_blackbox[s_bb_head].p_cw = (u16)(s_last_p_mw / 10);
        s_blackbox[s_bb_head].temp_dc = s_last_temp_dc;
        s_bb_head = (u16)((s_bb_head + 1U) % BLACKBOX_LEN);
        if (s_bb_count < BLACKBOX_LEN) s_bb_count++;
    }
}

/* --- Page Builders --- */
static lv_obj_t *create_page_container(lv_obj_t *scr)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_set_size(p, 240, 218);
    lv_obj_set_pos(p, 0, 22);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x0B1018), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(p, 0, LV_PART_MAIN);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static void build_meter_page(lv_obj_t *p)
{
    s_lbl_vbus = make_lbl(p, 10, 6,  0x4ADE80, &lv_font_montserrat_28, "0.000 V");
    s_lbl_ibus = make_lbl(p, 10, 40, 0x38BDF8, &lv_font_montserrat_28, "0.000 A");
    s_lbl_pwr  = make_lbl(p, 10, 74, 0xFACC15, &lv_font_montserrat_28, "0.00 W");

    s_lbl_energy     = make_lbl(p, 10, 112, 0xE2E8F0, &lv_font_montserrat_14, "+0/-0mAh 3.7V:0mAh");
    s_lbl_peak_time  = make_lbl(p, 10, 132, 0x94A3B8, &lv_font_montserrat_14, "Pk:0.00W Ip:0.00A 00:00:00");
    s_lbl_proto_temp = make_lbl(p, 10, 152, 0xF472B6, &lv_font_montserrat_14, "USB 5V  NTC:25.0C");

    s_alarm_banner = lv_obj_create(p);
    lv_obj_set_size(s_alarm_banner, 224, 24);
    lv_obj_set_pos(s_alarm_banner, 8, 2);
    lv_obj_set_style_bg_color(s_alarm_banner, lv_color_hex(0x991B1B), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_alarm_banner, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_alarm_banner, 4, LV_PART_MAIN);
    lv_obj_clear_flag(s_alarm_banner, LV_OBJ_FLAG_SCROLLABLE);
    s_lbl_alarm = lv_label_create(s_alarm_banner);
    lv_obj_set_style_text_font(s_lbl_alarm, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_alarm, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_label_set_text(s_lbl_alarm, "ALARM");
    lv_obj_center(s_lbl_alarm);
    lv_obj_add_flag(s_alarm_banner, LV_OBJ_FLAG_HIDDEN);

    make_btn(p, 6,   180, 54, 28, 0x334155, "BNK A",  bank_cycle_cb, NULL, &s_lbl_bank_btn);
    make_btn(p, 64,  180, 48, 28, 0x1E293B, "BIG",    big_btn_cb, NULL, NULL);
    make_btn(p, 116, 180, 52, 28, 0x1E293B, "RST",    reset_counters_cb, NULL, NULL);
    make_btn(p, 172, 180, 62, 28, 0x0369A1, "ZERO I", set_zero_curr_cb, NULL, NULL);
    apply_big_mode_layout();
}

static void build_curve_page(lv_obj_t *p)
{
    lv_obj_t *box = lv_obj_create(p);
    lv_obj_set_size(box, 228, 168);
    lv_obj_set_pos(box, 6, 4);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (int k = 0; k < PLOT_BARS; k++) {
        s_curve_v_bars[k] = lv_obj_create(box);
        lv_obj_set_size(s_curve_v_bars[k], 5, 2);
        lv_obj_set_pos(s_curve_v_bars[k], 2 + k * 8, 158);
        lv_obj_set_style_bg_color(s_curve_v_bars[k], lv_color_hex(0xFACC15), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_curve_v_bars[k], 0, LV_PART_MAIN);
        lv_obj_clear_flag(s_curve_v_bars[k], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        s_curve_i_bars[k] = lv_obj_create(box);
        lv_obj_set_size(s_curve_i_bars[k], 3, 2);
        lv_obj_set_pos(s_curve_i_bars[k], 4 + k * 8, 158);
        lv_obj_set_style_bg_color(s_curve_i_bars[k], lv_color_hex(0x38BDF8), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_curve_i_bars[k], 0, LV_PART_MAIN);
        lv_obj_clear_flag(s_curve_i_bars[k], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    s_lbl_curve_info = make_lbl(p, 8, 180, 0xE2E8F0, &lv_font_montserrat_14, "V:0.00..0.00V Ip:0.00A Drp:0");
}

static void build_ripple_page(lv_obj_t *p)
{
    lv_obj_t *box = lv_obj_create(p);
    lv_obj_set_size(box, 228, 154);
    lv_obj_set_pos(box, 6, 4);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (int k = 0; k < PLOT_BARS; k++) {
        s_ripple_bars[k] = lv_obj_create(box);
        lv_obj_set_size(s_ripple_bars[k], 6, 2);
        lv_obj_set_pos(s_ripple_bars[k], 2 + k * 8, 144);
        lv_obj_set_style_bg_color(s_ripple_bars[k], lv_color_hex(0x4ADE80), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_ripple_bars[k], 0, LV_PART_MAIN);
        lv_obj_clear_flag(s_ripple_bars[k], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    s_lbl_ripple_info = make_lbl(p, 8, 166, 0x4ADE80, &lv_font_montserrat_14, "Vpp:0mV Avg:0mV 0Hz\nPSU: --");
}

static void build_cable_page(lv_obj_t *p)
{
    make_lbl(p, 10, 8, 0xFACC15, &lv_font_montserrat_20, "Cable & Droop Test");
    s_lbl_cable_v0 = make_lbl(p, 10, 42, 0xE2E8F0, &lv_font_montserrat_14, "V0 (No-Load): --");
    s_lbl_cable_res = make_lbl(p, 10, 68, 0x4ADE80, &lv_font_montserrat_14, "1. Save V0 with no load\n2. Apply load (~1A) & measure");
    make_btn(p, 10, 124, 220, 38, 0x0284C7, "1. SAVE V0 (NO-LOAD)", cable_v0_cb, NULL, NULL);
    make_btn(p, 10, 168, 220, 38, 0x16A34A, "2. MEASURE R & DROOP", cable_meas_cb, NULL, NULL);
}

static void build_sensors_page(lv_obj_t *p)
{
    s_lbl_sns_usb  = make_lbl(p, 8, 6,  0x38BDF8, &lv_font_montserrat_14, "USB 5V\nD+:0.00V  D-:0.00V  NTC:25.0C");
    s_lbl_sns_baro = make_lbl(p, 8, 44, 0x4ADE80, &lv_font_montserrat_14, "Baro: --");
    make_btn(p, 8, 68, 110, 26, 0x1E293B, "ALT = 0m", baro_zero_cb, NULL, NULL);

    lv_obj_t *lvl_box = lv_obj_create(p);
    lv_obj_set_size(lvl_box, 62, 62);
    lv_obj_set_pos(lvl_box, 89, 104);
    lv_obj_set_style_bg_color(lvl_box, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_border_color(lvl_box, lv_color_hex(0x475569), LV_PART_MAIN);
    lv_obj_set_style_border_width(lvl_box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(lvl_box, 31, LV_PART_MAIN);
    lv_obj_clear_flag(lvl_box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_obj_bubble = lv_obj_create(lvl_box);
    lv_obj_set_size(s_obj_bubble, 10, 10);
    lv_obj_set_pos(s_obj_bubble, 22, 22);
    lv_obj_set_style_bg_color(s_obj_bubble, lv_color_hex(0x4ADE80), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_obj_bubble, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_obj_bubble, 5, LV_PART_MAIN);
    lv_obj_clear_flag(s_obj_bubble, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_sns_acc = make_lbl(p, 8, 178, 0xE2E8F0, &lv_font_montserrat_14, "G: X=0 Y=0");
}

static void build_setup_page(lv_obj_t *p)
{
    s_setup_objs[0] = make_btn(p, 8,   6,  108, 30, 0x1E293B, "Bright: 90%",    set_bright_cb,       NULL, &s_lbl_set_bright);
    s_setup_objs[1] = make_btn(p, 124, 6,  108, 30, 0x1E293B, "Haptics: ON",    set_toggle_motor_cb, NULL, &s_lbl_set_motor);
    s_setup_objs[2] = make_btn(p, 8,   42, 108, 30, 0x1E293B, "Tilt-Nav: ON",   set_toggle_tilt_cb,  NULL, &s_lbl_set_tilt);
    s_setup_objs[3] = make_btn(p, 124, 42, 108, 30, 0x0F172A, "BLE Beacon: ON", set_toggle_ble_cb,   NULL, &s_lbl_set_ble);
    s_setup_objs[4] = make_btn(p, 8,   78, 108, 30, 0x0F172A, "Alarms: ON",     set_toggle_alarm_cb, NULL, &s_lbl_set_alarm);
    s_setup_objs[5] = make_btn(p, 124, 78, 108, 30, 0x334155, "Bank: A",        bank_cycle_cb,       NULL, &s_lbl_set_bank);
    s_setup_objs[6] = make_btn(p, 8,   114, 108, 30, 0x0369A1, "ZERO I",        set_zero_curr_cb,    NULL, NULL);
    s_setup_objs[7] = make_btn(p, 124, 114, 108, 30, 0x166534, "SAVE VM",       btn_save_vm_cb,      NULL, NULL);

    s_lbl_set_cal = make_lbl(p, 8, 152, 0x94A3B8, &lv_font_montserrat_14, "Cal V:100% I:100% Z:0mA");
    make_lbl(p, 8, 172, 0x64748B, &lv_font_montserrat_14, "BTN1:Next/Hold=Set  Tilt=Page");
    lv_obj_t *btn_ub = make_btn(p, 168, 190, 64, 22, 0x7F1D1D, "UBOOT", btn_uboot_hold_cb, NULL, NULL);
    lv_obj_remove_event_cb(btn_ub, btn_uboot_hold_cb);
    lv_obj_add_event_cb(btn_ub, btn_uboot_hold_cb, LV_EVENT_LONG_PRESSED, NULL);
    make_lbl(p, 8, 194, 0x64748B, &lv_font_montserrat_14, "T3 OpenMeter v1.5");

    refresh_setup_labels();
    update_setup_focus();
}

void t3_meter_app_create(void)
{
    t3_bsp_init();
    vm_load();
    t3_bsp_backlight_set(s_cfg.brightness);

    static lv_indev_drv_t s_meter_indev_drv;
    lv_indev_drv_init(&s_meter_indev_drv);
    s_meter_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_meter_indev_drv.read_cb = meter_touch_read_cb;
    lv_indev_drv_register(&s_meter_indev_drv);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1018), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Sleek 22px Top Header (Page Title + 6 Dots + Bank/BLE Badge) */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 240, 22);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hdr, 0, LV_PART_MAIN);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hdr, hdr_click_cb, LV_EVENT_CLICKED, NULL);

    s_hdr_title = make_lbl(hdr, 8, 3, 0x38BDF8, &lv_font_montserrat_14, "LIVE");
    for (int i = 0; i < PAGE_COUNT; i++) {
        s_hdr_dots[i] = lv_obj_create(hdr);
        lv_obj_set_size(s_hdr_dots[i], 7, 7);
        lv_obj_set_pos(s_hdr_dots[i], 84 + i * 12, 7);
        lv_obj_set_style_radius(s_hdr_dots[i], 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_hdr_dots[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(s_hdr_dots[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(s_hdr_dots[i], lv_color_hex(i == 0 ? 0x38BDF8 : 0x334155), LV_PART_MAIN);
    }
    s_hdr_badge = make_lbl(hdr, 174, 3, 0x94A3B8, &lv_font_montserrat_14, "A | BLE");

    for (int i = 0; i < PAGE_COUNT; i++) {
        s_pages[i] = create_page_container(scr);
    }

    build_meter_page(s_pages[PAGE_METER]);
    build_curve_page(s_pages[PAGE_CURVE]);
    build_ripple_page(s_pages[PAGE_RIPPLE]);
    build_cable_page(s_pages[PAGE_CABLE]);
    build_sensors_page(s_pages[PAGE_SENSORS]);
    build_setup_page(s_pages[PAGE_SETUP]);

    switch_page(PAGE_METER);
    meter_tick_cb(NULL);
    lv_timer_create(meter_tick_cb, 100, NULL);
}

void jl_gui_init(void)
{
    t3_meter_app_create();
}
