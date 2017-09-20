#include "lora_communication.h"

#ifdef LORA

#include "lora_jobs.h"

#define SAMPLE_SIZE 19

/*
Communication library for the Feather FONA GSM board and the Feather LORA
*/
// Prototypes
#define UITOA_BUFFER_SIZE 6
void uitoa(uint16_t val, uint8_t *buff);

static osjob_t blinkjob;
void blinkfunc(osjob_t* job);

// Configuration

// application router ID -> Gateway EUI (little-endian format)
static const u1_t PROGMEM APPEUI[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
void os_getArtEui(u1_t* buf) { memcpy_P(buf, APPEUI, 8); }

// Device EUI (little endian format)
static const u1_t PROGMEM DEVEUI[8] = { 0x25, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01 };
void os_getDevEui(u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
devaddr_t DevAddr = 0x74345678;

// Device-specific AES key (big endian format) 
static const u1_t PROGMEM APPKEY[16] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45, 0x67, 0x89, 0x01 };
void os_getDevKey(u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 20;

//Retry joining after this many seconds
const unsigned RETRY_JOIN = 120;

//Set conection & TX timeout
ostime_t JOIN_TIME = sec2osticks(60); // wait x secondes before timeout
ostime_t TX_TIME = sec2osticks(120);

// Pin mapping
const lmic_pinmap lmic_pins = {
	.nss = 8,
	.rxtx = LMIC_UNUSED_PIN,
	.rst = 4,
	.dio = { 3,SDA,6 },
};

// interval with which the LED blinks in seconds
// used to give information about the LoRa state
// 5 s    - default
// 500 ms - LoRa module trying to join network
// 1 s    - LoRa module successfully joined network
// 100 ms - LoRa module not joined network after retrying.
uint8_t BLINK_INTERVAL = 1;

// LMIC Event Callback

// false - default
// true - Module is successfully connected to the gateway
boolean isJoined = false;

// false - default
// true - Data successfully sent
boolean isSent = false;

void onEvent(ev_t ev) {
	Serial.print(os_getTime());
	Serial.print(": ");
	switch (ev) {
	case EV_JOINING:
		Serial.println(F("EV_JOINING"));
		BLINK_INTERVAL = 500;
		break;
	case EV_JOINED:
		Serial.println(F("EV_JOINED"));
		BLINK_INTERVAL = 1;
		isJoined = true;
		// Reporting
		os_setCallback(&lora_report, lora_send);
		break;
	case EV_RFU1:
		Serial.println(F("EV_RFU1 - unhandled event"));
		break;
	case EV_JOIN_FAILED:
		Serial.println(F("EV_JOIN_FAILED"));
		BLINK_INTERVAL = 100;
		os_clearCallback(&lora_setup);
		Serial.println(F("Retry joining in 120 seconds"));
		os_setTimedCallback(&lora_setup, os_getTime() + sec2osticks(RETRY_JOIN), lora_init);
		break;
	case EV_TXCOMPLETE:
		Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
		if (LMIC.txrxFlags & TXRX_ACK)
			Serial.println(F("Received ack"));
		if (LMIC.dataLen) {
			Serial.println(F("Received "));
			Serial.println(LMIC.dataLen);
			Serial.println(F(" bytes of payload"));
		}
		isSent = true;
		break;
	default:
		Serial.println(F("Unknown event"));
		break;
	}
}

// Jobs

void blinkfunc(osjob_t* job)
{
	digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

	if (BLINK_INTERVAL >= 10)
		os_setTimedCallback(job, os_getTime() + ms2osticks(BLINK_INTERVAL), blinkfunc);
	else
		os_setTimedCallback(job, os_getTime() + sec2osticks(BLINK_INTERVAL), blinkfunc);
}


enum comm_status_code comm_setup(void)
{
	Serial.println(F("Connecting to gateway..."));
	// Blink job
	os_setCallback(&blinkjob, blinkfunc);

	// Reset the MAC state. Session and pending data transfers will be discarded.
	LMIC_reset();

	LMIC_startJoining();

	ostime_t start = os_getTime();
	while ((os_getTime() - start < JOIN_TIME) && (!isJoined))
	{
		os_runloop_once();
	}
	
	if (!isJoined)
	{
		Serial.println(F("Connection timeout"));
		BLINK_INTERVAL = 100;
		os_clearCallback(&lora_setup);
		LMIC_shutdown();
		Serial.println(F("Retry joining in RETRY_JOIN seconds"));
		os_setTimedCallback(&lora_setup, os_getTime() + sec2osticks(RETRY_JOIN), lora_init); // Retry joining
		return COMM_ERR_RETRY_LATER;
	}
	else
	{
		return COMM_OK;
	}
}

enum comm_status_code comm_send_report(void)
{
	if (isJoined) // otherwise, "LMIC_setTxData" will call LMIC_startJoining even though we had an error returned by comm_setup
	{
		// Check if there is not a current TX/RX job running
		if (LMIC.opmode & OP_TXRXPEND)
		{
			Serial.println(F("OP_TXRXPEND, not sending"));
			os_clearCallback(&lora_report);
			//comm_abort();
			Serial.println(F("Trying again in TX_INTERVAL seconds"));
			os_setTimedCallback(&lora_report, os_getTime() + sec2osticks(TX_INTERVAL), lora_send);
			return COMM_ERR_RETRY_LATER;
		}
		else
		{
			Serial.println(F("Starting Report"));
			isSent  = false;

			// (Re)Configure serial interface to the box
			Serial1.begin(38400);

			// Get sample data
			uint8_t buff[SAMPLE_SIZE];
			uint8_t code = -1; // get_data_from_box(buff);

			if (code != 0) { // Something wrong with sampling
				db("sampling failed - send '0'");
				for (int i = 0; i < SAMPLE_SIZE; i++)
					buff[i] = '0';
			}

			// Prepare upstream data transmission at the next possible time. 
			LMIC_setTxData2(1, buff, sizeof(buff), 0);
			Serial.println(F("Packet queued"));

			ostime_t start = os_getTime();
			while ((!isSent) && (os_getTime() - start < TX_TIME))
			{
				os_runloop_once();
			}

			if (!isSent)
			{
				Serial.println(F("TX timeout"));
				Serial1.end();
				os_clearCallback(&lora_report);
				//comm_abort();
				Serial.println(F("Trying again in TX_INTERVAL seconds"));
				os_setTimedCallback(&lora_report, os_getTime() + sec2osticks(TX_INTERVAL), lora_send);
				return COMM_ERR_RETRY_LATER;
			}
			Serial1.end();
			Serial.println(F("Scheduling next transmission..."));
			os_setTimedCallback(&lora_report, os_getTime() + sec2osticks(TX_INTERVAL), lora_send); // Next transmission in 60 secondes
			//comm_abort();
			return COMM_OK;
		}
	}
	Serial.println(F("Not connected to gateway"));
	return COMM_ERR_RETRY_LATER;
}

enum comm_status_code comm_abort(void)
{
	Serial.println(F("Abort"));
	os_radio(RADIO_RST); // put radio to sleep
	delay(2000);
	digitalWrite(LED_BUILTIN, LOW);
	return COMM_OK;
}


// 

// Custom uitoa, fixed base, expects buffer of the right size
void uitoa(uint16_t val, uint8_t *buff) {
	uint8_t i;
	// Common exception
	if (val == 0) {
		buff[0] = 48;
		buff[1] = 0;
		return;
	}

	// Do the magic
	i = UITOA_BUFFER_SIZE - 1;
	buff[i] = 0;  // From the tail, we'll go towards the head
	while (val) {
		buff[--i] = 48 + val % 10;  // Fill in each digit (--i happens first, so i still points to the digit when done)
		val /= 10;
	}

	// Left shift the result (remove padding)
	if (i) do { buff[0] = buff[i]; } while (*(buff++) != 0);
}

#endif // LORA
