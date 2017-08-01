// reporting_task.h

#ifndef _REPORTING_TASK_h
#define _REPORTING_TASK_h

#include "arduino.h"

#ifdef GSM
#define REPORTING_LOOPTIME  7200
#define MAX_BYTES_PER_REPORT 65536u
#elif LORA
#define REPORTING_LOOPTIME  600
#define MAX_BYTES_PER_REPORT 55u
#endif




#define START_COMM_MAX_RETRIES 5

#define RETRY_CONNECTION_MAX_TRIES 5
#define RETRY_CONNECTION_TIME REPORTING_LOOPTIME/(RETRY_CONNECTION_MAX_TRIES + 1)

#define FETCH_BUFFER_MAX_SIZE 512u


void reporting_setup(void);

void reporting_task(void);



#endif

