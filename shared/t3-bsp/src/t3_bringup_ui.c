#include "lvgl.h"
#include "system/includes.h"

#include "t3_board.h"
#include "t3_bsp.h"
#include "t3_bringup_ui.h"

static lv_obj_t *s_lbl_vbus = NULL;
static lv_obj_t *s_lbl_ibus = NULL;
static lv_obj_t *s_lbl_pwr = NULL;
static lv_obj_t *s_lbl_sensors = NULL;
static lv_obj_t *s_lbl_touch = NULL;
static lv_obj_t *s_lbl_accel_key = NULL;

static char s_vbus_buf[32];
static char s_ibus_buf[32];
static char s_pwr_buf[32];
static char s_sensor_buf[64];
static char s_touch_buf[32];
static char s_accel_key_buf[48];

static void format_mv_v(char *dst, size_t cap, const char *prefix, s32 mv)
{
    s32 neg = 0;
    if (mv < 0) {
        neg = 1;
        mv = -mv;
    }
    s32 whole = mv / 1000;
    s32 frac = mv % 1000;
    lv_snprintf(dst, cap, "%s %s%ld.%03ld V", prefix, neg ? "-" : "", (long)whole, (long)frac);
}

static void format_ma_a(char *dst, size_t cap, const char *prefix, s32 ma)
{
    s32 neg = 0;
    if (ma < 0) {
        neg = 1;
        ma = -ma;
    }
    s32 whole = ma / 1000;
    s32 frac = ma % 1000;
    lv_snprintf(dst, cap, "%s %s%ld.%03ld A", prefix, neg ? "-" : "", (long)whole, (long)frac);
}

static void format_mw_w(char *dst, size_t cap, const char *prefix, s32 mw)
{
    s32 whole = mw / 1000;
    s32 frac = mw % 1000;
    lv_snprintf(dst, cap, "%s %ld.%03ld W", prefix, (long)whole, (long)frac);
}

static void update_telemetry_cb(lv_timer_t *timer)
{
    (void)timer;
    t3_meter_sample_t sample;
    if (t3_bsp_read_ina226(&sample) == 0 && sample.valid) {
        format_mv_v(s_vbus_buf, sizeof(s_vbus_buf), "V:", sample.vbus_mv);
        format_ma_a(s_ibus_buf, sizeof(s_ibus_buf), "I:", sample.ibus_ma);
        format_mw_w(s_pwr_buf, sizeof(s_pwr_buf), "P:", sample.power_mw);
    } else {
        lv_snprintf(s_vbus_buf, sizeof(s_vbus_buf), "V: ---");
        lv_snprintf(s_ibus_buf, sizeof(s_ibus_buf), "I: ---");
        lv_snprintf(s_pwr_buf, sizeof(s_pwr_buf), "P: ---");
    }

    if (s_lbl_vbus) {
        lv_label_set_text_static(s_lbl_vbus, s_vbus_buf);
    }
    if (s_lbl_ibus) {
        lv_label_set_text_static(s_lbl_ibus, s_ibus_buf);
    }
    if (s_lbl_pwr) {
        lv_label_set_text_static(s_lbl_pwr, s_pwr_buf);
    }

    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_point_t pt;
            lv_indev_get_point(indev, &pt);
            lv_snprintf(s_touch_buf, sizeof(s_touch_buf), "TP: X=%3d Y=%3d", (int)pt.x, (int)pt.y);
            if (s_lbl_touch) {
                lv_label_set_text_static(s_lbl_touch, s_touch_buf);
            }
            break;
        }
        indev = lv_indev_get_next(indev);
    }

    t3_accel_sample_t acc;
    const char *pin_name = NULL;
    u32 key_mask = t3_bsp_scan_keys(&pin_name);
    if (t3_bsp_read_sc7a20(&acc) == 0 && acc.valid) {
        lv_snprintf(
            s_accel_key_buf,
            sizeof(s_accel_key_buf),
            "AX:%4d AY:%4d K:%s",
            (int)acc.ax,
            (int)acc.ay,
            pin_name ? pin_name : (key_mask ? "LOW" : "--")
        );
    } else {
        lv_snprintf(
            s_accel_key_buf,
            sizeof(s_accel_key_buf),
            "SC7:--  K:%s",
            pin_name ? pin_name : (key_mask ? "LOW" : "--")
        );
    }
    if (s_lbl_accel_key) {
        lv_label_set_text_static(s_lbl_accel_key, s_accel_key_buf);
    }
}

static void btn_buzz_cb(lv_event_t *e)
{
    (void)e;
    t3_bsp_motor_buzz(200, 100);
}

static void btn_uboot_cb(lv_event_t *e)
{
    (void)e;
    t3_bsp_enter_uboot();
}

void t3_bringup_ui_create(void)
{
    t3_bsp_init();
    t3_bsp_backlight_set(80);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1018), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text_static(title, "T3 BSP BRING-UP  AC707N");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    /* Live INA226 card */
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 220, 78);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x162030), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A3B55), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_vbus = lv_label_create(card);
    lv_obj_set_style_text_color(s_lbl_vbus, lv_color_hex(0x4ADE80), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_vbus, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_vbus, LV_ALIGN_TOP_LEFT, 2, 0);

    s_lbl_ibus = lv_label_create(card);
    lv_obj_set_style_text_color(s_lbl_ibus, lv_color_hex(0xFACC15), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_ibus, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_ibus, LV_ALIGN_LEFT_MID, 2, 2);

    s_lbl_pwr = lv_label_create(card);
    lv_obj_set_style_text_color(s_lbl_pwr, lv_color_hex(0xF472B6), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_pwr, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_lbl_pwr, LV_ALIGN_BOTTOM_LEFT, 2, 0);

    /* Sensor probe status */
    const t3_sensor_inventory_t *inv = t3_bsp_get_inventory();
    lv_snprintf(
        s_sensor_buf,
        sizeof(s_sensor_buf),
        "INA:%s  SC7:%s  BMP:%s  ENS:%s",
        inv->ina226_ok ? "OK" : "--",
        inv->sc7a20_ok ? "OK" : "--",
        inv->bmp280_ok ? "OK" : "--",
        inv->ens220_ok ? "OK" : "--"
    );
    s_lbl_sensors = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_sensors, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_sensors, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text_static(s_lbl_sensors, s_sensor_buf);
    lv_obj_align(s_lbl_sensors, LV_ALIGN_TOP_MID, 0, 112);

    /* Touch coordinate indicator */
    lv_snprintf(s_touch_buf, sizeof(s_touch_buf), "TP: X=--- Y=---");
    s_lbl_touch = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_touch, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_touch, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text_static(s_lbl_touch, s_touch_buf);
    lv_obj_align(s_lbl_touch, LV_ALIGN_TOP_MID, 0, 132);

    /* Accel + live physical key scanner */
    lv_snprintf(s_accel_key_buf, sizeof(s_accel_key_buf), "AX: --- AY: --- K:--");
    s_lbl_accel_key = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_accel_key, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_accel_key, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text_static(s_lbl_accel_key, s_accel_key_buf);
    lv_obj_align(s_lbl_accel_key, LV_ALIGN_TOP_MID, 0, 152);

    /* Action buttons */
    lv_obj_t *btn_buzz = lv_btn_create(scr);
    lv_obj_set_size(btn_buzz, 98, 44);
    lv_obj_align(btn_buzz, LV_ALIGN_BOTTOM_LEFT, 14, -14);
    lv_obj_set_style_bg_color(btn_buzz, lv_color_hex(0x2563EB), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_buzz, btn_buzz_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b = lv_label_create(btn_buzz);
    lv_label_set_text_static(lbl_b, "BUZZ");
    lv_obj_center(lbl_b);

    lv_obj_t *btn_uboot = lv_btn_create(scr);
    lv_obj_set_size(btn_uboot, 98, 44);
    lv_obj_align(btn_uboot, LV_ALIGN_BOTTOM_RIGHT, -14, -14);
    lv_obj_set_style_bg_color(btn_uboot, lv_color_hex(0xDC2626), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_uboot, btn_uboot_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_u = lv_label_create(btn_uboot);
    lv_label_set_text_static(lbl_u, "UBOOT");
    lv_obj_center(lbl_u);

    update_telemetry_cb(NULL);
    lv_timer_create(update_telemetry_cb, 250, NULL);
}

void jl_gui_init(void)
{
    t3_bringup_ui_create();
}
