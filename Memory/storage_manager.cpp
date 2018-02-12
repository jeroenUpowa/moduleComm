#include "storage_manager.h"
#include <SPI.h>
#include <EEPROM\src\EEPROM.h>

#define DB_MODULE "Storage"
#include "debug.h"
#include <stdio.h>
# include <string.h>

#define fx_expect_false(expr)  (expr)
#define fx_expect_true(expr)   (expr)
typedef unsigned char u8;

/* Ces parametres ne peuvent pas être modifies car ils sont propres au format compresse */
#define LZFX_MAX_LIT        (1 <<  5)  /* Nombre maximal d'octets recopiés littéralement */
#define LZFX_MAX_OFF        (1 << 13)  /* Distance maximale entre 2 motifs redondants */
#define LZFX_MAX_REF        ((1 << 8) + (1 << 3))  /* Taille maximale d'un motif redondant */

/* Erreurs predefinies */
#define LZFX_ESIZE      -1      /* Buffer de sortie trop petit */
#define LZFX_EARGS      -3      /* Arguments invalides (NULL) */

/* pins */
#define DATAOUT MOSI//MOSI
#define DATAIN  MISO//MISO
#define SPICLOCK  SCK//sck
#define SLAVESELECT A5//ss

/* opcodes */
#define WREN  0x06
#define WRDI  0x04
#define RDSR  0x05
#define WRSR  0x01
#define READ  0x03
#define WRIT  0x02
#define CE    0xC7

/* constants */
#define WIP_MASK      0x01
#define PAGE_SIZE     128
#define MEMORY_SIZE   60000   /* Dernier octet accordé à la partition de mémoire dédiée aux données brutes de la batterie */
#define MEMORY_COMPRESSED_MAX 65535   /* Dernier octet accordé à la partition de la mémoire dédiée aux données compressées */
#define Dernier_echantillon 240   /* 1 échantillon par minutes pendant 4 heures */

/* variables */
uint16_t adr_ecr = MEMORY_SIZE - 3*19 - 1;    /* adresse à laquelle sont écrites les données de la batterie */
uint16_t adr_lir = MEMORY_SIZE - 3*19 - 1;    /* adresse où sont lues les données de la batterie */
uint16_t adr_lir_committed = MEMORY_SIZE - 3*19 - 1;   /* adresse repère de lecture des données de la batterie */
uint32_t adr_lir_deb = MEMORY_SIZE - 3 * 19 - 1;   /* adresse du premier octet d'un paquet de 4h dans les donnees de la batterie */
uint32_t adr_lir_comp = MEMORY_COMPRESSED_MAX - 3 * 19 - 1;   /* adresse où sont lues les données compressées */
uint16_t adr_lir_committed_comp = MEMORY_COMPRESSED_MAX - 3 * 19 - 1;   /* adresse repère de lecture des données compressées */
uint32_t adr_ecr_comp = MEMORY_COMPRESSED_MAX - 3 * 19 - 1;   /* adresse à laquelle sont écrites les données compressées */
uint32_t resultat;   /* variable nulle lorsque la compression s'est bien passée ou indicatrice d'erreur */
uint16_t compteur_echantillon = 0;
int longueur_initiale_totale = 0;
int longueur_compressee_totale = 0;

/* Prototypes */
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
	//adr_lir = adr_lir_committed;
	wait_memory(1000);
	SPI.end();
}

void stor_abort_comp(void) {
	db("Abort comp");
	adr_lir_comp = adr_lir_committed_comp;
}

void stor_end(void) {
	db("End");
	adr_lir_committed = adr_lir;
	wait_memory(1000);
	SPI.end();
}

void stor_end_comp(void) {
	db("End comp");
	adr_lir_committed_comp = adr_lir_comp;
	compteur_echantillon = 0;
	longueur_initiale_totale = 0;
	longueur_compressee_totale = 0;
	adr_lir_deb = adr_lir;
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

	uint8_t result = 'O';

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
	if (adr_ecr + len > MEMORY_SIZE)
	{
		uint16_t ecrit_bas = MEMORY_SIZE - adr_ecr+1 ;
		if (write_eeprom(data, adr_ecr, ecrit_bas))
		{
			db("Probleme d'ecriture au bas de la memoire");
			return -1;
		}

		adr_ecr = 0;
		if (write_eeprom((data + ecrit_bas), adr_ecr, (len - ecrit_bas)))
		{
			db("Probleme d'ecriture en haut de la memoire");
			return -1;
		}
		adr_ecr += (len - ecrit_bas);
	}
	else
	{
		if (write_eeprom(data, adr_ecr, len)) 
		{
			db("Write error");
			return -1;
		}
		adr_ecr += len;
	}
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
	if (adr_lir + len > MEMORY_SIZE)
	{
		uint16_t lit_bas = MEMORY_SIZE + 1 - adr_lir;
		if (read_eeprom(buffer, adr_lir, lit_bas))
		{
			db("Erreur de lecture au bas de la memoire");
			return 0;
		}

		adr_lir = 0;
		if (read_eeprom((buffer + lit_bas), adr_lir, (len - lit_bas)))
		{
			db("Erreur de lecture en haut de la memoire");
			return 0;
		}
		adr_lir += (len - lit_bas);
	}
	else
	{
		if (read_eeprom(buffer, adr_lir, len )) 
		{      
			db("Read error");
			return 0;
		}
	
		adr_lir += len;
	}
	
	return len;
}

/*
* Cette fonction lit les donnees compressees de longueur 'len' qui sont stockées
* dans la mémoire à partir de l'adresse 'adr_lir_comp'.
* Return : len (longueur des donnes lues)
*/
uint16_t stor_read_comp(uint8_t *buffer, uint16_t maxlen)
{
	db("Lecture des donnees compressees");
	uint16_t len = min(stor_available_comp(), maxlen);
	
	if (adr_lir_comp+len > MEMORY_COMPRESSED_MAX)
	{
		uint16_t lit_bas = MEMORY_COMPRESSED_MAX + 1 - adr_lir_comp;
		if (read_eeprom(buffer, adr_lir_comp, lit_bas))
		{
			db("Erreur de lecture au bas de la memoire");
			return 0;
		}
		adr_lir_comp = MEMORY_SIZE + 1;
		if (read_eeprom((buffer + lit_bas), adr_lir_comp, (len - lit_bas)))
		{
			db("Erreur de lecture en haut de la memoire");
			return 0;
		}
		adr_lir_comp += (len - lit_bas);		
	}
	else
	{
		if (read_eeprom(buffer, adr_lir_comp, len)) 
		{
			db("Lire erreur");
			return 0;
		}
			adr_lir_comp += len;
	}
		
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
		return adr_ecr + (MEMORY_SIZE + 1 - adr_lir) ;
	}
}

uint16_t stor_available_comp(void)
{
	db("Donnees compressees disponibles");
	if (adr_ecr_comp >= adr_lir_comp)
	{
		return adr_ecr_comp - adr_lir_comp;
	}
	else
	{
		return ((adr_ecr_comp - MEMORY_SIZE - 1 ) + (MEMORY_COMPRESSED_MAX + 1 - adr_lir_comp));
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

uint8_t read_1_byte(uint16_t adresse) {
	uint8_t valeur[1];
	read_eeprom(valeur, adresse, 1);
	return valeur[0];
}

void write_1_byte (uint8_t donnee, uint16_t adresse){
	write_eeprom(&donnee, adresse, 1);
}



/* Fonctions de compression de toutes les données stockées en mémoire */

void recopiage(uint8_t* lit, uint16_t* adresse_entree, uint16_t* adresse_entree_debut, uint16_t* adresse_entree_fin, uint16_t* adresse_sortie, uint16_t* adresse_sortie_debut, uint16_t* adresse_sortie_fin, uint16_t* ilen, uint16_t* olen) {

	db("Ecriture des 19 premiers octets");
	uint16_t adresse_sortie_lit_1;
	*lit = 0; 
	*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;


	while (*adresse_entree >= *adresse_entree_debut ? (*adresse_entree < *adresse_entree_debut + *ilen) : (*adresse_entree < *adresse_entree_fin)) {

		
		*lit = *lit + 1;
		write_1_byte(read_1_byte(*adresse_entree > MEMORY_SIZE ? 0 : *adresse_entree), *adresse_sortie > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + 1 : *adresse_sortie);
		*adresse_entree == MEMORY_SIZE ? *adresse_entree = 0 : *adresse_entree = *adresse_entree + 1;
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;


		if (fx_expect_false(*lit == LZFX_MAX_LIT)) {
			if (*adresse_sortie - (MEMORY_SIZE + 1) < *lit + 1) {
				adresse_sortie_lit_1 = MEMORY_COMPRESSED_MAX + 1 - (*lit + 1 - (*adresse_sortie - (MEMORY_SIZE + 1)));
			}
			else {
				adresse_sortie_lit_1 = *adresse_sortie - *lit - 1;
			}
			write_1_byte(*lit - 1, adresse_sortie_lit_1); /* Terminate literal run */
			*lit = 0; 
			*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1; 
			
		}

	}

}

void Literal_run(uint16_t *adresse_entree, uint16_t *adresse_sortie, uint16_t *adresse_sortie_debut, uint16_t *adresse_sortie_fin, uint8_t* lit, uint16_t* olen) {

	db("Recopiage litteral");
	uint16_t adresse_sortie_lit_1;

	if (*lit == 0) {
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
	}
	*lit = *lit + 1;
	write_1_byte(read_1_byte(*adresse_entree > MEMORY_SIZE ? 0 : *adresse_entree), *adresse_sortie > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + 1 : *adresse_sortie);
	*adresse_entree = *adresse_entree + 1 > MEMORY_SIZE ?  0 : *adresse_entree + 1;
	*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
	
	if (fx_expect_false(*lit == LZFX_MAX_LIT)) {
		if (*adresse_sortie - (MEMORY_SIZE + 1) < *lit + 1) {
			adresse_sortie_lit_1 = MEMORY_COMPRESSED_MAX + 1 - (*lit + 1 - (*adresse_sortie - (MEMORY_SIZE + 1)));
			write_1_byte(*lit - 1, adresse_sortie_lit_1); /* Terminate literal run */
		}
		else {
			adresse_sortie_lit_1 = *adresse_sortie - *lit - 1;
			write_1_byte(*lit - 1, adresse_sortie_lit_1); /* Terminate literal run */
		}

		*lit = 0; 
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
		
	}

}

void Encodage_compression(unsigned int* len, int32_t* off, uint16_t* adresse_entree, uint16_t* adresse_sortie, uint8_t* lit) {

	db("Encodage de la compression");
	/* Format 1: [LLLooooo oooooooo] */
	if (*len < 7) {
		write_1_byte((*off >> 8) + (*len << 5), *adresse_sortie > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + 1 : *adresse_sortie);
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
		write_1_byte(*off, *adresse_sortie > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + 1 : *adresse_sortie);
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
	}

	/* Format 2: [111ooooo LLLLLLLL oooooooo] */
	else {
		write_1_byte((*off >> 8) + (7 << 5), *adresse_sortie > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + 1 : *adresse_sortie);
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
		write_1_byte(*len - 7, *adresse_sortie > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + 1 : *adresse_sortie);
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
		write_1_byte(*off, *adresse_sortie > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + 1 : *adresse_sortie);
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
	}

	*lit = 0;
	*adresse_entree = *adresse_entree + *len + 2;  /* ip = initial ip + #octets */
	if (*adresse_entree > MEMORY_SIZE) {
		unsigned int haut = *adresse_entree - MEMORY_SIZE - 1;
		*adresse_entree = haut;
	}

}

void Fin_Literal_run(uint16_t* adresse_sortie, uint16_t* adresse_sortie_debut, uint16_t* adresse_sortie_fin, uint8_t* lit, uint16_t* olen) {
	
	db("Fin du recopiage litteral");
	if (*lit == 0) {
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
	}
	uint16_t adresse_sortie_lit_1;

	if ((*lit+1) > (*adresse_sortie - (MEMORY_SIZE + 1))) {
		adresse_sortie_lit_1 = MEMORY_COMPRESSED_MAX + 1 - (*lit + 1 - (*adresse_sortie - (MEMORY_SIZE + 1)));  
		*adresse_sortie -= !(*lit);               /* Undo run if length is zero */
	}
	else {
		adresse_sortie_lit_1 = *adresse_sortie - *lit - 1;
		*adresse_sortie -= !(*lit);               /* Undo run if length is zero */
		*adresse_sortie == MEMORY_SIZE ? *adresse_sortie = MEMORY_COMPRESSED_MAX : *adresse_sortie = *adresse_sortie;
	}
	
	write_1_byte(*lit - 1, adresse_sortie_lit_1); /* Terminate literal run */

}

void Recopiage_final(uint16_t* adresse_entree, uint16_t* adresse_entree_debut, uint16_t* adresse_entree_fin, uint16_t* ilen, uint16_t* adresse_sortie, uint16_t* adresse_sortie_debut, uint16_t* adresse_sortie_fin, uint16_t* olen, uint8_t* lit) {

	db("Recopiage des 3 derniers octets");

	uint16_t adresse_sortie_lit_1;

	while (*adresse_entree_debut < *adresse_entree ? (*ilen - *adresse_entree + *adresse_entree_debut > 0) : (*adresse_entree < *adresse_entree_fin)) {

		if (*lit == 0) {
			*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
		}
		*lit = *lit + 1;
		write_1_byte(read_1_byte(*adresse_entree > MEMORY_SIZE ? 0 : *adresse_entree), *adresse_sortie > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + 1 : *adresse_sortie);
		*adresse_entree == MEMORY_SIZE ? *adresse_entree = 0 : *adresse_entree = *adresse_entree + 1;
		*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;
		
		if (fx_expect_false(*lit == LZFX_MAX_LIT)) {
			if (*adresse_sortie - (MEMORY_SIZE + 1) < *lit + 1) {
				adresse_sortie_lit_1 = MEMORY_COMPRESSED_MAX + 1 - (*lit + 1 - (*adresse_sortie - (MEMORY_SIZE + 1)));
			}
			else {
				adresse_sortie_lit_1 = *adresse_sortie - *lit - 1;
			}
			write_1_byte(*lit - 1, adresse_sortie_lit_1); /* Terminate literal run */
			*lit = 0; 
			*adresse_sortie == MEMORY_COMPRESSED_MAX ? *adresse_sortie = MEMORY_SIZE + 1 : *adresse_sortie = *adresse_sortie + 1;

		}
	}
}

void Continuite(uint32_t* resultat, unsigned int* len, int32_t* off, uint16_t* adresse_entree, uint16_t* adresse_entree_1, uint16_t* adresse_entree_2, int32_t* ref, int32_t* ref_1, int32_t* ref_2, unsigned int* maxlen, uint16_t* adresse_sortie, uint16_t* adresse_sortie_fin, uint8_t* lit) {

	db("Continuite d'un motif de compression d'un echantillon sur l'autre");
	uint16_t sortie_fin = *adresse_sortie_fin;

	if (*resultat <= 57343) {
		*(uint32_t*)len = *resultat >> (uint32_t)13;
		*(uint32_t*)off = *resultat & (uint32_t)0x1FFF;
	}
	else {
		*(uint32_t*)len = (uint32_t)7 + ((*resultat & (uint32_t)0x00FF00) >> (uint32_t)8);
		*(uint32_t*)off = ((*resultat & (uint32_t)0x1F0000) >> (uint32_t)8) + (*resultat & (uint32_t)0x0000FF);
	}
	*adresse_sortie_fin = sortie_fin;
	*ref = (*adresse_entree - *off - 1 > 0 ? *adresse_entree - *off - 1 : MEMORY_SIZE - *off + *adresse_entree);
	
	/* Vérification des valeurs des 2 adresses suivantes */
	if (*ref + 2 > MEMORY_SIZE) {
		unsigned int haut = *ref + 2 - MEMORY_SIZE;
		*ref_2 = haut - 1;
		if (*ref + 1 > MEMORY_SIZE) {
			*ref_1 = 0;
		}
		else {
			*ref_1 = *ref + 1;
		}
	}
	else {
		*ref_2 = *ref + 2;
		*ref_1 = *ref + 1;
	}

	/* Deduction de MAXLEN selon le numero de l'echantillon */
	if (compteur_echantillon < Dernier_echantillon) {
		*maxlen = *len + 18;
	}
	else if (compteur_echantillon == Dernier_echantillon) {
		*maxlen = *len + 16;
	}
	
	/* Verification de la longueur du motif en commençant par comparer le 4e octet */
	while ((*len < *maxlen) && read_1_byte(*ref + *len > MEMORY_SIZE ? *len - (MEMORY_SIZE - *ref + 1) : *ref + *len) == read_1_byte(*adresse_entree + *len > MEMORY_SIZE ? *len - (MEMORY_SIZE - *adresse_entree + 1) : *adresse_entree + *len)) {
		(*len)++;
	}
	*len = *len - 2;
	
	Encodage_compression(len, off, adresse_entree, adresse_sortie, lit);

}

void compression() {
	db("Compression");
	compteur_echantillon++;
	if (compteur_echantillon > Dernier_echantillon) {
		compteur_echantillon = 1;
		longueur_initiale_totale = 0;
		longueur_compressee_totale = 0;
		adr_lir_deb = adr_lir;
	}
#ifdef _DEBUG
	db("Numero de l'echantillon");	
	Serial.println(compteur_echantillon);
	resultat = lzfx_compress(adr_lir, adr_ecr_comp);
	db("ok ? ");
	Serial.println(resultat);
#endif
}


uint32_t lzfx_compress(uint16_t adresse_entree, uint16_t adresse_sortie) {
	db("Bienvenue dans LZFX");

	/* Déclaration, définitions, initialisations des adresses de début et fin du "buffer" d'entrée */
	uint16_t adresse_entree_debut = adr_ecr - 19;
	uint16_t adresse_entree_1;
	uint16_t adresse_entree_2;
	uint16_t ilen = 19;
	uint16_t adresse_entree_fin = adr_ecr; // La fin des donnees correspond à l'octet suivant le dernier lu (le prochain à lire)

	
	/* Déclaration, définitions, initialisations des adresses de début et fin du "buffer" de sortie */
	uint16_t adresse_sortie_debut = adresse_sortie;
	uint16_t adresse_sortie_lit_1;
	uint16_t olen = ilen + 5;
	uint16_t adresse_sortie_fin; //La fin des donnees correspond à l'octet suivant le dernier (la prochaine adresse libre)

	if (adresse_sortie_debut + olen <= MEMORY_COMPRESSED_MAX)
	{
		adresse_sortie_fin = adresse_sortie + olen;
	}
	else
	{
		uint16_t bas = MEMORY_COMPRESSED_MAX - adresse_sortie_debut + 1;
		adresse_sortie_fin = MEMORY_SIZE + 1 + olen - bas;
	}

	/* Déclaration, définitions, initialisations de variables */
	uint8_t lit; int32_t off;
	int32_t ref_1, ref_2, ref;
	unsigned int len, maxlen;

	/* Erreurs de parametrage */
	if (olen == NULL) return LZFX_EARGS;
	if (adresse_entree == NULL) {
		if (ilen != 0) return LZFX_EARGS;
		olen = 0;
		return 0;
	}
	if (adresse_sortie == NULL) {
		if (olen != 0) return LZFX_EARGS;
		return LZFX_EARGS;
	}

	/* Recopiage du premier echantillon */
	if (compteur_echantillon == 1) {
		recopiage(&lit, &adresse_entree, &adresse_entree_debut, &adresse_entree_fin, &adresse_sortie, &adresse_sortie_debut, &adresse_sortie_fin, &ilen, &olen);
	}

	/* Compression des echantillons suivants*/
	else {

		lit = 0;

		/* Lecture des donnees d'entree par 3 octets */
		while (adresse_entree > adresse_entree_fin ? (ilen - (adresse_entree - adresse_entree_debut) > 2) : (adresse_entree + 2 < adresse_entree_fin)) {   /* The NEXT macro reads 2 bytes ahead */
			
			/* Continuité d'un motif redondant d'un echantillon sur l'autre */
			if (resultat >= 8193) {
				db("Continuite");
				Continuite(&resultat, &len, &off, &adresse_entree, &adresse_entree_1, &adresse_entree_2, &ref, &ref_1, &ref_2, &maxlen, &adresse_sortie, &adresse_sortie_fin, &lit);
				resultat = 0;
				adresse_entree_fin = adr_ecr;
			}
			
			/* Continuité du recopiage litteral */
			else if (resultat > 0 && resultat <= 32) {
				lit = resultat;
				resultat = 0;
			}
			
			/* Absence de motif redondant d'un echantillon sur l'autre */
			else {

				/* Verification des valeurs des 2 adresses suivantes */
				if (adresse_entree + 2 > MEMORY_SIZE) {
					unsigned int haut = adresse_entree + 2 - MEMORY_SIZE;
					adresse_entree_2 = haut - 1;
					if (adresse_entree + 1 > MEMORY_SIZE) {
						adresse_entree_1 = 0;
					}
					else {
						adresse_entree_1 = adresse_entree + 1;
					}
				}
				else {
					adresse_entree_2 = adresse_entree + 2;
					adresse_entree_1 = adresse_entree + 1;
				}

				ref = adresse_entree - 1;

				/* Verification des valeurs des 2 adresses suivantes */
				if (ref + 2 > MEMORY_SIZE) {
					unsigned int haut = ref + 2 - MEMORY_SIZE;
					ref_2 = haut - 1;
					if (ref + 1 > MEMORY_SIZE) {
						ref_1 = 0;
					}
					else {
						ref_1 = ref + 1;
					}
				}
				else {
					ref_2 = ref + 2;
					ref_1 = ref + 1;
				}

				/* Decrementation de ref à la recherche d'un motif redondant */
				while ((adresse_entree < adr_lir_deb ? (((ref >= 0) && (ref < adresse_entree)) || ((ref > adr_lir_deb + 1) && (ref <= MEMORY_SIZE))) : ((ref > adr_lir_deb + 1) && (ref < adresse_entree)))

					&& ((read_1_byte(adresse_entree) != read_1_byte(ref))
						|| (read_1_byte(adresse_entree_1) != read_1_byte(ref_1))
						|| (read_1_byte(adresse_entree_2) != read_1_byte(ref_2)))) {
					
					ref--;

					if (ref < 0) {
						ref = MEMORY_SIZE;
					}

					// Verification des valeurs des 2 adresses suivantes
					if (ref + 2 > MEMORY_SIZE) {
						unsigned int haut = ref + 2 - MEMORY_SIZE;
						ref_2 = haut - 1;
						if (ref + 1 > MEMORY_SIZE) {
							ref_1 = 0;
						}
						else {
							ref_1 = ref + 1;
						}
					}
					else {
						ref_2 = ref + 2;
						ref_1 = ref + 1;
					}

				}


				if (compteur_echantillon < Dernier_echantillon) {
					
					/* Redondance */
					if (((ref < adresse_entree) | (adresse_entree < adr_lir_deb))
						&& ((off = (adresse_entree - ref - 1) > 0 ? (adresse_entree - ref - 1) : (MEMORY_SIZE - ref + adresse_entree)) < LZFX_MAX_OFF)

						&& (ref > adr_lir_deb ? 1 : (adr_lir_deb > adresse_entree))
						&& (read_1_byte(ref) == read_1_byte(adresse_entree))
						&& (read_1_byte(ref_1) == read_1_byte(adresse_entree_1))
						&& (read_1_byte(ref_2) == read_1_byte(adresse_entree_2))) {

						db("REDONDANCE !");

						len = 3;   /* We already know 3 bytes match */

					    /* Définition de la taille maximale du motif redondant */
						if (adresse_entree < adresse_entree_fin) {
							maxlen = adresse_entree_fin - adresse_entree > LZFX_MAX_REF ?
								LZFX_MAX_REF : adresse_entree_fin - adresse_entree;
						}
						else {
							maxlen = (MEMORY_SIZE + 1  - adresse_entree + adresse_entree_fin) > LZFX_MAX_REF ? LZFX_MAX_REF : MEMORY_SIZE + 1 - adresse_entree + adresse_entree_fin ;
						}
						Fin_Literal_run(&adresse_sortie, &adresse_sortie_debut, &adresse_sortie_fin, &lit, &olen);
						

						/* Verification de la longueur du motif en commençant par comparer le 4e octet */
						while ((len < maxlen) && read_1_byte(ref + len > MEMORY_SIZE ? len - (MEMORY_SIZE - ref + 1) : ref + len) == read_1_byte(adresse_entree + len > MEMORY_SIZE ? len - (MEMORY_SIZE - adresse_entree + 1) : adresse_entree + len)) {
							len++;
						}

						len -= 2;  /* We encode the length as #octets - 2 */
						
						/* Motif redondant jusqu'au dernier octet de l'echantillon : continuite */
						if (len == maxlen - 2) {
							adr_lir = adresse_entree;
							adr_ecr_comp = adresse_sortie;
							if (adresse_sortie > adresse_sortie_debut)
							{
								olen = adresse_sortie - adresse_sortie_debut;
							}
							else
							{
								olen = MEMORY_COMPRESSED_MAX + 1 - adresse_sortie_debut + adresse_sortie - MEMORY_SIZE - 1;
							}
							longueur_compressee_totale = longueur_compressee_totale + olen;
							longueur_initiale_totale = longueur_initiale_totale + ilen;
							if (len < 7) {
								return (((uint32_t)off) + ((uint32_t)len << (uint32_t)13));
							}
							else {
								return ((uint32_t)7 << (uint32_t)21) + (((uint32_t)off & (uint32_t)0xFF00) << (uint32_t)8) + (((uint32_t)len - (uint32_t)7) << (uint32_t)8) + ((uint32_t)off & (uint32_t)0x00FF);
							}
						}

						Encodage_compression(&len, &off, &adresse_entree, &adresse_sortie, &lit);
						
					} /* Redondance */
					
					else { /* Absence de redondance : recopiage litteral */
						
						db("Absence de redondance : recopiage litteral");
						
						/* Cas du premier octet du 2nd echantillon : recopiage */
						if (compteur_echantillon == 2 && adresse_entree == adresse_entree_debut) {
							lit = 19;
						}
						Literal_run(&adresse_entree, &adresse_sortie, &adresse_sortie_debut, &adresse_sortie_fin, &lit, &olen);
					}

				} /* echantillons < Dernier_echantillon */

				else if (compteur_echantillon == Dernier_echantillon) {

					db("Dernier echantillon !");

					/* Redondance */
					if (((ref < adresse_entree) | (adresse_entree < adr_lir_deb))
						&& ((off = (adresse_entree - ref - 1) > 0 ? (adresse_entree - ref - 1) : (MEMORY_SIZE - ref + adresse_entree)) < LZFX_MAX_OFF)
						&& (adresse_entree + 4 < adresse_entree_fin ? 1 : (ilen - adresse_entree - adresse_entree_debut > 4))  /* Backref takes up to 3 bytes, so don't bother */
						&& (ref > adr_lir_deb ? 1 : (adr_lir_deb > adresse_entree))
						&& (read_1_byte(ref) == read_1_byte(adresse_entree))
						&& (read_1_byte(ref_1) == read_1_byte(adresse_entree_1))
						&& (read_1_byte(ref_2) == read_1_byte(adresse_entree_2))) {
						
						len = 3;
						
						/* Définition du maximum d'octets similaires à la suite */
						if (adresse_entree < adresse_entree_fin) {
							maxlen = adresse_entree_fin - adresse_entree - 2 > LZFX_MAX_REF ?
								LZFX_MAX_REF : adresse_entree_fin - adresse_entree - 2;
						}
						else {
							maxlen = (MEMORY_SIZE + 1 - adresse_entree + adresse_entree_fin - 2) > LZFX_MAX_REF ? LZFX_MAX_REF : MEMORY_SIZE + 1 - adresse_entree + adresse_entree_fin - 2;
						}
						Fin_Literal_run(&adresse_sortie, &adresse_sortie_debut, &adresse_sortie_fin, &lit, &olen);
						
						/* Verification de la longueur du motif en commençant par comparer le 4e octet */
						while ((len < maxlen) && read_1_byte(ref + len > MEMORY_SIZE ? len - (MEMORY_SIZE - ref + 1) : ref + len) == read_1_byte(adresse_entree + len > MEMORY_SIZE ? len - (MEMORY_SIZE - adresse_entree + 1) : adresse_entree + len)) {
							len++;
						}

						len -= 2;  /* We encode the length as #octets - 2 */
						Encodage_compression(&len, &off, &adresse_entree, &adresse_sortie, &lit);

						/* Detection des 3 trois derniers octets */
						if (adresse_entree > adresse_entree_debut ? fx_expect_false(ilen - (adresse_entree - adresse_entree_debut) <= 3) : fx_expect_false(adresse_entree + 3 >= adresse_entree_fin)) {
							break;
						}
						
					}/* Redondance */

					else { /* Absence de redondance : recopiage */

						db("Absence de redondance : recopiage");
						Literal_run(&adresse_entree, &adresse_sortie, &adresse_sortie_debut, &adresse_sortie_fin, &lit, &olen);
					}
					
				}/* Dernier echantillon */

			} /* Absence de motif redondant d'un echantillon sur l'autre */

		}/* Boucle de lecture par 3 octets */

		db("Derniers octets !");

		/* Cas des echantillons dont les derniers octets suivent un Literal Run */
		if (compteur_echantillon != Dernier_echantillon) {
			adr_lir = adresse_entree;
			adr_ecr_comp = adresse_sortie;
			if (adresse_sortie > adresse_sortie_debut)
			{
				olen = adresse_sortie - adresse_sortie_debut;
			}
			else
			{
				olen = MEMORY_COMPRESSED_MAX + 1 - adresse_sortie_debut + adresse_sortie - MEMORY_SIZE - 1;
			}
			longueur_compressee_totale = longueur_compressee_totale + olen;
			longueur_initiale_totale = longueur_initiale_totale + ilen;
			return lit;
		}

		/* Cas des derniers octets du dernier echantillon */
		else {
			Recopiage_final(&adresse_entree, &adresse_entree_debut, &adresse_entree_fin, &ilen, &adresse_sortie, &adresse_sortie_debut, &adresse_sortie_fin, &olen, &lit);
			Fin_Literal_run(&adresse_sortie, &adresse_sortie_debut, &adresse_sortie_fin, &lit, &olen);
		}


	} /* compteur_echantillon =! 1 */

	/* Calcul de la taille de l'echantillon compresse */
	if (adresse_sortie > adresse_sortie_debut)
	{
		olen = adresse_sortie - adresse_sortie_debut;  
	}
	else
	{
		olen = MEMORY_COMPRESSED_MAX + 1 - adresse_sortie_debut + adresse_sortie - MEMORY_SIZE - 1;
	}

	/* Calcul de l'adresse de fin des donnees compressees */
	if (adresse_sortie_debut + olen <= MEMORY_COMPRESSED_MAX)
	{
		adresse_sortie_fin = adresse_sortie_debut + olen;
	}
	else
	{
		uint16_t bas = MEMORY_COMPRESSED_MAX + 1 - adresse_sortie_debut;
		adresse_sortie_fin = MEMORY_SIZE + 1 + olen - bas;
	}

	/* Calcul des longueurs totales de donnees  */
	longueur_compressee_totale = longueur_compressee_totale + olen;
	longueur_initiale_totale = longueur_initiale_totale + ilen;

	/* Mise a jour des pointeurs */
	adr_lir = adresse_entree_fin > MEMORY_SIZE ? 0 : adresse_entree_fin;
	adr_ecr_comp = adresse_sortie  > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + 1 : adresse_sortie;
	
	//Affichages
#ifdef _DEBUG
	int k,m;
	if (compteur_echantillon == 1) {
		for (k = 0; k < olen ; k++) {
			Serial.println (read_1_byte (adr_lir_comp + k > MEMORY_COMPRESSED_MAX ? MEMORY_SIZE + adr_lir_comp + k - MEMORY_COMPRESSED_MAX : adr_lir_comp + k));
		}
	}
	else {
		if (longueur_compressee_totale <= MEMORY_COMPRESSED_MAX + 1 - adr_lir_comp) {
			for (k = 0; k < longueur_compressee_totale; k++) {
				Serial.println(read_1_byte(adr_lir_comp + k));
			}
		}
		else {
			for (k = 0; k < MEMORY_COMPRESSED_MAX + 1 - adr_lir_comp ; k++) {
				Serial.println(read_1_byte(adr_lir_comp + k));
			}
			for (m = 0; m < longueur_compressee_totale - (MEMORY_COMPRESSED_MAX + 1 - adr_lir_comp); m++) {
				Serial.println(read_1_byte(MEMORY_SIZE + 1 + m));
			}
		}

	}
	db("olen : ");
	Serial.println(olen);
	db("ilen : ");
	Serial.println(ilen);
	db("adr_lir : ");
	Serial.println(adr_lir);
	db("adr_ecr_comp_vrai : ");
	Serial.println(adr_ecr_comp);
	db("Longueur initiale totale : ");
	Serial.println(longueur_initiale_totale);
	db("Longueur compressee totale : ");
	Serial.println(longueur_compressee_totale);
#endif
	return 0;
}


