#ifndef GREENHOUSE_TYPES_H
#define GREENHOUSE_TYPES_H

#include <rtthread.h>

#define GH_ABS(value) ((value) < 0 ? -(value) : (value))

#define GH_VALID_AIR_TEMP      (1U << 0)
#define GH_VALID_AIR_HUMIDITY  (1U << 1)
#define GH_VALID_LIGHT         (1U << 2)

#define GH_ALARM_TEMP_HIGH     (1U << 0)
#define GH_ALARM_HUMID_HIGH    (1U << 1)
#define GH_ALARM_LIGHT_HIGH    (1U << 2)
#define GH_ALARM_CHANNELS      3

typedef struct
{
    rt_uint32_t sequence;
    rt_tick_t tick;
    rt_int32_t air_temp_x10;
    rt_int32_t air_humidity_x10;
    rt_uint32_t light_lux;
    rt_uint8_t valid_mask;
} gh_sample_t;

typedef struct
{
    rt_int32_t temp_high_x10;
    rt_int32_t humidity_high_x10;
    rt_uint32_t light_high_lux;
    rt_int32_t hysteresis_x10;
} gh_thresholds_t;

typedef struct
{
    gh_sample_t sample;
    gh_thresholds_t thresholds;
    rt_uint32_t alarm_mask;
    rt_uint32_t dropped_alarm_messages;
    rt_uint32_t dropped_ui_messages;
} gh_status_t;

#endif
