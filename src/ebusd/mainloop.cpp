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

#include "mainloop.h"
#include <iomanip>
#include "main.h"
#include "log.h"
#include "data.h"
#include "config.h"

using std::dec;

/** the known column names (pairs of full length name and short length name). */
static const char* columnNames[] = {
	"type", "t",
	"circuit", "c",
	"name", "n",
	"comment", "co",
	"qq", "q",
	"zz", "z",
	"pbsb", "p",
	"id", "i",
	"fields", "f",
};

/** the number of known column names. */
static const size_t columnCount = sizeof(columnNames) / sizeof(char*);

MainLoop::MainLoop(const struct options& opt, shared_ptr<Device> device, MessageMap* messages)
	: m_device(device), m_messages(messages), m_address(opt.address), m_scanConfig(opt.scanConfig), m_enableHex(opt.enableHex)
{
	// setup Device
	m_device->setLogRaw(opt.logRaw);
	m_device->setDumpRawFile(opt.dumpFile);
	m_device->setDumpRawMaxSize(opt.dumpSize);
	m_device->setDumpRaw(opt.dump);

	// open Device
	result_t result = m_device->open();
	if (result != RESULT_OK)
		logError(lf_bus, "unable to open %s: %s", m_device->getName(), getResultCode(result));
	else if (!m_device->isValid())
		logError(lf_bus, "device %s not available", m_device->getName());

	// create BusHandler
	unsigned int latency;
	if (opt.latency<0) {
		latency = device->getLatency();
	} else {
		latency = (unsigned int)opt.latency;
	}
	m_busHandler = std::make_unique<BusHandler>(m_device.get(), m_messages,
			m_address, opt.answer,
			opt.acquireRetries, opt.sendRetries,
			latency, opt.acquireTimeout, opt.receiveTimeout,
			opt.masterCount, opt.generateSyn,
			opt.pollInterval);
	m_busHandler->start("bushandler");

	// create network
	m_htmlPath = opt.htmlPath;
	m_network = std::make_unique<Network>(opt.localOnly, opt.port, opt.httpPort, m_netQueue);
	m_network->start("network");
}

void MainLoop::run()
{
	bool running = true;
	time_t lastTaskRun, now;
	int taskDelay = 5;
	unsigned char lastScanAddress = 0; // 0 is known to be a master
	time(&now);
	lastTaskRun = now;

	while (running) {
		string result;

		// pick the next message to handle
		NetMessage* message = m_netQueue.pop(taskDelay);
		time(&now);
		if (now<lastTaskRun) {
			// clock skew
			lastTaskRun = now;
		} else if (now > lastTaskRun+taskDelay) {
			logDebug(lf_main, "performing regular tasks");
			if (m_scanConfig) {
				bool scanned = false;
				lastScanAddress = m_busHandler->getNextScanAddress(lastScanAddress, scanned);
				if (lastScanAddress==SYN) {
					taskDelay = 5;
					lastScanAddress = 0;
				} else {
					SymbolString slave(false);
					if (scanned) {
						auto message = m_messages->getScanMessage(lastScanAddress);
						slave = message->getLastSlaveData();
						scanned = message->getLastUpdateTime()>0;
					} else {
						result_t result = m_busHandler->scanAndWait(lastScanAddress, slave);
						taskDelay = (result == RESULT_ERR_NO_SIGNAL) ? 10 : 1;
						if (result!=RESULT_OK)
							logError(lf_main, "scan config %2.2x message: %s", lastScanAddress, getResultCode(result));
						else {
							scanned = true;
							logInfo(lf_main, "scan config %2.2x message received", lastScanAddress);
						}
					}
					if (scanned) {
						string file;
						result_t result = loadScanConfigFile(m_messages, lastScanAddress, slave, file);
						if (result==RESULT_OK) {
							logInfo(lf_main, "scan config %2.2x: file %s loaded", lastScanAddress, file.c_str());
							m_busHandler->setScanConfigLoaded(lastScanAddress, file);
						} else {
							m_busHandler->setScanConfigLoaded(lastScanAddress, "");
						}
					}
				}
			}
			time(&lastTaskRun);
		}
		if (message==NULL) {
			continue;
		}
		string request = message->getRequest();

		time_t since, until;
		time(&until);
		bool listening = message->isListening(&since);
		if (!listening)
			since = until;

		bool connected = true;
		if (request.length() > 0) {
			logDebug(lf_main, ">>> %s", request.c_str());
			result = decodeMessage(request, message->isHttp(), connected, listening, running);

			if (result.length() == 0 && !message->isHttp())
				result = getResultCode(RESULT_EMPTY);

			if (result.length() > 100)
				logDebug(lf_main, "<<< %s ...", result.substr(0, 100).c_str());
			else
				logDebug(lf_main, "<<< %s", result.c_str());

			if (result.length() == 0)
				result = "\n"; // only for HTTP
			else if (!message->isHttp())
				result += "\n\n";
		}
		if (listening) {
			result += getUpdates(since, until);
		}

		// send result to client
		message->setResult(result, listening, until, !connected);
	}
}

string MainLoop::decodeMessage(const string& data, const bool isHttp, bool& connected, bool& listening, bool& running)
{
	ostringstream result;

	// prepare data
	string token, previous;
	istringstream stream(data);
	vector<string> args;
	bool escaped = false;

	char delim = ' ';
	while (getline(stream, token, delim)) {
		if (!isHttp) {
			if (escaped) {
				args.pop_back();
				if (token.length() > 0 && token[token.length()-1] == '"') {
					token.erase(token.length() - 1, 1);
					escaped = false;
				}
				token = previous + " " + token;
			}
			else if (token.length() == 0) // allow multiple space chars for a single delimiter
				continue;
			else if (token[0] == '"') {
				token.erase(0, 1);
				if (token.length() > 0 && token[token.length()-1] == '"')
					token.erase(token.length() - 1, 1);
				else
					escaped = true;
			}
		}
		args.push_back(token);
		previous = token;
		if (isHttp)
			delim = (args.size() == 1) ? '?' : '\n';
	}

	if (isHttp) {
		const char* str = args.size() > 0 ? args[0].c_str() : "";
		if (strcmp(str, "GET") == 0)
			return executeGet(args, connected);

		connected = false;
		return "HTTP/1.0 405 Method Not Allowed\r\n\r\n";
	}

	if (args.size() == 0)
		return executeHelp();

	const char* str = args[0].c_str();
	if (args.size() == 2) {
		// check for "CMD -h"
		if (strcasecmp(args[1].c_str(), "-h") == 0 || strcasecmp(args[1].c_str(), "-?") == 0 || strcasecmp(args[1].c_str(), "--help") == 0)
			args.clear(); // empty args is used as command help indicator
		else if (strcasecmp(args[0].c_str(), "H") == 0 || strcasecmp(args[0].c_str(), "HELP") == 0) { // check for "HELP CMD"
			str = args[1].c_str();
			args.clear(); // empty args is used as command help indicator
		}
	}
	if (strcasecmp(str, "R") == 0 || strcasecmp(str, "READ") == 0)
		return executeRead(args);
	if (strcasecmp(str, "W") == 0 || strcasecmp(str, "WRITE") == 0)
		return executeWrite(args);
	if (strcasecmp(str, "HEX") == 0) {
		if (m_enableHex)
			return executeHex(args);
		return "ERR: command not enabled";
	}
	if (strcasecmp(str, "F") == 0 || strcasecmp(str, "FIND") == 0)
		return executeFind(args);
	if (strcasecmp(str, "L") == 0 || strcasecmp(str, "LISTEN") == 0)
		return executeListen(args, listening);
	if (strcasecmp(str, "S") == 0 || strcasecmp(str, "STATE") == 0)
		return executeState(args);
	if (strcasecmp(str, "G") == 0 || strcasecmp(str, "GRAB") == 0)
		return executeGrab(args);
	if (strcasecmp(str, "SCAN") == 0)
		return executeScan(args);
	if (strcasecmp(str, "LOG") == 0)
		return executeLog(args);
	if (strcasecmp(str, "RAW") == 0)
		return executeRaw(args);
	if (strcasecmp(str, "DUMP") == 0)
		return executeDump(args);
	if (strcasecmp(str, "RELOAD") == 0)
		return executeReload(args);
	if (strcasecmp(str, "STOP") == 0)
		return executeStop(args, running);
	if (strcasecmp(str, "Q") == 0 || strcasecmp(str, "QUIT") == 0)
		return executeQuit(args, connected);
	if (strcasecmp(str, "I") == 0 || strcasecmp(str, "INFO") == 0)
		return executeInfo(args);
	if (strcasecmp(str, "H") == 0 || strcasecmp(str, "HELP") == 0)
		return executeHelp();
	return "ERR: command not found";
}

result_t MainLoop::parseHexMaster(vector<string> &args, size_t argPos, SymbolString& master)
{
	ostringstream msg;
	while (argPos < args.size()) {
		if ((args[argPos].length() % 2) != 0) {
			return RESULT_ERR_INVALID_NUM;
		}
		msg << args[argPos++];
	}
	if (msg.str().size() < 4*2) // at least ZZ, PB, SB, NN
		return RESULT_ERR_INVALID_ARG;

	result_t ret;
	unsigned int length = parseInt(msg.str().substr(3*2, 2).c_str(), 16, 0, MAX_POS, ret);
	if (ret == RESULT_OK && (4+length)*2 != msg.str().size())
		return RESULT_ERR_INVALID_ARG;

	ret = master.push_back(m_address, false);
	if (ret == RESULT_OK)
		ret = master.parseHex(msg.str());
	if (ret == RESULT_OK && !isValidAddress(master[1]))
		ret = RESULT_ERR_INVALID_ADDR;

	return ret;
}

result_t MainLoop::readFromBus(shared_ptr<Message> message, string inputStr, const unsigned char dstAddress)
{
	result_t ret = RESULT_EMPTY;
	SymbolString master(true);
	SymbolString slave(false);
	for (unsigned char index=0; index<message->getCount(); index++) {
		istringstream input(inputStr);
		ret = message->prepareMaster(m_address, master, input, UI_FIELD_SEPARATOR, dstAddress, index);
		if (ret != RESULT_OK) {
			logError(lf_main, "prepare message part %d: %s", index, getResultCode(ret));
			break;
		}
		// send message
		ret = m_busHandler->sendAndWait(master, slave);
		if (ret != RESULT_OK) {
			logError(lf_main, "send message part %d: %s", index, getResultCode(ret));
			break;
		}
		ret = message->storeLastData(PartType::slaveData, slave, index);
		if (ret < RESULT_OK) {
			logError(lf_main, "store message part %d: %s", index, getResultCode(ret));
			break;
		}
	}
	return ret;
}

string MainLoop::executeRead(vector<string> &args)
{
	size_t argPos = 1;
	bool hex = false, verbose = false, numeric = false;
	time_t maxAge = 5*60;
	string circuit, params;
	unsigned char dstAddress = SYN, pollPriority = 0;
	while (args.size() > argPos && args[argPos][0] == '-') {
		if (args[argPos] == "-h") {
			hex = true;
		} else if (args[argPos] == "-f") {
			maxAge = 0;
		} else if (args[argPos] == "-v") {
			verbose = true;
		} else if (args[argPos] == "-n") {
			numeric = true;
		} else if (args[argPos] == "-m") {
			argPos++;
			if (args.size() > argPos) {
				result_t result;
				maxAge = parseInt(args[argPos].c_str(), 10, 0, 24*60*60, result);
				if (result != RESULT_OK) {
					argPos = 0; // print usage
					break;
				}
			}
			else {
				argPos = 0; // print usage
				break;
			}
		} else if (args[argPos] == "-c") {
			argPos++;
			if (argPos >= args.size()) {
				argPos = 0; // print usage
				break;
			}
			circuit = args[argPos];
		} else if (args[argPos] == "-d") {
			argPos++;
			if (argPos >= args.size()) {
				argPos = 0; // print usage
				break;
			}
			result_t ret;
			dstAddress = (unsigned char)parseInt(args[argPos].c_str(), 16, 0, 0xff, ret);
			if (ret != RESULT_OK || !isValidAddress(dstAddress) || isMaster(dstAddress))
				return getResultCode(RESULT_ERR_INVALID_ADDR);
		} else if (args[argPos] == "-p") {
			argPos++;
			if (argPos >= args.size()) {
				argPos = 0; // print usage
				break;
			}
			result_t ret;
			pollPriority = (unsigned char)parseInt(args[argPos].c_str(), 10, 1, 9, ret);
			if (ret != RESULT_OK)
				return getResultCode(RESULT_ERR_INVALID_NUM);
		} else if (args[argPos] == "-i") {
			argPos++;
			if (argPos >= args.size()) {
				argPos = 0; // print usage
				break;
			}
			params = args[argPos];
		} else {
			argPos = 0; // print usage
			break;
		}
		argPos++;
	}
	if (hex && (dstAddress != SYN || !circuit.empty() || verbose || numeric || pollPriority > 0 || args.size() < argPos + 1)) {
		argPos = 0; // print usage
	}

	time_t now;
	time(&now);

	if (hex && argPos > 0) {
		SymbolString cacheMaster(false);
		result_t ret = parseHexMaster(args, argPos, cacheMaster);
		if (ret != RESULT_OK)
			return getResultCode(ret);
		if (cacheMaster[1] == BROADCAST || isMaster(cacheMaster[1]))
			return getResultCode(RESULT_ERR_INVALID_ARG);

		logNotice(lf_main, "read hex cmd: %s", cacheMaster.getDataStr(true, false).c_str());

		// find message
		auto message = m_messages->find(cacheMaster, false, true, false, false);

		if (message == NULL)
			return getResultCode(RESULT_ERR_NOTFOUND);
		if (message->isWrite())
			return getResultCode(RESULT_ERR_INVALID_ARG);
		if (circuit.length() > 0 && circuit!=message->getCircuit())
			return getResultCode(RESULT_ERR_INVALID_ARG); // non-matching circuit
		if (message->getLastUpdateTime() + maxAge > now || (message->isPassive() && message->getLastUpdateTime() != 0)) {
			SymbolString& slave = message->getLastSlaveData();
			logNotice(lf_main, "hex read %s %s from cache", message->getCircuit().c_str(), message->getName().c_str());
			return slave.getDataStr();
		}

		// send message
		SymbolString master(true);
		master.addAll(cacheMaster);
		SymbolString slave(false);
		ret = m_busHandler->sendAndWait(master, slave);

		if (ret == RESULT_OK) {
			ret = message->storeLastData(cacheMaster, slave);
			ostringstream result;
			if (ret==RESULT_OK)
				ret = message->decodeLastData(result);
			if (ret >= RESULT_OK)
				logInfo(lf_main, "read hex %s %s cache update: %s", message->getCircuit().c_str(), message->getName().c_str(), result.str().c_str());
			else
				logError(lf_main, "read hex %s %s cache update: %s", message->getCircuit().c_str(), message->getName().c_str(), getResultCode(ret));
			return slave.getDataStr();
		}
		logError(lf_main, "read hex %s %s: %s", message->getCircuit().c_str(), message->getName().c_str(), getResultCode(ret));
		return getResultCode(ret);
	}
	if (argPos == 0 || args.size() < argPos + 1 || args.size() > argPos + 2)
		return "usage: read [-f] [-m SECONDS] [-c CIRCUIT] [-d ZZ] [-p PRIO] [-v] [-n] [-i VALUE[;VALUE]*] NAME [FIELD[.N]]\n"
			   "  or:  read [-f] [-m SECONDS] [-c CIRCUIT] -h ZZPBSBNNDx\n"
			   " Read value(s) or hex message.\n"
			   "  -f          force reading from the bus (same as '-m 0')\n"
			   "  -m SECONDS  only return cached value if age is less than SECONDS [300]\n"
			   "  -c CIRCUIT  limit to messages of CIRCUIT\n"
			   "  -d ZZ       override destination address ZZ\n"
			   "  -p PRIO     set the message poll priority (1-9)\n"
			   "  -v          be verbose (include circuit, name, field names, units, and comments)\n"
			   "  -n          use numeric value of value=name pairs\n"
			   "  -i VALUE    read additional message parameters from VALUE\n"
			   "  NAME        NAME of the message to send\n"
			   "  FIELD       only retrieve the field named FIELD\n"
			   "  N           only retrieve the N'th field named FIELD (0-based)\n"
			   "  -h          send hex read message (or answer from cache):\n"
			   "    ZZ        destination address\n"
			   "    PB SB     primary/secondary command byte\n"
			   "    NN        number of following data bytes\n"
			   "    Dx        data byte(s) to send";

	string fieldName;
	signed char fieldIndex = -2;
	if (args.size() == argPos + 2) {
		fieldName = args[argPos + 1];
		fieldIndex = -1;
		size_t pos = fieldName.find_last_of('.');
		if (pos != string::npos) {
			result_t result = RESULT_OK;
			fieldIndex = (char)parseInt(fieldName.substr(pos+1).c_str(), 10, 0, MAX_POS, result);
			if (result == RESULT_OK)
				fieldName = fieldName.substr(0, pos);
		}
	}

	ostringstream result;
	auto message = m_messages->find(circuit, args[argPos], false);

	// adjust poll priority
	if (message != NULL && pollPriority > 0 && message->setPollPriority(pollPriority)) {
		m_messages->addPollMessage(message);
	}

	if (dstAddress==SYN && maxAge > 0) {
		auto cacheMessage = m_messages->find(circuit, args[argPos], false, true);
		bool hasCache = cacheMessage != NULL;
		if (!hasCache || (message != NULL && message->getLastUpdateTime() > cacheMessage->getLastUpdateTime()))
			cacheMessage = message; // message is newer/better

		if (cacheMessage != NULL && (cacheMessage->getLastUpdateTime() + maxAge > now || (cacheMessage->isPassive() && cacheMessage->getLastUpdateTime() != 0))) {
			if (verbose)
				result << cacheMessage->getCircuit() << " " << cacheMessage->getName() << " ";
			result_t ret = cacheMessage->decodeLastData(result, (verbose?OF_VERBOSE:0)|(numeric?OF_NUMERIC:0), false, fieldIndex==-2 ? NULL : fieldName.c_str(), fieldIndex);
			if (ret != RESULT_OK) {
				if (ret < RESULT_OK)
					logError(lf_main, "read %s %s cached: %s", cacheMessage->getCircuit().c_str(), cacheMessage->getName().c_str(), getResultCode(ret));
				return getResultCode(ret);
			}
			logInfo(lf_main, "read %s %s cached: %s", cacheMessage->getCircuit().c_str(), cacheMessage->getName().c_str(), result.str().c_str());
			return result.str();
		}

		if (message == NULL && hasCache)
			return "ERR: no data stored";
		// else: read directly from bus
	}

	if (message == NULL)
		return getResultCode(RESULT_ERR_NOTFOUND);
	if (message->getDstAddress()==SYN && dstAddress==SYN)
		return getResultCode(RESULT_ERR_INVALID_ADDR);

	// read directly from bus
	result_t ret = readFromBus(message, params, dstAddress);
	if (ret != RESULT_OK)
		return getResultCode(ret);

	if (verbose)
		result << message->getCircuit() << " " << message->getName() << " ";
	ret = message->decodeLastData(PartType::slaveData, result, (verbose?OF_VERBOSE:0)|(numeric?OF_NUMERIC:0), false, fieldIndex==-2 ? NULL : fieldName.c_str(), fieldIndex);
	if (ret < RESULT_OK) {
		logError(lf_main, "read %s %s: decode %s", message->getCircuit().c_str(), message->getName().c_str(), getResultCode(ret));
		result.str("");
		result << getResultCode(ret) << " in decode";
		return result.str();
	}
	if (ret > RESULT_OK)
		return getResultCode(ret);
	logInfo(lf_main, "read %s %s: %s", message->getCircuit().c_str(), message->getName().c_str(), result.str().c_str());
	return result.str();
}

string MainLoop::executeWrite(vector<string> &args)
{
	size_t argPos = 1;
	bool hex = false;
	string circuit;
	unsigned char dstAddress = SYN;
	while (args.size() > argPos && args[argPos][0] == '-') {
		if (args[argPos] == "-h") {
			hex = true;
		} else if (args[argPos] == "-d") {
			argPos++;
			if (argPos >= args.size()) {
				argPos = 0; // print usage
				break;
			}
			result_t ret;
			dstAddress = (unsigned char)parseInt(args[argPos].c_str(), 16, 0, 0xff, ret);
			if (ret != RESULT_OK || !isValidAddress(dstAddress) || isMaster(dstAddress))
				return getResultCode(RESULT_ERR_INVALID_ADDR);
		} else if (args[argPos] == "-c") {
			argPos++;
			if (argPos >= args.size()) {
				argPos = 0; // print usage
				break;
			}
			circuit = args[argPos];
		} else {
			argPos = 0; // print usage
			break;
		}
		argPos++;
	}

	if (hex && (dstAddress != SYN || !circuit.empty() || args.size() < argPos + 1)) {
		argPos = 0; // print usage
	}

	if (hex && argPos > 0) {
		SymbolString cacheMaster(false);
		result_t ret = parseHexMaster(args, argPos, cacheMaster);
		if (ret != RESULT_OK)
			return getResultCode(ret);

		logNotice(lf_main, "write hex cmd: %s", cacheMaster.getDataStr(true, false).c_str());

		// find message
		auto message = m_messages->find(cacheMaster, false, false, true, false);

		if (message == NULL)
			return getResultCode(RESULT_ERR_NOTFOUND);
		if (!message->isWrite())
			return getResultCode(RESULT_ERR_INVALID_ARG);
		if (circuit.length() > 0 && circuit!=message->getCircuit())
			return getResultCode(RESULT_ERR_INVALID_ARG); // non-matching circuit

		// send message
		SymbolString master(true);
		master.addAll(cacheMaster);
		SymbolString slave(false);
		ret = m_busHandler->sendAndWait(master, slave);

		if (ret == RESULT_OK) {
			// also update read messages
			ret = message->storeLastData(cacheMaster, slave);
			ostringstream result;
			if (ret==RESULT_OK)
				ret = message->decodeLastData(result);
			if (ret >= RESULT_OK)
				logInfo(lf_main, "write hex %s %s cache update: %s", message->getCircuit().c_str(), message->getName().c_str(), result.str().c_str());
			else
				logError(lf_main, "write hex %s %s cache update: %s", message->getCircuit().c_str(), message->getName().c_str(), getResultCode(ret));
			if (master[1] == BROADCAST)
				return "done broadcast";
			if (isMaster(master[1]))
				return getResultCode(RESULT_OK);
			return slave.getDataStr();
		}
		logError(lf_main, "write hex %s %s: %s", message->getCircuit().c_str(), message->getName().c_str(), getResultCode(ret));
		return getResultCode(ret);
	}

	if (argPos == 0 || circuit.empty() || (args.size() != argPos + 2 && args.size() != argPos + 1))
		return "usage: write [-d ZZ] -c CIRCUIT NAME [VALUE[;VALUE]*]\n"
			   "  or:  write [-c CIRCUIT] -h ZZPBSBNNDx\n"
			   " Write value(s) or hex message.\n"
			   "  -d ZZ       override destination address ZZ\n"
			   "  -c CIRCUIT  CIRCUIT of the message to send\n"
			   "  NAME        NAME of the message to send\n"
			   "  VALUE       a single field VALUE\n"
			   "  -h          send hex write message:\n"
			   "    ZZ        destination address\n"
			   "    PB SB     primary/secondary command byte\n"
			   "    NN        number of following data bytes\n"
			   "    Dx        data byte(s) to send";

	auto message = m_messages->find(circuit, args[argPos], true);

	if (message == NULL)
		return getResultCode(RESULT_ERR_NOTFOUND);
	if (message->getDstAddress()==SYN && dstAddress==SYN)
		return getResultCode(RESULT_ERR_INVALID_ADDR);

	result_t ret = readFromBus(message, args.size() == argPos + 1 ? "" : args[argPos + 1], dstAddress); // allow missing values
	if (ret != RESULT_OK)
		return getResultCode(ret);

	dstAddress = message->getLastMasterData()[1];
	ostringstream result;
	if (dstAddress == BROADCAST || isMaster(dstAddress)) {
		logInfo(lf_main, "write %s %s: %s", message->getCircuit().c_str(), message->getName().c_str(), getResultCode(ret));
		if (dstAddress == BROADCAST)
			return "done broadcast";
		return getResultCode(RESULT_OK);
	}

	ret = message->decodeLastData(PartType::slaveData, result); // decode data
	if (ret >= RESULT_OK && result.str().empty()) {
		logInfo(lf_main, "write %s %s: decode %s", message->getCircuit().c_str(), message->getName().c_str(), getResultCode(ret));
		return getResultCode(RESULT_OK);
	}
	if (ret != RESULT_OK) {
		logError(lf_main, "write %s %s: decode %s", message->getCircuit().c_str(), message->getName().c_str(), getResultCode(ret));
		result.str("");
		result << getResultCode(ret) << " in decode";
		return result.str();
	}
	logInfo(lf_main, "write %s %s: %s", message->getCircuit().c_str(), message->getName().c_str(), result.str().c_str());
	return result.str();
}

string MainLoop::executeHex(vector<string> &args)
{
	size_t argPos = 1;
	if (args.size() < argPos + 1 || (args.size() > argPos && args[argPos][0] == '-')) {
		argPos = 0; // print usage
	}

	if (argPos > 0) {
		SymbolString cacheMaster(false);
		result_t ret = parseHexMaster(args, argPos, cacheMaster);
		if (ret != RESULT_OK)
			return getResultCode(ret);

		logNotice(lf_main, "hex cmd: %s", cacheMaster.getDataStr(true, false).c_str());

		// send message
		SymbolString master(true);
		master.addAll(cacheMaster);
		SymbolString slave(false);
		ret = m_busHandler->sendAndWait(master, slave);

		if (ret == RESULT_OK) {
			if (master[1] == BROADCAST)
				return "done broadcast";
			if (isMaster(master[1]))
				return getResultCode(RESULT_OK);
			return slave.getDataStr();
		}
		logError(lf_main, "hex: %s", getResultCode(ret));
		return getResultCode(ret);
	}

	return "usage: hex ZZPBSBNNDx\n"
		   " Send arbitrary data in hex (only if enabled).\n"
		   "  ZZ     destination address\n"
		   "  PB SB  primary/secondary command byte\n"
		   "  NN     number of following data bytes\n"
		   "  Dx     data byte(s) to send";
}

string MainLoop::executeFind(vector<string> &args)
{
	size_t argPos = 1;
	bool verbose = false, configFormat = false, exact = false, withRead = true, withWrite = false, withPassive = true, first = true, onlyWithData = false;
	vector<size_t> columns;
	string circuit;
	vector<unsigned char> id;
	while (args.size() > argPos && args[argPos][0] == '-') {
		if (args[argPos] == "-v")
			verbose = true;
		else if (args[argPos] == "-f")
			configFormat = true;
		else if (args[argPos] == "-F") {
			argPos++;
			if (argPos >= args.size()) {
				argPos = 0; // print usage
				break;
			}
			istringstream input(args[argPos]);
			string column;
			while (getline(input, column, ',')) {
				size_t idx = columnCount;
				for (size_t i = 0; i < columnCount; i++) {
					if (strcasecmp(columnNames[i], column.c_str()) == 0) {
						idx = i;
						break;
					}
				}
				if (idx==columnCount) {
					argPos = 0; // print usage
					break;
				}
				columns.push_back(idx/2);
			}
			if (columns.empty()) {
				argPos = 0; // print usage
				break;
			}
		} else if (args[argPos] == "-e")
			exact = true;
		else if (args[argPos] == "-r") {
			if (first) {
				first = false;
				withWrite = withPassive = false;
			}
			withRead = true;
		}
		else if (args[argPos] == "-w") {
			if (first) {
				first = false;
				withRead = withPassive = false;
			}
			withWrite = true;
		}
		else if (args[argPos] == "-p") {
			if (first) {
				first = false;
				withRead = withWrite = false;
			}
			withPassive = true;
		}
		else if (args[argPos] == "-d")
			onlyWithData = true;
		else if (args[argPos] == "-i") {
			argPos++;
			if (argPos >= args.size() || !id.empty()) {
				argPos = 0; // print usage
				break;
			}
			result_t result = Message::parseId(args[argPos], id);
			if (result != RESULT_OK) {
				return getResultCode(result);
			}
			if (id.empty()) {
				argPos = 0; // print usage
				break;
			}
		}
		else if (args[argPos] == "-c") {
			argPos++;
			if (argPos >= args.size()) {
				argPos = 0; // print usage
				break;
			}
			circuit = args[argPos];
		}
		else {
			argPos = 0; // print usage
			break;
		}
		argPos++;
	}
	if (argPos == 0 || args.size() < argPos || args.size() > argPos + 1)
		return "usage: find [-v] [-r] [-w] [-p] [-d] [-i ID] [-f] [-F COL[,COL]*] [-e] [-c CIRCUIT] [NAME]\n"
			   " Find message(s).\n"
			   "  -v            be verbose (append destination address and update time)\n"
			   "  -r            limit to active read messages (default: read + passive)\n"
			   "  -w            limit to active write messages (default: read + passive)\n"
			   "  -p            limit to passive messages (default: read + passive)\n"
			   "  -d            only include messages with actual data\n"
			   "  -i ID         limit to messages with ID (in hex, PB, SB and further ID bytes)\n"
			   "  -f            list messages in CSV configuration file format\n"
			   "  -F COL[,COL]* list messages in the specified format\n"
			   "                (COL: type,circuit,name,comment,qq,zz,pbsb,id,fields)\n"
			   "  -e            match NAME and optional CIRCUIT exactly (ignoring case)\n"
			   "  -c CIRCUIT    limit to messages of CIRCUIT (or a part thereof without '-e')\n"
			   "  NAME          NAME of the messages to find (or a part thereof without '-e')";

	deque<shared_ptr<Message>> messages = m_messages->findAll(
		circuit, args.size() == argPos ? "" : args[argPos], exact, withRead, withWrite, withPassive
	);

	bool found = false;
	ostringstream result;
	char str[32];
	for (auto it = messages.begin(); it < messages.end();) {
		auto message = *it++;
		if (!id.empty() && !message->checkIdPrefix(id)) {
			continue;
		}
		time_t lastup = message->getLastUpdateTime();
		if (onlyWithData && lastup == 0)
			continue;
		if (configFormat) {
			if (found)
				result << endl;
			message->dump(result);
		} else if (!columns.empty()) {
			if (found)
				result << endl;
			message->dump(result, &columns);
		} else {
			if (found)
				result << endl;
			result << message->getCircuit() << " " << message->getName() << " = ";
			if (lastup == 0)
				result << "no data stored";
			else {
				result_t ret = message->decodeLastData(result, verbose?OF_VERBOSE:0);
				if (ret!=RESULT_OK) {
					result << " (" << getResultCode(ret)
						   << " for " << message->getLastMasterData().getDataStr()
						   << " / " << message->getLastSlaveData().getDataStr() << ")";
				}
			}
			if (verbose) {
				unsigned char dstAddress = message->getDstAddress();
				if (dstAddress != SYN)
					sprintf(str, "%02x", dstAddress);
				else if (lastup != 0 && message->getLastMasterData().size()>1)
					sprintf(str, "%02x", message->getLastMasterData()[1]);
				else
					sprintf(str, "any");
				if (lastup != 0) {
					struct tm* td = localtime(&lastup);
					sprintf(str+strlen(str), ", lastup=%04d-%02d-%02d %02d:%02d:%02d",
						td->tm_year+1900, td->tm_mon+1, td->tm_mday,
						td->tm_hour, td->tm_min, td->tm_sec);
				}
				result << " [ZZ=" << str;
				if (message->isPassive())
					result << ", passive";
				else
					result << ", active";

				if (message->isWrite())
					result << " write]";
				else
					result << " read]";
			}
		}
		found = true;
	}
	if (!found)
		return getResultCode(RESULT_ERR_NOTFOUND);

	return result.str();
}

string MainLoop::executeListen(vector<string> &args, bool& listening)
{
	if (args.size() == 1) {
		if (listening)
			return "listen continued";

		listening = true;
		return "listen started";
	}

	if (args.size() != 2 || args[1] != "stop")
		return "usage: listen [stop]\n"
			   " Listen for updates or stop it.";

	listening = false;
	return "listen stopped";
}

string MainLoop::executeState(vector<string> &args)
{
	if (args.size() == 0)
		return "usage: state\n"
			   " Report bus state.";

	if (m_busHandler->hasSignal()) {
		ostringstream result;
		result << "signal acquired, "
			   << static_cast<unsigned>(m_busHandler->getSymbolRate()) << " symbols/sec ("
			   << static_cast<unsigned>(m_busHandler->getMaxSymbolRate()) << " max), "
			   << static_cast<unsigned>(m_busHandler->getMasterCount()) << " masters";
		return result.str();
	}
	return "no signal";
}

string MainLoop::executeGrab(vector<string> &args)
{
	bool all = args.size() == 2 && strcasecmp(args[1].c_str(), "ALL") == 0;
	if (args.size() == 1 || all)
		return m_busHandler->enableGrab(true, all) ? "grab started" : "grab continued";

	if (args.size() == 2) {
		if (strcasecmp(args[1].c_str(), "STOP") == 0)
			return m_busHandler->enableGrab(false) ? "grab stopped" : "grab not running";

		if (strcasecmp(args[1].c_str(), "RESULT") == 0) {
			ostringstream result;
			m_busHandler->formatGrabResult(result);
			return result.str();
		}
	}
	return "usage: grab [all|stop]\n"
		   "  or:  grab result\n"
		   " Start or stop grabbing unknown or all messages, or report the grabbed messages.";
}

string MainLoop::executeScan(vector<string> &args)
{
	if (args.size() == 1) {
		result_t result = m_busHandler->startScan();
		if (result != RESULT_OK)
			logError(lf_main, "scan: %s", getResultCode(result));

		return getResultCode(result);
	}

	if (args.size() == 2) {
		if (strcasecmp(args[1].c_str(), "FULL") == 0) {
			result_t result = m_busHandler->startScan(true);
			if (result != RESULT_OK)
				logError(lf_main, "full scan: %s", getResultCode(result));

			return getResultCode(result);
		}

		if (strcasecmp(args[1].c_str(), "RESULT") == 0) {
			ostringstream ret;
			m_busHandler->formatScanResult(ret);
			return ret.str();
		}

		result_t result;
		unsigned char dstAddress = (unsigned char)parseInt(args[1].c_str(), 16, 0, 0xff, result);
		if (result != RESULT_OK)
			return getResultCode(result);

		SymbolString slave(false);
		result = m_busHandler->scanAndWait(dstAddress, slave);
		if (result != RESULT_OK)
			return getResultCode(result);

		auto message = m_messages->getScanMessage(dstAddress); // never NULL due to scanAndWait() == RESULT_OK
		ostringstream ret;
		ret << hex << setw(2) << setfill('0') << static_cast<unsigned>(dstAddress);
		result = message->decodeLastData(ret, 0, true); // decode data
		if (result != RESULT_OK)
			return getResultCode(result);

		return ret.str();
	}

	return "usage: scan [full|ZZ]\n"
		   "  or:  scan result\n"
		   " Scan seen slaves, all slaves (full), a single slave (address ZZ), or report scan result.";
}

string MainLoop::executeLog(vector<string> &args)
{
	if (args.size() == 1) {
		ostringstream ret;
		char str[32];
		if (getLogFacilities(str)) {
			ret << str << ' ';
		}
		ret << getLogLevel();
		return ret.str();
	}
	bool result;
	// old format: log areas AREA[,AREA]*, log level LEVEL
	if ((args.size() == 3 || args.size() == 2) && strcasecmp(args[1].c_str(), "AREAS") == 0)
		result = setLogFacilities(args.size() == 3 ? args[2].c_str() : "");
	else if (args.size() == 3 && strcasecmp(args[1].c_str(), "LEVEL") == 0)
		result = setLogLevel(args[2].c_str());
	else if (args.size() == 2)
		result = setLogLevel(args[1].c_str()) || setLogFacilities(args[1].c_str());
	else if (args.size() == 3)
		result = setLogFacilities(args[1].c_str()) && setLogLevel(args[2].c_str());
	else
		return "usage: log [AREA[,AREA]*] [LEVEL]\n"
			   " Set log area(s) and/or log level or get current settings.\n"
			   "  AREA   log area to include (main|network|bus|update|all)\n"
			   "  LEVEL  log level to set (error|notice|info|debug)";

	if (result)
		return getResultCode(RESULT_OK);

	return getResultCode(RESULT_ERR_INVALID_ARG);
}

string MainLoop::executeRaw(vector<string> &args)
{
	if (args.size() != 1)
		return "usage: raw\n"
			   " Toggle logging raw bytes.";

	bool enabled = !m_device->getLogRaw();
	m_device->setLogRaw(enabled);

	return enabled ? "raw output enabled" : "raw output disabled";
}

string MainLoop::executeDump(vector<string> &args)
{
	if (args.size() != 1)
		return "usage: dump\n"
			   " Toggle dumping raw bytes.";

	bool enabled = !m_device->getDumpRaw();
	m_device->setDumpRaw(enabled);

	return enabled ? "dump enabled" : "dump disabled";
}

string MainLoop::executeReload(vector<string> &args)
{
	if (args.size() != 1)
		return "usage: reload\n"
			   " Reload CSV config files.";

	m_busHandler->clear();
	result_t result = loadConfigFiles(m_messages);

	return getResultCode(result);
}

string MainLoop::executeStop(vector<string> &args, bool& running)
{
	if (args.size() == 1) {
		running = false;
		return "daemon stopped";
	}

	return "usage: stop\n"
		   " Stop the daemon.";
}

string MainLoop::executeInfo(vector<string> &args)
{
	if (args.size() == 0)
		return "usage: info\n"
			   " Report information about the daemon, the configuration, and seen devices.";

	ostringstream result;
	result << "version: " << PACKAGE_STRING << "." REVISION "\n";
	if (m_busHandler->hasSignal()) {
		result << "signal: acquired\n";
		result << "symbol rate: " << static_cast<unsigned>(m_busHandler->getSymbolRate()) << "\n";
	} else {
		result << "signal: no signal\n";
	}
	result << "masters: " << static_cast<unsigned>(m_busHandler->getMasterCount()) << "\n";
	result << "messages: " << static_cast<unsigned>(m_messages->size());
	m_busHandler->formatSeenInfo(result);
	return result.str();
}

string MainLoop::executeQuit(vector<string> &args, bool& connected)
{
	if (args.size() == 1) {
		connected = false;
		return "connection closed";
	}

	return "usage: quit\n"
		   " Close client connection.";
}

string MainLoop::executeHelp()
{
	return "usage:\n"
		   " read|r   Read value(s):         read [-f] [-m SECONDS] [-c CIRCUIT] [-d ZZ] [-p PRIO] [-v] [-n] [-i VALUE[;VALUE]*] NAME [FIELD[.N]]\n"
		   "          Read hex message:      read [-f] [-m SECONDS] [-c CIRCUIT] -h ZZPBSBNNDx\n"
		   " write|w  Write value(s):        write [-d ZZ] -c CIRCUIT NAME [VALUE[;VALUE]*]\n"
		   "          Write hex message:     write [-c CIRCUIT] -h ZZPBSBNNDx\n"
		   " hex      Send hex data:         hex ZZPBSBNNDx\n"
		   " find|f   Find message(s):       find [-v] [-r] [-w] [-p] [-d] [-i ID] [-f] [-F COL[,COL]*] [-e] [-c CIRCUIT] [NAME]\n"
		   " listen|l Listen for updates:    listen [stop]\n"
		   " state|s  Report bus state\n"
		   " info|i   Report information about the daemon, the configuration, and seen devices.\n"
		   " grab|g   Grab messages:         grab [all|stop]\n"
		   "          Report the messages:   grab result\n"
		   " scan     Scan slaves:           scan [full|ZZ]\n"
		   "          Report scan result:    scan result\n"
		   " log      Set log area/level:    log [AREA[,AREA]*] [LEVEL]\n"
		   "                                   AREA: main|network|bus|update|all\n"
		   "                                   LEVEL: error|notice|info|debug\n"
		   " raw      Toggle logging raw bytes\n"
		   " dump     Toggle dumping raw bytes\n"
		   " reload   Reload CSV config files\n"
		   " stop     Stop the daemon\n"
		   " quit|q   Close connection\n"
		   " help|h   Print help             help [COMMAND]";
}

string MainLoop::executeGet(vector<string> &args, bool& connected)
{
	result_t ret = RESULT_OK;
	bool verbose = false, numeric = false, required = false;
	size_t argPos = 1;
	string uri = args[argPos++];
	ostringstream result;
	int type = -1;

	if (strncmp(uri.c_str(), "/data/", 6) == 0) {
		string circuit = "", name = "";
		size_t pos = uri.find('/', 6);
		if (pos == string::npos) {
			circuit = uri.substr(6);
		} else {
			circuit = uri.substr(6, pos-6);
			name = uri.substr(pos+1);
		}
		time_t since = 0;
		unsigned char pollPriority = 0;
		bool exact = false;
		if (args.size() > argPos) {
			string query = args[argPos++];
			istringstream stream(query);
			string token;
			while (getline(stream, token, '&')) {
				pos = token.find('=');
				string qname, value;
				if (pos != string::npos) {
					qname = token.substr(0, pos);
					value = token.substr(pos+1);
				} else {
					qname = token;
				}
				if (strcmp(qname.c_str(), "since") == 0) {
					since = parseInt(value.c_str(), 10, 0, 0xffffffff, ret);
				} else if (strcmp(qname.c_str(), "poll") == 0) {
					pollPriority = (unsigned char)parseInt(value.c_str(), 10, 1, 9, ret);
				} else if (strcmp(qname.c_str(), "exact") == 0) {
					exact = value.length()==0 || strcmp(value.c_str(), "1") == 0;
				} else if (strcmp(qname.c_str(), "verbose") == 0) {
					verbose = value.length()==0 || strcmp(value.c_str(), "1") == 0;
				} else if (strcmp(qname.c_str(), "numeric") == 0) {
					numeric = value.length()==0 || strcmp(value.c_str(), "1") == 0;
				} else if (strcmp(qname.c_str(), "required") == 0) {
					required = value.length()==0 || strcmp(value.c_str(), "1") == 0;
				}
				if (ret != RESULT_OK)
					break;
			}
		}
		auto messages = m_messages->findAll(circuit, name, exact, true, false, true);

		bool first = true;
		result << "{";
		string lastCircuit = "";
		time_t maxLastUp = 0;
		for (auto it = messages.begin(); ret == RESULT_OK && it < messages.end();) {
			auto message = *it++;
			unsigned char dstAddress = message->getDstAddress();
			if (dstAddress == SYN)
				continue;
			if (pollPriority > 0 && message->setPollPriority(pollPriority))
				m_messages->addPollMessage(message);
			time_t lastup = message->getLastUpdateTime();
			if (lastup == 0 && required) {
				// read directly from bus
				if (message->isPassive())
					continue; // not possible to actively read this message
				if (readFromBus(message, "") != RESULT_OK)
					continue;
				lastup = message->getLastUpdateTime();
			} else {
				if (since > 0 && lastup <= since)
					continue;
				if (lastup > maxLastUp)
					maxLastUp = lastup;
			}
			if (message->getCircuit() != lastCircuit) {
				if (lastCircuit.length() > 0)
					result << "\n },";
				lastCircuit = message->getCircuit();
				result << "\n \"" << lastCircuit << "\": {";
				first = true;
			}
			if (first)
				first = false;
			else
				result << ",";
			result << "\n  \"" << message->getName() << "\": {";
			result << "\n   \"lastup\": " << setw(0) << dec << static_cast<unsigned>(lastup);
			if (lastup != 0) {
				result << ",\n   \"zz\": \"" << setfill('0') << setw(2) << hex << static_cast<unsigned>(dstAddress) << "\"";
				size_t pos = (size_t)result.tellp();
				result << ",\n   \"fields\": {";
				result_t dret = message->decodeLastData(result, (verbose?OF_VERBOSE:0)|(numeric?OF_NUMERIC:0)|OF_JSON);
				if (dret==RESULT_OK) {
					result << "\n   }";
				} else {
					string prefix = result.str().substr(0, pos);
					result.str("");
					result.clear(); // remove written fields
					result << prefix << ",\n   \"decodeerror\": \"" << getResultCode(dret) << "\"";
				}
			}
			result << ",\n   \"passive\": " << (message->isPassive() ? "true" : "false");
			result << ",\n   \"write\": " << (message->isWrite() ? "true" : "false");
			result << "\n  }";
		}

		if (ret == RESULT_OK) {
			if (lastCircuit.length() > 0)
				result << "\n },";
			result << "\n \"global\": {";
			result << "\n  \"signal\": " << (m_busHandler->hasSignal() ? "1" : "0");
			result << ",\n  \"lastup\": " << setw(0) << dec << static_cast<unsigned>(maxLastUp);
			result << "\n }";
			result << "\n}";
			type = 6;
		}
	} else {
		if (uri.length() < 1 || uri[0] != '/' || uri.find("//") != string::npos || uri.find("..") != string::npos) {
			ret = RESULT_ERR_INVALID_ARG;
		} else {
			string filename = m_htmlPath+uri;
			if (uri[uri.length()-1] == '/')
				filename += "index.html";

			size_t pos = filename.find_last_of('.');
			if (pos != string::npos && pos != filename.length() - 1 && pos >= filename.length() - 5) {
				string ext = filename.substr(pos+1);
				if (ext == "html") {
					type = 0;
				} else if (ext == "css") {
					type = 1;
				} else if (ext == "js") {
					type = 2;
				} else if (ext == "png") {
					type = 3;
				} else if (ext == "jpg" || ext == "jpeg") {
					type = 4;
				} else if (ext == "svg") {
					type = 5;
				} else if (ext == "json") {
					type = 6;
				}
			}
			if (type < 0) {
				ret = RESULT_ERR_NOTFOUND;
			} else {
				ifstream ifs;
				ifs.open(filename.c_str(), ifstream::in | ifstream::binary);
				if (!ifs.is_open()) {
					ret = RESULT_ERR_NOTFOUND;
				} else {
					ifs >> result.rdbuf();
					ifs.close();
				}
			}
		}
	}

	string data = ret==RESULT_OK ? result.str() : "";
	result.str("");
	result.clear();
	result << "HTTP/1.0 ";
	switch (ret) {
	case RESULT_OK:
		result << "200 OK\r\nContent-Type: ";
		switch (type) {
		case 1:
			result << "text/css";
			break;
		case 2:
			result << "application/javascript";
			break;
		case 3:
			result << "image/png";
			break;
		case 4:
			result << "image/jpeg";
			break;
		case 5:
			result << "image/svg+xml";
			break;
		case 6:
			result << "application/json;charset=utf-8";
			break;
		default:
			result << "text/html";
			break;
		}
		result << "\r\nContent-Length: " << setw(0) << dec << static_cast<unsigned>(data.length());
		break;
	case RESULT_ERR_NOTFOUND:
		result << "404 Not Found";
		break;
	case RESULT_ERR_INVALID_ARG:
	case RESULT_ERR_INVALID_NUM:
	case RESULT_ERR_OUT_OF_RANGE:
		result << "400 Bad Request";
		break;
	default:
		result << "500 Internal Server Error";
		break;
	}
	result << "\r\nServer: ebusd/" PACKAGE_VERSION "\r\n\r\n";
	result << data;
	connected = false;
	return result.str();
}

string MainLoop::getUpdates(time_t since, time_t until)
{
	ostringstream result;

	auto messages = m_messages->findAll("", "", false, true, true, true);

	for (auto it = messages.begin(); it < messages.end();) {
		auto message = *it++;
		unsigned char dstAddress = message->getDstAddress();
		if (dstAddress == SYN)
			continue;
		time_t lastchg = message->getLastChangeTime();
		if (lastchg < since || lastchg >= until)
			continue;
		result << message->getCircuit() << " " << message->getName() << " = ";
		message->decodeLastData(result);
		result << endl;
	}

	return result.str();
}
