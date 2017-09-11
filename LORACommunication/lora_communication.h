#ifndef _COMMUNICATION_H_
#define _COMMUNICATION_H_

#include "Arduino.h"

/*
	Communication library for the Feather FONA GSM board and the Feather LORA

*/

enum comm_status_code {
	COMM_OK,          // Function executed
	COMM_ERR_RETRY,       // Module unexpected error, retry a few times or abort
	COMM_ERR_RETRY_LATER    // Connection error, connection closed, data discarded, report aborted, module shutdown
};

/*
	Configure Serial and IO pins to operate the communication module. If the module is ON, turn it OFF
	Always returns COMM_OK
*/
enum comm_status_code comm_setup(void);

/*
	Turn on the module, connect to the network, start a session and prepare to send data
	Returns COMM_OK if the module is connected and a data session was open.
	Returns COMM_ERR_RETRY if the boot or some of the issued commands failed.
	Returns COMM_ERR_RETRY_LATER if the timeout of the network subscription was reached
*/
enum comm_status_code comm_start_report(uint16_t totallen);

/*
	Send binary data for the report contents
	Always returns COMM_OK, no overflow check is performed
*/
enum comm_status_code comm_fill_report(const uint8_t *buffer, int length);

/*
	Issue the report and await for results, then shut down the module
	Returns COMM_OK on a successfuly sent report. Returns COMM_ERR_RETRY on module error. Returns COMM_ERR_RETRY_LATER on timeouts and connection errors
*/
enum comm_status_code comm_send_report(void);

/*
	Stop any on-going opperation and shut down the module. Performs a hardware reset if the module is not responding (might take several seconds)
	Returns COMM_OK if the module was shut down, COMM_ERR_RETRY if the module didn't answer to the shutdown command even after reset
*/
enum comm_status_code comm_abort(void);



#ifdef __GSM_TEST__
// Expose internal functions for test suite
extern const char OK_REPLY[];
enum comm_status_code power_on(void);
enum comm_status_code power_off(void);
inline enum comm_status_code get_reply(const char * tosend, const char * expected_reply, uint16_t timeout);
enum comm_status_code get_reply(const uint8_t *tosend, const uint8_t *expected_reply, uint16_t timeout);
inline void flush_input(void);
#endif



#endif // !_COMMUNICATION_H_
