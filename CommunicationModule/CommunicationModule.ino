#include <lmic.h>
#include <SPI.h>

#define VERSION    "1.1"
#define DISTR_DATE "2017-10-20"


#define DB_MODULE "Communication Module"
#include "debug.h"


#ifdef GSM
#include <SoftwareSerial.h>
#ifndef __AVR_ATmega32U4__
#error Wrong board !
#endif
#elif LORA
#ifndef __SAMD21G18A__
#error Wrong board !
#include "lora_communication.h"
#include "lora_jobs.h"
#endif
#else
#error Define either GSM or LORA on the project settings !
#endif

#include "communication.h"

#include "task_scheduler.h"
#include "storage_manager.h"

#include "sampling_task.h"
#include "reporting_task.h"

void setup()
{
	db_start();
	db_wait();

	db("Starting");
	db("comm setup");
	comm_status_code comm_code = comm_setup();
	while (comm_code != COMM_OK) {
		db("comm setup - failed");
		db("comm setup - retry");
		comm_code = comm_setup();
	}

	db("storage setup");
	stor_setup();

	db("sampling setup");
	sampling_setup();

	db("reporting setup");
	reporting_setup();

	db("start testing");
	uint8_t code = do_tests();

	while (code != COMMENCE_REPORTING)
	{
		db("tests failed");
		for (int i = 0; i < 12; i++) {
			delay(10000); // wait 2 minutes
		}
		db("retest");
		code = do_tests();
	}
	db("tests finished");
	
	// // // // TEST ZONE
//	for (int i = 0; i < 10; i++) {
//		db_print("i: ");
//		db_println(i);
//		sampling_task();
//		delay(500);
//	}

//	reporting_task();
//	db("reporting finished");
//	while (1);
	// // // //  // // // 

	// Launch tasks
	db("scheduler setup");
	delay(100);
	sched_setup();

	db("scheduler add task");
	sched_add_task(sampling_task, SAMPLING_LOOPTIME, SAMPLING_LOOPTIME);

	db("scheduler add task");
	sched_add_task(reporting_task, REPORTING_LOOPTIME, REPORTING_LOOPTIME);

	db("scheduler mainloop");
	delay(100);
	sched_mainloop();
}

void loop()
{

}

uint8_t do_tests(void)
{
	uint8_t buffer[50];
	uint8_t code = sampling_test(buffer);

	db_print("sampling test result: ");
	for (int i = 0; i < 33; i++) {
		db_print(" ");
		db_print(buffer[i]); // , HEX);
	}
	db_println("");
	db_println("Storage test");

	code = stor_test();
	buffer[33] = code;

	if (code == 'O') {
		db("stor test success");
	}
	else {
		db("stor test failed");
	}
	buffer[34] = 0;

	code = reporting_test(buffer, 35);
	return code;
}