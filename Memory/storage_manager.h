#ifndef _STORAGE_MANAGER_h
#define _STORAGE_MANAGER_h

#include "arduino.h"


/*
* --- GESTION DE LA MEMOIRE ---
*
* --- Structure de la mémoire ---
* Concatenation de blocks de 18 bytes avec chaque block un échantillon de données.
*
* --- Fonctions accessibles aux autres parties du système ---
* 1) void stor_write(byte *sample)
*
* 2) int stor_read(byte *buffer, int maxlen)
*
* 3) void stor_confirm_read(bool do_commit)
*
* --- Variables internes pour gestion ---
* 1) addresse_actuel: l'addresse où le prochain échantillon va être stocké
* Cette adresse est incrémentée après chaque appel de fonction 'store_echantillon'
* Cette adresse est remis à son valeur initielle quand toute la mémoire
* utilisée est reportée
*
* 2) addresse_lu: l'addresse où la prochaine lecture de données va se effectuer
* Cette adresse est incrémentée après chaque appel de fonction 'get_data'
* Cette adresse est remis à son valeur initielle quand toute la mémoire
* utilisée est lue
*
*/

/*
	Set up memory interface
*/
void stor_setup(void);

uint8_t stor_start(void);

void stor_abort(void);

void stor_end(void);

uint8_t stor_test(void);

/*
	Ce fonction va stocker les données avec longueur 'len' dans la mémoire à partir de la
	première addresse qui est libre.
*/
uint8_t stor_write(uint8_t * data, uint16_t len);

/*
	Ce fonction va lire et retourner les données qui sont stockés
	dans la mémoire à partir de l'adresse 'addresse_lu'.
*/
uint16_t stor_read(uint8_t *buffer, uint16_t maxlen);

/*
Query available lenght of data store
*/
uint16_t stor_available(void);

#endif

