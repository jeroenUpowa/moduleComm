#ifndef _LORA_JOBS_H_
#define _LORA_JOBS_H_


#include "debug.h"
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>


const char* getcode(enum comm_status_code code);
/*
	to print the status code
*/
static osjob_t lora_setup;
static osjob_t lora_report;

/*
	Jobs to run lora_init and lora_send 
*/

void lora_init(osjob_t* job);
/*
	LoRa communication setup: 
	-Connect to Gateway, if it fails, try again after RETRY_JOIN seconds
	-Print the status code
*/
void lora_send(osjob_t* job);
/*
	LoRa communication report:
	-Send data to Gateway
	-Schedule next transmission in TX_INTERVAL
	.... (more details coming soon)
*/


#endif _LORA_JOBS_H_