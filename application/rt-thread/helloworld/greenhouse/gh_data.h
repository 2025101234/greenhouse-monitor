#ifndef GREENHOUSE_DATA_H
#define GREENHOUSE_DATA_H

#include "gh_types.h"

rt_err_t gh_data_init(void);
rt_err_t gh_data_publish(const gh_sample_t *sample);
rt_err_t gh_data_recv_alarm(gh_sample_t *sample, rt_int32_t timeout);
rt_err_t gh_data_recv_ui(gh_sample_t *sample, rt_int32_t timeout);
rt_err_t gh_data_get_status(gh_status_t *status);
rt_err_t gh_data_get_thresholds(gh_thresholds_t *thresholds);
rt_err_t gh_data_set_thresholds(const gh_thresholds_t *thresholds);
void gh_data_set_alarm_mask(rt_uint32_t alarm_mask);

#endif
