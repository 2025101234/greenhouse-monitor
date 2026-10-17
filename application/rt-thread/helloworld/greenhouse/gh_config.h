#ifndef GREENHOUSE_CONFIG_H
#define GREENHOUSE_CONFIG_H

/* Physical board confirmed from the PCB silkscreen and board photographs. */
#define GH_BOARD_NAME                   "ArtInChip D12X-DEMO68"
#define GH_BOARD_REVISION               "V1.2"

/*
 * Real DHT11 and BH1750 modes are enabled for the greenhouse configuration.
 * Set GH_USE_SIMULATED_SENSORS to 1 temporarily if you need to test the UI
 * before the external sensors are connected.
 */
#define GH_USE_SIMULATED_SENSORS       0
#define GH_USE_DHT11                   1
#define GH_USE_BH1750                  1
#define GH_DHT11_GPIO                  "PC.6"

/* The official demo68 baseline already contains LVGL. */
#define GH_ENABLE_LVGL_UI              1

/* D12X-DEMO68: BZ1 is driven by PC7/PWM1_B through an NPN transistor. */
#define GH_ENABLE_REAL_BUZZER          1

/* Offline screen-only mode: the unpopulated Wi-Fi module is not required. */
#define GH_ENABLE_HTTP_SERVER          0
#define GH_HTTP_SERVER_PORT             80
#define GH_HTTP_RX_BUFFER_SIZE         1024
#define GH_HTTP_TX_BUFFER_SIZE         2048

#define GH_I2C_BUS_NAME                "i2c1"
#define GH_SHT30_ADDRESS               0x44
#define GH_BH1750_ADDRESS              0x23
#define GH_BH1750_ALT_ADDRESS          0x5c

#define GH_SENSOR_PERIOD_MS            2000
#define GH_UI_START_DELAY_MS           5000
#define GH_QUEUE_DEPTH                 8
#define GH_ALARM_CONFIRM_SAMPLES       3
#define GH_MAX_LIGHT_LUX               1000000U
#define GH_MAX_HYSTERESIS_X10          1000

#define GH_BUZZER_PWM_DEVICE           "pwm"
#define GH_BUZZER_PWM_CHANNEL          1
#define GH_BUZZER_GPIO                 "PC.7"
/* The official schematic specifies a 4 kHz, 50 percent duty-cycle input. */
#define GH_BUZZER_PERIOD_NS            250000
#define GH_BUZZER_PULSE_NS             125000

/* Confirmed board-level devices. The project intentionally does not use TF. */
#define GH_WIFI_CHIP                    "RTL8189FTV (optional)"
#define GH_WIFI_MODULE_POPULATED        0
#define GH_WIFI_SDIO_BUS                "SDC0 (PB6-PB11)"
#define GH_WIFI_POWER_GPIO              "PD.17 (WIFI_ON/WL_REG_ON)"
#define GH_TF_CARD_BUS                  "SDC1 (PC0-PC6)"

#if GH_ENABLE_REAL_BUZZER && !defined(AIC_USING_PWM1)
#error "GH_ENABLE_REAL_BUZZER requires CONFIG_AIC_USING_PWM1=y"
#endif

#if GH_ENABLE_REAL_BUZZER && defined(AIC_PWM_CH_NUM) && \
    (GH_BUZZER_PWM_CHANNEL >= AIC_PWM_CH_NUM)
#error "GH_BUZZER_PWM_CHANNEL is outside the configured PWM channel range"
#endif

#if GH_USE_DHT11 && !defined(RT_USING_PIN)
#error "DHT11 requires RT-Thread pin device support"
#endif

#if !GH_USE_SIMULATED_SENSORS && GH_USE_BH1750 && !defined(AIC_USING_I2C1)
#error "Real BH1750 sensor requires CONFIG_AIC_USING_I2C1=y"
#endif

#if !GH_USE_SIMULATED_SENSORS && !GH_USE_DHT11 && !defined(AIC_USING_I2C1)
#error "Real SHT30 sensor requires CONFIG_AIC_USING_I2C1=y"
#endif

#endif
