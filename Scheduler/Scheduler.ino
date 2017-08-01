








#include "task_scheduler.h"

#define DB_MODULE "Scheduler"
#include "debug.h"

void short_blink() {
	digitalWrite(LED_BUILTIN, HIGH);
	delay(20);
	digitalWrite(LED_BUILTIN, LOW);
}
void long_blink() {
	digitalWrite(LED_BUILTIN, HIGH);
	delay(1000);
	digitalWrite(LED_BUILTIN, LOW);
}
void say_hello() {
	Serial.println("Hello");
	Serial.flush();
}
void say_hello_nicelly() {
	delay(500);
	Serial.begin(115200);
	while (!Serial);
	Serial.println("Hello good sir");
	Serial.flush();
}


void setup()
{
	// Board setup
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, LOW);

	Serial.begin(115200);
	while (!Serial);

	Serial.println("Hey there v1, starting in a couple seconds...");
	Serial.flush();

	delay(2000);

	// Launch tasks
	Serial.println("Setup");
	Serial.flush();

	sched_setup();

	Serial.println("Adding tasks");
	Serial.flush();

	sched_add_task(long_blink, 10, 10);

	sched_add_task(say_hello, 1, 3);
	sched_add_task(say_hello_nicelly, 2, 3);

	Serial.println("Entering mainloop");
	Serial.flush();

	delay(200);

	sched_mainloop();
}

void loop()
{

}
