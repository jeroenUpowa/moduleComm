




#include "storage_manager.h"

#define DB_MODULE "Memory"
#include "debug.h"

void setup() {

	Serial.begin(115200);
	while (!Serial);

	Serial.println("Begin");

#ifdef __SAMD21G18A__
	pinMode(8, OUTPUT);
	digitalWrite(8, HIGH); //disable radio device to avoid conflicts
#endif

	stor_setup();
	stor_start();
}

void loop() {
	delay(1000);
	int maxlen = 148;
	byte buf[148];
	for (int I = 0; I<maxlen; I++)
	{
		buf[I] = 0;
	}

	int len = stor_read(buf, maxlen);
	print_samples(buf, len);

	byte sample[18];
	for (int I = 0; I<18; I++)
	{
		sample[I] = I;
	}
	for (int I = 0; I<15; I++)
	{
		stor_write(sample, 18);
	}

	len = stor_read(buf, maxlen);
	print_samples(buf, len);

	// end of execution
	while (1) {}
}

void print_samples(byte *buf, int len)
{
	Serial.print("Length: ");
	Serial.println(len);
	for (int adr = 0; adr < len; adr++)
	{
		Serial.print(buf[adr], DEC);
		Serial.print(" ");
	}
	Serial.print('\n');
}

