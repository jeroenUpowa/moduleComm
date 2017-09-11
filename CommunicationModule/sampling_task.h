#ifndef _SAMPLING_TASK_h
#define _SAMPLING_TASK_h

#include "arduino.h"

#define SAMPLING_LOOPTIME  10

#define SAMPLE_SIZE 19


void sampling_setup(void);

void sampling_task(void);

uint8_t sampling_test(uint8_t *buffer);

#endif