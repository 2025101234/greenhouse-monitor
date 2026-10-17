#include <stdlib.h>
#include <errno.h>
#include <finsh.h>
#include "gh_config.h"
#include "gh_board.h"
#include "gh_data.h"
#include "gh_sensors.h"
#include "gh_network.h"

static void gh_print_x10(const char *name, rt_int32_t value, const char *unit)
{
    rt_kprintf("%s: %ld.%ld %s\n", name, (long)(value / 10),
               (long)GH_ABS(value % 10), unit);
}

static rt_bool_t gh_parse_long(const char *text, long *value)
{
    char *end;

    if ((text == RT_NULL) || (value == RT_NULL) || (*text == '\0'))
        return RT_FALSE;
    errno = 0;
    *value = strtol(text, &end, 10);
    return (end != text) && (*end == '\0') && (errno != ERANGE);
}

static void gh_status(int argc, char **argv)
{
    gh_status_t status;
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    gh_data_get_status(&status);
    rt_kprintf("--- Greenhouse status ---\n");
    rt_kprintf("sequence: %lu, tick: %lu, valid: 0x%02x\n",
               (unsigned long)status.sample.sequence,
               (unsigned long)status.sample.tick,
               (unsigned int)status.sample.valid_mask);
    gh_print_x10("air temperature", status.sample.air_temp_x10, "C");
    gh_print_x10("air humidity", status.sample.air_humidity_x10, "%");
    if (status.sample.valid_mask & GH_VALID_LIGHT)
        rt_kprintf("light: %lu lux\n", (unsigned long)status.sample.light_lux);
    else
        rt_kprintf("light: -- (BH1750 unavailable; check I2C1 TX1/RX1)\n");
    rt_kprintf("alarm: 0x%02lx, dropped alarm/ui: %lu/%lu\n",
               (unsigned long)status.alarm_mask,
               (unsigned long)status.dropped_alarm_messages,
               (unsigned long)status.dropped_ui_messages);
    rt_kprintf("thresholds: temp=%ld.%ld C, humidity=%ld.%ld %%, light=%lu lux\n",
               (long)(status.thresholds.temp_high_x10 / 10),
               (long)GH_ABS(status.thresholds.temp_high_x10 % 10),
               (long)(status.thresholds.humidity_high_x10 / 10),
               (long)GH_ABS(status.thresholds.humidity_high_x10 % 10),
               (unsigned long)status.thresholds.light_high_lux);
}
MSH_CMD_EXPORT(gh_status, show greenhouse realtime status);

static void gh_dht11_test(int argc, char **argv)
{
    gh_sample_t sample;
    rt_err_t result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    result = gh_sensors_read_now(&sample);
    if (result != RT_EOK)
    {
        rt_kprintf("DHT11 read failed: %d (check VCC/GND/DATA and J8 PC.6)\n",
                   result);
        return;
    }

    rt_kprintf("DHT11 read ok: sequence=%lu, temperature=%ld.%ld C, humidity=%ld.%ld %%, valid=0x%02x\n",
               (unsigned long)sample.sequence, (long)(sample.air_temp_x10 / 10),
               (long)GH_ABS(sample.air_temp_x10 % 10),
               (long)(sample.air_humidity_x10 / 10),
               (long)GH_ABS(sample.air_humidity_x10 % 10),
               (unsigned int)sample.valid_mask);
}
MSH_CMD_EXPORT(gh_dht11_test, read DHT11 once and print the result);

static void gh_sensor_test(int argc, char **argv)
{
    gh_sample_t sample;
    rt_err_t result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);
    result = gh_sensors_read_now(&sample);
    if (result != RT_EOK)
    {
        rt_kprintf("sensor read failed: %d (check DHT11 PC.6 and BH1750 I2C1)\n",
                   result);
        return;
    }
    rt_kprintf("sensor read ok: seq=%lu valid=0x%02x\n",
               (unsigned long)sample.sequence,
               (unsigned int)sample.valid_mask);
    if (sample.valid_mask & GH_VALID_AIR_TEMP)
        gh_print_x10("air temperature", sample.air_temp_x10, "C");
    if (sample.valid_mask & GH_VALID_AIR_HUMIDITY)
        gh_print_x10("air humidity", sample.air_humidity_x10, "%");
    if (sample.valid_mask & GH_VALID_LIGHT)
        rt_kprintf("light: %lu lux\n", (unsigned long)sample.light_lux);
    else
        rt_kprintf("light: unavailable\n");
}
MSH_CMD_EXPORT(gh_sensor_test, read DHT11 and BH1750 once);

static void gh_threshold(int argc, char **argv)
{
    gh_thresholds_t limits;
    long temp;
    long humidity;
    long light;
    gh_data_get_thresholds(&limits);

    if (argc == 4)
    {
        if (!gh_parse_long(argv[1], &temp) ||
            !gh_parse_long(argv[2], &humidity) ||
            !gh_parse_long(argv[3], &light))
        {
            rt_kprintf("threshold values must be decimal integers\n");
            return;
        }
        if ((temp < -400) || (temp > 1250) ||
            (humidity < 0) || (humidity > 1000))
        {
            rt_kprintf("temperature must be -400..1250 and humidity 0..1000 (x10)\n");
            return;
        }
        if ((light < 0) || (light > (long)GH_MAX_LIGHT_LUX))
        {
            rt_kprintf("light threshold must be 0..%u lux\n", GH_MAX_LIGHT_LUX);
            return;
        }
        limits.temp_high_x10 = (rt_int32_t)temp;
        limits.humidity_high_x10 = (rt_int32_t)humidity;
        limits.light_high_lux = (rt_uint32_t)light;
        if (gh_data_set_thresholds(&limits) != RT_EOK)
        {
            rt_kprintf("invalid threshold values\n");
            return;
        }
    }
    else if (argc != 1)
    {
        rt_kprintf("usage: gh_threshold <temp_x10> <humidity_x10> <light_lux>\n");
        return;
    }

    rt_kprintf("thresholds: temp=%ld humidity=%ld light=%lu\n",
               (long)limits.temp_high_x10, (long)limits.humidity_high_x10,
               (unsigned long)limits.light_high_lux);
}
MSH_CMD_EXPORT(gh_threshold, show or set alarm thresholds (saved to /data when available));

static void gh_alarm_test(int argc, char **argv)
{
    int i;
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    for (i = 0; i < 3; i++)
    {
        gh_sensors_publish_test_alarm();
        rt_thread_mdelay(50);
    }
    rt_kprintf("three alarm samples published\n");
}
MSH_CMD_EXPORT(gh_alarm_test, publish three over-limit test samples);

static void gh_buzzer_test(int argc, char **argv)
{
    long duration_ms = 500;
    long parsed_duration;
    rt_err_t result;

    if (argc == 2)
    {
        if (!gh_parse_long(argv[1], &parsed_duration))
        {
            rt_kprintf("duration must be a decimal integer\n");
            return;
        }
        duration_ms = parsed_duration;
    }
    else if (argc != 1)
    {
        rt_kprintf("usage: gh_buzzer_test [duration_ms]\n");
        return;
    }
    if ((duration_ms < 50) || (duration_ms > 5000))
    {
        rt_kprintf("duration must be 50..5000 ms\n");
        return;
    }

    result = gh_board_buzzer_set(RT_TRUE);
    if (result != RT_EOK)
    {
        rt_kprintf("buzzer start failed: %d\n", result);
        return;
    }
    rt_thread_mdelay((rt_int32_t)duration_ms);
    gh_board_buzzer_set(RT_FALSE);
    rt_kprintf("buzzer test complete: %ld ms\n", duration_ms);
}
MSH_CMD_EXPORT(gh_buzzer_test, test the onboard BZ1 buzzer);

static void gh_hwinfo(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    gh_board_print_info();
}
MSH_CMD_EXPORT(gh_hwinfo, show D12X-DEMO68-V1.2 hardware profile);

static void gh_json(int argc, char **argv)
{
    char json[256];
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    gh_network_make_status_json(json, sizeof(json));
    rt_kprintf("%s\n", json);
}
MSH_CMD_EXPORT(gh_json, print the JSON reserved for the HTTP API);
