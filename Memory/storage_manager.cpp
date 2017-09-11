#include "storage_manager.h"
#include <SPI.h>

#define DB_MODULE "Storage"
#include "debug.h"

// pins
#define DATAOUT MOSI//MOSI
#define DATAIN  MISO//MISO
#define SPICLOCK  SCK//sck
#define SLAVESELECT A5//ss

// opcodes
#define WREN  0x06
#define WRDI  0x04
#define RDSR  0x05
#define WRSR  0x01
#define READ  0x03
#define WRIT  0x02
#define CE    0xC7

// constants
#define WIP_MASK      0x01
#define PAGE_SIZE     128
#define MEMORY_SIZE   65536   // 64 kB

// variables
uint16_t adr_ecr = 0;
uint16_t adr_lir = 0;
uint16_t adr_lir_committed = 0;

// Prototypes
uint8_t wait_memory(uint16_t timeout);
uint8_t memory_is_busy(void);
uint8_t write_eeprom(uint8_t *data, uint16_t address, uint16_t len);
uint8_t write_eeprom_page(uint8_t *data, uint16_t address, uint16_t len);
uint8_t read_eeprom(uint8_t *buffer, uint16_t address, uint16_t len);

/*
	Set up memory interface, configure interfaces and pins
*/
void stor_setup(void) {
	db("Setup");
	pinMode(SLAVESELECT, OUTPUT);
	digitalWrite(SLAVESELECT, HIGH); //disable device
}

uint8_t stor_start(void) {
	db("Start");
	SPI.begin();
	return wait_memory(1000);
}

void stor_abort(void) {
	db("Abort");
	adr_lir = adr_lir_committed;
	wait_memory(1000);
	SPI.end();
}

void stor_end(void) {
	db("End");
	adr_lir_committed = adr_lir;
	wait_memory(1000);
	SPI.end();
}

void stor_erase_eeprom()
{
	db("Erase");
	digitalWrite(SLAVESELECT, LOW);
	SPI.transfer(WREN); //write enable
	digitalWrite(SLAVESELECT, HIGH);
	wait_memory(1000);

	digitalWrite(SLAVESELECT, LOW);
	SPI.transfer(CE); //write instruction
	digitalWrite(SLAVESELECT, HIGH);
	wait_memory(1000);
}

uint8_t stor_test(void) 
{
	stor_start();

	uint16_t adr = 0x000100;
	const uint16_t len = 10;
	uint8_t buf[len];
	uint8_t data[len];
	for (int i = 0; i < len; i++)
		data[i] = i;

	uint8_t result = 0;

	read_eeprom(buf, adr, len);
#ifdef _DEBUG
	for (int i = 0; i < len; i++) {
		Serial.print(" ");
		Serial.print(buf[i], HEX);
	}
	Serial.println("");
#endif
	stor_erase_eeprom();
	read_eeprom(buf, adr, len);
	for (int i = 0; i < len; i++) {
#ifdef _DEBUG
		Serial.print(" ");
		Serial.print(buf[i], HEX);
#endif
		if (buf[i] != 0xFF)
			result = -1;
	}
#ifdef _DEBUG
	Serial.print("\nStorage : Write 1st\n");
#endif
	write_eeprom(data, adr, len);
	read_eeprom(buf, adr, len);
	for (int i = 0; i < len; i++) {
#ifdef _DEBUG
		Serial.print(" ");
		Serial.print(buf[i], HEX);
#endif
		if (buf[i] != data[i])
			result = -1;
	}
#ifdef _DEBUG
	Serial.print("\nStorage : Write 2nd\n");
#endif
	// overwrite test
	for (int i = 0; i < len; i++)
		data[i] = 2*i;
	write_eeprom(data, adr, len);
	read_eeprom(buf, adr, len);
	for (int i = 0; i < len; i++) {
#ifdef _DEBUG
		Serial.print(" ");
		Serial.print(buf[i], HEX);
#endif
		if (buf[i] != data[i])
			result = -1;
	}
#ifdef _DEBUG
	Serial.print("\n");
#endif
	stor_end();
	return result;
}


/*
* Ce fonction va stocker un échantillon de données dans la mémoire à partir de la
* première addresse qui est libre.
*/
uint8_t stor_write(uint8_t *data, uint16_t len)
{
	db("Write sample");
	if (write_eeprom(data, adr_ecr, len)) {
		db("Write error");
		return -1;
	}
	adr_ecr += len;
	return 0;
}

/*
* Ce fonction va lire et retourner les données avec longueur 'len' qui sont stockés
* dans la mémoire à partir de l'adresse 'addresse_lu'.
* Return : len (longueur des donnes lus)
*/
uint16_t stor_read(uint8_t *buffer, uint16_t maxlen)
{
	db("Read sample");
	uint16_t len = min(stor_available(), maxlen);

	if (read_eeprom(buffer, adr_lir, len)) {
		db("Read error");
		return 0;
	}
	adr_lir += len;
	return len;
}


uint16_t stor_available(void)
{
	db("Query available");
	if (adr_ecr >= adr_lir)
	{
		return adr_ecr - adr_lir;
	}
	else
	{
		return adr_ecr + (MEMORY_SIZE - adr_lir);
	}
}


uint8_t memory_is_busy()
{
	digitalWrite(SLAVESELECT, LOW);
	SPI.transfer(RDSR);
	uint8_t status_reg = SPI.transfer(0xFF);
	digitalWrite(SLAVESELECT, HIGH);

	return ((status_reg & WIP_MASK) == 1);
}


uint8_t wait_memory(uint16_t timeout) {
	while (timeout && memory_is_busy()) {
		delay(1);
		timeout--;
	}
	return !timeout; // 0 on ok, 1 on timeout
}


/*
* This method will write the data in the buffer 'data' with length 'len' into the memory
* starting from address 'address'. Memory page overflows are handeled in this method.
*/
uint8_t write_eeprom(uint8_t *data, uint16_t address, uint16_t len)
{
	uint8_t code;
	while (len > 0)
	{
		if ((address % PAGE_SIZE) + len >= PAGE_SIZE)
		{
			uint16_t len_part = PAGE_SIZE - (address % PAGE_SIZE);
			code = write_eeprom_page(data, address, len_part);
			len -= len_part;
			address += len_part;
			data += len_part;
		}
		else
		{
			code = write_eeprom_page(data, address, len);
			len = 0;
		}

		if (code) return code;
	}
	return 0;
}

/*
* This method writes the data 'data' with length 'len' in the memory starting from the given
* address 'address'.
* If input satisfies following condition: (address % PAGE_SIZE) + len <= PAGE_SIGE
* Then the method will execute correctly.
*/
uint8_t write_eeprom_page(uint8_t *data, uint16_t address, uint16_t len)
{
	//fill eeprom w/ buffer
	digitalWrite(SLAVESELECT, LOW);
	SPI.transfer(WREN); //write enable
	digitalWrite(SLAVESELECT, HIGH);
	if (wait_memory(1000)) {
		return -1;
	}

	digitalWrite(SLAVESELECT, LOW);
	SPI.transfer(WRIT); //write instruction
	SPI.transfer((uint8_t)(address >> 8));   //send MSByte address first
	SPI.transfer((uint8_t)(address));      //send LSByte address
										//write len bytes
	for (uint16_t i = 0; i<len; i++)
	{
		SPI.transfer(data[i]); //write data byte
	}
	digitalWrite(SLAVESELECT, HIGH); //release chip
									 //wait for eeprom to finish writing
	if (wait_memory(1000)) {
		return -1;
	}
	return 0;
}

/*
* This method reads a given number of bytes 'len' from the memory starting from address
* 'address' and stores them in the given data buffer 'buf'.
*
*/
uint8_t read_eeprom(uint8_t *buffer, uint16_t address, uint16_t len)
{
	digitalWrite(SLAVESELECT, LOW);
	SPI.transfer(READ); //transmit read opcode
	SPI.transfer((uint8_t)(address >> 8));   //send MSByte address first
	SPI.transfer((uint8_t)(address));      //send LSByte address
	for (uint16_t i = 0; i<len; i++)
	{
		buffer[i] = SPI.transfer(0xFF); //get data byte
	}
	digitalWrite(SLAVESELECT, HIGH);

	return wait_memory(1000);
}