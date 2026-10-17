#include <rtdevice.h>
#include <rthw.h>
#include "gh_sensors.h"
#include "gh_config.h"
#include "gh_data.h"

static rt_uint32_t g_sequence;
static struct rt_mutex g_sensor_lock;
static rt_bool_t g_sensor_lock_ready;

#if !GH_USE_SIMULATED_SENSORS
#if !GH_USE_DHT11 || GH_USE_BH1750
static struct rt_i2c_bus_device *g_i2c_bus;
#endif

static rt_err_t gh_dht11_wait_level(rt_base_t pin, int level,
                                    rt_uint32_t timeout_us)
{
    while (timeout_us-- > 0)
    {
        if (rt_pin_read(pin) == level)
            return RT_EOK;
        rt_hw_us_delay(1);
    }
    return -RT_ETIMEOUT;
}

static rt_err_t gh_read_dht11(rt_int32_t *temp_x10,
                              rt_int32_t *humidity_x10)
{
    rt_uint8_t data[5] = {0};
    rt_base_t pin;
    rt_uint32_t bit;
    rt_uint32_t high_us;
    rt_int32_t temperature;
    rt_int32_t humidity;

    if ((temp_x10 == RT_NULL) || (humidity_x10 == RT_NULL))
        return -RT_EINVAL;

    pin = rt_pin_get(GH_DHT11_GPIO);
    if (pin < 0)
        return -RT_ENOSYS;

    /* DHT11 start signal: host low >=18 ms, then release the line. */
    rt_pin_mode(pin, PIN_MODE_OUTPUT);
    rt_pin_write(pin, PIN_LOW);
    rt_thread_mdelay(20);
    rt_pin_write(pin, PIN_HIGH);
    rt_hw_us_delay(30);
    rt_pin_mode(pin, PIN_MODE_INPUT_PULLUP);

    /* Sensor response: low ~80 us, high ~80 us, then data starts low. */
    if (gh_dht11_wait_level(pin, PIN_LOW, 100) != RT_EOK ||
        gh_dht11_wait_level(pin, PIN_HIGH, 100) != RT_EOK ||
        gh_dht11_wait_level(pin, PIN_LOW, 100) != RT_EOK)
        return -RT_ERROR;

    for (bit = 0; bit < 40; bit++)
    {
        /* Each data bit starts with a low pulse of about 50 us. */
        if (gh_dht11_wait_level(pin, PIN_HIGH, 100) != RT_EOK)
            return -RT_ERROR;

        high_us = 0;
        while ((rt_pin_read(pin) == PIN_HIGH) && (high_us < 120))
        {
            rt_hw_us_delay(1);
            high_us++;
        }
        if (high_us >= 120)
            return -RT_ERROR;

        data[bit / 8] <<= 1;
        if (high_us > 40)
            data[bit / 8] |= 1U;
    }

    rt_pin_mode(pin, PIN_MODE_INPUT_PULLUP);
    if ((rt_uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
        return -RT_ERROR;

    humidity = (rt_int32_t)data[0] * 10 + data[1];
    temperature = (rt_int32_t)(data[2] & 0x7f) * 10 + data[3];
    if (data[2] & 0x80)
        temperature = -temperature;
    if ((humidity < 0) || (humidity > 1000) ||
        (temperature < -400) || (temperature > 800))
        return -RT_ERROR;

    *temp_x10 = temperature;
    *humidity_x10 = humidity;
    return RT_EOK;
}

#if !GH_USE_DHT11

static rt_uint8_t gh_crc8(const rt_uint8_t *data, rt_size_t length)
{
    rt_uint8_t crc = 0xff;
    rt_size_t i;
    rt_uint8_t bit;

    for (i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? (rt_uint8_t)((crc << 1) ^ 0x31) : (rt_uint8_t)(crc << 1);
    }
    return crc;
}

static rt_err_t gh_read_sht30(rt_int32_t *temp_x10, rt_int32_t *humidity_x10)
{
    rt_uint8_t command[2] = {0x2c, 0x06};
    rt_uint8_t data[6];
    rt_uint16_t raw_temp;
    rt_uint16_t raw_humidity;

    if ((g_i2c_bus == RT_NULL) || (temp_x10 == RT_NULL) || (humidity_x10 == RT_NULL))
        return -RT_EINVAL;
    if (rt_i2c_master_send(g_i2c_bus, GH_SHT30_ADDRESS, 0,
                           command, sizeof(command)) != sizeof(command))
        return -RT_ERROR;

    rt_thread_mdelay(20);
    if (rt_i2c_master_recv(g_i2c_bus, GH_SHT30_ADDRESS, 0,
                           data, sizeof(data)) != sizeof(data))
        return -RT_ERROR;
    if ((gh_crc8(data, 2) != data[2]) || (gh_crc8(&data[3], 2) != data[5]))
        return -RT_ERROR;

    raw_temp = (rt_uint16_t)((data[0] << 8) | data[1]);
    raw_humidity = (rt_uint16_t)((data[3] << 8) | data[4]);
    *temp_x10 = -450 + (1750L * raw_temp) / 65535L;
    *humidity_x10 = (1000L * raw_humidity) / 65535L;
    return RT_EOK;
}
#endif

#if GH_USE_BH1750
static rt_err_t gh_read_bh1750(rt_uint32_t *light_lux)
{
    rt_uint8_t power_on = 0x01;
    rt_uint8_t command = 0x10;
    rt_uint8_t data[2];
    rt_uint16_t raw;
    rt_uint8_t address = GH_BH1750_ADDRESS;

    if ((g_i2c_bus == RT_NULL) || (light_lux == RT_NULL))
        return -RT_EINVAL;
    /* BH1750 powers up in the power-down state.  Wake it explicitly before
     * issuing the one-shot high-resolution measurement command. */
    if (rt_i2c_master_send(g_i2c_bus, address, 0, &power_on, 1) != 1)
    {
        /* Some BH1750 modules strap ADDR high and therefore answer at 0x5c. */
        address = GH_BH1750_ALT_ADDRESS;
        if (rt_i2c_master_send(g_i2c_bus, address, 0, &power_on, 1) != 1)
            return -RT_ERROR;
    }
    if (rt_i2c_master_send(g_i2c_bus, address, 0,
                           &command, 1) != 1)
        return -RT_ERROR;

    rt_thread_mdelay(180);
    if (rt_i2c_master_recv(g_i2c_bus, address, 0,
                           data, sizeof(data)) != sizeof(data))
        return -RT_ERROR;

    raw = (rt_uint16_t)((data[0] << 8) | data[1]);
    *light_lux = ((rt_uint32_t)raw * 10U) / 12U;
    return RT_EOK;
}
#endif
#endif

#if GH_USE_SIMULATED_SENSORS
static void gh_make_simulated_sample(gh_sample_t *sample)
{
    rt_int32_t phase = (rt_int32_t)(sample->sequence % 40U);
    rt_int32_t triangle = phase <= 20 ? phase : 40 - phase;

    sample->air_temp_x10 = 220 + triangle * 3;
    sample->air_humidity_x10 = 720 - triangle * 5;
    sample->light_lux = 0;
    sample->valid_mask = GH_VALID_AIR_TEMP | GH_VALID_AIR_HUMIDITY;
}
#else

static rt_err_t gh_read_real_sample(gh_sample_t *sample)
{
    rt_err_t result;

#if !GH_USE_DHT11 || GH_USE_BH1750
    if (g_i2c_bus == RT_NULL)
    {
        g_i2c_bus = rt_i2c_bus_device_find(GH_I2C_BUS_NAME);
        if (g_i2c_bus == RT_NULL)
            return -RT_ENOSYS;
    }
#endif

    sample->valid_mask = 0;
#if GH_USE_DHT11
    result = gh_read_dht11(&sample->air_temp_x10, &sample->air_humidity_x10);
#else
    result = gh_read_sht30(&sample->air_temp_x10, &sample->air_humidity_x10);
#endif
    if (result == RT_EOK)
        sample->valid_mask |= GH_VALID_AIR_TEMP | GH_VALID_AIR_HUMIDITY;

#if GH_USE_BH1750
    result = gh_read_bh1750(&sample->light_lux);
    if (result == RT_EOK)
        sample->valid_mask |= GH_VALID_LIGHT;
#endif

    return sample->valid_mask != 0 ? RT_EOK : -RT_ERROR;
}
#endif

rt_err_t gh_sensors_read_now(gh_sample_t *sample)
{
    rt_err_t result;

    if (sample == RT_NULL)
        return -RT_EINVAL;

    rt_memset(sample, 0, sizeof(*sample));
    sample->sequence = ++g_sequence;
    sample->tick = rt_tick_get();

    if (g_sensor_lock_ready)
        rt_mutex_take(&g_sensor_lock, RT_WAITING_FOREVER);

#if GH_USE_SIMULATED_SENSORS
    gh_make_simulated_sample(sample);
    result = RT_EOK;
#else
    result = gh_read_real_sample(sample);
#endif

    if (g_sensor_lock_ready)
        rt_mutex_release(&g_sensor_lock);
    return result;
}

static void gh_sensor_thread(void *parameter)
{
    gh_sample_t sample;
    rt_err_t result;

    RT_UNUSED(parameter);
    while (1)
    {
        result = gh_sensors_read_now(&sample);
        if (result == RT_EOK)
            gh_data_publish(&sample);
        else
            rt_kprintf("[GH] sensor read failed: %d\n", result);

        rt_thread_mdelay(GH_SENSOR_PERIOD_MS);
    }
}

rt_err_t gh_sensors_start(void)
{
    rt_err_t result;
    rt_thread_t thread = rt_thread_create("ghsensor", gh_sensor_thread, RT_NULL,
                                          4096, 12, 10);
    if (thread == RT_NULL)
        return -RT_ENOMEM;

    result = rt_mutex_init(&g_sensor_lock, "ghslock", RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
        return result;
    g_sensor_lock_ready = RT_TRUE;
    return rt_thread_startup(thread);
}

rt_err_t gh_sensors_publish_test_alarm(void)
{
    gh_sample_t sample;

    rt_memset(&sample, 0, sizeof(sample));
    sample.sequence = ++g_sequence;
    sample.tick = rt_tick_get();
    sample.air_temp_x10 = 450;
    sample.air_humidity_x10 = 950;
    sample.light_lux = 0;
    sample.valid_mask = GH_VALID_AIR_TEMP | GH_VALID_AIR_HUMIDITY;
    return gh_data_publish(&sample);
}
