#ifndef _TASK_SCHEDULER_H_
#define _TASK_SCHEDULER_H_

#include "Arduino.h"

/**
	Simple scheduler

	Add tasks to be run cyclically
	Call the mainloop to enter sleep and run tasks when they're ready

	Tasks are executed after every interruption, if they are ready, and in the order they were added to the scheduler
	If a task is late to be executed, it might be executed multiple times in a row with no interval until it's not ready anymore

	The sleep mode is configured to the one with the least energy comsumption
*/

/*
	Starts the timer system and turn on the required clocks and interruptions
	Uses WDT on AVR architectures and RTC on SAMD ones
	A 1hz interrupt is configured and handled by the scheduler
*/
void sched_setup(void);

/*
	Adds the function to the task list, with a initial delay of <delay> seconds and to be run every <looptime> seconds from thereon
*/
uint8_t sched_add_task(void(*task)(void), int32_t delay, int32_t looptime);

/*
	Enters the scheduler main loop
	The scheduler will run any ready tasks and then enter sleep mode until the next interrupt wakes up the processor
*/
void sched_mainloop(void);

#endif // !_TASK_SCHEDULER_H_

