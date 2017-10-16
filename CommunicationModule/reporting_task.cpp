// 
// 
// 

#define DB_MODULE "Reporting Task"
#include "debug.h"

#include "task_scheduler.h"
#include "reporting_task.h"
#include "sampling_task.h"
#include "storage_manager.h"
#include "communication.h"

uint8_t connection_retries = 0;

void reporting_setup(void){
	db("Setup");

}

// Retry reporting_task later
void reschedule(void) {
	if (connection_retries < RETRY_CONNECTION_MAX_TRIES) {
		db("rescheduling");
		sched_add_task(reporting_task, RETRY_CONNECTION_TIME, 0);
		connection_retries++;
	}
}

char * getIdBox(void) {
	char buff[15] = "";
	uint8_t code = get_opid_from_box((uint8_t *) buff);
	return buff;
}

char * getPaygstate(void) {
	char buff[31] = "";
	uint8_t temp[15];
	uint8_t code = get_paygState_from_box(temp);

	for (int i=0; i < sizeof(temp); ++i) {
		char hex[3];
		int integer = (int) temp[i];
		sprintf(hex, "%2.2x", integer);
		strcat(buff, hex);
	}

	db_print("content of buff: "); db_println(buff);
	return buff;
}

/*
	Check available data,
	Start report,
	Fetch and fill in data,
	Send report

	For a complete explanation, see flow chart : https://docs.google.com/drawings/d/1VrfocBie4MKbHMRCDibGaBfLuZse3hcOaG2OG4jfAq4/edit
*/
#define STOR_FUN_MAX_RETRIES 3
void reporting_task(void){
	db("Start");
	// Check
	// Turn on memory
	uint8_t tries = 0;
	boolean memfailed = false;
	while (stor_start() && tries < STOR_FUN_MAX_RETRIES) tries++;
	if (tries == STOR_FUN_MAX_RETRIES) {
		db("Failed to start memory");
		stor_abort();
		memfailed = true;
	}

	// Query available data
	db("querrying data");
	uint16_t available = 0;
	if (memfailed)
		available = SAMPLE_SIZE;
	else
		available = stor_available();

	if (available == 0) { // If not enough samples available, abort
		db("not enough samples, abort");
		stor_abort();
		memfailed = true;
		available = SAMPLE_SIZE;
	}
	// If too many samples, trim and signal that the task has to be re-run later
	bool samples_remaining = false;
	if (available > MAX_BYTES_PER_REPORT) {
		db("too many samples, trim");
		samples_remaining = true;
		available = MAX_BYTES_PER_REPORT - (MAX_BYTES_PER_REPORT % SAMPLE_SIZE); // Fit a full number of samples
	}
	db("got amount of data");

	// Start Comm session
	comm_status_code code;
	tries = 0;
	while (tries < START_COMM_MAX_RETRIES) {
		db("attempting to start report");

		char url_add[100];
		strcpy(url_add, getIdBox());
		char contLen[30];
		sprintf(contLen, "&CL=%u", available);
		strcat(url_add, contLen);
		db_print("url add: ");
		db_println(url_add);
		char paygState[50];
		strcat(url_add, "&PGS=");
		strcat(url_add, getPaygstate());
		strcat(url_add, "\"");
		db_print("url add: ");
		db_println(url_add);

		code = comm_start_report(available, 2, url_add); // 2 == post data

		// If module error: Try a few more times and die
		if (code == COMM_ERR_RETRY) {
			db("module error");
			tries++;
			continue;
		}

		// If connection error: 
		if (code == COMM_ERR_RETRY_LATER) { // Reschedule task for later
			db("connection failed");
			reschedule();
			//comm_abort(); RETRY_LATER shuts down the module already
			if(!memfailed)
				stor_abort();
			return;
		}
		
		// Else: We did it !
		break;
	}
	if (tries == START_COMM_MAX_RETRIES) { // Failed to start report.
		db("reached max retries on start");
		reschedule();
		comm_abort();
		if(!memfailed)
			stor_abort();
		return;
	}
	// Else :
	db("module connected");

	// Fill in the samples
	uint8_t buffer[FETCH_BUFFER_MAX_SIZE];
	while (available != 0) {
	//	Fetch a reasonable amount of samples
		db("reading samples from memory");
		uint16_t fetchlen = FETCH_BUFFER_MAX_SIZE;
		if (fetchlen > available) {
			fetchlen = available;
		}

		if (memfailed) {
			for (int i = 0; i < SAMPLE_SIZE; i++)
				buffer[i] = 101;
		}
		else {
			uint16_t readlen;
			tries = 0;
			while ((readlen = stor_read(buffer, fetchlen)) && readlen != fetchlen && tries < STOR_FUN_MAX_RETRIES) tries++;
			if (tries == STOR_FUN_MAX_RETRIES) {
				db("Failed to read memory");
				reschedule();
				comm_abort();
				stor_abort();
				return;
			}
		}
		available -= fetchlen;

	//	Fill in Comm report
		comm_fill_report(buffer, fetchlen);
	}

	// Dispatch report
	db("sending report");
	tries = 0;
	uint8_t reply[301];
	for (int i = 0; i < 300; i++)
		reply[i] = '0';
	reply[300] = 0;
	while (tries < START_COMM_MAX_RETRIES) {
		code = comm_send_report(reply);
		// If connection error: Reschedule
		if (code == COMM_ERR_RETRY_LATER) {
			db("connection error");
			reschedule();
			//comm_abort(); RETRY_LATER shuts down the module already
			if(!memfailed)
				stor_abort();
			return;
		}
		// If other errors: Retry
		if (code == COMM_ERR_RETRY) {
			db("module error, retrying");
			tries++;
			continue;
		}
		// else: We did it!
		db("report sent");
		break;
	}
	if (tries == START_COMM_MAX_RETRIES) { // Failed to send report.
		db("reached max retries on send");
		comm_abort();
		if(!memfailed)
			stor_abort();
		return;
	}
	// Else : success
	
	// Commit read head
	if (memfailed) {
		db("Finished - memfailed");
	}
	else {
		db("confirming read data");
		stor_end();
	}
	// Communication closed on successful send_report :)

	// Samples left to send ? Time slot left to send ?
	if (samples_remaining && connection_retries < RETRY_CONNECTION_MAX_TRIES-1) {
		db("scheduling extra job");
		sched_add_task(reporting_task, RETRY_CONNECTION_TIME, 0);
	}

	// Reporting successful, reset retry counter
	connection_retries = 0; // We did it, it's over...
	
	return;
}

uint8_t reporting_test(uint8_t *buffer, int length) {
	db("Start");
	uint8_t tries = 0;
	comm_status_code code;

	db("attempting to start report");
	code = comm_start_report(length, 1, getIdBox()); // 1 = test

	// If module error: stop
	if (code == COMM_ERR_RETRY || code == COMM_ERR_RETRY_LATER) {
		comm_abort();
		return RETEST;
	}
	// Else :
	db("module connected");

	//	Fill in Comm report
	comm_fill_report(buffer, length);

	// Dispatch report
	db("sending report");
	tries = 0;
	uint8_t reply[50];
	for (int i = 0; i < 49; i++)
		reply[i] = '0';
	reply[49] = 0;
	while (tries < START_COMM_MAX_RETRIES) {
		code = comm_send_report(reply);
		if (code == COMM_ERR_RETRY || code == COMM_ERR_RETRY_LATER) {
			db("module error, retrying");
			tries++;
			continue;
		}
		// else: We did it!
		db("report sent");
		break;
	}
	if (tries == START_COMM_MAX_RETRIES) { // Failed to send report.
		db("reached max retries on send");
		comm_abort();
		return RETEST;
	}
	// Else : success
#ifdef _DEBUG
	Serial.print("Server reply: ");
	Serial.print((char *) reply);
	Serial.println();
#endif // DEBUG

	char error[] = { 'E', 'R', 'R', 'O', 'R' };
	char ok_ret[] = { 'O', 'K', ' ', ':', ' ', 'r', 'e', 't', 'e', 's', 't' };
	char ok_rep[] = { 'O', 'K', ' ', ':', ' ', 'r', 'e', 'p', 'o', 'r', 't' };

	if (strncmp(ok_rep, (char *)reply, sizeof ok_rep) == 0) {
		db("got start report reply");
		return COMMENCE_REPORTING;
	} 
	else if (strncmp(ok_ret, (char *)reply, sizeof ok_ret) == 0) {
		db("got ok retest reply");
		return RETEST;
	}
	else if (strncmp(error, (char *)reply, sizeof error) == 0) {
		db("got error reply");
		return RETEST;
	}
	else {
		db("got unknown replytype");
		return RETEST;
	}
}