/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-10-18     Meco Man     The first version
 * 2021-12-17     Wayne        Add input event
 * 2023-05-02     Ning Fang    Add ArtInChip input event
 */
#include <rtconfig.h>
#ifdef KERNEL_RTTHREAD
#include <lvgl.h>
#include <stdbool.h>
#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>
#include <string.h>
#include <../components/drivers/include/drivers/touch.h>

static lv_indev_state_t last_state = LV_INDEV_STATE_REL;
static rt_int16_t last_x = 0;
static rt_int16_t last_y = 0;

/* A quick DOWN/UP must survive a frame render. A single last_state loses
 * taps when both events arrive before LVGL next reads the pointer. */
#define TOUCH_QUEUE_SIZE 16
struct queued_touch {
    rt_int16_t x, y;
    lv_indev_state_t state;
};
static struct queued_touch touch_queue[TOUCH_QUEUE_SIZE];
static unsigned touch_head, touch_tail, touch_count;
static volatile rt_bool_t touch_trace;
static volatile rt_uint32_t received_events, consumed_events, dropped_events, input_polls;
static rt_int16_t last_raw_x, last_raw_y, last_mapped_x, last_mapped_y;
#ifndef AIC_LVGL_TOUCH_ROTATION
#define AIC_LVGL_TOUCH_ROTATION 0
#endif
#if AIC_LVGL_TOUCH_ROTATION != 0 && AIC_LVGL_TOUCH_ROTATION != 180
#error "AIC_LVGL_TOUCH_ROTATION must be 0 or 180"
#endif
static unsigned touch_rotation = AIC_LVGL_TOUCH_ROTATION;

static void gh_touch(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "on") == 0) touch_trace = RT_TRUE;
    else if (argc == 2 && strcmp(argv[1], "off") == 0) touch_trace = RT_FALSE;
    else if (argc == 3 && strcmp(argv[1], "rotate") == 0 &&
        (strcmp(argv[2], "0") == 0 || strcmp(argv[2], "180") == 0))
    {
        rt_base_t level = rt_hw_interrupt_disable();
        touch_rotation = strcmp(argv[2], "180") == 0 ? 180 : 0;
        touch_head = touch_tail = touch_count = 0;
        last_state = LV_INDEV_STATE_RELEASED;
        rt_hw_interrupt_enable(level);
    }
    rt_kprintf("Touch: trace=%s, rotation=%u, received=%lu, consumed=%lu, dropped=%lu, polls=%lu, queued=%u\n",
        touch_trace ? "on" : "off", touch_rotation,
        (unsigned long)received_events, (unsigned long)consumed_events,
        (unsigned long)dropped_events, (unsigned long)input_polls, touch_count);
    rt_kprintf("Last raw=(%d,%d), UI=(%d,%d). Commands: gh_touch on/off; gh_touch rotate 0/180\n",
        last_raw_x, last_raw_y, last_mapped_x, last_mapped_y);
}
MSH_CMD_EXPORT(gh_touch, trace physical touch coordinates and LVGL queue);

static void input_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    rt_base_t level = rt_hw_interrupt_disable();
    input_polls++;
    if (touch_count) {
        last_x = touch_queue[touch_tail].x;
        last_y = touch_queue[touch_tail].y;
        last_state = touch_queue[touch_tail].state;
        touch_tail = (touch_tail + 1U) % TOUCH_QUEUE_SIZE;
        touch_count--;
        consumed_events++;
    }
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = last_state;
    data->continue_reading = touch_count != 0;
    rt_hw_interrupt_enable(level);
    (void)indev_drv;
}

void aic_touch_inputevent_cb(rt_int16_t x, rt_int16_t y, rt_uint8_t state)
{
    lv_indev_state_t next_state;
    rt_base_t level;
    rt_int16_t raw_x = x, raw_y = y;
    /* Touch calibration is independent of framebuffer rotation. On this
     * board GT911 coordinates already match the visible button positions. */
    if (touch_rotation == 180) {
        x = 479 - x;
        y = 271 - y;
    }
    switch (state)
    {
    case RT_TOUCH_EVENT_UP:
        next_state = LV_INDEV_STATE_RELEASED;
        break;
    case RT_TOUCH_EVENT_MOVE:
    case RT_TOUCH_EVENT_DOWN:
        next_state = LV_INDEV_STATE_PRESSED;
        break;
    default:
        return;
    }
    level = rt_hw_interrupt_disable();
    received_events++;
    last_raw_x = raw_x;
    last_raw_y = raw_y;
    last_mapped_x = x;
    last_mapped_y = y;
    /* Drop the oldest on overflow, preserving the newest release event. */
    if (touch_count == TOUCH_QUEUE_SIZE) {
        touch_tail = (touch_tail + 1U) % TOUCH_QUEUE_SIZE;
        touch_count--;
        dropped_events++;
    }
    touch_queue[touch_head].x = x;
    touch_queue[touch_head].y = y;
    touch_queue[touch_head].state = next_state;
    touch_head = (touch_head + 1U) % TOUCH_QUEUE_SIZE;
    touch_count++;
    rt_hw_interrupt_enable(level);
    /* Log only DOWN and UP. Printing MOVE continuously slows UART and touch. */
    if (touch_trace && state != RT_TOUCH_EVENT_MOVE)
        rt_kprintf("[TOUCH] %s raw=(%d,%d) UI=(%d,%d)\n",
            state == RT_TOUCH_EVENT_DOWN ? "DOWN" : "UP",
            raw_x, raw_y, x, y);
}

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    /* Basic initialization */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = input_read;

    /* Register the driver in LVGL and save the created input device object */
    lv_indev_drv_register(&indev_drv);
}
#endif
