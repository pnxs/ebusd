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

#include "bushandler.h"
#include "message.h"
#include "data.h"
#include "result.h"
#include "symbol.h"
#include "log.h"
#include "config.h"
#include <unistd.h>
#include <string>
#include <vector>
#include <deque>
#include <cstring>
#include <time.h>
#include <iomanip>
#include <Address.h>

using std::dec;

// the string used for answering to a scan request (07h 04h)
#define SCAN_ANSWER ("ebusd.eu;" PACKAGE_NAME ";" SCAN_VERSION ";100")

/**
 * Return the string corresponding to the @a BusState.
 * @param state the @a BusState.
 * @return the string corresponding to the @a BusState.
 */
const char* getStateCode(BusState state) {
	switch (state)
	{
	case BusState::noSignal:   return "no signal";
	case BusState::skip:       return "skip";
	case BusState::ready:      return "ready";
	case BusState::sendCmd:    return "send command";
	case BusState::recvCmdAck: return "receive command ACK";
	case BusState::recvRes:    return "receive response";
	case BusState::sendResAck: return "send response ACK";
	case BusState::recvCmd:    return "receive command";
	case BusState::recvResAck: return "receive response ACK";
	case BusState::sendCmdAck: return "send command ACK";
	case BusState::sendRes:    return "send response";
	case BusState::sendSyn:    return "send SYN";
	default:            return "unknown";
	}
}

result_t PollRequest::prepare(const libebus::Address &ownMasterAddress)
{
	istringstream input;
	result_t result = m_message->prepareMaster(ownMasterAddress, m_master, input, m_index);
	if (result == RESULT_OK)
		logInfo(lf_bus, "poll cmd: %s", m_master.getDataStr().c_str());
	return result;
}

bool PollRequest::notify(result_t result, SymbolString& slave)
{
	if (result == RESULT_OK) {
		result = m_message->storeLastData(PartType::slaveData, slave, m_index);
		if (result>=RESULT_OK && m_index+1 < m_message->getCount()) {
			m_index++;
			return true;
		}
	}
	ostringstream output;
	if (result==RESULT_OK)
		result = m_message->decodeLastData(output); // decode data
	if (result < RESULT_OK)
		logError(lf_bus, "poll %s %s failed: %s", m_message->getCircuit().c_str(), m_message->getName().c_str(), getResultCode(result));
	else
		logNotice(lf_bus, "poll %s %s: %s", m_message->getCircuit().c_str(), m_message->getName().c_str(), output.str().c_str());

	return false;
}


result_t ScanRequest::prepare(const libebus::Address &ownMasterAddress)
{
	if (m_slaves.empty())
		return RESULT_ERR_EOF;
	unsigned char dstAddress = m_slaves.front();
	if (m_index==0 && m_messages.size()==m_allMessages.size()) // first message for this address
		m_busHandler->setScanResult(dstAddress, "");

	istringstream input;
	result_t result = m_message->prepareMaster(ownMasterAddress, m_master, input, UI_FIELD_SEPARATOR, dstAddress, m_index);
	if (result >= RESULT_OK)
		logInfo(lf_bus, "scan %2.2x cmd: %s", dstAddress, m_master.getDataStr().c_str());
	return result;
}

bool ScanRequest::notify(result_t result, SymbolString& slave)
{
	unsigned char dstAddress = m_master[1];
	if (result == RESULT_OK) {
		if (m_message==m_messageMap->getScanMessage()) {
			auto message = m_messageMap->getScanMessage(dstAddress);
			if (message!=NULL) {
				m_message = message;
				m_message->storeLastData(PartType::masterData, m_master, m_index); // expected to work since this is a clone
			}
		}
		result = m_message->storeLastData(PartType::slaveData, slave, m_index);
		if (result>=RESULT_OK && m_index+1 < m_message->getCount()) {
			m_index++;
			result = prepare(m_master[0]);
			if (result >= RESULT_OK)
				return true;
		}
		if (result==RESULT_OK)
			result = m_message->decodeLastData(m_scanResult, 0, true); // decode data
	}
	if (result < RESULT_OK) {
		if (!m_slaves.empty())
			m_slaves.pop_front();
		if (result == RESULT_ERR_TIMEOUT)
			logNotice(lf_bus, "scan %2.2x timed out (%d slaves left)", dstAddress, m_slaves.size());
		else
			logError(lf_bus, "scan %2.2x failed (%d slaves left): %s", dstAddress, m_slaves.size(), getResultCode(result));
		m_messages.clear(); // skip remaining secondary messages
	} else if (m_messages.empty()) {
		if (!m_slaves.empty())
			m_slaves.pop_front();
		logNotice(lf_bus, "scan %2.2x completed (%d slaves left)", dstAddress, m_slaves.size());
	}
	if (m_messages.empty()) // last message for this address
		m_busHandler->setScanResult(dstAddress, m_scanResult.str());

	if (m_slaves.empty()) {
		logNotice(lf_bus, "scan finished");
		m_busHandler->setScanFinished();
		return false;
	}
	if (m_messages.empty()) {
		m_messages = m_allMessages;
		m_scanResult.str("");
		m_scanResult.clear();
	}
	m_index = 0;
	m_message = m_messages.front();
	m_messages.pop_front();
	if (prepare(m_master[0]) < RESULT_OK) {
		m_busHandler->setScanFinished();
		return false; // give up
	}
	return true;
}


bool ActiveBusRequest::notify(result_t result, SymbolString& slave)
{
	if (result == RESULT_OK)
		logDebug(lf_bus, "read res: %s", slave.getDataStr().c_str());

	m_result = result;
	m_slave.addAll(slave);

	return false;
}


void BusHandler::clear()
{
	memset(m_seenAddresses, 0, sizeof(m_seenAddresses));
	m_masterCount = 1;
	m_scanResults.clear();
}

result_t BusHandler::sendAndWait(SymbolString& master, SymbolString& slave)
{
	result_t result = RESULT_ERR_NO_SIGNAL;
	slave.clear();
	auto request = make_shared<ActiveBusRequest>(master, slave);
	logInfo(lf_bus, "send message: %s", master.getDataStr().c_str());

	for (int sendRetries = m_failedSendRetries + 1; sendRetries >= 0; sendRetries--) {
		m_nextRequests.push(request);
		bool success = m_finishedRequests.remove(request, true);
		result = success ? request->m_result : RESULT_ERR_TIMEOUT;

		if (result == RESULT_OK) {
			auto message = m_messages->find(master);
			if (message != NULL)
				m_messages->invalidateCache(message);
			break;
		}
		if (!success || result == RESULT_ERR_NO_SIGNAL || result == RESULT_ERR_SEND || result == RESULT_ERR_DEVICE) {
			logError(lf_bus, "send to %2.2x: %s, give up", master[1], getResultCode(result));
			break;
		}
		logError(lf_bus, "send to %2.2x: %s%s", master[1], getResultCode(result), sendRetries>0 ? ", retry" : "");

		request->m_busLostRetries = 0;
	}

	return result;
}

void BusHandler::run()
{
	unsigned int symCount = 0;
	time_t lastTime;
	time(&lastTime);
	do {
		if (m_device->isValid()) {
			result_t result = handleSymbol();
			if (result != RESULT_ERR_TIMEOUT)
				symCount++;
			time_t now;
			time(&now);
			if (now > lastTime) {
				m_symPerSec = symCount / (unsigned int)(now-lastTime);
				if (m_symPerSec > m_maxSymPerSec) {
					m_maxSymPerSec = m_symPerSec;
					if (m_maxSymPerSec > 100)
						logNotice(lf_bus, "max. symbols per second: %d", m_maxSymPerSec);
				}
				lastTime = now;
				symCount = 0;
			}
		} else {
			if (!Wait(10))
				break;
			result_t result = m_device->open();

			if (result == RESULT_OK)
				logNotice(lf_bus, "re-opened %s", m_device->getName());
			else {
				logError(lf_bus, "unable to open %s: %s", m_device->getName(), getResultCode(result));
				setState(BusState::noSignal, result);
			}
			symCount = 0;
		}
	} while (isRunning());
}

result_t BusHandler::handleSymbol()
{
	long timeout = SYN_TIMEOUT;
	unsigned char sendSymbol = ESC;
	bool sending = false;
	shared_ptr<BusRequest> startRequest;

	// check if another symbol has to be sent and determine timeout for receive
	switch (m_state)
	{
	case BusState::noSignal:
		timeout = m_generateSynInterval>0 ? m_generateSynInterval : SIGNAL_TIMEOUT;
		break;

	case BusState::skip:
		timeout = SYN_TIMEOUT;
		break;

	case BusState::ready:
		if (m_currentRequest != NULL)
			setState(BusState::ready, RESULT_ERR_TIMEOUT); // just to be sure an old BusRequest is cleaned up
		if (m_remainLockCount == 0 && m_currentRequest == NULL) {
			startRequest = m_nextRequests.peek();
			if (startRequest == NULL && m_pollInterval > 0) { // check for poll/scan
				time_t now;
				time(&now);
				if (m_lastPoll == 0 || difftime(now, m_lastPoll) > m_pollInterval) {
					auto message = m_messages->getNextPoll();
					if (message != NULL) {
						m_lastPoll = now;
						auto request = make_shared<PollRequest>(message);
						result_t ret = request->prepare(m_ownMasterAddress);
						if (ret != RESULT_OK) {
							logError(lf_bus, "prepare poll message: %s", getResultCode(ret));
						}
						else {
							startRequest = request;
							m_nextRequests.push(request);
						}
					}
				}
			}
			if (startRequest != NULL) { // initiate arbitration
				sendSymbol = m_ownMasterAddress.binAddr();
				sending = true;
			}
		}
		break;

	case BusState::recvCmd:
	case BusState::recvCmdAck:
		timeout = m_slaveRecvTimeout;
		break;

	case BusState::recvRes:
		if (m_response.size() > 0 || m_slaveRecvTimeout > SYN_TIMEOUT)
			timeout = m_slaveRecvTimeout;
		else
			timeout = SYN_TIMEOUT;
		break;

	case BusState::recvResAck:
		timeout = m_slaveRecvTimeout+m_transferLatency;
		break;

	case BusState::sendCmd:
		if (m_currentRequest != NULL) {
			sendSymbol = m_currentRequest->m_master[m_nextSendPos]; // escaped command
			sending = true;
		}
		break;

	case BusState::sendResAck:
		if (m_currentRequest != NULL) {
			sendSymbol = m_responseCrcValid ? ACK : NAK;
			sending = true;
		}
		break;

	case BusState::sendCmdAck:
		if (m_answer) {
			sendSymbol = m_commandCrcValid ? ACK : NAK;
			sending = true;
		}
		break;

	case BusState::sendRes:
		if (m_answer) {
			sendSymbol = m_response[m_nextSendPos]; // escaped response
			sending = true;
		}
		break;

	case BusState::sendSyn:
		sendSymbol = SYN;
		sending = true;
		break;
	}

	// send symbol if necessary
	result_t result;
	if (sending) {
		result = m_device->send(sendSymbol);
		if (result == RESULT_OK) {
			if (m_state == BusState::ready)
				timeout = m_transferLatency+m_busAcquireTimeout;
			else
				timeout = m_transferLatency+SEND_TIMEOUT;
		} else {
			sending = false;
			timeout = SYN_TIMEOUT;
			if (startRequest != NULL && m_nextRequests.remove(startRequest)) {
				m_currentRequest = startRequest; // force the failed request to be notified
			}
			setState(BusState::skip, result);
		}
	}

	// receive next symbol (optionally check reception of sent symbol)
	unsigned char recvSymbol;
	result = m_device->recv(timeout+m_transferLatency, recvSymbol);

	if (!sending && result == RESULT_ERR_TIMEOUT && m_generateSynInterval > 0
	&& timeout >= m_generateSynInterval && (m_state == BusState::noSignal || m_state == BusState::skip)) {
		// check if acting as AUTO-SYN generator is required
		result = m_device->send(SYN);
		if (result == RESULT_OK) {
			recvSymbol = ESC;
			result = m_device->recv(SEND_TIMEOUT, recvSymbol);
			if (result == RESULT_ERR_TIMEOUT) {
				return setState(BusState::noSignal, result);
			}
			if (result != RESULT_OK)
				logError(lf_bus, "unable to receive sent AUTO-SYN symbol: %s", getResultCode(result));
			else if (recvSymbol != SYN) {
				logError(lf_bus, "received %2.2x instead of AUTO-SYN symbol", recvSymbol);
			} else {
				if (m_generateSynInterval != SYN_TIMEOUT) {
					// received own AUTO-SYN symbol back again: act as AUTO-SYN generator now
					m_generateSynInterval = SYN_TIMEOUT;
					logNotice(lf_bus, "acting as AUTO-SYN generator");
				}
				m_remainLockCount = 0;
				return setState(BusState::ready, result);
			}
		}
		return setState(BusState::skip, result);
	}
	time_t now;
	time(&now);
	if (result != RESULT_OK) {
		if (sending && startRequest != NULL && m_nextRequests.remove(startRequest)) {
			m_currentRequest = startRequest; // force the failed request to be notified
		}
		if ((m_generateSynInterval != SYN_TIMEOUT && difftime(now, m_lastReceive) > 1) // at least one full second has passed since last received symbol
			|| m_state == BusState::noSignal)
			return setState(BusState::noSignal, result);

		return setState(BusState::skip, result);
	}

	m_lastReceive = now;
	if ((recvSymbol == SYN) && (m_state != BusState::sendSyn)) {
		if (!sending && m_remainLockCount > 0 && m_command.size() != 1)
			m_remainLockCount--;
		else if (!sending && m_remainLockCount == 0 && m_command.size() == 1)
			m_remainLockCount = 1; // wait for next AUTO-SYN after SYN / address / SYN (bus locked for own priority)

		return setState(BusState::ready, m_state == BusState::skip ? RESULT_OK : RESULT_ERR_SYN);
	}

	unsigned int headerLen, crcPos;

	switch (m_state)
	{
	case BusState::noSignal:
		return setState(BusState::skip, RESULT_OK);

	case BusState::skip:
		return RESULT_OK;

	case BusState::ready:
		if (startRequest != NULL && sending) {
			if (!m_nextRequests.remove(startRequest)) {
				// request already removed (e.g. due to timeout)
				return setState(BusState::skip, RESULT_ERR_TIMEOUT);
			}
			m_currentRequest = startRequest;
			// check arbitration
			if (recvSymbol == sendSymbol) { // arbitration successful
				m_nextSendPos = 1;
				m_repeat = false;
				return setState(BusState::sendCmd, RESULT_OK);
			}
			// arbitration lost. if same priority class found, try again after next AUTO-SYN
			m_remainLockCount = libebus::Address(recvSymbol).isMaster() ? 2 : 1; // number of SYN to wait for before next send try
			if ((recvSymbol & 0x0f) != (sendSymbol & 0x0f) && m_lockCount > m_remainLockCount)
				// if different priority class found, try again after N AUTO-SYN symbols (at least next AUTO-SYN)
				m_remainLockCount = m_lockCount;
			setState(m_state, RESULT_ERR_BUS_LOST); // try again later
		}
		result = m_command.push_back(recvSymbol, false); // expect no escaping for master address
		if (result < RESULT_OK)
			return setState(BusState::skip, result);

		m_repeat = false;
		return setState(BusState::recvCmd, RESULT_OK);

	case BusState::recvCmd:
		headerLen = 4;
		crcPos = m_command.size() > headerLen ? headerLen + 1 + m_command[headerLen] : 0xff; // header symbols are never escaped
		result = m_command.push_back(recvSymbol, true, m_command.size() < crcPos);
		if (result < RESULT_OK)
			return setState(BusState::skip, result);

		if (result == RESULT_OK && crcPos != 0xff && m_command.size() == crcPos + 1) { // CRC received
			libebus::Address dstAddress = m_command[1];
			m_commandCrcValid = m_command[headerLen + 1 + m_command[headerLen]] == m_command.getCRC(); // header symbols are never escaped
			if (m_commandCrcValid) {
				if (dstAddress == BROADCAST) {
					receiveCompleted();
					return setState(BusState::skip, RESULT_OK);
				}
				addSeenAddress(m_command[0]);
				if (m_answer
				        && (dstAddress == m_ownMasterAddress || dstAddress == m_ownSlaveAddress))
					return setState(BusState::sendCmdAck, RESULT_OK);

				return setState(BusState::recvCmdAck, RESULT_OK);
			}
			if (dstAddress == BROADCAST)
				return setState(BusState::skip, RESULT_ERR_CRC);

			if (m_answer
			        && (dstAddress == m_ownMasterAddress || dstAddress == m_ownSlaveAddress)) {
				return setState(BusState::sendCmdAck, RESULT_ERR_CRC);
			}
			if (m_repeat)
				return setState(BusState::skip, RESULT_ERR_CRC);
			return setState(BusState::recvCmdAck, RESULT_ERR_CRC);
		}
		return RESULT_OK;

	case BusState::recvCmdAck:
		if (recvSymbol == ACK) {
			if (!m_commandCrcValid)
				return setState(BusState::skip, RESULT_ERR_ACK);

			if (m_currentRequest != NULL) {
				if (libebus::Address(m_currentRequest->m_master[1]).isMaster()) {
					return setState(BusState::sendSyn, RESULT_OK);
				}
			}
			else if (libebus::Address(m_command[1]).isMaster()) { // header symbols are never escaped
				receiveCompleted();
				return setState(BusState::skip, RESULT_OK);
			}

			m_repeat = false;
			return setState(BusState::recvRes, RESULT_OK);
		}
		if (recvSymbol == NAK) {
			if (!m_repeat) {
				m_repeat = true;
				m_nextSendPos = 0;
				m_command.clear();
				if (m_currentRequest != NULL)
					return setState(BusState::sendCmd, RESULT_ERR_NAK, true);

				return setState(BusState::recvCmd, RESULT_ERR_NAK);
			}

			return setState(BusState::skip, RESULT_ERR_NAK);
		}

		return setState(BusState::skip, RESULT_ERR_ACK);

	case BusState::recvRes:
		headerLen = 0;
		crcPos = m_response.size() > headerLen ? headerLen + 1 + m_response[headerLen] : 0xff;
		result = m_response.push_back(recvSymbol, true, m_response.size() < crcPos);
		if (result < RESULT_OK)
			return setState(BusState::skip, result);

		if (result == RESULT_OK && crcPos != 0xff && m_response.size() == crcPos + 1) { // CRC received
			m_responseCrcValid = m_response[headerLen + 1 + m_response[headerLen]] == m_response.getCRC();
			if (m_responseCrcValid) {
				if (m_currentRequest != NULL)
					return setState(BusState::sendResAck, RESULT_OK);

				return setState(BusState::recvResAck, RESULT_OK);
			}
			if (m_repeat) {
				if (m_currentRequest != NULL)
					return setState(BusState::sendSyn, RESULT_ERR_CRC);

				return setState(BusState::skip, RESULT_ERR_CRC);
			}
			if (m_currentRequest != NULL)
				return setState(BusState::sendResAck, RESULT_ERR_CRC);

			return setState(BusState::recvResAck, RESULT_ERR_CRC);
		}
		return RESULT_OK;

	case BusState::recvResAck:
		if (recvSymbol == ACK) {
			if (!m_responseCrcValid)
				return setState(BusState::skip, RESULT_ERR_ACK);

			receiveCompleted();
			return setState(BusState::skip, RESULT_OK);
		}
		if (recvSymbol == NAK) {
			if (!m_repeat) {
				m_repeat = true;
				m_response.clear();
				return setState(BusState::recvRes, RESULT_ERR_NAK, true);
			}
			return setState(BusState::skip, RESULT_ERR_NAK);
		}
		return setState(BusState::skip, RESULT_ERR_ACK);

	case BusState::sendCmd:
		if (m_currentRequest != NULL && sending && recvSymbol == sendSymbol) {
			// successfully sent
			m_nextSendPos++;
			if (m_nextSendPos >= m_currentRequest->m_master.size()) {
				// master data completely sent
				if (m_currentRequest->m_master[1] == BROADCAST)
					return setState(BusState::sendSyn, RESULT_OK);

				m_commandCrcValid = true;
				return setState(BusState::recvCmdAck, RESULT_OK);
			}
			return RESULT_OK;
		}
		return setState(BusState::skip, RESULT_ERR_INVALID_ARG);

	case BusState::sendResAck:
		if (m_currentRequest != NULL && sending && recvSymbol == sendSymbol) {
			// successfully sent
			if (!m_responseCrcValid) {
				if (!m_repeat) {
					m_repeat = true;
					m_response.clear();
					return setState(BusState::recvRes, RESULT_ERR_NAK, true);
				}
				return setState(BusState::sendSyn, RESULT_ERR_ACK);
			}
			return setState(BusState::sendSyn, RESULT_OK);
		}
		return setState(BusState::skip, RESULT_ERR_INVALID_ARG);

	case BusState::sendCmdAck:
		if (sending && m_answer && recvSymbol == sendSymbol) {
			// successfully sent
			if (!m_commandCrcValid) {
				if (!m_repeat) {
					m_repeat = true;
					m_command.clear();
					return setState(BusState::recvCmd, RESULT_ERR_NAK, true);
				}
				return setState(BusState::skip, RESULT_ERR_ACK);
			}
			if (libebus::Address(m_command[1]).isMaster()) {
				receiveCompleted(); // decode command and store value
				return setState(BusState::skip, RESULT_OK);
			}

			m_nextSendPos = 0;
			m_repeat = false;
			istringstream input; // TODO create input from database of internal variables
			auto message = m_messages->find(m_command);
			if (message == NULL) {
				message = m_messages->find(m_command, true);
				if (message!=NULL && message->getSrcAddress()!=SYN)
					message = NULL;
			}
			if (message == NULL || message->isWrite())
				return setState(BusState::skip, RESULT_ERR_INVALID_ARG); // don't know this request or definition has wrong direction, deny
			if (message == m_messages->getScanMessage()) {
				input.str(SCAN_ANSWER);
			}

			// build response and store in m_response for sending back to requesting master
			m_response.clear(true); // escape while sending response
			result = message->prepareSlave(input, m_response);
			if (result != RESULT_OK)
				return setState(BusState::skip, result);
			return setState(BusState::sendRes, RESULT_OK);
		}
		return setState(BusState::skip, RESULT_ERR_INVALID_ARG);

	case BusState::sendRes:
		if (sending && m_answer && recvSymbol == sendSymbol) {
			// successfully sent
			m_nextSendPos++;
			if (m_nextSendPos >= m_response.size()) {
				// slave data completely sent
				return setState(BusState::recvResAck, RESULT_OK);
			}
			return RESULT_OK;
		}
		return setState(BusState::skip, RESULT_ERR_INVALID_ARG);

	case BusState::sendSyn:
		if (sending && recvSymbol == sendSymbol) {
			// successfully sent
			return setState(BusState::skip, RESULT_OK);
		}
		return setState(BusState::skip, RESULT_ERR_INVALID_ARG);

	}

	return RESULT_OK;
}

result_t BusHandler::setState(BusState state, result_t result, bool firstRepetition)
{
	if (m_currentRequest != NULL) {
		if (result == RESULT_ERR_BUS_LOST && m_currentRequest->m_busLostRetries < m_busLostRetries) {
			logDebug(lf_bus, "%s during %s, retry", getResultCode(result), getStateCode(m_state));
			m_currentRequest->m_busLostRetries++;
			m_nextRequests.push(m_currentRequest); // repeat
			m_currentRequest = NULL;
		}
		else if (state == BusState::sendSyn || (result != RESULT_OK && !firstRepetition)) {
			logDebug(lf_bus, "notify request: %s", getResultCode(result));
			unsigned char dstAddress = m_currentRequest->m_master[1];
			if (result == RESULT_OK)
				addSeenAddress(dstAddress);

			bool restart = m_currentRequest->notify(
				result == RESULT_ERR_SYN && (m_state == BusState::recvCmdAck || m_state == BusState::recvRes) ? RESULT_ERR_TIMEOUT : result, m_response
			);
			if (restart) {
				m_currentRequest->m_busLostRetries = 0;
				m_nextRequests.push(m_currentRequest);
			}
			else if (m_currentRequest->m_deleteOnFinish) {
				m_currentRequest.reset();
			} else {
				m_finishedRequests.push(m_currentRequest);
			}
			m_currentRequest = NULL;
		}
	}

	if (state ==BusState:: noSignal) { // notify all requests
		m_response.clear(false); // notify with empty response
		while ((m_currentRequest = m_nextRequests.pop()) != NULL) {
			bool restart = m_currentRequest->notify(RESULT_ERR_NO_SIGNAL, m_response);
			if (restart) { // should not occur with no signal
				m_currentRequest->m_busLostRetries = 0;
				m_nextRequests.push(m_currentRequest);
			}
			else if (m_currentRequest->m_deleteOnFinish)
				m_currentRequest.reset();
			else
				m_finishedRequests.push(m_currentRequest);
		}
	}

	if (state == m_state)
		return result;

	if (result < RESULT_OK || (result != RESULT_OK && state == BusState::skip))
		logDebug(lf_bus, "%s during %s, switching to %s", getResultCode(result), getStateCode(m_state), getStateCode(state));
	else if (m_currentRequest != NULL || state == BusState::sendCmd || state == BusState::sendResAck || state == BusState::sendSyn)
		logDebug(lf_bus, "switching from %s to %s", getStateCode(m_state), getStateCode(state));

	if (state == BusState::noSignal)
		logError(lf_bus, "signal lost");
	else if (m_state == BusState::noSignal)
		logNotice(lf_bus, "signal acquired");

	m_state = state;

	if (state == BusState::ready || state == BusState::skip) {
		m_command.clear();
		m_commandCrcValid = false;
		m_response.clear(false); // unescape while receiving response
		m_responseCrcValid = false;
		m_nextSendPos = 0;
	}

	return result;
}

void BusHandler::addSeenAddress(libebus::Address address)
{
	if (not address.isValid(false))
		return;
	if (not address.isMaster()) {
		m_seenAddresses[address.binAddr()] |= SEEN;
		address = address.getMasterAddress().binAddr();
		if (address==SYN)
			return;
	}
	if ((m_seenAddresses[address.binAddr()]&SEEN)==0) {
		if (!m_answer || address != m_ownMasterAddress) {
			m_masterCount++;
			if (m_autoLockCount && m_masterCount>m_lockCount)
				m_lockCount = m_masterCount;
			logNotice(lf_bus, "new master %2.2x, master count %d", address, m_masterCount);
		}
		m_seenAddresses[address.binAddr()] |= SEEN;
	}
}

void BusHandler::receiveCompleted()
{
	libebus::Address srcAddress = m_command[0];
	libebus::Address dstAddress = m_command[1];

	if (srcAddress == dstAddress) {
		logError(lf_bus, "invalid self-addressed message from %2.2x", srcAddress);
		return;
	}

	addSeenAddress(srcAddress.binAddr());
	addSeenAddress(dstAddress.binAddr());

	bool master = dstAddress.isMaster();
	if (dstAddress == BROADCAST)
		logInfo(lf_update, "update BC cmd: %s", m_command.getDataStr().c_str());
	else if (master)
		logInfo(lf_update, "update MM cmd: %s", m_command.getDataStr().c_str());
	else
		logInfo(lf_update, "update MS cmd: %s / %s", m_command.getDataStr().c_str(), m_response.getDataStr().c_str());

	auto message = m_messages->find(m_command);
	if (m_grabUnknownMessages==GrabRequest::all || (message==NULL && m_grabUnknownMessages==GrabRequest::unknown)) {
		string data;
		string key = data = m_command.getDataStr();
		if (key.length() > 2*(1+1+2+1+4)) {
			key = key.substr(0, 2*(1+1+2+1+4)); // QQZZPBSBNN + up to 4 DD bytes
		}
		if (dstAddress != BROADCAST && !master) {
			data += " / " + m_response.getDataStr();
		}
		if (message) {
			data += " = "+message->getCircuit()+" "+message->getName();
		}
		m_grabbedUnknownMessages[key] = data;
	}
	if (message == NULL) {
		if (dstAddress == BROADCAST)
			logNotice(lf_update, "unknown BC cmd: %s", m_command.getDataStr().c_str());
		else if (master)
			logNotice(lf_update, "unknown MM cmd: %s", m_command.getDataStr().c_str());
		else
			logNotice(lf_update, "unknown MS cmd: %s / %s", m_command.getDataStr().c_str(), m_response.getDataStr().c_str());
	}
	else {
		m_messages->invalidateCache(message);
		string circuit = message->getCircuit();
		string name = message->getName();
		result_t result = message->storeLastData(m_command, m_response);
		ostringstream output;
		if (result==RESULT_OK)
			result = message->decodeLastData(output);
		if (result < RESULT_OK)
			logError(lf_update, "unable to parse %s %s from %s / %s: %s", circuit.c_str(), name.c_str(), m_command.getDataStr().c_str(), m_response.getDataStr().c_str(), getResultCode(result));
		else {
			string data = output.str();
			if (m_answer && dstAddress == (master ? m_ownMasterAddress : m_ownSlaveAddress)) {
				logNotice(lf_update, "self-update %s %s QQ=%2.2x: %s", circuit.c_str(), name.c_str(), srcAddress, data.c_str()); // TODO store in database of internal variables
			}
			else if (message->getDstAddress() == SYN) { // any destination
				if (message->getSrcAddress() == SYN) // any destination and any source
					logNotice(lf_update, "update %s %s QQ=%2.2x ZZ=%2.2x: %s", circuit.c_str(), name.c_str(), srcAddress, dstAddress, data.c_str());
				else
					logNotice(lf_update, "update %s %s ZZ=%2.2x: %s", circuit.c_str(), name.c_str(), dstAddress, data.c_str());
			}
			else if (message->getSrcAddress() == SYN) // any source
				logNotice(lf_update, "update %s %s QQ=%2.2x: %s", circuit.c_str(), name.c_str(), srcAddress, data.c_str());
			else
				logNotice(lf_update, "update %s %s: %s", circuit.c_str(), name.c_str(), data.c_str());
		}
	}
}

result_t BusHandler::startScan(bool full)
{
	auto messages = m_messages->findAll("scan", "");
	for (auto it = messages.begin(); it < messages.end(); it++) {
		auto message = *it;
		if (message->getPrimaryCommand() == 0x07 && message->getSecondaryCommand() == 0x04)
			messages.erase(it--); // query pb 0x07 / sb 0x04 only once
	}

	auto scanMessage = m_messages->getScanMessage();
	if (scanMessage==NULL)
		return RESULT_ERR_NOTFOUND;

	m_scanResults.clear();

	deque<unsigned char> slaves;
	for (unsigned char slave = 1; slave != 0; slave++) { // 0 is known to be a master
		libebus::Address slaveAddr(slave);
		if (not slaveAddr.isValid(false) || slaveAddr.isMaster())
			continue;
		if (!full && (m_seenAddresses[slaveAddr.binAddr()]&SEEN)==0) {
			unsigned char master = slaveAddr.getMasterAddress().binAddr(); // check if we saw the corresponding master already
			if (master == SYN || (m_seenAddresses[master]&SEEN)==0)
				continue;
		}
		slaves.push_back(slaveAddr.binAddr());
	}
	messages.push_front(scanMessage);
	auto request = make_shared<ScanRequest>(m_messages, messages, slaves, this);
	result_t result = request->prepare(m_ownMasterAddress);
	if (result < RESULT_OK) {
		return result==RESULT_ERR_EOF ? RESULT_EMPTY : result;
	}
	m_runningScans++;
	m_nextRequests.push(request);
	return RESULT_OK;
}

void BusHandler::setScanResult(unsigned char dstAddress, string str)
{
	m_seenAddresses[dstAddress] |= SCAN_INIT;
	if (str.length()>0) {
		m_seenAddresses[dstAddress] |= SCAN_DONE;
		m_scanResults[dstAddress] = str;
		logNotice(lf_bus, "scan %2.2x: %s", dstAddress, str.c_str());
	}
}

void BusHandler::setScanFinished()
{
	if (m_runningScans>0)
		m_runningScans--;
}

void BusHandler::formatScanResult(ostringstream& output)
{
	if (m_runningScans>0) {
		output << static_cast<unsigned>(m_runningScans) << " scan(s) still running" << endl;
	}
	bool first = true;
	for (unsigned char slave = 1; slave != 0; slave++) { // 0 is known to be a master
		map<unsigned char, string>::iterator it = m_scanResults.find(slave);
		if (it != m_scanResults.end()) {
			if (first)
				first = false;
			else
				output << endl;
			output << hex << setw(2) << setfill('0') << static_cast<unsigned>(slave) << it->second;
		}
	}
	if (first) {
		// fallback to autoscan results
		for (unsigned char slave = 1; slave != 0; slave++) { // 0 is known to be a master
			libebus::Address slaveAddr(slave);
			if (slaveAddr.isValid(false) && not slaveAddr.isMaster() && (m_seenAddresses[slave]&SCAN_DONE)!=0) {
				auto message = m_messages->getScanMessage(slave);
				if (message!=NULL && message->getLastUpdateTime()>0) {
					if (first)
						first = false;
					else
						output << endl;
					output << hex << setw(2) << setfill('0') << static_cast<unsigned>(slave);
					message->decodeLastData(output, 0, true);
				}
			}
		}
	}
}

void BusHandler::formatSeenInfo(ostringstream& output)
{
	unsigned char rawAddress = 0;

	for (int index=0; index<256; index++, rawAddress++) {
		libebus::Address address(rawAddress);
		if (address.isValid(false)
		&& ((m_seenAddresses[address.binAddr()]&SEEN)!=0 || address==m_ownMasterAddress || address==m_ownSlaveAddress)) {
			output << endl << "address " << setfill('0') << setw(2) << hex << static_cast<unsigned>(address.binAddr());
			libebus::Address master;
			if (address.isMaster()) {
				output << ": master";
				master = address;
			} else {
				output << ": slave";
				master = address.getMasterAddress();
			}
			if (master != SYN)
				output << " #" << setw(0) << dec << static_cast<unsigned>(master.getMasterNumber());
			if (address==m_ownMasterAddress || (m_answer && address==m_ownSlaveAddress)) {
				output << ", ebusd";
				if (m_answer) {
					output << " (answering)";
				}
				if ((m_seenAddresses[address.binAddr()]&SEEN)!=0) {
					output << ", conflict";
				}
			}
			if ((m_seenAddresses[address.binAddr()]&SCAN_DONE)!=0) {
				output << ", scanned";
				auto message = m_messages->getScanMessage(address);
				if (message!=NULL && message->getLastUpdateTime()>0) {
					// add detailed scan info: Manufacturer ID SW HW
					output << " \"";
					result_t result = message->decodeLastData(output, OF_VERBOSE);
					if (result!=RESULT_OK)
						output << "\" error: " << getResultCode(result);
					else
						output << "\"";
				}
			}
			string loadedFiles = m_messages->getLoadedFiles(address);
			if (!loadedFiles.empty())
				output << ", loaded " << loadedFiles;
		}
	}
}

result_t BusHandler::scanAndWait(const libebus::Address& dstAddress, SymbolString& slave)
{
	if (not dstAddress.isValid(false) || dstAddress.isMaster())
		return RESULT_ERR_INVALID_ADDR;
	m_seenAddresses[dstAddress.binAddr()] |= SCAN_INIT;
	auto scanMessage = m_messages->getScanMessage();
	if (scanMessage==NULL) {
		return RESULT_ERR_NOTFOUND;
	}
	istringstream input;
	SymbolString master;
	result_t result = scanMessage->prepareMaster(m_ownMasterAddress, master, input, UI_FIELD_SEPARATOR, dstAddress.binAddr());
	if (result==RESULT_OK) {
		result = sendAndWait(master, slave);
		if (result==RESULT_OK) {
			auto message = m_messages->getScanMessage(dstAddress.binAddr());
			if (message!=NULL && message!=scanMessage) {
				scanMessage = message;
				scanMessage->storeLastData(PartType::masterData, master, 0); // update the cache, expected to work since this is a clone
			}
		}
		if (result!=RESULT_ERR_NO_SIGNAL)
			m_seenAddresses[dstAddress.binAddr()] |= SCAN_DONE;
	}
	if (result!=RESULT_OK)
		return result;

	return scanMessage->storeLastData(PartType::slaveData, slave, 0); // update the cache
}

bool BusHandler::enableGrab(bool enable, bool all)
{
	GrabRequest request = enable ? (all ? GrabRequest::all : GrabRequest::unknown) : GrabRequest::none;
	if (request==m_grabUnknownMessages)
		return false;
	if (m_grabUnknownMessages==GrabRequest::none)
		m_grabbedUnknownMessages.clear();
	m_grabUnknownMessages = request;
	return true;
}

void BusHandler::formatGrabResult(ostringstream& output)
{
	if (m_grabUnknownMessages == GrabRequest::none) {
		output << "grab disabled";
	} else {
		bool first = true;
		for (map<string, string>::iterator it = m_grabbedUnknownMessages.begin(); it != m_grabbedUnknownMessages.end(); it++) {
			if (first)
				first = false;
			else
				output << endl;
			output << it->second;
		}
	}
}

unsigned char BusHandler::getNextScanAddress(unsigned char lastAddress, bool& scanned) {
	libebus::Address ebusAddr(lastAddress);

	if (lastAddress==SYN)
		return SYN;
	while (++lastAddress!=0) { // 0 is known to be a master
		if (not ebusAddr.isValid(false) || ebusAddr.isMaster())
			continue;
		if ((m_seenAddresses[lastAddress]&(SEEN|LOAD_INIT))==SEEN) {
			scanned = (m_seenAddresses[lastAddress]&SCAN_INIT)!=0;
			return lastAddress;
		}
		unsigned char master = ebusAddr.getMasterAddress().binAddr();
		if (master!=SYN && (m_seenAddresses[master]&SEEN)!=0 && (m_seenAddresses[lastAddress]&LOAD_INIT)==0) {
			scanned = (m_seenAddresses[lastAddress]&SCAN_INIT)!=0;
			return lastAddress;
		}
	}
	return SYN;
}

void BusHandler::setScanConfigLoaded(unsigned char address, string file) {
	m_seenAddresses[address] |= LOAD_INIT;
	if (!file.empty()) {
		m_seenAddresses[address] |= LOAD_DONE;
		m_messages->addLoadedFile(address, file);
	}
}
