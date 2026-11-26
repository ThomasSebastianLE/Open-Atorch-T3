#!/usr/bin/env python3
"""
Apply (or restore) the idempotent ATORCH/JUWEI T3 BSP overlay on sdk/ac707n/SDK.

Always keeps <file>.t3orig backups on first touch and re-applies cleanly from
the original file so repeated runs are deterministic.
"""
from __future__ import annotations

import argparse
import pathlib
import shutil
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SDK_DIR = REPO_ROOT / "sdk" / "ac707n" / "SDK"
BSP_SRC = REPO_ROOT / "shared" / "t3-bsp"
BSP_DST = SDK_DIR / "apps" / "watch" / "t3_bsp"
DOOM_SRC = REPO_ROOT / "projects" / "t3-doom"
DOOM_DST = SDK_DIR / "apps" / "watch" / "t3_doom"
DOOM_ENGINE_SRC = DOOM_SRC / "upstream" / "nRF52840Doom" / "NRF52840Doom" / "Doom"
HENNER_SRC = REPO_ROOT / "projects" / "t3-hennergotchi"
HENNER_DST = SDK_DIR / "apps" / "watch" / "t3_hennergotchi"
METER_SRC = REPO_ROOT / "projects" / "t3-meter-fw"
METER_DST = SDK_DIR / "apps" / "watch" / "t3_meter_fw"

DOOM_EXCLUDED_SRCS = {
    "doom_iwad.c",
    "i_main.c",
    "i_video.c",
    "i_audio.c",
    "i_spi_support.c",
    "s_sound.c",
}

PATCHED_FILES = [
    "Makefile",
    "build/include_dir.txt",
    "build/Makefile.mk",
    "apps/watch/sdk_used_list.c",
    "apps/watch/board/br35/sdk_config.h",
    "apps/watch/include/app_config.h",
    "apps/watch/board/br35/board_ac707n_demo/board_ac707n_demo_cfg.h",
    "apps/watch/board/br35/board_ac707n_demo/board_ac707n_demo.c",
    "apps/watch/include/ui/lcd/lcd_conf.h",
    "cpu/br35/ui_driver/lcd_drive/lcd_spi/lcd_spi_st7789v_240x240.c",
    "apps/common/device/touch_panel/cst816d/cst816d.c",
    "apps/common/ui/lvgl_v8/lv_conf.h",
    "apps/common/device/usb/device/descriptor.c",
    "apps/common/device/usb/device/user_setup.c",
    "cpu/br35/tools/download.c",
]


def load_base(rel: str) -> tuple[pathlib.Path, str]:
    target = SDK_DIR / rel
    if not target.exists():
        raise FileNotFoundError(f"Missing SDK file: {target}")
    orig = target.with_name(target.name + ".t3orig")
    if not orig.exists():
        shutil.copy2(target, orig)
    return target, orig.read_text(encoding="utf-8", errors="surrogateescape")


def write_lf(path: pathlib.Path, content: str) -> None:
    content = content.replace("\r\n", "\n")
    new_bytes = content.encode("utf-8", errors="surrogateescape")
    if path.exists() and path.read_bytes() == new_bytes:
        return
    path.write_bytes(new_bytes)


def replace_once(text: str, old: str, new: str, rel: str) -> str:
    if old not in text:
        raise RuntimeError(f"[{rel}] Pattern not found:\n{old}")
    return text.replace(old, new, 1)


def sync_bsp_tree() -> None:
    if not (BSP_SRC / "include" / "t3_board.h").exists():
        raise FileNotFoundError(f"Missing BSP source directory: {BSP_SRC}")
    expected: set[pathlib.Path] = set()
    for sub in ("include", "src"):
        src_dir = BSP_SRC / sub
        dst_dir = BSP_DST / sub
        dst_dir.mkdir(parents=True, exist_ok=True)
        for src_file in src_dir.rglob("*"):
            if src_file.is_dir():
                continue
            rel = src_file.relative_to(BSP_SRC)
            dst_file = BSP_DST / rel
            dst_file.parent.mkdir(parents=True, exist_ok=True)
            data = src_file.read_bytes()
            if not (dst_file.exists() and dst_file.read_bytes() == data):
                dst_file.write_bytes(data)
            expected.add(dst_file)
    if BSP_DST.exists():
        for existing in list(BSP_DST.rglob("*")):
            if existing.is_file() and existing not in expected:
                existing.unlink()


def restore_all() -> None:
    restored = 0
    for rel in PATCHED_FILES:
        target = SDK_DIR / rel
        orig = target.with_name(target.name + ".t3orig")
        if orig.exists():
            shutil.copy2(orig, target)
            restored += 1
    if BSP_DST.exists():
        shutil.rmtree(BSP_DST)
    if DOOM_DST.exists():
        shutil.rmtree(DOOM_DST)
    if HENNER_DST.exists():
        shutil.rmtree(HENNER_DST)
    print(f"[apply_t3_bsp] Restored {restored} SDK files and removed {BSP_DST}")


def sync_tree(src_root: pathlib.Path, dst_root: pathlib.Path, subdirs: tuple[str, ...]) -> list[str]:
    synced_c: list[str] = []
    expected: set[pathlib.Path] = set()
    for sub in subdirs:
        src_dir = src_root / sub
        if not src_dir.exists():
            continue
        for src_file in src_dir.rglob("*"):
            if src_file.is_dir():
                continue
            rel = src_file.relative_to(src_root)
            dst_file = dst_root / rel
            dst_file.parent.mkdir(parents=True, exist_ok=True)
            data = src_file.read_bytes()
            if not (dst_file.exists() and dst_file.read_bytes() == data):
                dst_file.write_bytes(data)
            expected.add(dst_file)
            if dst_file.suffix == ".c":
                synced_c.append(dst_file.relative_to(SDK_DIR).as_posix())
    if dst_root.exists():
        for existing in list(dst_root.rglob("*")):
            if existing.is_file() and existing not in expected:
                existing.unlink()
    return sorted(synced_c)


def apply_all(project: str = "bringup") -> None:
    sync_bsp_tree()
    doom_c_files: list[str] = []
    if project == "doom":
        doom_c_files.extend(sync_tree(DOOM_SRC, DOOM_DST, ("include", "src")))
        engine_c = sync_tree(DOOM_ENGINE_SRC, DOOM_DST / "Doom", ("include", "source"))
        doom_c_files.extend(
            c for c in engine_c if pathlib.Path(c).name not in DOOM_EXCLUDED_SRCS
        )
    elif DOOM_DST.exists():
        shutil.rmtree(DOOM_DST)

    henner_c_files: list[str] = []
    if project == "henner":
        henner_c_files.extend(sync_tree(HENNER_SRC, HENNER_DST, ("include", "src")))
    elif HENNER_DST.exists():
        shutil.rmtree(HENNER_DST)

    meter_c_files: list[str] = []
    if project == "meter":
        meter_c_files.extend(sync_tree(METER_SRC, METER_DST, ("include", "src")))
    elif METER_DST.exists():
        shutil.rmtree(METER_DST)

    # 0. Makefile: enable parallel jobserver forwarding to build/Makefile.mk
    p, txt = load_base("Makefile")
    txt = replace_once(
        txt,
        "\tmake -f build/Makefile.mk",
        "\t+$(MAKE) -f build/Makefile.mk",
        "Makefile",
    )
    write_lf(p, txt)

    # 1. build/include_dir.txt
    p, txt = load_base("build/include_dir.txt")
    if project == "doom":
        txt = "-Iapps/watch/t3_doom/include\n-Iapps/watch/t3_doom/Doom/include\n" + txt.rstrip() + "\n-Iapps/watch/t3_bsp/include\n"
    elif project == "henner":
        txt = txt.rstrip() + "\n-Iapps/watch/t3_bsp/include\n-Iapps/watch/t3_hennergotchi/include\n"
    elif project == "meter":
        txt = txt.rstrip() + "\n-Iapps/watch/t3_bsp/include\n-Iapps/watch/t3_meter_fw/include\n"
    else:
        txt = txt.rstrip() + "\n-Iapps/watch/t3_bsp/include\n"
    write_lf(p, txt)

    # 2. build/Makefile.mk
    p, txt = load_base("build/Makefile.mk")
    if project == "doom":
        src_list = ["apps/watch/t3_bsp/src/t3_bsp.c"] + doom_c_files
    elif project == "henner":
        src_list = ["apps/watch/t3_bsp/src/t3_bsp.c"] + henner_c_files
    elif project == "meter":
        src_list = ["apps/watch/t3_bsp/src/t3_bsp.c"] + meter_c_files
    else:
        src_list = ["apps/watch/t3_bsp/src/t3_bsp.c", "apps/watch/t3_bsp/src/t3_bringup_ui.c"]
    txt = replace_once(
        txt,
        "include build/fileList.mk",
        "c_SRC_FILES += " + " ".join(src_list) + "\n\ninclude build/fileList.mk",
        "build/Makefile.mk",
    )
    write_lf(p, txt)

    # 3. apps/watch/board/br35/sdk_config.h
    p, txt = load_base("apps/watch/board/br35/sdk_config.h")
    repls = [
        ("#define CONFIG_FLASH_SIZE 0x800000 // flash容量", "#define CONFIG_FLASH_SIZE 0x200000 // flash容量 (2MB T3)"),
        ("#define TCFG_USB_SLAVE_MSD_ENABLE 1 // 读卡器使能", "#define TCFG_USB_SLAVE_MSD_ENABLE 0 // 读卡器使能"),
        ("#define TCFG_USB_SLAVE_AUDIO_SPK_ENABLE 1 // USB扬声器使能", "#define TCFG_USB_SLAVE_AUDIO_SPK_ENABLE 0 // USB扬声器使能"),
        ("#define TCFG_USB_SLAVE_AUDIO_MIC_ENABLE 1 // USB麦克风使能", "#define TCFG_USB_SLAVE_AUDIO_MIC_ENABLE 0 // USB麦克风使能"),
        ("#define TCFG_HW_SPI1_ENABLE 1 // 硬件SPI1", "#define TCFG_HW_SPI1_ENABLE 0 // 硬件SPI1 (T3: PA07 reserved for LCD)"),
        ("#define TCFG_HW_SPI2_ENABLE 1 // 硬件SPI2", "#define TCFG_HW_SPI2_ENABLE 0 // 硬件SPI2 (T3: PC00/PC02/PC03 in use)"),
        ("#define TCFG_APP_PC_EN 0 // PC模式", "#define TCFG_APP_PC_EN 1 // USB Slave enabled for T3 UBOOT hook\n#define TCFG_OTG_MODE (OTG_SLAVE_MODE)"),
        ("#define TCFG_ADKEY_ENABLE 1 // AD按键配置", "#define TCFG_ADKEY_ENABLE 0 // AD按键配置"),
        ("#define TCFG_LOWPOWER_LOWPOWER_SEL 2 // 低功耗模式", "#define TCFG_LOWPOWER_LOWPOWER_SEL 0 // USB-powered T3: disable deep sleep"),
        ("#define TCFG_SYS_LVD_EN 1 // 电池电量检测", "#define TCFG_SYS_LVD_EN 0 // USB-powered T3: disable battery LVD"),
        ("#define CONFIG_JL_UI_ENABLE 1 // 杰理UI", "#define CONFIG_JL_UI_ENABLE 0 // 杰理UI"),
        ("#define CONFIG_LVGL_UI_ENABLE 0 // LVGL", "#define CONFIG_LVGL_UI_ENABLE 1 // LVGL"),
        ("#define TCFG_LCD_PIN_RESET IO_PORTC_03 // LCD RESET", "#define TCFG_LCD_PIN_RESET IO_PORTC_00 // LCD RESET (T3 PC00)"),
        ("#define TCFG_LCD_PIN_DC IO_PORTA_02 // LCD DC", "#define TCFG_LCD_PIN_DC NO_CONFIG_PORT // T3: PA02 is CST816D RST"),
        ("#define TCFG_LCD_PIN_TE IO_PORTC_00 // LCD TE", "#define TCFG_LCD_PIN_TE NO_CONFIG_PORT // T3: PC00 is LCD RST"),
    ]
    if project in ("doom", "henner"):
        repls.extend([
            ("#define TCFG_APP_BT_EN 1 // 蓝牙模式", "#define TCFG_APP_BT_EN 0 // 蓝牙模式 disabled in t3-doom"),
            ("#define TCFG_USER_BT_CLASSIC_ENABLE 1 // 经典蓝牙开关", "#define TCFG_USER_BT_CLASSIC_ENABLE 0 // 经典蓝牙开关 disabled in t3-doom"),
            ("#define TCFG_USER_BLE_ENABLE 1 // BLE", "#define TCFG_USER_BLE_ENABLE 0 // BLE disabled in t3-doom"),
            ("#define TCFG_BT_AI_ENABLE 1 // AI配置", "#define TCFG_BT_AI_ENABLE 0 // AI配置"),
            ("#define TCFG_BT_AI_SEL_PROTOCOL RCSP_MODE_EN // AI协议选择", "#define TCFG_BT_AI_SEL_PROTOCOL 0 // AI协议选择"),
            ("#define TCFG_BT_SUPPORT_HFP 1 // HFP", "#define TCFG_BT_SUPPORT_HFP 0 // HFP"),
            ("#define TCFG_BT_SUPPORT_AVCTP 1 // AVRCP", "#define TCFG_BT_SUPPORT_AVCTP 0 // AVRCP"),
            ("#define TCFG_BT_SUPPORT_A2DP 1 // A2DP", "#define TCFG_BT_SUPPORT_A2DP 0 // A2DP"),
            ("#define TCFG_BT_SUPPORT_HID 1 // HID", "#define TCFG_BT_SUPPORT_HID 0 // HID"),
            ("#define TCFG_BT_SUPPORT_SPP 1 // SPP", "#define TCFG_BT_SUPPORT_SPP 0 // SPP"),
            ("#define TCFG_BT_SUPPORT_PNP 1 // PNP", "#define TCFG_BT_SUPPORT_PNP 0 // PNP"),
            ("#define TCFG_BT_SUPPORT_MAP 1 // MAP", "#define TCFG_BT_SUPPORT_MAP 0 // MAP"),
            ("#define TCFG_BT_SUPPORT_PBAP_LIST 1 // pbap单条查询", "#define TCFG_BT_SUPPORT_PBAP_LIST 0 // pbap"),
        ])
    for old, new in repls:
        txt = replace_once(txt, old, new, "sdk_config.h")
    write_lf(p, txt)

    # 4. apps/watch/include/app_config.h
    p, txt = load_base("apps/watch/include/app_config.h")
    extra_app_cfg = "#define USB_PC_NO_APP_MODE                        1"
    if project == "meter":
        extra_app_cfg += "\n#define TCFG_BT_BLE_ADV_ENABLE                    1"
        txt = replace_once(
            txt,
            "#define BT_AI_SEL_PROTOCOL  \t\t(AI_SEL_TRANS | AI_SEL_RCSP | AI_SEL_ALIPAY | AI_SEL_FMNA)",
            "#define BT_AI_SEL_PROTOCOL  \t\tRCSP_MODE_EN",
            "app_config.h (RCSP_MODE_EN)",
        )
    txt = replace_once(
        txt,
        "#define USB_PC_NO_APP_MODE                        0",
        extra_app_cfg,
        "app_config.h",
    )
    write_lf(p, txt)

    # 5. apps/watch/board/br35/board_ac707n_demo/board_ac707n_demo_cfg.h
    p, txt = load_base("apps/watch/board/br35/board_ac707n_demo/board_ac707n_demo_cfg.h")
    branch_2mb = """#elif ((!TCFG_NORFLASH_DEV_ENABLE)&&(!TCFG_NANDFLASH_DEV_ENABLE)&&(CONFIG_FLASH_SIZE == 2*1024*1024))
#define TCFG_VIRFAT_FLASH_ENABLE\t\t\t\t\tDISABLE
#define TCFG_VIRFAT_INSERT_FLASH_ENABLE  \t\t\tDISABLE
#define TCFG_VIRFAT_EXT_FLASH_ENABLE  \t    \t\tDISABLE
#define TCFG_SDFILE_INSERT_FLASH_ENABLE  \t\t\tDISABLE
#define TCFG_SDFILE_EXTERN_FLASH_ENABLE\t\t\t\tDISABLE
#define TCFG_DATA_STORAGE_FDB_ENABLE  \t\t\tDISABLE
#define TCFG_DATA_STORAGE_FDB_EXFLASH_ENABLE\tDISABLE
#define TCFG_DATA_STORAGE_VM_ENABLE   \t\t\tENABLE
//------------------------------------------------------------------------------------------
#else
#error "FLASH_DEV_PART_CONFIG_ERROR !!!"
"""
    txt = replace_once(
        txt,
        '#else\n#error "FLASH_DEV_PART_CONFIG_ERROR !!!"',
        branch_2mb,
        "board_ac707n_demo_cfg.h (flash)",
    )
    if project in ("doom", "henner", "meter"):
        txt = replace_once(
            txt,
            "#define TCFG_JL_WALLPAPER_ENABLE                        ENABLE_THIS_MOUDLE",
            "#define TCFG_JL_WALLPAPER_ENABLE                        DISABLE_THIS_MOUDLE",
            "board_ac707n_demo_cfg.h (wallpaper)",
        )
    if project in ("henner", "meter"):
        txt = replace_once(
            txt,
            "#define TCFG_TOUCH_PANEL_ENABLE \t\t\t\t1",
            "#define TCFG_TOUCH_PANEL_ENABLE \t\t\t\t0",
            "board_ac707n_demo_cfg.h (tp)",
        )
    repls_cfg = [
        ("#define TCFG_LCD_SPI_GC9B71_ENABLE  1", "#define TCFG_LCD_SPI_GC9B71_ENABLE  0\n#define TCFG_LCD_SPI_ST7789V_ENABLE 1"),
        ("#define TCFG_LCD_TE_USED_PEND 1", "#define TCFG_LCD_TE_USED_PEND 0"),
        ("#define TCFG_LCD_TE_QUICK_SYNC                  1//te优化策略", "#define TCFG_LCD_TE_QUICK_SYNC                  0//te优化策略"),
        ("#define TCFG_UI_SHUT_DOWN_TIME\t\t\t\t\tENABLE // 自动息屏", "#define TCFG_UI_SHUT_DOWN_TIME\t\t\t\t\tDISABLE // T3 keep display active"),
        ("#define TCFG_RDEC_KEY_ENABLE\t\t\t\t\tENABLE_THIS_MOUDLE //是否使能RDEC按键", "#define TCFG_RDEC_KEY_ENABLE\t\t\t\t\tDISABLE_THIS_MOUDLE // T3 uses DP/DM for USB"),
        ("#define TCFG_IMUSENSOR_ENABLE                \t1    //imu Sensor使能", "#define TCFG_IMUSENSOR_ENABLE                \t0    // Probed by shared/t3-bsp"),
        ("#define TCFG_LSM6DSL_ENABLE                     1", "#define TCFG_LSM6DSL_ENABLE                     0"),
        ("#define TCFG_SPORT_HEALTH_ENABLE\t\t\tENABLE", "#define TCFG_SPORT_HEALTH_ENABLE\t\t\tDISABLE"),
    ]
    for old, new in repls_cfg:
        txt = replace_once(txt, old, new, "board_ac707n_demo_cfg.h")
    write_lf(p, txt)

    # 6. apps/watch/board/br35/board_ac707n_demo/board_ac707n_demo.c
    p, txt = load_base("apps/watch/board/br35/board_ac707n_demo/board_ac707n_demo.c")
    txt = replace_once(
        txt,
        "    .pin_reset = TCFG_LCD_PIN_RESET,\n"
        "    .pin_en = IO_PORTC_01,\n"
        "    .pin_en_ex = IO_PORTC_02,\n"
        "    .pin_te = TCFG_LCD_PIN_TE,\n"
        "    .pin_bl = TCFG_LCD_PIN_BL,",
        "    .pin_reset = TCFG_LCD_PIN_RESET,\n"
        "    .pin_en = NO_CONFIG_PORT,\n"
        "    .pin_en_ex = NO_CONFIG_PORT,\n"
        "    .pin_te = NO_CONFIG_PORT,\n"
        "    .pin_bl = TCFG_LCD_PIN_BL,",
        "board_ac707n_demo.c",
    )
    write_lf(p, txt)

    # 7. apps/watch/include/ui/lcd/lcd_conf.h
    p, txt = load_base("apps/watch/include/ui/lcd/lcd_conf.h")
    txt = replace_once(
        txt,
        "#if defined(TCFG_LCD_RGB_ST7789V_ENABLE) && TCFG_LCD_RGB_ST7789V_ENABLE",
        "#if (defined(TCFG_LCD_RGB_ST7789V_ENABLE) && TCFG_LCD_RGB_ST7789V_ENABLE) || (defined(TCFG_LCD_SPI_ST7789V_ENABLE) && TCFG_LCD_SPI_ST7789V_ENABLE)",
        "lcd_conf.h",
    )
    write_lf(p, txt)

    # 8. cpu/br35/ui_driver/lcd_drive/lcd_spi/lcd_spi_st7789v_240x240.c
    p, txt = load_base("cpu/br35/ui_driver/lcd_drive/lcd_spi/lcd_spi_st7789v_240x240.c")
    txt = replace_once(
        txt,
        "#include \"app_config.h\"",
        "#include \"app_config.h\"\n#include \"ui/lcd/lcd_drive.h\"\n#include \"ui/lcd/lcd_conf.h\"",
        "lcd_spi_st7789v_240x240.c (includes1)",
    )
    txt = replace_once(
        txt,
        "#include \"imd.h\"\n#include \"asm/imb.h\"\n#include \"system/includes.h\"\n#include \"ui/ui_api.h\"",
        "#include \"includes.h\"\n#include \"ui/ui_api.h\"",
        "lcd_spi_st7789v_240x240.c (includes2)",
    )
    txt = replace_once(
        txt,
        "#define SCR_X       0",
        "#ifndef LCD_DRIVE_CONFIG\n#define LCD_DRIVE_CONFIG SPI_4WIRE_RGB565_1T8B\n#endif\n\n#define SCR_X       0",
        "lcd_spi_st7789v_240x240.c (config)",
    )
    txt = replace_once(
        txt,
        "        .spi_mode\t= SPI_IF_MODE,\n"
        "        .pixel_type = PIXEL_TYPE,\n"
        "        .out_format = OUT_FORMAT,",
        "        .spi_mode\t= SPI_IF_MODE(LCD_DRIVE_CONFIG),\n"
        "        .pixel_type = PIXEL_TYPE(LCD_DRIVE_CONFIG),\n"
        "        .out_format = OUT_FORMAT(LCD_DRIVE_CONFIG),",
        "lcd_spi_st7789v_240x240.c (macros)",
    )
    for old_tx, new_tx in [
        ("    .te_en = false,\n", ""),
        ("static void lcd_entersleep(void)",
         "static void delay_2ms(int cnt)\n"
         "{\n"
         "    if (cnt * 2 > 10) {\n"
         "        os_time_dly(cnt * 2 / 10 + 1);\n"
         "    } else {\n"
         "        mdelay(2 * cnt);\n"
         "    }\n"
         "}\n\n"
         "static void lcd_entersleep(void)"),
        ("imd_spi_tx_cmd(0x28, 1);", "lcd_write_cmd(0x28, NULL, 0);"),
        ("imd_spi_tx_cmd(0x10, 1);", "lcd_write_cmd(0x10, NULL, 0);"),
        ("imd_spi_tx_cmd(0x11, 1);", "lcd_write_cmd(0x11, NULL, 0);"),
        ("imd_spi_tx_cmd(0x29, 1);", "lcd_write_cmd(0x29, NULL, 0);"),
        ("REGISTER_LCD_DEVICE() = {", "REGISTER_LCD_DEVICE(st7789v) = {"),
    ]:
        txt = replace_once(txt, old_tx, new_tx, f"lcd_spi_st7789v_240x240.c ({old_tx})")
    write_lf(p, txt)

    # 9. apps/common/device/touch_panel/cst816d/cst816d.c
    p, txt = load_base("apps/common/device/touch_panel/cst816d/cst816d.c")
    txt = replace_once(
        txt,
        "#else\n#define Y_MIRROR  \t\t1",
        "#elif (defined TCFG_LCD_SPI_ST7789V_ENABLE && TCFG_LCD_SPI_ST7789V_ENABLE)\n"
        "#define X_MIRROR  \t\t0\n"
        "#define Y_MIRROR  \t\t0\n"
        "#define VK_X      \t\t240\n"
        "#define VK_Y            240\n"
        "#define TP_FPS\t\t\t60\n"
        "#else\n#define Y_MIRROR  \t\t1",
        "cst816d.c",
    )
    txt = replace_once(
        txt,
        "    if (!cst816d_upgrade()) {\n"
        "        log_error(\"cst816d_upgrade fail!\\n\");\n"
        "        /* ASSERT(0); */\n"
        "    }\n",
        "    /* T3 safety: NEVER overwrite CST816D/T flash with SDK 320x386 watch firmware */\n"
        "    (void)cst816d_upgrade;\n",
        "cst816d.c (disable upgrade)",
    )
    write_lf(p, txt)

    # 10. apps/common/ui/lvgl_v8/lv_conf.h
    p, txt = load_base("apps/common/ui/lvgl_v8/lv_conf.h")
    txt = replace_once(txt, "#define LV_FONT_MONTSERRAT_14 0", "#define LV_FONT_MONTSERRAT_14 1", "lv_conf.h (14)")
    txt = replace_once(txt, "#define LV_FONT_MONTSERRAT_20 0", "#define LV_FONT_MONTSERRAT_20 1", "lv_conf.h (20)")
    txt = replace_once(txt, "#define LV_FONT_MONTSERRAT_28 0", "#define LV_FONT_MONTSERRAT_28 1", "lv_conf.h (28)")
    txt = replace_once(
        txt,
        "#define LV_FONT_DEFAULT &lv_font_montserrat_none",
        "#define LV_FONT_DEFAULT &lv_font_montserrat_14",
        "lv_conf.h (default)",
    )
    write_lf(p, txt)

    # 11. apps/common/device/usb/device/descriptor.c
    p, txt = load_base("apps/common/device/usb/device/descriptor.c")
    txt = replace_once(
        txt,
        "    0x54, 0x36,     // idVendor: 0x3654 - Zhuhai Jieli Technology\n"
        "    'U', 'A',     // idProduct: chip id",
        "    0x83, 0x04,     // idVendor: 0x0483 (T3 USB-UBOOT compat)\n"
        "    0x50, 0x57,     // idProduct: 0x5750 (T3 USB-UBOOT compat)",
        "descriptor.c",
    )
    write_lf(p, txt)

    # 11b. apps/common/device/usb/device/user_setup.c (live touch/sensor USB diag)
    p, txt = load_base("apps/common/device/usb/device/user_setup.c")
    txt = replace_once(
        txt,
        "        if ((req->wValue == 0xfc) && (req->wIndex == 0xfe)) {",
        "        if ((req->wValue == 0xfd) && (req->wIndex == 0xfe) && (req->wLength == 16)) {\n"
        "            extern void t3_bsp_get_usb_diag(u8 out16[16]);\n"
        "            u8 diag_buf[16];\n"
        "            t3_bsp_get_usb_diag(diag_buf);\n"
        "            usb_set_data_payload(usb_device, req, diag_buf, sizeof(diag_buf));\n"
        "            ret = 1;\n"
        "            break;\n"
        "        }\n"
        "        if ((req->wValue == 0xfc) && (req->wIndex == 0xfe)) {",
        "user_setup.c",
    )
    write_lf(p, txt)

    # 12. cpu/br35/tools/download.c
    p, txt = load_base("cpu/br35/tools/download.c")
    txt = replace_once(
        txt,
        "##!/bin/sh\n",
        "##!/bin/sh\nset -e\ncd \"$(dirname \"$0\")\"\n",
        "download.c (cd)",
    )
    txt = replace_once(
        txt,
        "/opt/utils/report_segment_usage --sdk_path ${ROOT} \\\n"
        "--tbl_file symbol_tbl.txt \\\n"
        "--seg_file segment_list.txt \\\n"
        "--map_file $1.map \\\n"
        "--module_depth \"{\\\"apps\\\":1,\\\"lib\\\":2,\\\"[lib]\\\":2}\"",
        "if [ -x /opt/utils/report_segment_usage ]; then\n"
        "/opt/utils/report_segment_usage --sdk_path ${ROOT} \\\n"
        "--tbl_file symbol_tbl.txt \\\n"
        "--seg_file segment_list.txt \\\n"
        "--map_file $1.map \\\n"
        "--module_depth \"{\\\"apps\\\":1,\\\"lib\\\":2,\\\"[lib]\\\":2}\"\n"
        "fi",
        "download.c (report_segment_usage)",
    )
    txt = replace_once(
        txt,
        "host-client -project ${NICKNAME}$2 -f ${files} $1.elf",
        "if command -v host-client >/dev/null 2>&1; then\n"
        "host-client -project ${NICKNAME}$2 -f ${files} $1.elf\n"
        "fi",
        "download.c (host-client)",
    )
    write_lf(p, txt)

    # 13. apps/watch/sdk_used_list.c (preserve symbols called by precompiled .a libs across LTO)
    p, txt = load_base("apps/watch/sdk_used_list.c")
    txt = txt.rstrip() + (
        "\nlcd_get_te_frame_period_us\n"
        "lcd_get_te_line_period_us\n"
        "lcd_get_spi_frame_period_us\n"
        "lv_res_fopen\n"
        "lv_res_fget_attrs\n"
        "lv_res_fclose\n"
        "jl_wallpaper_sharing_recv\n"
        "check_power_on_voltage\n"
        "get_vbat_value\n"
        "get_vbat_percent\n"
        "get_self_battery_level\n"
    )
    write_lf(p, txt)

    # 14. apps/watch/ble/bt_ble.c (custom T3 meter BLE beacon payload & toggle)
    p, txt = load_base("apps/watch/ble/bt_ble.c")
    txt = replace_once(
        txt,
        "__attribute__((weak))\n"
        "void ble_module_enable(u8 en)\n"
        "{\n"
        "    log_info(\"mode_en:%d\\n\", en);\n"
        "    bt_ble_adv_enable(en);\n"
        "}",
        "__attribute__((weak))\n"
        "void ble_module_enable(u8 en)\n"
        "{\n"
        "    log_info(\"mode_en:%d\\n\", en);\n"
        "    bt_ble_adv_enable(en);\n"
        "}\n\n"
        "__attribute__((weak))\n"
        "void t3_ble_beacon_update(u8 enable, const u8 *payload16)\n"
        "{\n"
        "    if (!enable) {\n"
        "        bt_ble_adv_enable(0);\n"
        "        return;\n"
        "    }\n"
        "    if (payload16) {\n"
        "        u8 off = 0;\n"
        "        adv_data[off++] = 2;\n"
        "        adv_data[off++] = 0x01;\n"
        "        adv_data[off++] = 0x06;\n"
        "        adv_data[off++] = 7;\n"
        "        adv_data[off++] = 0x08;\n"
        "        adv_data[off++] = 'T';\n"
        "        adv_data[off++] = '3';\n"
        "        adv_data[off++] = '_';\n"
        "        adv_data[off++] = 'M';\n"
        "        adv_data[off++] = 'T';\n"
        "        adv_data[off++] = 'R';\n"
        "        adv_data[off++] = 19;\n"
        "        adv_data[off++] = 0xFF;\n"
        "        adv_data[off++] = 0x33;\n"
        "        adv_data[off++] = 0x54;\n"
        "        memcpy(&adv_data[off], payload16, 16);\n"
        "        off += 16;\n"
        "        adv_data_len = off;\n"
        "        ble_user_cmd_prepare(BLE_CMD_ADV_DATA, 2, adv_data_len, adv_data);\n"
        "    }\n"
        "    bt_ble_adv_enable(1);\n"
        "}",
        "bt_ble.c (t3_ble_beacon_update)",
    )
    write_lf(p, txt)

    # 15. Native RCSP WebBLE GATT (0xFFE0 / 0xFFE1 / 0xFFE2 as T3_MTR) for meter project
    if project == "meter":
        p, txt = load_base("apps/common/third_party_profile/multi_protocol_main.c")
        txt = replace_once(
            txt,
            "    // END\n    0x00, 0x00,\n};",
            "    // 0x0050 PRIMARY_SERVICE ffe0 (T3 ATorch WebBLE)\n"
            "    0x0a, 0x00, 0x02, 0x00, 0x50, 0x00, 0x00, 0x28, 0xe0, 0xff,\n"
            "    // 0x0051 CHARACTERISTIC ffe1 WRITE_WITHOUT_RESPONSE | WRITE | NOTIFY\n"
            "    0x0d, 0x00, 0x02, 0x00, 0x51, 0x00, 0x03, 0x28, 0x1c, 0x52, 0x00, 0xe1, 0xff,\n"
            "    // 0x0052 VALUE ffe1 WRITE_WITHOUT_RESPONSE | WRITE | NOTIFY | DYNAMIC\n"
            "    0x08, 0x00, 0x1c, 0x01, 0x52, 0x00, 0xe1, 0xff,\n"
            "    // 0x0053 CLIENT_CHARACTERISTIC_CONFIGURATION\n"
            "    0x0a, 0x00, 0x0a, 0x01, 0x53, 0x00, 0x02, 0x29, 0x00, 0x00,\n"
            "    // 0x0054 CHARACTERISTIC ffe2 WRITE_WITHOUT_RESPONSE | WRITE\n"
            "    0x0d, 0x00, 0x02, 0x00, 0x54, 0x00, 0x03, 0x28, 0x0c, 0x55, 0x00, 0xe2, 0xff,\n"
            "    // 0x0055 VALUE ffe2 WRITE_WITHOUT_RESPONSE | WRITE | DYNAMIC\n"
            "    0x08, 0x00, 0x0c, 0x01, 0x55, 0x00, 0xe2, 0xff,\n\n"
            "    // END\n    0x00, 0x00,\n};",
            "multi_protocol_main.c (0xFFE0)",
        )
        write_lf(p, txt)

        p, txt = load_base("apps/common/third_party_profile/jieli/rcsp/adv/ble_rcsp_adv.c")
        txt = replace_once(
            txt,
            "int rcsp_make_set_adv_data(void)\n"
            "{\n"
            "    u8 i;\n"
            "    u8 *buf = adv_data;\n"
            "    buf[0] = 0x1E;\n"
            "    buf[1] = 0xFF;",
            "int rcsp_make_set_adv_data(void)\n"
            "{\n"
            "    u8 offset = 0;\n"
            "    u8 *buf = adv_data;\n"
            "    memset(buf, 0, 31);\n"
            "    offset += make_eir_packet_val(&buf[offset], offset, HCI_EIR_DATATYPE_FLAGS, 0x06, 1);\n"
            "    offset += make_eir_packet_val(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS, 0xFFE0, 2);\n"
            "    offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)\"T3_MTR\", 6);\n"
            "    __this->modify_flag = 0;\n"
            "    adv_data_len = offset;\n"
            "    app_ble_adv_data_set(rcsp_server_ble_hdl, buf, offset);\n"
            "    app_ble_adv_data_set(rcsp_server_ble_hdl1, buf, offset);\n"
            "    return 0;\n"
            "}\n\n"
            "static int rcsp_make_set_adv_data_orig_unused(void)\n"
            "{\n"
            "    u8 i;\n"
            "    u8 *buf = adv_data;\n"
            "    buf[0] = 0x1E;\n"
            "    buf[1] = 0xFF;",
            "ble_rcsp_adv.c (adv T3_MTR + 0xFFE0)",
        )
        txt = replace_once(
            txt,
            "    offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)edr_name, name_len);\n"
            "    scan_rsp_data_len = offset;\n"
            "    log_info(\"rsp_data(%d):\", offset);\n"
            "    log_info_hexdump(buf, offset);\n"
            "    /* ble_op_set_rsp_data(offset, buf); */\n"
            "    app_ble_rsp_data_set(rcsp_server_ble_hdl, buf, 31);\n"
            "    app_ble_rsp_data_set(rcsp_server_ble_hdl1, buf, 31);",
            "    static const u8 ffe0_uuid_16bit[] = {0xE0, 0xFF};\n"
            "    offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS, (void *)ffe0_uuid_16bit, 2);\n"
            "    offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)\"T3_MTR\", 6);\n"
            "    scan_rsp_data_len = offset;\n"
            "    log_info(\"rsp_data(%d):\", offset);\n"
            "    log_info_hexdump(buf, offset);\n"
            "    app_ble_rsp_data_set(rcsp_server_ble_hdl, buf, offset);\n"
            "    app_ble_rsp_data_set(rcsp_server_ble_hdl1, buf, offset);",
            "ble_rcsp_adv.c (rsp T3_MTR + 0xFFE0)",
        )
        write_lf(p, txt)

        p, txt = load_base("apps/common/third_party_profile/jieli/rcsp/ble_rcsp_server.c")
        txt = replace_once(
            txt,
            "void bt_rcsp_custom_att_write_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)\n{",
            "static u8 s_t3_ffe1_notify_en = 0;\n\n"
            "void bt_rcsp_custom_att_write_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)\n"
            "{\n"
            "    if (att_handle == 0x0053) {\n"
            "        check_connetion_updata_deal(connection_handle);\n"
            "        s_t3_ffe1_notify_en = (buffer && buffer_size > 0 && buffer[0]) ? 1U : 0U;\n"
            "    } else if (att_handle == 0x0052 || att_handle == 0x0055) {\n"
            "        extern void t3_ble_on_rx(const u8 *data, u16 len);\n"
            "        t3_ble_on_rx(buffer, buffer_size);\n"
            "    }",
            "ble_rcsp_server.c (0xFFE1/0xFFE2 write)",
        )
        txt = replace_once(
            txt,
            "uint16_t bt_rcsp_custom_att_read_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)",
            "int t3_ble_gatt_notify(const u8 *data, u16 len)\n"
            "{\n"
            "    if (!s_t3_ffe1_notify_en || !data || len == 0) {\n"
            "        return -1;\n"
            "    }\n"
            "    if (app_ble_get_hdl_con_handle(rcsp_server_ble_hdl)) {\n"
            "        return app_ble_att_send_data(rcsp_server_ble_hdl, 0x0052, (u8 *)data, len, ATT_OP_NOTIFY);\n"
            "    }\n"
            "    if (app_ble_get_hdl_con_handle(rcsp_server_ble_hdl1)) {\n"
            "        return app_ble_att_send_data(rcsp_server_ble_hdl1, 0x0052, (u8 *)data, len, ATT_OP_NOTIFY);\n"
            "    }\n"
            "    return -1;\n"
            "}\n\n"
            "uint16_t bt_rcsp_custom_att_read_callback(void *hdl, hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)",
            "ble_rcsp_server.c (t3_ble_gatt_notify)",
        )
        write_lf(p, txt)

    print(f"[apply_t3_bsp] Synced BSP -> {BSP_DST} and patched {len(PATCHED_FILES)} SDK files.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Apply or restore T3 BSP overlay on AC707N SDK.")
    parser.add_argument("--restore", action="store_true", help="Restore original SDK files from .t3orig backups")
    parser.add_argument("--project", choices=("bringup", "doom", "henner", "meter"), default="bringup", help="Target T3 subproject")
    args = parser.parse_args()
    if args.restore:
        restore_all()
    else:
        apply_all(project=args.project)
    return 0


if __name__ == "__main__":
    sys.exit(main())
