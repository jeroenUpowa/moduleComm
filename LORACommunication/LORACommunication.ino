


#include "communication.h"

#define DB_MODULE "LORA Communication"
#include "debug.h"
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>


#if defined(ARDUINO_SAMD_ZERO) && defined(SERIAL_PORT_USBVIRTUAL)
// Required for Serial on Zero based boards
#define Serial SERIAL_PORT_USBVIRTUAL
#endif

// to print the status code
const char* getcode(enum comm_status_code code)
{
	switch (code)
	{
	case COMM_OK: return "COMM_OK";
	case COMM_ERR_RETRY: return "COMM_ERR_RETRY";
	case COMM_ERR_RETRY_LATER: return "COMM_ERR_RETRY_LATER";
	}
}


void setup()
{
	const char report_string[] = "Hello World, this is LORA";

	enum comm_status_code code;
	code = comm_setup();
	Serial.println(getcode(code));

	enum comm_status_code code_start;
	code_start = comm_start_report(strlen(report_string));
	Serial.println(getcode(code_start));


	enum comm_status_code code_fill;
	code_fill = comm_fill_report((const uint8_t *)report_string, strlen(report_string));
	Serial.println(getcode(code_fill));

	enum comm_status_code code_report;
	code_report = comm_send_report();
	Serial.println(getcode(code_report));

}


void loop() {
	//os_runloop_once();
}
