#include "gh_ui.h"
#include "gh_config.h"
#include "gh_data.h"

#if GH_ENABLE_LVGL_UI && defined(LPKG_USING_LVGL)
#include <lvgl.h>
#include <finsh.h>
#include <string.h>

/* Every LVGL call runs on the LVGL thread. Pages are created once and cached.
 * The timer consumes RT-Thread messages; no cross-thread lv_async_call exists. */
#define HISTORY_SIZE 60U
#define PAGE_COUNT 4
#define UI_VERSION "1.1.2"
#define BG 0xf3f6f4
#define INK 0x18372e
#define MUTED 0x6e8178
#define GREEN 0x16785c
#define LINE 0xdce5df

enum { PAGE_HOME, PAGE_TREND, PAGE_REPORT, PAGE_MENU };
static volatile rt_bool_t g_data_ready;
static rt_bool_t g_built;
static lv_obj_t *g_pages[PAGE_COUNT], *g_nav[PAGE_COUNT];
static lv_obj_t *g_badge, *g_values[3], *g_limits[3];
static lv_obj_t *g_summary, *g_summary_hint, *g_sample_label;
static lv_obj_t *g_chart, *g_trend_value, *g_trend_hint, *g_metric_btn[3];
static lv_chart_series_t *g_series;
static lv_obj_t *g_report_rows[3], *g_report_ranges[3], *g_report_hint;
static lv_obj_t *g_edit_values[3], *g_edit_hint;
static gh_thresholds_t g_draft;
static gh_status_t g_status;
static gh_sample_t g_history[HISTORY_SIZE];
static rt_uint32_t g_alarm_history[HISTORY_SIZE];
static unsigned g_count, g_next, g_page, g_metric;
static rt_bool_t g_have_sample, g_stale, g_history_dirty;
static volatile rt_uint32_t g_switches, g_updates, g_max_update_ms;
static rt_uint32_t g_last_alarm = 0xffffffffU;
static rt_uint8_t g_last_valid = 0xff;
static rt_bool_t g_last_stale = RT_TRUE;
static const char *g_names[3] = { "Temperature", "Air humidity", "Light" };
static const char *g_units[3] = { "C", "% RH", "lux" };
static const unsigned g_valid[3] = {
    GH_VALID_AIR_TEMP, GH_VALID_AIR_HUMIDITY, GH_VALID_LIGHT
};
static const rt_uint32_t g_colors[3] = { 0xc57a34, 0x16785c, 0x7362b6 };

static void render_page(void);
static void select_page(unsigned page);

static void set_text(lv_obj_t *label, const char *text)
{
    if (label != RT_NULL && strcmp(lv_label_get_text(label), text) != 0)
        lv_label_set_text(label, text);
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                     rt_uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       int w, const lv_font_t *font, rt_uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y,
                        int w, int h, lv_event_cb_t cb, unsigned id)
{
    lv_obj_t *obj = lv_btn_create(parent);
    lv_obj_t *caption;
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xe5eee8), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xc4ddd0), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(INK), 0);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, (void *)(rt_ubase_t)id);
    caption = lv_label_create(obj);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    return obj;
}

static void format_tenths(char *buf, rt_size_t size, rt_int32_t value)
{
    rt_snprintf(buf, size, "%s%ld.%ld", value < 0 ? "-" : "",
                (long)(GH_ABS(value) / 10), (long)(GH_ABS(value) % 10));
}

static rt_int32_t sample_value(const gh_sample_t *sample, unsigned metric)
{
    if (metric == 0) return sample->air_temp_x10;
    if (metric == 1) return sample->air_humidity_x10;
    return (rt_int32_t)sample->light_lux;
}

static void format_value(char *buf, rt_size_t size, rt_int32_t value,
                          unsigned metric)
{
    if (metric == 2)
        rt_snprintf(buf, size, "%lu", (unsigned long)value);
    else
        format_tenths(buf, size, value);
}

static rt_int32_t limit_value(unsigned metric)
{
    if (metric == 0) return g_status.thresholds.temp_high_x10;
    if (metric == 1) return g_status.thresholds.humidity_high_x10;
    return (rt_int32_t)g_status.thresholds.light_high_lux;
}

static const char *status_text(void)
{
    if (!g_have_sample) return "WAITING";
    if (g_stale || g_status.sample.valid_mask == 0) return "NO DATA";
    if (g_status.alarm_mask != 0) return "ALARM";
    if (g_status.sample.valid_mask != 7) return "PARTIAL";
    return "LIVE";
}

static void nav_cb(lv_event_t *event)
{
    rt_kprintf("[UI] navigation clicked: page=%u\n",
        (unsigned)(rt_ubase_t)lv_event_get_user_data(event));
    select_page((unsigned)(rt_ubase_t)lv_event_get_user_data(event));
}

static void metric_cb(lv_event_t *event)
{
    unsigned metric = (unsigned)(rt_ubase_t)lv_event_get_user_data(event);
    unsigned i;
    if (metric > 2) return;
    g_metric = metric;
    for (i = 0; i < 3; i++)
    {
        lv_obj_set_style_bg_color(g_metric_btn[i],
            lv_color_hex(i == metric ? g_colors[i] : 0xe5eee8), 0);
        lv_obj_set_style_text_color(g_metric_btn[i],
            lv_color_hex(i == metric ? 0xffffff : INK), 0);
    }
    g_history_dirty = RT_TRUE;
    render_page();
}

static void update_draft(void)
{
    char buf[24];
    format_tenths(buf, sizeof(buf), g_draft.temp_high_x10);
    set_text(g_edit_values[0], buf);
    format_tenths(buf, sizeof(buf), g_draft.humidity_high_x10);
    set_text(g_edit_values[1], buf);
    rt_snprintf(buf, sizeof(buf), "%lu", (unsigned long)g_draft.light_high_lux);
    set_text(g_edit_values[2], buf);
}

static void edit_cb(lv_event_t *event)
{
    unsigned action = (unsigned)(rt_ubase_t)lv_event_get_user_data(event);
    unsigned metric = action / 2;
    int sign = action & 1 ? 1 : -1;
    rt_int32_t value;
    if (metric == 0)
    {
        value = g_draft.temp_high_x10 + sign * 10;
        if (value >= -400 && value <= 1250) g_draft.temp_high_x10 = value;
    }
    else if (metric == 1)
    {
        value = g_draft.humidity_high_x10 + sign * 10;
        if (value >= 0 && value <= 1000) g_draft.humidity_high_x10 = value;
    }
    else if (metric == 2)
    {
        value = (rt_int32_t)g_draft.light_high_lux + sign * 100;
        if (value >= 0 && value <= (rt_int32_t)GH_MAX_LIGHT_LUX)
            g_draft.light_high_lux = (rt_uint32_t)value;
    }
    update_draft();
    set_text(g_edit_hint, "Unsaved changes. Tap Apply to use these limits.");
}

static void apply_cb(lv_event_t *event)
{
    RT_UNUSED(event);
    if (!g_data_ready || gh_data_set_thresholds(&g_draft) != RT_EOK)
    {
        set_text(g_edit_hint, "Could not apply. Check the values and try again.");
        return;
    }
    g_status.thresholds = g_draft;
    set_text(g_edit_hint, "Applied. Above-limit readings trigger the alarm.");
}

static void cancel_cb(lv_event_t *event)
{
    RT_UNUSED(event);
    select_page(PAGE_HOME);
}

static void build_home(lv_obj_t *page)
{
    unsigned i;
    static const rt_uint32_t card_colors[3] = {0xfff2e4, 0xe5f1ea, 0xeeeafb};
    lv_obj_t *panel;
    for (i = 0; i < 3; i++)
    {
        lv_obj_t *card = box(page, 10 + i * 156, 8, 148, 102, card_colors[i], 8);
        label(card, g_names[i], 12, 10, 128, &lv_font_montserrat_12, g_colors[i]);
        g_values[i] = label(card, "--", 10, 29, 132, &lv_font_montserrat_28, INK);
        label(card, g_units[i], 12, 61, 128, &lv_font_montserrat_12, MUTED);
        g_limits[i] = label(card, "Limit --", 12, 81, 128, &lv_font_montserrat_12, MUTED);
    }
    panel = box(page, 10, 118, 460, 64, 0xffffff, 8);
    box(panel, 0, 0, 4, 64, GREEN, 0);
    g_summary = label(panel, "Waiting for sensors", 16, 9, 426,
                       &lv_font_montserrat_16, INK);
    g_summary_hint = label(panel, "Connect DHT11 and BH1750 to begin.", 16, 32,
                            426, &lv_font_montserrat_12, MUTED);
    g_sample_label = label(panel, "", 16, 48, 426, &lv_font_montserrat_12, MUTED);
}

static void chart_tick_cb(lv_event_t *event)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(event);
    if (g_metric == 2 && dsc->part == LV_PART_TICKS &&
        dsc->id == LV_CHART_AXIS_PRIMARY_Y && dsc->text != RT_NULL)
        rt_snprintf(dsc->text, dsc->text_length, "%ld", (long)dsc->value * 100);
}

static void build_trend(lv_obj_t *page)
{
    unsigned i;
    for (i = 0; i < 3; i++)
        g_metric_btn[i] = button(page, g_names[i], 10 + i * 156, 6,
                                 148, 32, metric_cb, i);
    g_chart = lv_chart_create(page);
    lv_obj_remove_style_all(g_chart);
    lv_obj_set_pos(g_chart, 52, 48);
    lv_obj_set_size(g_chart, 410, 108);
    lv_obj_clear_flag(g_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_chart, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(g_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_line_color(g_chart, lv_color_hex(LINE), LV_PART_MAIN);
    lv_obj_set_style_line_width(g_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(g_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(g_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(g_chart, &lv_font_montserrat_12, LV_PART_TICKS);
    lv_obj_set_style_text_color(g_chart, lv_color_hex(MUTED), LV_PART_TICKS);
    lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_chart, HISTORY_SIZE);
    lv_chart_set_div_line_count(g_chart, 4, 5);
    lv_chart_set_axis_tick(g_chart, LV_CHART_AXIS_PRIMARY_Y, 2, 0, 4, 1, true, 44);
    lv_obj_add_event_cb(g_chart, chart_tick_cb, LV_EVENT_DRAW_PART_BEGIN, RT_NULL);
    g_series = lv_chart_add_series(g_chart, lv_color_hex(g_colors[0]),
                                   LV_CHART_AXIS_PRIMARY_Y);
    g_trend_value = label(page, "Latest --", 12, 169, 200,
                           &lv_font_montserrat_12, INK);
    g_trend_hint = label(page, "60 readings / RAM only", 228, 169, 240,
                          &lv_font_montserrat_12, MUTED);
}

static void build_report(lv_obj_t *page)
{
    lv_obj_t *table = box(page, 10, 8, 460, 142, 0xffffff, 8);
    unsigned i;
    label(table, "RECENT READINGS", 12, 9, 210, &lv_font_montserrat_12, GREEN);
    label(table, "Average", 142, 9, 98, &lv_font_montserrat_12, MUTED);
    label(table, "Min / Max", 270, 9, 168, &lv_font_montserrat_12, MUTED);
    for (i = 0; i < 3; i++)
    {
        box(table, 12, 32 + i * 35, 436, 1, LINE, 0);
        label(table, g_names[i], 12, 42 + i * 35, 124,
               &lv_font_montserrat_12, g_colors[i]);
        g_report_rows[i] = label(table, "--", 142, 42 + i * 35, 120,
                                  &lv_font_montserrat_12, INK);
        g_report_ranges[i] = label(table, "-- / --", 270, 42 + i * 35, 174,
                                    &lv_font_montserrat_12, INK);
    }
    g_report_hint = label(page, "Waiting for samples", 14, 164, 452,
                           &lv_font_montserrat_12, MUTED);
}

static void build_menu(lv_obj_t *page)
{
    unsigned i;
    for (i = 0; i < 3; i++)
    {
        int y = 5 + i * 44;
        lv_obj_t *row = box(page, 10, y, 460, 40, 0xffffff, 6);
        label(row, g_names[i], 10, 5, 134, &lv_font_montserrat_12, INK);
        label(row, g_units[i], 10, 22, 134, &lv_font_montserrat_12, MUTED);
        g_edit_values[i] = label(row, "--", 153, 9, 132,
                                  &lv_font_montserrat_20, g_colors[i]);
        button(row, "-", 330, 2, 56, 36, edit_cb, i * 2);
        button(row, "+", 392, 2, 56, 36, edit_cb, i * 2 + 1);
    }
    button(page, "Cancel", 10, 136, 110, 34, cancel_cb, 0);
    {
        lv_obj_t *apply = button(page, "Apply limits", 128, 136, 152, 34, apply_cb, 0);
        lv_obj_set_style_bg_color(apply, lv_color_hex(GREEN), 0);
        lv_obj_set_style_text_color(apply, lv_color_hex(0xffffff), 0);
    }
    g_edit_hint = label(page, "Adjust upper limits, then tap Apply.", 12, 174,
                         456, &lv_font_montserrat_12, MUTED);
}

static void push_history(const gh_sample_t *sample, rt_uint32_t alarm)
{
    g_history[g_next] = *sample;
    g_alarm_history[g_next] = alarm;
    g_next = (g_next + 1U) % HISTORY_SIZE;
    if (g_count < HISTORY_SIZE) g_count++;
    g_history_dirty = RT_TRUE;
}

static void render_home(void)
{
    char value[24], buf[72];
    unsigned i;
    const gh_sample_t *sample = &g_status.sample;
    for (i = 0; i < 3; i++)
    {
        if (!g_stale && (sample->valid_mask & g_valid[i]))
            format_value(value, sizeof(value), sample_value(sample, i), i);
        else
            rt_snprintf(value, sizeof(value), "--");
        set_text(g_values[i], value);
        format_value(value, sizeof(value), limit_value(i), i);
        rt_snprintf(buf, sizeof(buf), "Limit %s %s", value, g_units[i]);
        set_text(g_limits[i], buf);
    }
    if (!g_have_sample || g_stale || sample->valid_mask == 0)
    {
        set_text(g_summary, g_have_sample ? "Sensor data unavailable" : "Waiting for sensors");
        set_text(g_summary_hint, "Check sensor power and signal wiring.");
    }
    else if (g_status.alarm_mask)
    {
        set_text(g_summary, "Above the configured limit");
        rt_snprintf(buf, sizeof(buf), "%s%s%s",
            g_status.alarm_mask & GH_ALARM_TEMP_HIGH ? "Temperature  " : "",
            g_status.alarm_mask & GH_ALARM_HUMID_HIGH ? "Humidity  " : "",
            g_status.alarm_mask & GH_ALARM_LIGHT_HIGH ? "Light" : "");
        set_text(g_summary_hint, buf);
    }
    else
    {
        set_text(g_summary, "Readings within upper limits");
        set_text(g_summary_hint, sample->valid_mask == 7 ?
            "All three sensor readings are available." : "Partial data: one or more sensors unavailable.");
    }
    rt_snprintf(buf, sizeof(buf), "Sample #%lu   |   RT-Thread   |   v" UI_VERSION,
                (unsigned long)sample->sequence);
    set_text(g_sample_label, buf);
}

static void render_trend(void)
{
    char value[24], buf[64];
    if (g_history_dirty)
    {
        lv_coord_t *points = lv_chart_get_y_array(g_chart, g_series);
        rt_int32_t high = g_metric == 0 ? 50 : (g_metric == 1 ? 100 : 10);
        rt_int32_t low = g_metric == 0 ? -10 : 0;
        unsigned i, oldest = g_count == HISTORY_SIZE ? g_next : 0;
        for (i = 0; i < HISTORY_SIZE; i++) points[i] = LV_CHART_POINT_NONE;
        for (i = 0; i < g_count; i++)
        {
            const gh_sample_t *sample = &g_history[(oldest + i) % HISTORY_SIZE];
            rt_int32_t v;
            if (!(sample->valid_mask & g_valid[g_metric])) continue;
            v = sample_value(sample, g_metric);
            if (g_metric < 2) v /= 10;
            else v /= 100; /* 100 lux units keep values within lv_coord_t. */
            if (v > high) high = v + 10;
            if (v < low) low = v - 10;
            points[HISTORY_SIZE - g_count + i] = (lv_coord_t)v;
        }
        lv_chart_set_x_start_point(g_chart, g_series, 0);
        lv_chart_set_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, low, high);
        lv_chart_set_series_color(g_chart, g_series, lv_color_hex(g_colors[g_metric]));
        lv_chart_refresh(g_chart);
        g_history_dirty = RT_FALSE;
    }
    if (!g_stale && (g_status.sample.valid_mask & g_valid[g_metric]))
        format_value(value, sizeof(value), sample_value(&g_status.sample, g_metric), g_metric);
    else
        rt_snprintf(value, sizeof(value), "--");
    rt_snprintf(buf, sizeof(buf), "Latest  %s %s", value, g_units[g_metric]);
    set_text(g_trend_value, buf);
    rt_snprintf(buf, sizeof(buf), "%u / 60 readings  |  RAM only", g_count);
    set_text(g_trend_hint, buf);
}

static void render_report(void)
{
    char avg_text[24], min_text[24], max_text[24], buf[80];
    unsigned metric, i, alarms = 0;
    for (metric = 0; metric < 3; metric++)
    {
        rt_int64_t sum = 0;
        rt_int32_t min = 0, max = 0;
        unsigned count = 0;
        for (i = 0; i < g_count; i++)
        {
            rt_int32_t value;
            if (!(g_history[i].valid_mask & g_valid[metric])) continue;
            value = sample_value(&g_history[i], metric);
            if (!count || value < min) min = value;
            if (!count || value > max) max = value;
            sum += value;
            count++;
        }
        if (count)
        {
            format_value(avg_text, sizeof(avg_text), (rt_int32_t)(sum / count), metric);
            format_value(min_text, sizeof(min_text), min, metric);
            format_value(max_text, sizeof(max_text), max, metric);
            /* Fixed-width columns are separate labels below, avoiding spaces-as-layout. */
            rt_snprintf(buf, sizeof(buf), "%s %s", avg_text, g_units[metric]);
            set_text(g_report_rows[metric], buf);
            rt_snprintf(buf, sizeof(buf), "%s / %s", min_text, max_text);
            set_text(g_report_ranges[metric], buf);
        }
        else
        {
            set_text(g_report_rows[metric], "--");
            set_text(g_report_ranges[metric], "-- / --");
        }
    }
    for (i = 0; i < g_count; i++) if (g_alarm_history[i]) alarms++;
    rt_snprintf(buf, sizeof(buf), "%u readings   |   %u alarm samples   |   Cleared on reboot",
                g_count, alarms);
    set_text(g_report_hint, buf);
}

static void render_page(void)
{
    if (!g_built) return;
    set_text(g_badge, status_text());
    if (g_last_alarm != g_status.alarm_mask || g_last_valid != g_status.sample.valid_mask ||
        g_last_stale != g_stale)
    {
        rt_uint32_t color = g_status.alarm_mask ? 0xb74b35 : GREEN;
        if (g_stale || !g_status.sample.valid_mask) color = MUTED;
        lv_obj_set_style_text_color(g_badge, lv_color_hex(color), 0);
        g_last_alarm = g_status.alarm_mask;
        g_last_valid = g_status.sample.valid_mask;
        g_last_stale = g_stale;
    }
    if (g_page == PAGE_HOME) render_home();
    else if (g_page == PAGE_TREND) render_trend();
    else if (g_page == PAGE_REPORT) render_report();
}

static void select_page(unsigned page)
{
    unsigned i;
    if (!g_built || page >= PAGE_COUNT) return;
    if (g_page != page)
    {
        lv_obj_add_flag(g_pages[g_page], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_pages[page], LV_OBJ_FLAG_HIDDEN);
        g_page = page;
        g_switches++;
        for (i = 0; i < PAGE_COUNT; i++)
        {
            lv_obj_set_style_bg_color(g_nav[i], lv_color_hex(i == page ? GREEN : 0xffffff), 0);
            lv_obj_set_style_text_color(g_nav[i], lv_color_hex(i == page ? 0xffffff : MUTED), 0);
        }
        if (page == PAGE_MENU)
        {
            g_draft = g_status.thresholds;
            update_draft();
            set_text(g_edit_hint, "Adjust upper limits, then tap Apply.");
        }
        if (page == PAGE_TREND) g_history_dirty = RT_TRUE;
    }
    render_page();
}

static void poll_data(lv_timer_t *timer)
{
    gh_sample_t sample;
    gh_status_t status;
    rt_tick_t start = rt_tick_get();
    unsigned consumed = 0;
    rt_bool_t changed = RT_FALSE, stale;
    RT_UNUSED(timer);
    if (!g_data_ready || gh_data_get_status(&status) != RT_EOK) return;
    while (consumed++ < GH_QUEUE_DEPTH && gh_data_recv_ui(&sample, 0) == RT_EOK)
    {
        if (g_have_sample && sample.sequence == g_status.sample.sequence) continue;
        g_status.sample = sample;
        push_history(&sample, status.alarm_mask);
        g_have_sample = RT_TRUE;
        changed = RT_TRUE;
    }
    if (g_status.alarm_mask != status.alarm_mask ||
        memcmp(&g_status.thresholds, &status.thresholds, sizeof(status.thresholds)) != 0)
        changed = RT_TRUE;
    g_status.thresholds = status.thresholds;
    g_status.alarm_mask = status.alarm_mask;
    stale = !g_have_sample || (rt_tick_t)(rt_tick_get() - g_status.sample.tick) >
            rt_tick_from_millisecond(GH_SENSOR_PERIOD_MS * 4);
    if (stale != g_stale) changed = RT_TRUE;
    g_stale = stale;
    if (changed)
    {
        rt_uint32_t ms;
        render_page();
        g_updates++;
        ms = (rt_uint32_t)((rt_tick_get() - start) * 1000U / RT_TICK_PER_SECOND);
        if (ms > g_max_update_ms) g_max_update_ms = ms;
    }
}

/* Called by lv_user_gui_init AFTER lv_init and display/input registration. */
void gh_ui_lvgl_init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_t *bar;
    unsigned i;
    static const char *tabs[PAGE_COUNT] = {
        LV_SYMBOL_HOME " Home", LV_SYMBOL_EYE_OPEN " Trends",
        LV_SYMBOL_LIST " Report", LV_SYMBOL_SETTINGS " Menu"
    };
    if (g_built) return;
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    bar = box(screen, 0, 0, 480, 42, 0xffffff, 0);
    box(bar, 12, 12, 4, 18, GREEN, 0);
    label(bar, "Greenhouse", 24, 9, 240, &lv_font_montserrat_20, INK);
    g_badge = label(bar, "WAITING", 348, 14, 120, &lv_font_montserrat_12, MUTED);
    lv_obj_set_style_text_align(g_badge, LV_TEXT_ALIGN_RIGHT, 0);
    for (i = 0; i < PAGE_COUNT; i++)
    {
        g_pages[i] = box(screen, 0, 42, 480, 190, BG, 0);
        if (i != PAGE_HOME) lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    build_home(g_pages[PAGE_HOME]);
    build_trend(g_pages[PAGE_TREND]);
    build_report(g_pages[PAGE_REPORT]);
    build_menu(g_pages[PAGE_MENU]);
    bar = box(screen, 0, 232, 480, 40, 0xffffff, 0);
    for (i = 0; i < PAGE_COUNT; i++)
        g_nav[i] = button(bar, tabs[i], 6 + i * 118, 3, 114, 34, nav_cb, i);
    g_status.thresholds.temp_high_x10 = 350;
    g_status.thresholds.humidity_high_x10 = 850;
    g_status.thresholds.light_high_lux = 80000;
    g_page = PAGE_MENU; /* Force the initial Home selection style. */
    g_stale = RT_TRUE;
    g_built = RT_TRUE;
    select_page(PAGE_HOME);
    for (i = 0; i < 3; i++)
    {
        lv_obj_set_style_bg_color(g_metric_btn[i], lv_color_hex(i ? 0xe5eee8 : g_colors[0]), 0);
        lv_obj_set_style_text_color(g_metric_btn[i], lv_color_hex(i ? INK : 0xffffff), 0);
    }
    if (lv_timer_create(poll_data, 50, RT_NULL) == RT_NULL)
        rt_kprintf("[GH] UI polling timer allocation failed\n");
}

static void gh_ui_perf(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    rt_kprintf("UI v" UI_VERSION ": cached pages=4, switches=%lu, updates=%lu, max update=%lu ms\n",
        (unsigned long)g_switches, (unsigned long)g_updates, (unsigned long)g_max_update_ms);
    rt_kprintf("No page rebuilds; no UI-thread flash writes; LVGL access stays on its thread.\n");
}
MSH_CMD_EXPORT(gh_ui_perf, show greenhouse UI update counters);

rt_err_t gh_ui_start(void)
{
    g_data_ready = RT_TRUE;
    return RT_EOK;
}
#else
void gh_ui_lvgl_init(void) {}
rt_err_t gh_ui_start(void) { return RT_EOK; }
#endif
