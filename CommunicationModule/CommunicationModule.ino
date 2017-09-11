#include <lmic.h>
#include <SPI.h>


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
	db("gsm setup");
	comm_setup();

	db("storage setup");
	stor_setup();

	db("sampling setup");
	sampling_setup();

	db("reporting setup");
	reporting_setup();

	uint8_t buffer[50];
	uint8_t code = sampling_test(buffer);
	Serial.print("sampling test result: ");
	for (int i = 0; i < 33; i++) {
		Serial.print(" ");
		Serial.print(buffer[i], HEX);
	}

	Serial.println("");
	Serial.println("Storage test");
	code = stor_test();
	buffer[33] = code;
	if (code == 0)
		db("stor test success");
	else
		db("stor test failed");

	code = reporting_test(buffer, 34);

	// // // // TEST ZONE

	//sampling_task();
	//delay(1000);
	

	while (1);
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
