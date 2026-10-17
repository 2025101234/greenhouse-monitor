#ifndef GREENHOUSE_UI_H
#define GREENHOUSE_UI_H

#include <rtthread.h>

rt_err_t gh_ui_start(void);
/* Must only be called from lv_user_gui_init on the LVGL thread. */
void gh_ui_lvgl_init(void);

#endif
