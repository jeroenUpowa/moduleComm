#include "communication.h"
#include "lora_jobs.h"
/*
	Jobs for LoRa module
*/

const char* getcode(enum comm_status_code code)
{
	switch (code)
	{
	case COMM_OK: return "COMM_OK";
	case COMM_ERR_RETRY: return "COMM_ERR_RETRY";
	case COMM_ERR_RETRY_LATER: return "COMM_ERR_RETRY_LATER";
	}
}

void lora_init(osjob_t* job)
{
	enum comm_status_code code;
	code = comm_setup();
	Serial.println(getcode(code));
}

void lora_send(osjob_t* job)
{
	enum comm_status_code code_report;
	code_report = comm_send_report();
	Serial.println(getcode(code_report));
}
