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
	while (stor_start() && tries < STOR_FUN_MAX_RETRIES) tries++;
	if (tries == STOR_FUN_MAX_RETRIES) {
		db("Failed to start memory");
		stor_abort();
		return;
	}

	// Query available data
	db("querrying data");
	uint16_t available = stor_available();
	if (available == 0) { // If not enough samples available, abort
		db("not enough samples, abort");
		stor_abort();
		return;
	}
	// If too many samples, trim and signal that the task has to be re-run later
	bool samples_remaining = false;
	if (available > MAX_BYTES_PER_REPORT) {
		db("too many samples, trim");
		samples_remaining = true;
		available = MAX_BYTES_PER_REPORT - MAX_BYTES_PER_REPORT%SAMPLE_SIZE; // Fit a full number of samples
	}
	db("got amount of data");

	// Start Comm session
	comm_status_code code;
	tries = 0;
	while (tries < START_COMM_MAX_RETRIES) {
		db("attempting to start report");
		code = comm_start_report(available);

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

		available -= fetchlen;

	//	Fill in Comm report
		comm_fill_report(buffer, fetchlen);
	}

	// Dispatch report
	db("sending report");
	tries = 0;
	while (tries < START_COMM_MAX_RETRIES) {
		code = comm_send_report();
		// If connection error: Reschedule
		if (code == COMM_ERR_RETRY_LATER) {
			db("connection error");
			reschedule();
			//comm_abort(); RETRY_LATER shuts down the module already
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
		stor_abort();
		return;
	}
	// Else : success
	
	// Commit read head
	db("confirming read data");
	stor_end();
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
