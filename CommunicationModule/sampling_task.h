#ifndef _SAMPLING_TASK_h
#define _SAMPLING_TASK_h

#include "arduino.h"

#define SAMPLING_LOOPTIME  60

#define SAMPLE_SIZE 19
#define OPID_SIZE   14
#define PAYG_SIZE   13


void sampling_setup(void);

void sampling_task(void);

uint8_t sampling_test(uint8_t *buffer);

uint8_t get_paygState_from_box(uint8_t *buffer);

uint8_t get_data_from_box(uint8_t *buffer);

uint8_t get_opid_from_box(uint8_t *buffer);

#endif