#include <rtthread.h>
#include "gh_config.h"
#include "gh_board.h"
#include "gh_data.h"
#include "gh_sensors.h"
#include "gh_alarm.h"
#include "gh_ui.h"
#include "gh_network.h"

static int greenhouse_app_init(void)
{
    rt_err_t result;

    result = gh_board_init();
    if (result != RT_EOK)
        rt_kprintf("[GH] board hardware is not fully ready: %d\n", result);

    result = gh_data_init();
    if (result != RT_EOK)
        return result;
    result = gh_alarm_start();
    if (result != RT_EOK)
        return result;
    result = gh_ui_start();
    if (result != RT_EOK)
        return result;
    result = gh_network_start();
    if (result != RT_EOK)
        return result;
    result = gh_sensors_start();
    if (result != RT_EOK)
        return result;

    rt_kprintf("[GH] greenhouse framework started (%s sensors)\n",
#if GH_USE_SIMULATED_SENSORS
               "simulated"
#else
               "real"
#endif
    );
    return RT_EOK;
}
INIT_APP_EXPORT(greenhouse_app_init);
