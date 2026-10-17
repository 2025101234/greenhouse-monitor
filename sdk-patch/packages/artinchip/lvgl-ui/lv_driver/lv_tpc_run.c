/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-01-13     RiceChen     the first version
 */
#include <rtconfig.h>
#ifdef KERNEL_RTTHREAD
#include <rtdevice.h>
#include <rtthread.h>
#include <finsh.h>
#include "lv_tpc_run.h"
#include "lv_port_indev.h"
#include "aic_osal.h"
#include "../components/drivers/include/drivers/touch.h"

#ifdef AIC_TOUCH_PANEL_GT911
    extern rt_size_t gt911_read_point(struct rt_touch_device *touch, void *buf,
                                    rt_size_t read_num);
#endif

#define THREAD_PRIORITY   18 /* Read touch before the priority-20 GUI renders. */
#define THREAD_STACK_SIZE 4096
#define THREAD_TIMESLICE  5

static rt_thread_t  gt911_thread = RT_NULL;
static rt_sem_t     gt911_sem = RT_NULL;
static rt_device_t  dev = RT_NULL;
static struct rt_touch_data *read_data;
static struct rt_touch_info info;
static volatile rt_uint32_t irq_count, read_count, event_count, empty_count;

static void gh_touch_hw(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    rt_kprintf("GT911: thread=%s, range=%ldx%ld, points=%u, IRQ=%lu, reads=%lu, events=%lu, empty=%lu\n",
        gt911_thread != RT_NULL ? "created" : "absent",
        (long)info.range_x, (long)info.range_y, info.point_num,
        (unsigned long)irq_count, (unsigned long)read_count,
        (unsigned long)event_count, (unsigned long)empty_count);
    rt_kprintf("Touch uses i2c0, PA.10 reset, PA.11 IRQ; 20ms read fallback enabled.\n");
}
MSH_CMD_EXPORT(gh_touch_hw, show GT911 interrupt and read counters);

static void gt911_entry(void *parameter)
{
    while (1)
    {
        /* Recover if the first IRQ was missed or the interrupt line stays
         * asserted. The GT911 status register tells us whether data is ready. */
        rt_sem_take(gt911_sem, rt_tick_from_millisecond(20));
        int num = 0;
        read_count++;
#ifdef AIC_TOUCH_PANEL_GT911
        num = rt_device_read(dev, 0, read_data, info.point_num);
#endif
        if (num == info.point_num)
        {
            for (rt_uint8_t i = 0; i < info.point_num; i++)
            {
                if (read_data[i].event == RT_TOUCH_EVENT_DOWN
                        || read_data[i].event == RT_TOUCH_EVENT_UP
                        || read_data[i].event == RT_TOUCH_EVENT_MOVE)
                {
                    rt_uint16_t  u16X, u16Y;

                    u16X = read_data[i].x_coordinate;
                    u16Y = read_data[i].y_coordinate;
                    event_count++;
                    aic_touch_inputevent_cb(u16X, u16Y, read_data[i].event);
                }
            }
        }
        else empty_count++;
        //aicos_msleep(10);
        rt_device_control(dev, RT_TOUCH_CTRL_ENABLE_INT, RT_NULL);
    }
}

static rt_err_t rx_callback(rt_device_t dev, rt_size_t size)
{
#ifdef AIC_PM_POWER_TOUCH_WAKEUP
    rt_uint8_t sleep_mode;
#endif
    irq_count++;
    rt_device_control(dev, RT_TOUCH_CTRL_DISABLE_INT, RT_NULL);
    rt_sem_release(gt911_sem);
#ifdef AIC_PM_POWER_TOUCH_WAKEUP
    sleep_mode = rt_pm_get_sleep_mode();
    if (sleep_mode != PM_SLEEP_MODE_NONE && !wakeup_triggered)
    {
        rt_pm_module_request(PM_POWER_ID, PM_SLEEP_MODE_NONE);
        wakeup_triggered = 1;
    }
    /* touch timer restart */
    rt_timer_start(touch_timer);
#endif
    return 0;
}

int tpc_run(const char *name, rt_uint16_t x, rt_uint16_t y)
{
    rt_uint8_t id[8] = {0};
    rt_err_t result;
    RT_UNUSED(x);
    RT_UNUSED(y);
    if (gt911_thread != RT_NULL)
        return 0;

    dev = rt_device_find(name);
    if (dev == RT_NULL)
    {
        rt_kprintf("can't find device:%s\n", name);
        return -1;
    }

    if (rt_device_control(dev, RT_TOUCH_CTRL_GET_ID, id) == RT_EOK)
        rt_kprintf("id = GT%c%c%c\n", id[0], id[1], id[2]);
    rt_memset(&info, 0, sizeof(info));
    result = rt_device_control(dev, RT_TOUCH_CTRL_GET_INFO, &info);
    if (result != RT_EOK || info.point_num == 0 || info.point_num > 5 ||
        info.range_x <= 0 || info.range_y <= 0)
    {
        rt_kprintf("GT911 invalid info: result=%d, points=%u, range=%ldx%ld\n",
            result, info.point_num, (long)info.range_x, (long)info.range_y);
        return -1;
    }
    rt_kprintf("range_x=%ld, range_y=%ld, point_num=%u\n",
        (long)info.range_x, (long)info.range_y, info.point_num);
    read_data = rt_malloc(sizeof(*read_data) * info.point_num);
    if (read_data == RT_NULL)
        return -RT_ENOMEM;
    gt911_sem = rt_sem_create("dsem", 0, RT_IPC_FLAG_FIFO);

    if (gt911_sem == RT_NULL)
    {
        rt_kprintf("create dynamic semaphore failed.\n");
        rt_free(read_data);
        read_data = RT_NULL;
        return -1;
    }

    gt911_thread = rt_thread_create("gt911",
                                     gt911_entry,
                                     RT_NULL,
                                     THREAD_STACK_SIZE,
                                     THREAD_PRIORITY,
                                     THREAD_TIMESLICE);

    if (gt911_thread == RT_NULL)
        goto cleanup;
    /* Resources exist before registering a callback/enabling IRQs. */
    rt_device_set_rx_indicate(dev, rx_callback);
    if (rt_device_open(dev, RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_device_control(dev, RT_TOUCH_CTRL_DISABLE_INT, RT_NULL);
        goto cleanup;
    }
    if (rt_thread_startup(gt911_thread) != RT_EOK)
    {
        rt_device_close(dev);
        goto cleanup;
    }
    return 0;

cleanup:
    rt_device_set_rx_indicate(dev, RT_NULL);
    if (gt911_thread != RT_NULL) rt_thread_delete(gt911_thread);
    gt911_thread = RT_NULL;
    rt_sem_delete(gt911_sem);
    gt911_sem = RT_NULL;
    rt_free(read_data);
    read_data = RT_NULL;
    rt_kprintf("GT911 touch reader startup failed\n");
    return -1;
}
#endif
