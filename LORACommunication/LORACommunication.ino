
#include "lora_communication.h"
#include "lora_jobs.h"

#define DB_MODULE "LORA Communication"


#if defined(ARDUINO_SAMD_ZERO) && defined(SERIAL_PORT_USBVIRTUAL)
// Required for Serial on Zero based boards
#define Serial SERIAL_PORT_USBVIRTUAL
#endif

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

void setup()
{
	// Configure pins
	pinMode(LED_BUILTIN, OUTPUT);
	pinMode(lmic_pins.rst, OUTPUT);

	digitalWrite(LED_BUILTIN, HIGH);
	// Start Serial
	while (!Serial);
	Serial.begin(115200);
	delay(1000);
	Serial.println(F("Starting"));
	//Hard-resetting the radio
	digitalWrite(lmic_pins.rst, LOW);
	delay(2000);
	digitalWrite(lmic_pins.rst, HIGH);

	// Starting OS
	os_init();

	// LMIC init
	os_setCallback(&lora_setup, lora_init);

	os_runloop();
}


void loop() {
	
}
