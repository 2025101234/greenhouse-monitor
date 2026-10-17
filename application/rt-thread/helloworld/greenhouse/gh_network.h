#ifndef GREENHOUSE_NETWORK_H
#define GREENHOUSE_NETWORK_H

#include <rtthread.h>

rt_err_t gh_network_start(void);
rt_size_t gh_network_make_status_json(char *buffer, rt_size_t buffer_size);

#endif
