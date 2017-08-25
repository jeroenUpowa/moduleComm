#include "communication.h"

#ifdef LORA


#include "lora_jobs.h"

#define SAMPLE_SIZE 19

/*
Communication library for the Feather LORA
*/

// Sampling task

#ifdef __AVR__
#include "util/crc16.h"
#else
static inline uint8_t _crc_ibutton_update(uint8_t crc, uint8_t data) __attribute__((always_inline, unused));
static inline uint8_t _crc_ibutton_update(uint8_t crc, uint8_t data)
{
	unsigned int i;

	crc = crc ^ data;
	for (i = 0; i < 8; i++) {
		if (crc & 0x01) {
			crc = (crc >> 1) ^ 0x8C;
		}
		else {
			crc >>= 1;
		}
	}
	return crc;
}

#endif // __AVR__


// Fills 0 until the 16th, then preamble address
const byte msg_header[] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0xc5,0x6a,0x29 };


struct command {
	byte bytes[8]; // The message 'string'
	uint8_t len; // The message lenght
	uint8_t anslen; // The lenght of the expected answer
	uint8_t datapos; // The position of the data on the answer
	uint8_t datalen; // The lenght of the data
};

//const struct command cmd_OPID = { { 0x07, 0x01, 0x0e, 0x9a }, 4, ???}; // Unknown lenght
//const struct command cmd_PPID = { { 0x07, 0x08, 0x14, 0xcb }, 4, ???};
const struct command cmd_PS = { { 0x06, 0x09, 0x57 }, 3, 8, 5, 1 }; // PAYG state
const struct command cmd_OCS = { { 0x06, 0x0a, 0xb5 }, 3, 8, 5, 1 }; // Output state
const struct command cmd_SSC = { { 0x06, 0x0b, 0xeb }, 3, 8, 5, 1 }; // System Status Code
const struct command cmd_RPD = { { 0x06, 0x05, 0xf4 }, 3, 8, 5, 2 }; // Remaining PAYG days
const struct command cmd_RSOC = { { 0x06, 0x0c, 0x68 }, 3, 8, 5, 1 }; // Relative SOC
const struct command cmd_RC = { { 0x06, 0x0d, 0x36 }, 3, 8, 5, 2 }; // Remaining Capaciry
const struct command cmd_FCC = { { 0x06, 0x0e, 0xd4 }, 3, 8, 5, 2 }; // Full Charge Capacity
const struct command cmd_ACC = { { 0x06, 0x0f, 0x8a }, 3, 8, 5, 2 }; // Accumulative energy output
const struct command cmd_AC = { { 0x06, 0x10, 0x56 }, 3, 8, 5, 2 }; // Discharge cycles
const struct command cmd_PD = { { 0x06, 0x07, 0x48 }, 3, 8, 5, 2 }; // Top up days
const struct command cmd_RDB = { { 0x06, 0x013, 0xb4 }, 3, 8, 5, 2 }; // Running days
const struct command cmd_HTOP = { { 0x06, 0x11, 0x08 }, 3, 14, 5, 8 }; // HASH TOP
const struct command cmd_CV1 = { { 0x08, 0x00, 0x3f, 0x02, 0x0b }, 5, 10, 7, 2 }; // Cell voltage
const struct command cmd_CV2 = { { 0x08, 0x00, 0x3e, 0x02, 0xcf }, 5, 10, 7, 2 };
const struct command cmd_CV3 = { { 0x08, 0x00, 0x3d, 0x02, 0x9a }, 5, 10, 7, 2 };
const struct command cmd_CV4 = { { 0x08, 0x00, 0x3c, 0x02, 0x5e }, 5, 10, 7, 2 };
const struct command cmd_BV = { { 0x08, 0x00, 0x09, 0x02, 0x8c }, 5, 10, 7, 2 }; // Battery voltage
const struct command cmd_BC = { { 0x08, 0x00, 0x0a, 0x02, 0xd9 }, 5, 10, 7, 2 }; // Battery current
const struct command cmd_BT = { { 0x08, 0x00, 0x08, 0x02, 0x48 }, 5, 10, 7, 2 }; // Battery temperature
																				 // cmd_passcode write passcode ...


const struct command * const msg_commands[] = {
	&cmd_RSOC,
	&cmd_RC,
	&cmd_FCC,
	&cmd_CV1,
	&cmd_CV2,
	&cmd_CV3,
	&cmd_CV4,
	&cmd_BV,
	&cmd_BT,
	&cmd_BC
};

#define MAX_TRIES 5
inline uint8_t send_command(const struct command *comm, uint8_t *databuff) {
	uint8_t recv[32];

	// Flush the input buffer
	delay(50);
	while (Serial1.available())
	{
		Serial1.read();
		delay(5);
	}

	// send the command
	db("sending command");
	Serial1.write(msg_header, 19);
	Serial1.write(comm->bytes, comm->len);

	// get the answer
	Serial1.setTimeout(1000);
	uint8_t nbytes = Serial1.readBytes(recv, comm->anslen);

	// Check answer
	if (nbytes != comm->anslen) { // Command timeout
		db("command timeout");
		return -1;
	}

	// Check [C5 6A 29] header/preamble
	if (recv[0] != 0xC5 || recv[1] != 0x6A || recv[2] != 0x29) {
		db("bad header");
		return -1;
	}

	// Check sum
	uint8_t sum = 0;
	for (int j = 0; j < comm->anslen - 1; j++) sum = _crc_ibutton_update(sum, recv[j]); // Maxim/Dallas CRC8
	if (sum != recv[comm->anslen - 1]) { // Bad checksum
		db("bad checksum");
		return -1;
	}

	// we did it, copy the data
	memcpy(databuff, recv + comm->datapos, comm->datalen);
	db_module(); db_print("got :");
#ifdef _DEBUG
	for (int i; i < comm->datalen; i++) {
		Serial.print(" ");
		Serial.print((byte)databuff[i], HEX);
	}
	db_println();
#endif // _DEBUG
	return 0;
}

inline uint8_t get_data_from_box(uint8_t *buffer) {
	db("getting data from box");
	// run all sampling commands
	for (int i = 0; i < 10; i++) {
		uint8_t tries = 0;

		while (send_command(msg_commands[i], buffer) && tries < MAX_TRIES) {
			tries++;
		}
		if (tries == MAX_TRIES) { // We didn't make it
			db("excessive retries");
			return -1;
		}

		buffer += msg_commands[i]->datalen;
	}
	db("sampling successful");
	return 0;
}
//// END SAMPLING_TASK


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
static const u1_t PROGMEM DEVEUI[8] = { 0x08, 0x05, 0x04, 0x05, 0x05, 0x06, 0x07, 0x08 };
void os_getDevEui(u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
devaddr_t DevAddr = 0x74345678;

// Device-specific AES key (big endian format) 
static const u1_t PROGMEM APPKEY[16] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45, 0x67, 0x89, 0x01 };
void os_getDevKey(u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 300;

//Retry joining after this many seconds
const unsigned RETRY_JOIN = 120;

//Set conection & TX timeout
ostime_t JOIN_TIME = sec2osticks(360); // wait x secondes before timeout
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
		LMIC_shutdown();
		Serial.println(F("Retry joining in 120 seconds"));
		os_setTimedCallback(&lora_setup, os_getTime() + sec2osticks(RETRY_JOIN), lora_init);
		comm_abort();
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
	isJoined = false;
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
		comm_abort();
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
		// Blink job
		os_setCallback(&blinkjob, blinkfunc);

		// Check if there is not a current TX/RX job running
		if (LMIC.opmode & OP_TXRXPEND)
		{
			Serial.println(F("OP_TXRXPEND, not sending"));
			BLINK_INTERVAL = 100;
			os_clearCallback(&lora_report);
			Serial.println(F("Trying again in TX_INTERVAL seconds"));
			os_setTimedCallback(&lora_report, os_getTime() + sec2osticks(TX_INTERVAL), lora_send);
			comm_abort();
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
			uint8_t code = get_data_from_box(buff);

			if (code != 0) { // Something wrong
				db("sampling aborted");
				Serial.println(F("Error with OV box..."));
				BLINK_INTERVAL = 100;
				Serial1.end();
			
				// Envoi échantillon spécial d'erreur
				uint8_t buff_spec[SAMPLE_SIZE] = {};
				LMIC_setTxData2(1, buff_spec, sizeof(buff_spec), 0);

				ostime_t start = os_getTime();
				while ((!isSent) && (os_getTime() - start<TX_TIME))
				{
					os_runloop_once();
				}

				if (!isSent)
				{
					Serial.println(F("TX timeout"));
					Serial1.end();
					BLINK_INTERVAL = 100;
					os_clearCallback(&lora_report);
					Serial.println(F("Trying again in TX_INTERVAL seconds"));
					os_setTimedCallback(&lora_report, os_getTime() + sec2osticks(TX_INTERVAL), lora_send);
					comm_abort();
					return COMM_ERR_RETRY_LATER;
				}
				Serial.println(F("Packet queued"));
				Serial.println(F("Scheduling next transmission..."));
				os_setTimedCallback(&lora_report, os_getTime() + sec2osticks(TX_INTERVAL), lora_send); // Next transmission in TX_INTERVAl secondes
				comm_abort();
				return COMM_ERR_RETRY;
			}



			// Prepare upstream data transmission at the next possible time. 
			LMIC_setTxData2(1, buff, sizeof(buff), 0);
			Serial.println(F("Packet queued"));

			ostime_t start = os_getTime();
			while ((!isSent) && (os_getTime() - start<TX_TIME))
			{
				os_runloop_once();
			}

			if (!isSent)
			{
				Serial.println(F("TX timeout"));
				Serial1.end();
				BLINK_INTERVAL = 100;
				os_clearCallback(&lora_report);
				Serial.println(F("Trying again in TX_INTERVAL seconds"));
				os_setTimedCallback(&lora_report, os_getTime() + sec2osticks(TX_INTERVAL), lora_send);
				comm_abort();
				return COMM_ERR_RETRY_LATER;
			}
			Serial1.end();
			Serial.println(F("Scheduling next transmission..."));
			os_setTimedCallback(&lora_report, os_getTime() + sec2osticks(TX_INTERVAL), lora_send); // Next transmission in TX_INTERVAL secondes
			comm_abort();
			return COMM_OK;
		}
	}
	Serial.println(F("Not connected to gateway"));
	Serial.println(F("Trying to join in RETRY_JOIN seconds"));
	BLINK_INTERVAL = 100;
	os_clearCallback(&lora_report);
	LMIC_shutdown();
	os_setTimedCallback(&lora_setup, os_getTime() + sec2osticks(RETRY_JOIN), lora_init);
	comm_abort();
	return COMM_ERR_RETRY_LATER;
}

enum comm_status_code comm_abort(void)
{
	Serial.println(F("Going into sleeping mode..."));
	os_radio(RADIO_RST); // put radio to sleep
	delay(2000);
	os_clearCallback(&blinkjob);
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
