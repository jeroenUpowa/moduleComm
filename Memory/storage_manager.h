#ifndef _STORAGE_MANAGER_h
#define _STORAGE_MANAGER_h

#include "arduino.h"


/*
* --- GESTION DE LA MEMOIRE ---
*
* --- Structure de la m�moire ---
* Concatenation de blocks de 18 bytes avec chaque block un �chantillon de donn�es.
*
* --- Fonctions accessibles aux autres parties du syst�me ---
* 1) void stor_write(byte *sample)
*
* 2) int stor_read(byte *buffer, int maxlen)
*
* 3) void stor_confirm_read(bool do_commit)
*
* --- Variables internes pour gestion ---
* 1) addresse_actuel: l'addresse o� le prochain �chantillon va �tre stock�
* Cette adresse est incr�ment�e apr�s chaque appel de fonction 'store_echantillon'
* Cette adresse est remis � son valeur initielle quand toute la m�moire
* utilis�e est report�e
*
* 2) addresse_lu: l'addresse o� la prochaine lecture de donn�es va se effectuer
* Cette adresse est incr�ment�e apr�s chaque appel de fonction 'get_data'
* Cette adresse est remis � son valeur initielle quand toute la m�moire
* utilis�e est lue
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
	Ce fonction va stocker les donn�es avec longueur 'len' dans la m�moire � partir de la
	premi�re addresse qui est libre.
*/
uint8_t stor_write(uint8_t * data, uint16_t len);

/*
	Ce fonction va lire et retourner les donn�es qui sont stock�s
	dans la m�moire � partir de l'adresse 'addresse_lu'.
*/
uint16_t stor_read(uint8_t *buffer, uint16_t maxlen);

/*
Query available lenght of data store
*/
uint16_t stor_available(void);

#endif

