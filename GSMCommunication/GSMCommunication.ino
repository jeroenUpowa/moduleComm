







#define __GSM_TEST__
#include "communication.h"



#define DB_MODULE "GSM Communication"
#include "debug.h"


void setup() {
	const char report_string[] = "Hello World, this is FONA";

	// put your setup code here, to run once:
	Serial.begin(115200);
	while (!Serial);

	db("\nCommunication module test\n");

	enum comm_status_code code = comm_setup();

	db("\nsetup return code :");
	db_println(code);

	delay(1000);

	if (get_reply("AT", OK_REPLY, 100) == COMM_OK) {
		db("\ngot repply\n");
	}
	else {
		db("\nno reply\n");
	}

	db("\npower on\n");
	code = power_on();
	db("\npower on return code :");
	db_println(code);


	delay(1000);

	if (get_reply("AT", OK_REPLY, 100) == COMM_OK) {
		db("\ngot repply\n");
	}
	else {
		db("\nno reply\n");
	}

	db("\ncomm abort\n");
	code = comm_abort();
	db("\n comm abort return code :");
	db_println(code);


	//while (1);

	db("\npower off\n");
	code = power_off();
	db("\npower off return code :");
	db_println(code);


	unsigned long report_start_time = millis();

	db("\nstart report\n");
	code = comm_start_report(strlen(report_string));
	db("\nstart report return code :");
	db_println(code);


	unsigned long report_fill_time = millis();

	db("\nfill report\n");
	code = comm_fill_report((const uint8_t *)report_string, strlen(report_string));
	db("\nfill return code :");
	db_println(code);


	unsigned long report_send_time = millis();

	db("\nsend report\n");
	code = comm_send_report();
	db("\nsend report return code :");
	db_println(code);


	unsigned long report_end_time = millis();

	db("\n ------- Time to start report: ");
	db_println(report_fill_time - report_start_time);

	db("\n ------- Time to fill report: ");
	db_println(report_send_time - report_fill_time);

	db("\n ------- Time to send report: ");
	db_println(report_end_time - report_send_time);

	db("\n ------- Time to perform all operations: ");
	db_println(report_end_time - report_start_time);

}

void loop() {
	// put your main code here, to run repeatedly:
	;
}
