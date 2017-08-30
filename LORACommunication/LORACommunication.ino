


#include "communication.h"
#include "lora_jobs.h"

#define DB_MODULE "LORA Communication"


#if defined(ARDUINO_SAMD_ZERO) && defined(SERIAL_PORT_USBVIRTUAL)
// Required for Serial on Zero based boards
#define Serial SERIAL_PORT_USBVIRTUAL
#endif

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
