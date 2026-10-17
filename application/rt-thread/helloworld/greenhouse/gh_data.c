#include "gh_data.h"
#include "gh_config.h"

#if defined(RT_USING_DFS)
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#endif

static struct rt_mutex g_lock;
static rt_mq_t g_alarm_queue;
static rt_mq_t g_ui_queue;
static gh_status_t g_status;
static rt_bool_t g_initialized;

#if defined(RT_USING_DFS)
#define GH_LIMITS_FILE              "/data/greenhouse_limits.bin"
#define GH_LIMITS_MAGIC             0x47484C31UL
static rt_sem_t g_save_sem;

struct gh_limits_file
{
    rt_uint32_t magic;
    gh_thresholds_t limits;
    rt_uint32_t checksum;
};

static rt_uint32_t gh_limits_checksum(const struct gh_limits_file *file)
{
    const rt_uint8_t *bytes = (const rt_uint8_t *)file;
    rt_size_t i;
    rt_uint32_t checksum = 0;

    /* Hash exactly the bytes before checksum.  Using offsetof() keeps this
     * correct if the compiler inserts alignment padding in the file struct. */
    for (i = 0; i < offsetof(struct gh_limits_file, checksum); i++)
        checksum = (checksum * 33U) ^ bytes[i];
    return checksum;
}

static void gh_data_load_thresholds(void)
{
    struct gh_limits_file file;
    int fd;

    fd = open(GH_LIMITS_FILE, O_RDONLY, 0);
    if (fd < 0)
        return;
    if ((read(fd, &file, sizeof(file)) == (ssize_t)sizeof(file)) &&
        (file.magic == GH_LIMITS_MAGIC) &&
        (file.checksum == gh_limits_checksum(&file)) &&
        (file.limits.temp_high_x10 >= -400) &&
        (file.limits.temp_high_x10 <= 1250) &&
        (file.limits.humidity_high_x10 >= 0) &&
        (file.limits.humidity_high_x10 <= 1000) &&
        (file.limits.light_high_lux <= GH_MAX_LIGHT_LUX) &&
        (file.limits.hysteresis_x10 >= 0) &&
        (file.limits.hysteresis_x10 <= GH_MAX_HYSTERESIS_X10))
    {
        g_status.thresholds = file.limits;
    }
    close(fd);
}

static void gh_data_save_thresholds(const gh_thresholds_t *limits)
{
    struct gh_limits_file file;
    int fd;

    file.magic = GH_LIMITS_MAGIC;
    file.limits = *limits;
    file.checksum = gh_limits_checksum(&file);
    fd = open(GH_LIMITS_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        rt_kprintf("[GH] cannot open threshold file; RAM limits remain active\n");
        return;
    }
    if (write(fd, &file, sizeof(file)) != (ssize_t)sizeof(file))
        rt_kprintf("[GH] threshold persistence write failed\n");
    close(fd);
}

static void gh_limits_save_thread(void *parameter)
{
    gh_thresholds_t limits;
    RT_UNUSED(parameter);
    while (1)
    {
        rt_sem_take(g_save_sem, RT_WAITING_FOREVER);
        /* Coalesce rapid Apply taps before writing the latest snapshot. */
        while (rt_sem_take(g_save_sem, 0) == RT_EOK) {}
        rt_mutex_take(&g_lock, RT_WAITING_FOREVER);
        limits = g_status.thresholds;
        rt_mutex_release(&g_lock);
        gh_data_save_thresholds(&limits);
    }
}
#endif

rt_err_t gh_data_init(void)
{
    rt_err_t result;

    rt_memset(&g_status, 0, sizeof(g_status));
    g_initialized = RT_FALSE;
    g_status.thresholds.temp_high_x10 = 350;
    g_status.thresholds.humidity_high_x10 = 850;
    g_status.thresholds.light_high_lux = 80000;
    g_status.thresholds.hysteresis_x10 = 20;
#if defined(RT_USING_DFS)
    gh_data_load_thresholds();
#endif

    result = rt_mutex_init(&g_lock, "ghlock", RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
        return result;

    g_alarm_queue = rt_mq_create("ghalarm", sizeof(gh_sample_t),
                                 GH_QUEUE_DEPTH, RT_IPC_FLAG_FIFO);
    g_ui_queue = rt_mq_create("ghui", sizeof(gh_sample_t),
                              GH_QUEUE_DEPTH, RT_IPC_FLAG_FIFO);
    if ((g_alarm_queue == RT_NULL) || (g_ui_queue == RT_NULL))
    {
        if (g_alarm_queue != RT_NULL)
        {
            rt_mq_delete(g_alarm_queue);
            g_alarm_queue = RT_NULL;
        }
        if (g_ui_queue != RT_NULL)
        {
            rt_mq_delete(g_ui_queue);
            g_ui_queue = RT_NULL;
        }
        return -RT_ENOMEM;
    }

    g_initialized = RT_TRUE;

#if defined(RT_USING_DFS)
    {
        rt_thread_t saver;
        g_save_sem = rt_sem_create("ghsave", 0, RT_IPC_FLAG_FIFO);
        saver = g_save_sem != RT_NULL ? rt_thread_create("ghsave", gh_limits_save_thread,
                    RT_NULL, 4096, 26, 10) : RT_NULL;
        if (saver == RT_NULL)
        {
            if (g_save_sem != RT_NULL) rt_sem_delete(g_save_sem);
            g_save_sem = RT_NULL;
            rt_kprintf("[GH] persistence worker unavailable; thresholds use RAM only\n");
        }
        else if (rt_thread_startup(saver) != RT_EOK)
        {
            rt_thread_delete(saver);
            rt_sem_delete(g_save_sem);
            g_save_sem = RT_NULL;
            rt_kprintf("[GH] persistence worker startup failed\n");
        }
    }
#endif

    return RT_EOK;
}

rt_err_t gh_data_publish(const gh_sample_t *sample)
{
    rt_err_t alarm_result;
    rt_err_t ui_result;

    if ((sample == RT_NULL) || !g_initialized)
        return -RT_EINVAL;

    rt_mutex_take(&g_lock, RT_WAITING_FOREVER);
    g_status.sample = *sample;
    rt_mutex_release(&g_lock);

    alarm_result = rt_mq_send(g_alarm_queue, sample, sizeof(*sample));
    ui_result = rt_mq_send(g_ui_queue, sample, sizeof(*sample));

    if ((alarm_result != RT_EOK) || (ui_result != RT_EOK))
    {
        rt_mutex_take(&g_lock, RT_WAITING_FOREVER);
        if (alarm_result != RT_EOK)
            g_status.dropped_alarm_messages++;
        if (ui_result != RT_EOK)
            g_status.dropped_ui_messages++;
        rt_mutex_release(&g_lock);
    }

    return alarm_result != RT_EOK ? alarm_result : ui_result;
}

rt_err_t gh_data_recv_alarm(gh_sample_t *sample, rt_int32_t timeout)
{
    if ((sample == RT_NULL) || (g_alarm_queue == RT_NULL))
        return -RT_EINVAL;
    return rt_mq_recv(g_alarm_queue, sample, sizeof(*sample), timeout);
}

rt_err_t gh_data_recv_ui(gh_sample_t *sample, rt_int32_t timeout)
{
    if ((sample == RT_NULL) || (g_ui_queue == RT_NULL))
        return -RT_EINVAL;
    return rt_mq_recv(g_ui_queue, sample, sizeof(*sample), timeout);
}

rt_err_t gh_data_get_status(gh_status_t *status)
{
    if ((status == RT_NULL) || !g_initialized)
        return -RT_EINVAL;

    rt_mutex_take(&g_lock, RT_WAITING_FOREVER);
    *status = g_status;
    rt_mutex_release(&g_lock);
    return RT_EOK;
}

rt_err_t gh_data_get_thresholds(gh_thresholds_t *thresholds)
{
    if ((thresholds == RT_NULL) || !g_initialized)
        return -RT_EINVAL;

    rt_mutex_take(&g_lock, RT_WAITING_FOREVER);
    *thresholds = g_status.thresholds;
    rt_mutex_release(&g_lock);
    return RT_EOK;
}

rt_err_t gh_data_set_thresholds(const gh_thresholds_t *thresholds)
{
    if ((thresholds == RT_NULL) || !g_initialized)
        return -RT_EINVAL;
    if ((thresholds->temp_high_x10 < -400) ||
        (thresholds->temp_high_x10 > 1250) ||
        (thresholds->humidity_high_x10 < 0) ||
        (thresholds->humidity_high_x10 > 1000) ||
        (thresholds->light_high_lux > GH_MAX_LIGHT_LUX) ||
        (thresholds->hysteresis_x10 < 0) ||
        (thresholds->hysteresis_x10 > GH_MAX_HYSTERESIS_X10))
        return -RT_EINVAL;

    rt_mutex_take(&g_lock, RT_WAITING_FOREVER);
    g_status.thresholds = *thresholds;
    rt_mutex_release(&g_lock);
#if defined(RT_USING_DFS)
    if (g_save_sem != RT_NULL) rt_sem_release(g_save_sem);
#endif
    return RT_EOK;
}

void gh_data_set_alarm_mask(rt_uint32_t alarm_mask)
{
    if (!g_initialized)
        return;
    rt_mutex_take(&g_lock, RT_WAITING_FOREVER);
    g_status.alarm_mask = alarm_mask;
    rt_mutex_release(&g_lock);
}
