#ifndef T3_BOARD_H
#define T3_BOARD_H

/*
 * ATORCH / JUWEI T3 (Zhuhai JieLi AC707N / BR35) hardware pin & bus map.
 * Derived from static analysis of OEM firmware V2.0.4 + BR35 SDK headers.
 */

/* Display: Sitronix ST7789V 240x240 RGB565 IPS via JL_DBI */
#define T3_LCD_WIDTH            240
#define T3_LCD_HEIGHT           240
#define T3_LCD_RST_IO           IO_PORTC_00
#define T3_LCD_PWR_IO           IO_PORTA_07
#define T3_LCD_BL_IO            IO_LCD_PG
#define T3_LCD_PWM_FREQ_HZ      10000U

/* Capacitive Touch: Hynitron CST816D on I2C0 */
#define T3_TP_SCL_IO            IO_PORTA_06
#define T3_TP_SDA_IO            IO_PORTA_05
#define T3_TP_RST_IO            IO_PORTA_02
#define T3_TP_INT_IO            IO_PORTB_08
#define T3_TP_I2C_ADDR          0x15

/* Sensor Bus: I2C1 (bit-banged soft-I2C on PB02/PB01) */
#define T3_I2C1_SCL_IO          IO_PORTB_02
#define T3_I2C1_SDA_IO          IO_PORTB_01

/* TI INA226 Bidirectional Current/Voltage Monitor (7-bit address 0x40) */
#define T3_INA226_ADDR          0x40
#define T3_INA226_REG_CONFIG    0x00
#define T3_INA226_REG_SHUNT     0x01
#define T3_INA226_REG_BUS       0x02
#define T3_INA226_REG_DIE_ID    0xFF
#define T3_INA226_CFG_DEFAULT   0x4527U
#define T3_INA226_DIE_ID_VAL    0x2260U
#define T3_INA226_VBUS_LSB_V    0.0016123f  /* ~1.29:1 divider * 1.25 mV/LSB */
#define T3_INA226_IBUS_LSB_A    0.0005f     /* 2.5 uV/LSB / 5 mOhm = 0.5 mA/LSB */

/* Silan SC7A20 3-axis accelerometer (7-bit address 0x19) */
#define T3_SC7A20_ADDR          0x19
#define T3_SC7A20_REG_WHOAMI    0x0F
#define T3_SC7A20_WHOAMI_VAL    0x11

/* Barometer variants on I2C1 */
#define T3_BMP280_ADDR          0x77
#define T3_BMP280_REG_ID        0xD0
#define T3_BMP280_ID_VAL        0x58

#define T3_ENS220_ADDR          0x20
#define T3_ENS220_REG_STATUS    0x14

/* Haptic Vibration Motor on Power-Gate PWM */
#define T3_MOTOR_PWM_IO         IO_MT_PG
#define T3_MOTOR_PWM_FREQ       10000U

/* Internal GPADC Channels */
#define T3_NTC_IO               IO_PORTB_00
#define T3_NTC_ADC_CH           0x00100007U
#define T3_USB_DM_ADC_CH        0x00100009U
#define T3_USB_DP_ADC_CH        0x0010000aU
#define T3_RIPPLE_ADC_CH        0x00100004U

/* Flash App Slot Layout (2 MB SPI NOR) */
#define T3_APP_SLOT_OFFSET      0x005000U
#define T3_APP_SLOT_MAX_SIZE    0x133000U
#define T3_APP_SCRAMBLE_KEY     0x8D07U

#endif /* T3_BOARD_H */
