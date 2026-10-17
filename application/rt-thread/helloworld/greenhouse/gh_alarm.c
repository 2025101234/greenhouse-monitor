#include <rtdevice.h>
#include "gh_alarm.h"
#include "gh_board.h"
#include "gh_config.h"
#include "gh_data.h"

static rt_uint32_t gh_alarm_raw_mask(const gh_sample_t *sample,
                                     const gh_thresholds_t *limits,
                                     rt_uint32_t current_mask)
{
    rt_uint32_t mask = 0;
    rt_int32_t h = limits->hysteresis_x10;

    if (sample->valid_mask & GH_VALID_AIR_TEMP)
    {
        rt_int32_t on_level = limits->temp_high_x10;
        rt_int32_t off_level = on_level - h;
        if (sample->air_temp_x10 >= ((current_mask & GH_ALARM_TEMP_HIGH) ? off_level : on_level))
            mask |= GH_ALARM_TEMP_HIGH;
    }
    if (sample->valid_mask & GH_VALID_AIR_HUMIDITY)
    {
        rt_int32_t on_level = limits->humidity_high_x10;
        rt_int32_t off_level = on_level - h;
        if (sample->air_humidity_x10 >= ((current_mask & GH_ALARM_HUMID_HIGH) ? off_level : on_level))
            mask |= GH_ALARM_HUMID_HIGH;
    }
    if ((sample->valid_mask & GH_VALID_LIGHT) &&
        (sample->light_lux >= limits->light_high_lux))
        mask |= GH_ALARM_LIGHT_HIGH;

    return mask;
}

static void gh_alarm_thread(void *parameter)
{
    gh_sample_t sample;
    gh_thresholds_t limits;
    rt_uint8_t counters[GH_ALARM_CHANNELS] = {0};
    rt_uint32_t active_mask = 0;
    rt_uint32_t raw_mask;
    rt_uint32_t confirmed_mask;
    rt_uint32_t bit;
    int i;
    rt_err_t buzzer_result;

    RT_UNUSED(parameter);
    while (1)
    {
        if (gh_data_recv_alarm(&sample, RT_WAITING_FOREVER) != RT_EOK)
            continue;
        gh_data_get_thresholds(&limits);
        raw_mask = gh_alarm_raw_mask(&sample, &limits, active_mask);
        confirmed_mask = 0;

        for (i = 0; i < GH_ALARM_CHANNELS; i++)
        {
            bit = 1U << i;
            if (raw_mask & bit)
            {
                if (active_mask & bit)
                    confirmed_mask |= bit;
                else
                {
                    if (counters[i] < GH_ALARM_CONFIRM_SAMPLES)
                        counters[i]++;
                    if (counters[i] >= GH_ALARM_CONFIRM_SAMPLES)
                        confirmed_mask |= bit;
                }
            }
            else
            {
                counters[i] = 0;
            }
        }

        if (confirmed_mask != active_mask)
        {
            active_mask = confirmed_mask;
            gh_data_set_alarm_mask(active_mask);
            buzzer_result = gh_board_buzzer_set(active_mask != 0 ? RT_TRUE : RT_FALSE);
            if (buzzer_result != RT_EOK)
                rt_kprintf("[GH] buzzer state change failed: %d\n", buzzer_result);
            rt_kprintf("[GH] alarm mask changed: 0x%02lx\n",
                       (unsigned long)active_mask);
        }
    }
}

rt_err_t gh_alarm_start(void)
{
    rt_thread_t thread = rt_thread_create("ghalarm", gh_alarm_thread, RT_NULL,
                                          3072, 13, 10);
    if (thread == RT_NULL)
        return -RT_ENOMEM;
    return rt_thread_startup(thread);
}
