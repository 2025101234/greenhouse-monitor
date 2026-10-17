#include <rtdevice.h>
#include "gh_board.h"
#include "gh_config.h"

static struct rt_device_pwm *g_buzzer_pwm;

static const char *gh_device_state(const char *name)
{
    return rt_device_find(name) != RT_NULL ? "ready" : "not registered";
}

rt_err_t gh_board_buzzer_set(rt_bool_t enabled)
{
#if GH_ENABLE_REAL_BUZZER
    rt_err_t result;

    if (g_buzzer_pwm == RT_NULL)
        g_buzzer_pwm = (struct rt_device_pwm *)rt_device_find(GH_BUZZER_PWM_DEVICE);
    if (g_buzzer_pwm == RT_NULL)
        return -RT_ENOSYS;

    if (!enabled)
        return rt_pwm_disable(g_buzzer_pwm, GH_BUZZER_PWM_CHANNEL);

    result = rt_pwm_set(g_buzzer_pwm, GH_BUZZER_PWM_CHANNEL,
                        GH_BUZZER_PERIOD_NS, GH_BUZZER_PULSE_NS);
    if (result != RT_EOK)
        return result;
    return rt_pwm_enable(g_buzzer_pwm, GH_BUZZER_PWM_CHANNEL);
#else
    RT_UNUSED(enabled);
    return RT_EOK;
#endif
}

void gh_board_print_info(void)
{
    rt_kprintf("--- Greenhouse board profile ---\n");
    rt_kprintf("board: %s %s\n", GH_BOARD_NAME, GH_BOARD_REVISION);
    rt_kprintf("buzzer: BZ1, %s/PWM1_B, 4 kHz (%s)\n",
               GH_BUZZER_GPIO, gh_device_state(GH_BUZZER_PWM_DEVICE));
    rt_kprintf("touch bus: i2c0 (%s)\n", gh_device_state("i2c0"));
    rt_kprintf("sensor bus reserved: i2c1 (%s)\n", gh_device_state("i2c1"));
    rt_kprintf("console/upgrade: onboard USB-to-UART0 (%s)\n",
               gh_device_state("uart0"));
    rt_kprintf("sensor connector: J8 PC.6=DHT11 DATA, I2C1=BH1750 (TX1=SCL/RX1=SDA)\n");
    rt_kprintf("wifi: %s, module=%s, %s, power=%s (%s)\n",
               GH_WIFI_CHIP,
#if GH_WIFI_MODULE_POPULATED
               "populated",
#else
               "not populated",
#endif
               GH_WIFI_SDIO_BUS, GH_WIFI_POWER_GPIO,
#ifdef AIC_WLAN_REALTEK
               "driver enabled"
#else
               "driver not enabled in defconfig"
#endif
    );
#if defined(AIC_WLAN_REALTEK) && defined(AIC_PRGB_24BIT)
    rt_kprintf("[GH] warning: Wi-Fi PD17 and RGB888 LCD_R1 are multiplexed; "
               "verify R53/panel mode before using Wi-Fi.\n");
#endif
    rt_kprintf("TF slot: %s (not used by this project)\n", GH_TF_CARD_BUS);
}

rt_err_t gh_board_init(void)
{
    rt_err_t result = RT_EOK;

#if GH_ENABLE_REAL_BUZZER
    g_buzzer_pwm = (struct rt_device_pwm *)rt_device_find(GH_BUZZER_PWM_DEVICE);
    if (g_buzzer_pwm == RT_NULL)
    {
        rt_kprintf("[GH] board error: PWM device is not registered\n");
        result = -RT_ENOSYS;
    }
    else
    {
        /* Always enter the application with the active-high buzzer off. */
        rt_pwm_disable(g_buzzer_pwm, GH_BUZZER_PWM_CHANNEL);
    }
#endif

    rt_kprintf("[GH] board profile: %s %s\n", GH_BOARD_NAME, GH_BOARD_REVISION);
    return result;
}
