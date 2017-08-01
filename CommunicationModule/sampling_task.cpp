// 
// 
// 

#define DB_MODULE "Sampling Task"
#include "debug.h"

#include "sampling_task.h"
#include "storage_manager.h"

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
const byte msg_header[] = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0xc5,0x6a,0x29};


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

void sampling_setup(void) {
	db("Setup");

}

static uint16_t total_samples = 0;

inline void get_dummy_data(uint8_t *buffer) {
	db("Generating dummy data");
	for (uint8_t i = 0; i < SAMPLE_SIZE; i++) {
		buffer[i] = '0';
	}
	uint8_t i;
	uint16_t val = total_samples;
	total_samples++;
	// Common exception
	if (val == 0) {
		return;
	}
	i = SAMPLE_SIZE;
	while (val) {
		buffer[--i] = 48 + val % 10;  // Fill in each digit (--i happens first, so it still points to the digit when done)
		val /= 10;
	}
}


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


inline uint8_t get_special_data_from_box(uint8_t *buffer) {
	uint8_t recv[2];
	uint8_t tries = 0;
	// RSOC
	while(send_command(msg_commands[0], recv) && tries < MAX_TRIES) tries++;

	uint8_t soc = recv[0];

	tries = 0;
	// BC
	while (send_command(msg_commands[9], recv) && tries < MAX_TRIES) tries++;

	int bc = (int)((recv[1] << 8) + recv[0]);

	db_module(); db_print(F("got soc: ")); db_print(soc); db_print(F(", bc: ")); db_println(bc);

	for (int i = 0; i < 18; i++)
		buffer[i] = '0';
	buffer[0] = '#';
	int val = total_samples;
	total_samples++;
	// fill in 1..5 with sample number
	uint8_t i = 6;
	while (val) {
		buffer[--i] = 48 + val % 10;
		val /= 10;
	}
	buffer[6] = ',';
	// fill in 7-8 with SoC level
	val = soc; // from OV box
	i = 9;
	while (val) {
		buffer[--i] = 48 + val % 10;
		val /= 10;
	}
	buffer[9] = 'P'; //  % sign
	buffer[10] = ',';
	// fill in 11-15 with battery current
	int curr = bc; // from OV box
	if (curr < 0) {
		buffer[11] = '-';
		curr = abs(curr);
	}
	else
		buffer[11] = '+';

	i = 16;
	while (curr) {
		buffer[--i] = 48 + curr % 10;
		curr /= 10;
	}
	buffer[16] = 'm';
	buffer[17] = 'A';

#ifdef _DEBUG
	Serial.write(buffer, 18);
	Serial.println();
#endif // _DEBUG


	return 0;
}


/* Sampling task
	Retrieves data from the box and stores it into the memory

	For a complete diagram, see https://docs.google.com/drawings/d/16fO7-UE9T9I5ijiOXyMK4tholkQQAXtwBO2k14w45mg/edit

*/

#define STOR_FUN_MAX_RETRIES 3
void sampling_task(void) {
	db("running Sampling task");

	// Start the EEPROM SPI
	uint8_t tries = 0;
	while (stor_start() && tries < STOR_FUN_MAX_RETRIES) tries++;
	if (tries == STOR_FUN_MAX_RETRIES) {
		db("Failed to start memory");
		stor_abort();
		return;
	}
	// (Re)Configure serial interface to the box
	Serial1.begin(38400);
	
	// Get sample data
	uint8_t buff[SAMPLE_SIZE];
	//uint8_t code = get_dummy_data(buff);
	uint8_t code = get_data_from_box(buff);
	//uint8_t code = get_special_data_from_box(buff);

	if (code != 0) { // Something wrong
		db("sampling aborted");
		stor_abort();
		Serial1.end();
		return;
	}

	// Store this sample to the external eeprom
	db("writting sample to storage");
	stor_write(buff, SAMPLE_SIZE);

	stor_end();
	Serial1.end();

	// Go back to sleep
	db("end");
	delay(100);
}



