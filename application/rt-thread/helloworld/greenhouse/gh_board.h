#ifndef GREENHOUSE_BOARD_H
#define GREENHOUSE_BOARD_H

#include <rtthread.h>

rt_err_t gh_board_init(void);
rt_err_t gh_board_buzzer_set(rt_bool_t enabled);
void gh_board_print_info(void);

#endif
