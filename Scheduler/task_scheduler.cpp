#include "task_scheduler.h"

#define DB_MODULE "Scheduler"
#include "debug.h"

#ifdef __AVR_ATmega32U4__ /* Using ATmega32u4 - GSM module - Watchdog used for sleep management*/
#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/power.h>
#elif __SAMD21G18A__ /* Using SAM-D21 - LORA module - RTC used for sleep management*/
#include <samd.h>
#endif


/*
	Simple scheduler
	1Hz tick interruption updates the timers
	mainloop calls tasks ready to be executed (non-preemptivelly) in a fifo execution order
	made for cyclic tasks (looptime)
*/

#define SCHED_MAX_TASKS 8


struct task_handle {
	void (*task)(void);
	volatile int32_t delay;
	int32_t looptime;
};

struct task_handle task_list[SCHED_MAX_TASKS];


inline void tick_callback(void);
void sched_sleep(void);


void sched_setup(void) {
	db("Setup");
	// Structure setup
	uint8_t i;
	for (i = 0; i < SCHED_MAX_TASKS; i++) {
		task_list[i].task = NULL;
	}

	// Clocks setup
#ifdef __AVR_ATmega32U4__ /* Using ATmega32u4 - GSM module */
	// Board setup

	// We'll be using the Watchdog since it's always ON
	// Configure the Watchdog for 1Hz interrupts
	noInterrupts();
	wdt_reset();
	/* Setup Watchdog */ // Source : MICROCHIP APP NOTE AVR132
	// Use Timed Sequence for disabling Watchdog System Reset Mode if it has been enabled unintentionally.
	MCUSR &= ~(1 << WDRF);                                 // Clear WDRF if it has been unintentionally set.
	WDTCSR = (1 << WDCE) | (1 << WDE);                     // Enable configuration change.
	WDTCSR = (1 << WDIF) | (1 << WDIE) |                     // Enable Watchdog Interrupt Mode.
		(1 << WDCE) | (0 << WDE) |                     // Disable Watchdog System Reset Mode if unintentionally enabled.
		(0 << WDP3) | (1 << WDP2) | (1 << WDP1) | (0 << WDP0); // Set Watchdog Timeout period to 1.0 sec.

	wdt_reset();
	interrupts();
}

ISR(WDT_vect) { // Watchdog interrupt. Interrupt driven tick, called every 1s, updates task delays
	tick_callback();
}  // After the IRC returns, the CPU runs the mainloop

#endif
#ifdef __SAMD21G18A__
	// Board setup
	
	// Using the RTC module
	// Enable clock source
	PM->APBAMASK.reg |= PM_APBAMASK_RTC;

	SYSCTRL->XOSC32K.reg = SYSCTRL_XOSC32K_ONDEMAND |
		SYSCTRL_XOSC32K_RUNSTDBY |
		SYSCTRL_XOSC32K_EN32K |
		SYSCTRL_XOSC32K_XTALEN |
		SYSCTRL_XOSC32K_STARTUP(6) |
		SYSCTRL_XOSC32K_ENABLE;

	// Configure GCLK2
	GCLK->GENDIV.reg = GCLK_GENDIV_ID(2) | GCLK_GENDIV_DIV(4); // With DIVSEL the division factor is 2^(DIV + 1), so we use 2^5 for a 32 factor (1khz out)
	while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY);
	GCLK->GENCTRL.reg = (GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_XOSC32K | GCLK_GENCTRL_ID(2) | GCLK_GENCTRL_DIVSEL);
	while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY);
	GCLK->CLKCTRL.reg = (uint32_t)((GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK2 | (RTC_GCLK_ID << GCLK_CLKCTRL_ID_Pos)));
	while (GCLK->STATUS.bit.SYNCBUSY);
	
	// Reseting the module
	// Using MODE 1 for set period overflow
	RTC->MODE1.CTRL.reg &= ~RTC_MODE1_CTRL_ENABLE; // disable RTC
	while (RTC->MODE1.STATUS.bit.SYNCBUSY);
	RTC->MODE1.CTRL.reg |= RTC_MODE1_CTRL_SWRST; // software reset
	while (RTC->MODE1.STATUS.bit.SYNCBUSY);

	RTC->MODE1.CTRL.reg |= RTC_MODE1_CTRL_MODE_COUNT16 // 16bit counter mode
		| RTC_MODE1_CTRL_PRESCALER_DIV1024; // 1hz count
	RTC->MODE1.PER.reg |= RTC_MODE1_PER_PER(0); // count to 1 at 1hz for 1s period

	RTC->MODE1.INTFLAG.reg = RTC_MODE1_INTFLAG_OVF; // clear flag
	RTC->MODE1.INTENSET.reg |= RTC_MODE1_INTENSET_OVF; // enable overflow interrupt
	//RTC->MODE1.INTENCLR.reg |= RTC_MODE1_INTENCLR_CMP0 | RTC_MODE1_INTENCLR_CMP1 | RTC_MODE1_INTENCLR_SYNCRDY; // Disable other interrupts

	NVIC_EnableIRQ(RTC_IRQn); // enable RTC interrupt 

	RTC->MODE1.CTRL.reg |= RTC_MODE1_CTRL_ENABLE; // enable RTC
	while (RTC->MODE1.STATUS.bit.SYNCBUSY);
}

void RTC_Handler(void)  // Fills in a weak reference on the Core definitions
{
	RTC->MODE1.INTFLAG.reg = RTC_MODE1_INTFLAG_OVF; // must clear flag (by writting 1 to it)
	tick_callback();

}
#endif


inline void tick_callback(void) {
	uint8_t i;
	// Interrupts should be already non-rentrant, and this one has a 1s cycle
	for (i = 0; i < SCHED_MAX_TASKS; i++) { // For every (valid) task
		if (task_list[i].task != NULL) {
			task_list[i].delay -= 1;  // Update time by 1 tick
		}
	}
}


uint8_t sched_add_task(void (*task)(void), int32_t delay, int32_t looptime) {
	db("Add task");
	uint8_t i;
	// Add to list
	noInterrupts(); // Atomic access to the task list
	for (i = 0; i < SCHED_MAX_TASKS; i++) {// Find an empty slot
		if (task_list[i].task == NULL) {
			task_list[i].task = task;
			task_list[i].delay = delay;
			task_list[i].looptime = looptime;
			break;
		}
	}
	interrupts(); // End of atomic

	if (i == SCHED_MAX_TASKS) {  // Max tasks limit reached
		return -1;
	}
	return i; // Return task index
}

void sched_mainloop(void) {
	db("Mainloop");
	// Run tasks and go to sleep
	while (1) {
		uint8_t i;
		for (i = 0; i < SCHED_MAX_TASKS; i++) { // For every (valid) task
			if (task_list[i].task != NULL && task_list[i].delay <= 0) {  // If it can be run

				task_list[i].task();  // Run task

				noInterrupts(); // Atomic access to the task delay
				if (task_list[i].looptime > 0) { // Cyclic
					task_list[i].delay += task_list[i].looptime;
				}
				else { // One-shot
					task_list[i].task = NULL;
				}
				interrupts(); // End of atomic
			}
		}
		
		sched_sleep();

		db_start();

		// "I'm alive"
		digitalWrite(LED_BUILTIN, HIGH);
		delay(1);
		digitalWrite(LED_BUILTIN, LOW);
	} // while(1)
}


void sched_sleep(void) {
#ifdef __AVR_ATmega32U4__
	power_all_disable(); // Disable peripherals

#ifdef _DEBUG
						 // Keep USB serial working...
	set_sleep_mode(SLEEP_MODE_IDLE);
#else
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
#endif
	sleep_enable();
	sleep_cpu(); // Will wake up every 1s due to the WDT/RTC interrupt
	sleep_disable();
	interrupts();

	power_all_enable(); // Enable peripherals

#elif __SAMD21G18A__
	SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
	__WFI();
#endif
}