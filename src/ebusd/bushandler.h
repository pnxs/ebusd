/*
 * ebusd - daemon for communication with eBUS heating systems.
 * Copyright (C) 2014-2016 John Baier <ebusd@ebusd.eu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BUSHANDLER_H_
#define BUSHANDLER_H_

#include "message.h"
#include "data.h"
#include "symbol.h"
#include "result.h"
#include "device.h"
#include "queue.h"
#include "thread.h"
#include <string>
#include <vector>
#include <map>
#include <pthread.h>

/** @file bushandler.h
 * Classes, functions, and constants related to handling of symbols on the eBUS.
 *
 * The following table shows the possible states, symbols, and state transition
 * depending on the kind of message to send/receive:
 * @image html states.png "ebusd BusHandler states"
 */


/** the default time [us] for retrieving a symbol from an addressed slave. */
#define SLAVE_RECV_TIMEOUT 15000

/** the maximum allowed time [us] for retrieving the AUTO-SYN symbol (45ms + 2*1,2% + 1 Symbol). */
#define SYN_TIMEOUT 50800

/** the time [us] for determining bus signal availability (AUTO-SYN timeout * 5). */
#define SIGNAL_TIMEOUT 250000

/** the maximum duration [us] of a single symbol (Start+8Bit+Stop+Extra @ 2400Bd-2*1,2%). */
#define SYMBOL_DURATION 4700

/** the maximum allowed time [us] for retrieving back a sent symbol (2x symbol duration). */
#define SEND_TIMEOUT (2*SYMBOL_DURATION)

/** the possible bus states. */
enum class BusState {
	noSignal,	//!< no signal on the bus
	skip,        //!< skip all symbols until next @a SYN
	ready,       //!< ready for next master (after @a SYN symbol, send/receive QQ)
	recvCmd,     //!< receive command (ZZ, PBSB, master data) [passive set]
	recvCmdAck,  //!< receive command ACK/NACK [passive set + active set+get]
	recvRes,     //!< receive response (slave data) [passive set + active get]
	recvResAck,  //!< receive response ACK/NACK [passive set]
	sendCmd,     //!< send command (ZZ, PBSB, master data) [active set+get]
	sendResAck,  //!< send response ACK/NACK [active get]
	sendCmdAck,  //!< send command ACK/NACK [passive get]
	sendRes,     //!< send response (slave data) [passive get]
	sendSyn,     //!< send SYN for completed transfer [active set+get]
};

/** the possible grab request kinds. */
enum class GrabRequest {
	none,	//!< no grabbing at all
	unknown, //!< grab unknown messages only
	all,     //!< grab all messages
};

/** bit for the seen state: seen. */
#define SEEN 0x01

/** bit for the seen state: scan initiated. */
#define SCAN_INIT 0x02

/** bit for the seen state: scan finished. */
#define SCAN_DONE 0x04

/** bit for the seen state: configuration loading initiated. */
#define LOAD_INIT 0x08

/** bit for the seen state: configuration loaded. */
#define LOAD_DONE 0x10

class BusHandler;

/**
 * Generic request for sending to and receiving from the bus.
 */
class BusRequest
{
	friend class BusHandler;
public:

	/**
	 * Constructor.
	 * @param master the escaped master data @a SymbolString to send.
	 * @param deleteOnFinish whether to automatically delete this @a BusRequest when finished.
	 */
	BusRequest(SymbolString& master, const bool deleteOnFinish)
		: m_master(master), m_busLostRetries(0),
		  m_deleteOnFinish(deleteOnFinish) {}

	/**
	 * Destructor.
	 */
	virtual ~BusRequest() {}

	/**
	 * Notify the request of the specified result.
	 * @param result the result of the request.
	 * @param slave the slave data @a SymbolString received.
	 * @return true if the request needs to be restarted.
	 */
	virtual bool notify(result_t result, SymbolString& slave) = 0;

protected:

	/** the escaped master data @a SymbolString to send. */
	SymbolString& m_master;

	/** the number of times a send is repeated due to lost arbitration. */
	unsigned int m_busLostRetries;

	/** whether to automatically delete this @a BusRequest when finished. */
	const bool m_deleteOnFinish;

};


/**
 * A poll @a BusRequest handled by @a BusHandler itself.
 */
class PollRequest : public BusRequest
{
	friend class BusHandler;
public:

	/**
	 * Constructor.
	 * @param message the associated @a Message.
	 */
	PollRequest(shared_ptr<Message> message)
		: BusRequest(m_master, true), m_message(message), m_index(0) {}

	/**
	 * Destructor.
	 */
	virtual ~PollRequest() {}

	/**
	 * Prepare the master data.
	 * @param masterAddress the master bus address to use.
	 * @return the result code.
	 */
	result_t prepare(unsigned char masterAddress);

	// @copydoc
	virtual bool notify(result_t result, SymbolString& slave);

private:

	/** the escaped master data @a SymbolString. */
	SymbolString m_master;

	/** the associated @a Message. */
	shared_ptr<Message> m_message;

	/** the current part index in @a m_message. */
	unsigned char m_index;

};


/**
 * A scan @a BusRequest handled by @a BusHandler itself.
 */
class ScanRequest : public BusRequest
{
	friend class BusHandler;
public:

	/**
	 * Constructor.
	 * @param messageMap the @a MessageMap instance.
	 * @param messages the @a Message instances to query starting with the primary one.
	 * @param slaves the slave addresses to scan.
	 * @param busHandler the @a BusHandler instance to notify of final scan result.
	 */
	ScanRequest(MessageMap* messageMap, deque<shared_ptr<Message>> messages, deque<unsigned char> slaves, BusHandler* busHandler)
		: BusRequest(m_master, true), m_messageMap(messageMap), m_index(0), m_allMessages(messages), m_messages(messages), m_slaves(slaves), m_busHandler(busHandler)
	{
		m_message = m_messages.front();
		m_messages.pop_front();
	}

	/**
	 * Destructor.
	 */
	virtual ~ScanRequest() {}

	/**
	 * Prepare the next master data.
	 * @param masterAddress the master bus address to use.
	 * @return the result code.
	 */
	result_t prepare(unsigned char masterAddress);

	// @copydoc
	virtual bool notify(result_t result, SymbolString& slave);

private:

	/** the @a MessageMap instance. */
	MessageMap* m_messageMap;

	/** the escaped master data @a SymbolString. */
	SymbolString m_master;

	/** the currently queried @a Message. */
	shared_ptr<Message> m_message;

	/** the current part index in @a m_message. */
	unsigned char m_index;

	/** all secondary @a Message instances. */
	const deque<shared_ptr<Message>> m_allMessages;

	/** the remaining secondary @a Message instances. */
	deque<shared_ptr<Message>> m_messages;

	/** the slave addresses to scan. */
	deque<unsigned char> m_slaves;

	/** the @a ostringstream for building the scan result of a single slave. */
	ostringstream m_scanResult;

	/** the @a BusHandler instance to notify of final scan result. */
	BusHandler* m_busHandler;

};


/**
 * An active @a BusRequest that can be waited for.
 */
class ActiveBusRequest : public BusRequest
{
	friend class BusHandler;
public:

	/**
	 * Constructor.
	 * @param master the escaped master data @a SymbolString to send.
	 * @param slave reference to @a SymbolString for filling in the received slave data.
	 */
	ActiveBusRequest(SymbolString& master, SymbolString& slave)
		: BusRequest(master, false), m_result(RESULT_ERR_NO_SIGNAL), m_slave(slave) {}

	/**
	 * Destructor.
	 */
	virtual ~ActiveBusRequest() {}

	// @copydoc
	virtual bool notify(result_t result, SymbolString& slave);

private:

	/** the result of handling the request. */
	result_t m_result;

	/** reference to @a SymbolString for filling in the received slave data. */
	SymbolString& m_slave;

};


/**
 * Handles input from and output to the bus with respect to the eBUS protocol.
 */
class BusHandler : public WaitThread
{
public:

	/**
	 * Construct a new instance.
	 * @param device the @a Device instance for accessing the bus.
	 * @param messages the @a MessageMap instance with all known @a Message instances.
	 * @param ownAddress the own master address.
	 * @param answer whether to answer queries for the own master/slave address.
	 * @param busLostRetries the number of times a send is repeated due to lost arbitration.
	 * @param failedSendRetries the number of times a failed send is repeated (other than lost arbitration).
	 * @param transferLatency the bus transfer latency in microseconds.
	 * @param busAcquireTimeout the maximum time in microseconds for bus acquisition.
	 * @param slaveRecvTimeout the maximum time in microseconds an addressed slave is expected to acknowledge.
	 * @param lockCount the number of AUTO-SYN symbols before sending is allowed after lost arbitration, or 0 for auto detection.
	 * @param generateSyn whether to enable AUTO-SYN symbol generation.
	 * @param pollInterval the interval in seconds in which poll messages are cycled, or 0 if disabled.
	 */
	BusHandler(Device* device, MessageMap* messages,
			const unsigned char ownAddress, const bool answer,
			const unsigned int busLostRetries, const unsigned int failedSendRetries,
			const unsigned int transferLatency, const unsigned int busAcquireTimeout, const unsigned int slaveRecvTimeout,
			const unsigned int lockCount, const bool generateSyn,
			const unsigned int pollInterval)
		: m_device(device), m_messages(messages),
		  m_ownMasterAddress(ownAddress), m_ownSlaveAddress((unsigned char)(ownAddress+5)), m_answer(answer),
		  m_busLostRetries(busLostRetries), m_failedSendRetries(failedSendRetries),
		  m_transferLatency(transferLatency), m_busAcquireTimeout(busAcquireTimeout), m_slaveRecvTimeout(slaveRecvTimeout),
		  m_autoLockCount(lockCount==0), m_lockCount(lockCount<=3 ? 3 : lockCount), m_remainLockCount(m_autoLockCount),
		  m_generateSynInterval(generateSyn ? SYN_TIMEOUT*getMasterNumber(ownAddress)+SYMBOL_DURATION : 0),
		  m_pollInterval(pollInterval), m_command(false), m_response(false)
    {
		memset(m_seenAddresses, 0, sizeof(m_seenAddresses));
	}

	/**
	 * Destructor.
	 */
	virtual ~BusHandler() {
		stop();
	}

	/**
	 * Clear stored values (e.g. scan results).
	 */
	void clear();

	/**
	 * Send a message on the bus and wait for the answer.
	 * @param master the escaped @a SymbolString with the master data to send.
	 * @param slave the @a SymbolString that will be filled with retrieved slave data.
	 * @return the result code.
	 */
	result_t sendAndWait(SymbolString& master, SymbolString& slave);

	/**
	 * Main thread entry.
	 */
	virtual void run();

	/**
	 * Initiate a scan of the slave addresses.
	 * @param full true for a full scan (all slaves), false for scanning only already seen slaves.
	 * @return the result code.
	 */
	result_t startScan(bool full=false);

	/**
	 * Set the scan result @a string for a scanned slave address.
	 * @param dstAddress the scanned slave address.
	 * @param str the scan result @a string to set, or empty if not a single part of the scan was successful.
	 */
	void setScanResult(unsigned char dstAddress, string str);

	/**
	 * Called from @a ScanRequest upon completion.
	 */
	void setScanFinished();

	/**
	 * Format the scan result to the @a ostringstream.
	 * @param output the @a ostringstream to format the scan result to.
	 */
	void formatScanResult(ostringstream& output);

	/**
	 * Format information about seen participants to the @a ostringstream.
	 * @param output the @a ostringstream to append the info to.
	 */
	void formatSeenInfo(ostringstream& output);

	/**
	 * Send a scan message on the bus and wait for the answer.
	 * @param dstAddress the destination slave address to send to.
	 * @param slave the @a SymbolString that will be filled with retrieved slave data.
	 * @return the result code.
	 */
	result_t scanAndWait(unsigned char dstAddress, SymbolString& slave);

	/**
	 * Start or stop grabbing unknown messages.
	 * @param enable true to enable grabbing, false to disable it.
	 * @param all true to grab all messages, false to grab unknown messages only (only relevant if @a enable is true).
	 * @return true when the grabbing was changed.
	 */
	bool enableGrab(bool enable=true, bool all=false);

	/**
	 * Format the grabbed unknown messages to the @a ostringstream.
	 * @param output the @a ostringstream to format the messages to.
	 */
	void formatGrabResult(ostringstream& output);

	/**
	 * Return true when a signal on the bus is available.
	 * @return true when a signal on the bus is available.
	 */
	bool hasSignal() { return m_state != BusState::noSignal; }

	/**
	 * Return the current symbol rate.
	 * @return the number of received symbols in the last second.
	 */
	unsigned int getSymbolRate() { return m_symPerSec; }

	/**
	 * Return the maximum seen symbol rate.
	 * @return the maximum number of received symbols per second ever seen.
	 */
	unsigned int getMaxSymbolRate() { return m_maxSymPerSec; }

	/**
	 * Return the number of masters already seen.
	 * @return the number of masters already seen (including ebusd itself).
	 */
	unsigned int getMasterCount() { return m_masterCount; }

	/**
	 * Get the next slave address that still needs to be scanned or loaded.
	 * @param lastAddress the last returned slave address, or 0 for returning the first one.
	 * @param scanned set to true when the slave is already scanned but not yet loaded, set to false when it still needs to be scanned and loaded.
	 * @return the next slave address that still needs to be scanned or loaded, or @a SYN.
	 */
	unsigned char getNextScanAddress(unsigned char lastAddress, bool& scanned);

	/**
	 * Set the state of the participant to configuration @a LOADED.
	 * @param address the slave address.
	 * @param file the file from which the configuration was loaded, or empty if loading was not possible.
	 */
	void setScanConfigLoaded(unsigned char address, string file);

private:

	/**
	 * Handle the next symbol on the bus.
	 * @return RESULT_OK on success, or an error code.
	 */
	result_t handleSymbol();

	/**
	 * Set a new @a BusState and add a log message if necessary.
	 * @param state the new @a BusState.
	 * @param result the result code.
	 * @param firstRepetition true if the first repetition of a message part is being started.
	 * @return the result code.
	 */
	result_t setState(BusState state, result_t result, bool firstRepetition=false);

	/**
	 * Add a seen bus address.
	 * @param address the seen bus address.
	 */
	void addSeenAddress(unsigned char address);

	/**
	 * Called when a passive reception was successfully completed.
	 */
	void receiveCompleted();

	/** the @a Device instance for accessing the bus. */
	Device* m_device;

	/** the @a MessageMap instance with all known @a Message instances. */
	MessageMap* m_messages;

	/** the own master address. */
	const unsigned char m_ownMasterAddress;

	/** the own slave address. */
	const unsigned char m_ownSlaveAddress;

	/** whether to answer queries for the own master/slave address. */
	const bool m_answer;

	/** the number of times a send is repeated due to lost arbitration. */
	const unsigned int m_busLostRetries;

	/** the number of times a failed send is repeated (other than lost arbitration). */
	const unsigned int m_failedSendRetries;

	/** the bus transfer latency in microseconds. */
	const unsigned int m_transferLatency;

	/** the maximum time in microseconds for bus acquisition. */
	const unsigned int m_busAcquireTimeout;

	/** the maximum time in microseconds an addressed slave is expected to acknowledge. */
	const unsigned int m_slaveRecvTimeout;

	/** the number of masters already seen. */
	unsigned int m_masterCount = 1;

	/** whether m_lockCount shall be detected automatically. */
	const bool m_autoLockCount;

	/** the number of AUTO-SYN symbols before sending is allowed after lost arbitration. */
	unsigned int m_lockCount;

	/** the remaining number of AUTO-SYN symbols before sending is allowed again. */
	unsigned int m_remainLockCount;

	/** the interval in microseconds after which to generate an AUTO-SYN symbol, or 0 if disabled. */
	long m_generateSynInterval;

	/** the interval in seconds in which poll messages are cycled, or 0 if disabled. */
	const unsigned int m_pollInterval;

	/** the time of the last received symbol, or 0 for never. */
	time_t m_lastReceive = 0;

	/** the time of the last poll, or 0 for never. */
	time_t m_lastPoll = 0;

	/** the queue of @a BusRequests that shall be handled. */
	Queue<shared_ptr<BusRequest>> m_nextRequests;

	/** the currently handled BusRequest, or NULL. */
	shared_ptr<BusRequest> m_currentRequest;

	/** the queue of @a BusRequests that are already finished. */
	Queue<shared_ptr<BusRequest>> m_finishedRequests;

	/** the number of scan request currently running. */
	unsigned int m_runningScans = 0;

	/** the offset of the next symbol that needs to be sent from the command or response,
	 * (only relevant if m_request is set and state is @a bs_command or @a bs_response). */
	unsigned char m_nextSendPos = 0;

	/** the number of received symbols in the last second. */
	unsigned int m_symPerSec = 0;

	/** the maximum number of received symbols per second ever seen. */
	unsigned int m_maxSymPerSec = 0;

	/** the current @a BusState. */
	BusState m_state = BusState::noSignal;

	/** whether the current message part is being repeated. */
	bool m_repeat = false;

	/** the unescaped received command. */
	SymbolString m_command;

	/** whether the command CRC is valid. */
	bool m_commandCrcValid = false;

	/** the unescaped received response or escaped response to send. */
	SymbolString m_response;

	/** whether the response CRC is valid. */
	bool m_responseCrcValid = false;

	/** the participating bus addresses seen so far (0 if not seen yet, or combination of @a SEEN bits). */
	unsigned char m_seenAddresses[256];

	/** the scan results by slave address. */
	map<unsigned char, string> m_scanResults;

	/** whether to grab unknown messages. */
	GrabRequest m_grabUnknownMessages = GrabRequest::all;

	/** the grabbed unknown messages by ID prefix (QQZZPBSBNNDD with up to 4 DD bytes).*/
	map<string, string> m_grabbedUnknownMessages;

};


#endif // BUSHANDLER_H_
