#ifndef GREENHOUSE_SENSORS_H
#define GREENHOUSE_SENSORS_H

#include "gh_types.h"

rt_err_t gh_sensors_start(void);
rt_err_t gh_sensors_read_now(gh_sample_t *sample);
rt_err_t gh_sensors_publish_test_alarm(void);

#endif
