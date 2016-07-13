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

#ifndef LIBEBUS_MESSAGE_H_
#define LIBEBUS_MESSAGE_H_

#include "data.h"
#include "result.h"
#include "symbol.h"
#include <string>
#include <vector>
#include <deque>
#include <map>

/** @file message.h
 * Classes and functions for decoding and encoding of complete messages on the
 * eBUS to and from readable values.
 *
 * A @a Message has a unique numeric key (see Message#getKey()) as well as a
 * unique name and circuit (see Message#getCircuit() and Message#getName()).
 * The numeric key is built from the message type (active/passive, read/write),
 * the source and destination address, the primary and secondary command byte,
 * as well as additional command ID bytes (see Message#getId()).
 *
 * Whenever a @a Message gets decoded from a master and slave @a SymbolString
 * (see Message#decode()), it stores these strings for later retrieval from
 * cache (see Message#decodeLastData()).
 *
 * In order to make a @a Message available (see Message#isAvailable()) under
 * certain conditions only, it may have assigned a @a Condition instance.
 *
 * A @a Condition is either a @a SimpleCondition referencing another
 * @a Message, numeric field, and field value, or a @a CombinedCondition
 * applying a logical AND on two or more other @a Condition instances.
 *
 * The @a MessageMap stores all @a Message and @a Condition instances by their
 * unique keys, and also keeps track of messages with polling enabled. It reads
 * the instances from configuration files by inheriting the @a FileReader
 * template class.
 */

class Condition;
class SimpleCondition;
class CombinedCondition;
class MessageMap;

/**
 * Defines parameters of a message sent or received on the bus.
 */
class Message
{
	friend class MessageMap;
public:

	/**
	 * Construct a new instance.
	 * @param circuit the optional circuit name.
	 * @param name the message name (unique within the same circuit and type).
	 * @param isWrite whether this is a write message.
	 * @param isPassive true if message can only be initiated by a participant other than us,
	 * false if message can be initiated by any participant.
	 * @param comment the comment.
	 * @param srcAddress the source address, or @a SYN for any (only relevant if passive).
	 * @param dstAddress the destination address, or @a SYN for any (set later).
	 * @param id the primary, secondary, and optional further ID bytes.
	 * @param data the @a DataField for encoding/decoding the message.
	 * @param deleteData whether to delete the @a DataField during destruction.
	 * @param pollPriority the priority for polling, or 0 for no polling at all.
	 * @param condition the @a Condition for this message, or NULL.
	 */
	Message(const string& circuit, const string& name,
			const bool isWrite, const bool isPassive, const string comment,
			const unsigned char srcAddress, const unsigned char dstAddress,
			const vector<unsigned char> id,
			shared_ptr<DataField> data, const bool deleteData,
			const unsigned char pollPriority,
			Condition* condition=NULL);

	/**
	 * Construct a new simple instance (e.g. for scanning).
	 * @param circuit the circuit name, or empty for not storing by name.
	 * @param name the message name (unique within the same circuit and type), or empty for not storing by name.
	 * @param isWrite whether this is a write message.
	 * @param isPassive true if message can only be initiated by a participant other than us,
	 * false if message can be initiated by any participant.
	 * @param pb the primary ID byte.
	 * @param sb the secondary ID byte.
	 * @param data the @a DataField for encoding/decoding the message.
	 * @param deleteData whether to delete the @a DataField during destruction.
	 */
	Message(const string& circuit, const string& name,
			const bool isWrite, const bool isPassive,
			const unsigned char pb, const unsigned char sb,
			shared_ptr<DataField> data, const bool deleteData);

	/**
	 * Destructor.
	 */
	virtual ~Message() { }

	/**
	 * Parse an ID part from the input @a string.
	 * @param input the input @a string, hex digits optionally separated by space.
	 * @param id the vector to which to add the parsed values.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	static result_t parseId(string input, vector<unsigned char>& id);

	/**
	 * Factory method for creating new instances.
	 * @param it the iterator to traverse for the definition parts.
	 * @param end the iterator pointing to the end of the definition parts.
	 * @param defaultsRows a @a vector with rows containing defaults, or NULL.
	 * @param condition the @a Condition instance for the message, or NULL.
	 * @param filename the name of the file being read.
	 * @param templates the @a DataFieldTemplates to be referenced by name, or NULL.
	 * @param messages the @a vector to which to add created instances.
	 * @return @a RESULT_OK on success, or an error code.
	 * Note: the caller needs to free the created instances.
	 */
	static result_t create(vector<string>::iterator& it, const vector<string>::iterator end,
			vector< vector<string> >* defaultsRows, Condition* condition, const string& filename,
			DataFieldTemplates* templates, vector<shared_ptr<Message>>& messages);

	/**
	 * Derive a new @a Message from this message.
	 * @param dstAddress the new destination address.
	 * @param srcAddress the new source address, or @a SYN to keep the current source address.
	 * @param circuit the new circuit name, or empty to use the current circuit name.
	 * @return the derived @a Message instance.
	 */
	virtual shared_ptr<Message> derive(const unsigned char dstAddress, const unsigned char srcAddress=SYN, const string circuit="");

	/**
	 * Derive a new @a Message from this message.
	 * @param dstAddress the new destination address.
	 * @param extendCircuit whether to extend the current circuit name with a dot and the new destination address in hex.
	 * @return the derived @a Message instance.
	 */
	shared_ptr<Message> derive(const unsigned char dstAddress, const bool extendCircuit);

	/**
	 * Get the optional circuit name.
	 * @return the optional circuit name.
	 */
	string getCircuit() const { return m_circuit; }

	/**
	 * Get the message name (unique within the same circuit and type).
	 * @return the message name (unique within the same circuit and type).
	 */
	string getName() const { return m_name; }

	/**
	 * Get whether this is a write message.
	 * @return whether this is a write message.
	 */
	bool isWrite() const { return m_isWrite; }

	/**
	 * Get whether message can be initiated only by a participant other than us.
	 * @return true if message can only be initiated by a participant other than us,
	 * false if message can be initiated by any participant.
	 */
	bool isPassive() const { return m_isPassive; }

	/**
	 * Get the comment.
	 * @return the comment.
	 */
	string getComment() const { return m_comment; }

	/**
	 * Get the source address.
	 * @return the source address, or @a SYN for any.
	 */
	unsigned char getSrcAddress() const { return m_srcAddress; }

	/**
	 * Get the destination address.
	 * @return the destination address, or @a SYN for any.
	 */
	unsigned char getDstAddress() const { return m_dstAddress; }

	/**
	 * Get the primary command byte.
	 * @return the primary command byte.
	 */
	unsigned char getPrimaryCommand() const { return m_id[0]; }

	/**
	 * Get the secondary command byte.
	 * @return the secondary command byte.
	 */
	unsigned char getSecondaryCommand() const { return m_id[1]; }

	/**
	 * Get the length of the ID bytes (without primary and secondary command bytes).
	 * @return the length of the ID bytes (without primary and secondary command bytes).
	 */
	virtual unsigned char getIdLength() const { return (unsigned char)(m_id.size() - 2); }

	/**
	 * Check if the full command ID starts with the given value.
	 * @param id the ID bytes to check against.
	 * @return true if the full command ID starts with the given value.
	 */
	bool checkIdPrefix(vector<unsigned char>& id);

	/**
	 * Check the ID against the master @a SymbolString data.
	 * @param master the master @a SymbolString to check against.
	 * @param index the variable in which to store the message part index, or NULL to ignore.
	 * @return true if the ID matches, false otherwise.
	 */
	virtual bool checkId(SymbolString& master, unsigned char* index=NULL);

	/**
	 * Check the ID against the other @a Message.
	 * @param other the other @a Message to check against.
	 * @return true if the ID matches, false otherwise.
	 */
	virtual bool checkId(Message& other);

	/**
	 * Return the key for storing in @a MessageMap.
	 * @return the key for storing in @a MessageMap.
	 */
	unsigned long long getKey() { return m_key; }

	/**
	 * Return the derived key for storing in @a MessageMap.
	 * @param dstAddress the destination address for the derivation.
	 * @return the derived key for storing in @a MessageMap.
	 */
	unsigned long long getDerivedKey(const unsigned char dstAddress);

	/**
	 * Get the polling priority, or 0 for no polling at all.
	 * @return the polling priority, or 0 for no polling at all.
	 */
	unsigned char getPollPriority() const { return m_pollPriority; }

	/**
	 * Set the polling priority.
	 * @param priority the polling priority, or 0 for no polling at all.
	 * @return true when the priority was changed and polling was not enabled before, false otherwise.
	 */
	bool setPollPriority(unsigned char priority);

	/**
	 * Set the poll priority suitable for resolving a @a Condition.
	 */
	void setUsedByCondition();

	/**
	 * Return whether this @a Message depends on a @a Condition.
	 * @return true when this @a Message depends on a @a Condition.
	 */
	bool isConditional() const { return m_condition!=NULL; }

	/**
	 * Return whether this @a Message is available (optionally depending on a @a Condition evaluation).
	 * @return true when this @a Message is available.
	 */
	bool isAvailable();

	/**
	 * Return whether the field is available.
	 * @param fieldName the name of the field to find, or NULL for any.
	 * @param numeric true for a numeric field, false for a string field.
	 * @return true if the field is available.
	 */
	bool hasField(const char* fieldName, bool numeric=true);

	/**
	 * @return the number of parts this message is composed of.
	 */
	virtual unsigned char getCount() { return 1; }

	/**
	 * Prepare the master @a SymbolString for sending a query or command to the bus.
	 * @param srcAddress the source address to set.
	 * @param masterData the master data @a SymbolString for writing symbols to.
	 * @param input the @a istringstream to parse the formatted value(s) from.
	 * @param separator the separator character between multiple fields.
	 * @param dstAddress the destination address to set, or @a SYN to keep the address defined during construction.
	 * @param index the index of the part to prepare.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	result_t prepareMaster(const unsigned char srcAddress, SymbolString& masterData,
			istringstream& input, char separator=UI_FIELD_SEPARATOR,
			const unsigned char dstAddress=SYN, unsigned char index=0);

protected:

	/**
	 * Prepare a part of the master data @a SymbolString for sending (everything including NN).
	 * @param master the master data @a SymbolString for writing symbols to.
	 * @param input the @a istringstream to parse the formatted value(s) from.
	 * @param separator the separator character between multiple fields.
	 * @param index the index of the part to prepare.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t prepareMasterPart(SymbolString& master, istringstream& input, char separator, unsigned char index);

public:

	/**
	 * Prepare the slave @a SymbolString for sending an answer to the bus.
	 * @param input the @a istringstream to parse the formatted value(s) from.
	 * @param slaveData the slave data @a SymbolString for writing symbols to.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t prepareSlave(istringstream& input, SymbolString& slaveData);

	/**
	 * Store the last seen master and slave data.
	 * @param master the last seen master data.
	 * @param slave the last seen slave data.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t storeLastData(SymbolString& master, SymbolString& slave);

	/**
	 * Store last seen master or slave data.
	 * @param partType the @a PartType of the data.
	 * @param data the last seen data.
	 * @param index the index of the part to store.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t storeLastData(const PartType partType, SymbolString& data, unsigned char index);

	/**
	 * Decode the value from the last stored data.
	 * @param partType the @a PartType of the data.
	 * @param output the @a ostringstream to append the formatted value to.
	 * @param outputFormat the @a OutputFormat options to use.
	 * @param leadingSeparator whether to prepend a separator before the formatted value.
	 * @param fieldName the optional name of a field to limit the output to.
	 * @param fieldIndex the optional index of the named field to limit the output to, or -1.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t decodeLastData(const PartType partType,
			ostringstream& output, OutputFormat outputFormat=0,
			bool leadingSeparator=false, const char* fieldName=NULL, signed char fieldIndex=-1);

	/**
	 * Decode the value from the last stored data.
	 * @param output the @a ostringstream to append the formatted value to.
	 * @param outputFormat the @a OutputFormat options to use.
	 * @param leadingSeparator whether to prepend a separator before the formatted value.
	 * @param fieldName the optional name of a field to limit the output to.
	 * @param fieldIndex the optional index of the named field to limit the output to, or -1.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t decodeLastData(ostringstream& output, OutputFormat outputFormat=0,
			bool leadingSeparator=false, const char* fieldName=NULL, signed char fieldIndex=-1);

	/**
	 * Decode a particular numeric field value from the last stored data.
	 * @param output the variable in which to store the value.
	 * @param fieldName the name of the field to decode, or NULL for the first field.
	 * @param fieldIndex the optional index of the named field, or -1.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t decodeLastDataNumField(unsigned int& output, const char* fieldName, signed char fieldIndex=-1);

	/**
	 * Get the last seen master data.
	 * @return the last seen master @a SymbolString.
	 */
	SymbolString& getLastMasterData() { return m_lastMasterData; }

	/**
	 * Get the last seen slave data.
	 * @return the last seen slave @a SymbolString.
	 */
	SymbolString& getLastSlaveData() { return m_lastSlaveData; }

	/**
	 * Get the time when @a m_lastValue was last stored.
	 * @return the time when @a m_lastValue was last stored, or 0 if this message was not decoded yet.
	 */
	time_t getLastUpdateTime() { return m_lastUpdateTime; }

	/**
	 * Get the time when @a m_lastValue was last changed.
	 * @return the time when @a m_lastValue was last changed, or 0 if this message was not decoded yet.
	 */
	time_t getLastChangeTime() { return m_lastChangeTime; }

	/**
	 * Get the time when this message was last polled for.
	 * @return the time when this message was last polled for, or 0 for never.
	 */
	time_t getLastPollTime() { return m_lastPollTime; }

	/**
	 * Return whether this @a Message needs to be polled after the other one.
	 * @param other the other @a Message to compare with.
	 * @return true if this @a Message needs to be polled after the other one.
	 */
	bool isLessPollWeight(const Message* other);

	/**
	 * Write the message definition or parts of it to the @a ostream.
	 * @param output the @a ostream to append the formatted value to.
	 * @param columns the list of column indexes to write, or NULL for all.
	 * @param withConditions whether to include the optional conditions prefix.
	 */
	void dump(ostream& output, vector<size_t>* columns=NULL, bool withConditions=false);

protected:

	/**
	 * Write the specified column to the @a ostream.
	 * @param output the @a ostream to append the formatted value to.
	 * @param column the column indexes to write.
	 * @param withConditions whether to include the optional conditions prefix.
	 */
	virtual void dumpColumn(ostream& output, size_t column, bool withConditions);

	/** the optional circuit name. */
	const string m_circuit;

	/** the message name (unique within the same circuit and type). */
	const string m_name;

	/** whether this is a write message. */
	const bool m_isWrite;

	/** true if message can only be initiated by a participant other than us,
	 * false if message can be initiated by any participant. */
	const bool m_isPassive;

	/** the comment. */
	const string m_comment;

	/** the source address, or @a SYN for any (only relevant if passive). */
	const unsigned char m_srcAddress;

	/** the destination address, or @a SYN for any (only for temporary scan messages). */
	const unsigned char m_dstAddress;

	/** the primary, secondary, and optionally further command ID bytes. */
	vector<unsigned char> m_id;

	/**
	 * the key for storing in @a MessageMap.
	 * <ul>
	 * <li>byte 7:
	 *  <ul>
	 *   <li>bits 5-7: length of ID bytes (without PB/SB)</li>
	 *   <li>bits 0-4:
	 *    <ul>
	 *     <li>master number (1..25) of sender for passive message</li>
	 *     <li>0x00 for passive message with any sender</li>
	 *     <li>0x1f for active write</li>
	 *     <li>0x1e for active read</li>
	 *    </ul>
	 *  </ul>
	 * </li>
	 * <li>byte 6: ZZ or SYN for any</li>
	 * <li>byte 5: PB</li>
	 * <li>byte 4: SB</li>
	 * <li>bytes 3-0: ID bytes (with cyclic xor if more than 4)</li>
	 * </ul>
	 */
	unsigned long long m_key;

	/** the @a DataField for encoding/decoding the message. */
	shared_ptr<DataField> m_data;

	/** whether to delete the @a DataField during destruction. */
	const bool m_deleteData;

	/** the priority for polling, or 0 for no polling at all. */
	unsigned char m_pollPriority;

	/** whether this message is used by a @a Condition. */
	bool m_usedByCondition;

	/** the @a Condition for this message, or NULL. */
	Condition* m_condition;

	/** the last seen master data. */
	SymbolString m_lastMasterData;

	/** the last seen slave data. */
	SymbolString m_lastSlaveData;

	/** the system time when the message was last updated, 0 for never. */
	time_t m_lastUpdateTime;

	/** the system time when the message content was last changed, 0 for never. */
	time_t m_lastChangeTime;

	/** the number of times this messages was already polled for. */
	unsigned int m_pollCount;

	/** the system time when this message was last polled for, 0 for never. */
	time_t m_lastPollTime;

};


/**
 * A chained @a Message that needs more than one read/write on the bus to collect/send the data.
 */
class ChainedMessage : public Message
{
public:

	/**
	 * Construct a new instance.
	 * @param circuit the optional circuit name.
	 * @param name the message name (unique within the same circuit and type).
	 * @param isWrite whether this is a write message.
	 * @param comment the comment.
	 * @param srcAddress the source address, or @a SYN for any (only relevant if passive).
	 * @param dstAddress the destination address, or @a SYN for any (set later).
	 * @param id the primary, secondary, and optional further ID bytes common to each part of the chain.
	 * @param ids the primary, secondary, and optional further ID bytes for each part of the chain.
	 * @param lengths the data length for each part of the chain.
	 * @param data the @a DataField for encoding/decoding the chained message.
	 * @param deleteData whether to delete the @a DataField during destruction.
	 * @param pollPriority the priority for polling, or 0 for no polling at all.
	 * @param condition the @a Condition for this message, or NULL.
	 */
	ChainedMessage(const string circuit, const string name,
			const bool isWrite, const string comment,
			const unsigned char srcAddress, const unsigned char dstAddress,
			const vector<unsigned char> id,
			vector< vector<unsigned char> > ids, vector<unsigned char> lengths,
			shared_ptr<DataField> data, const bool deleteData,
			const unsigned char pollPriority,
			Condition* condition=NULL);

	virtual ~ChainedMessage();

	// @copydoc
	virtual shared_ptr<Message> derive(const unsigned char dstAddress, const unsigned char srcAddress=SYN, const string circuit="");

	// @copydoc
	virtual unsigned char getIdLength() const { return (unsigned char)(m_ids[0].size() - 2); }

	// @copydoc
	virtual bool checkId(SymbolString& master, unsigned char* index=NULL);

	// @copydoc
	virtual bool checkId(Message& other);

	// @copydoc
	virtual unsigned char getCount() { return (unsigned char)m_ids.size(); }

protected:

	// @copydoc
	virtual result_t prepareMasterPart(SymbolString& master, istringstream& input, char separator, unsigned char index);

public:

	// @copydoc
	virtual result_t storeLastData(SymbolString& master, SymbolString& slave);

	// @copydoc
	virtual result_t storeLastData(const PartType partType, SymbolString& data, unsigned char index);

protected:

	// @copydoc
	virtual void dumpColumn(ostream& output, size_t column, bool withConditions);

private:

	/** the primary, secondary, and optional further ID bytes for each part of the chain. */
	const vector< vector<unsigned char> > m_ids;

	/** the data length for each part of the chain. */
	const vector<unsigned char> m_lengths;

	/** the maximum allowed time difference of any data pair. */
	const time_t m_maxTimeDiff;

	/** array of the last seen master datas. */
	SymbolString** m_lastMasterDatas;

	/** array of the last seen slave datas. */
	SymbolString** m_lastSlaveDatas;

	/** array of the system times when the corresponding master data was last updated, 0 for never. */
	time_t* m_lastMasterUpdateTimes;

	/** array of the system times when the corresponding slave data was last updated, 0 for never. */
	time_t* m_lastSlaveUpdateTimes;

};


/**
 * A function that compares the weighted poll priority of two @a Message instances.
 */
struct compareMessagePriority : binary_function <shared_ptr<Message>,shared_ptr<Message>,bool> {
	/**
	 * Compare the weighted poll priority of the two @a Message instances.
	 * @param x the first @a Message.
	 * @param y the second @a Message.
	 * @return whether @a x is smaller than @a y with regard to their weighted poll priority.
	 */
	bool operator() (shared_ptr<Message> x, shared_ptr<Message> y) const { return x->isLessPollWeight(y.get()); };
};


/**
 * Helper class extending @a priority_queue to hold distinct values only.
 */
class MessagePriorityQueue
	: public priority_queue<shared_ptr<Message>, vector<shared_ptr<Message>>, compareMessagePriority>
{
public:
	/**
	 * Add data to the queue and ensure it is contained only once.
	 * @param __x the element to add.
	 */
	void push(const value_type& __x)
	{
		for (auto it = c.begin(); it != c.end(); it++) {
			if (*it==__x) {
				c.erase(it);
				break;
			}
		}
		priority_queue<shared_ptr<Message>, vector<shared_ptr<Message>>, compareMessagePriority>::push(__x);
	}
};


/**
 * An abstract condition based on the value of one or more @a Message instances.
 */
class Condition
{
public:

	/**
	 * Construct a new instance.
	 */
	Condition()
		: m_lastCheckTime(0), m_isTrue(false) { }

	/**
	 * Destructor.
	 */
	virtual ~Condition() { }

	/**
	 * Factory method for creating a new instance.
	 * @param condName the name of the condition.
	 * @param it the iterator to traverse for the definition parts.
	 * @param end the iterator pointing to the end of the definition parts.
	 * @param defaultDest the valid destination address extracted from the file name (from ZZ part), or empty.
	 * @param defaultCircuit the valid circuit name extracted from the file name (from IDENT part), or empty.
	 * @param returnValue the variable in which to store the created instance.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	static result_t create(const string condName, vector<string>::iterator& it, const vector<string>::iterator end, string defaultDest, string defaultCircuit, SimpleCondition*& returnValue);

	/**
	 * Derive a new @a SimpleCondition from this condition.
	 * @param valueList the @a string with the new list of values.
	 * @return the derived @a SimpleCondition instance, or NULL if the value list is invalid.
	 */
	virtual SimpleCondition* derive(string valueList) { return NULL; };

	/**
	 * Write the condition definition to the @a ostream.
	 * @param output the @a ostream to append to.
	 */
	virtual void dump(ostream& output) = 0;

	/**
	 * Combine this condition with another instance using a logical and.
	 * @param other the @a Condition to combine with.
	 * @return the @a CombinedCondition instance.
	 */
	virtual CombinedCondition* combineAnd(Condition* other) = 0;

	/**
	 * Resolve the referred @a Message instance(s) and field index(es).
	 * @param messages the @a MessageMap instance for resolving.
	 * @param errorMessage a @a ostringstream to which to add optional error messages.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t resolve(MessageMap* messages, ostringstream& errorMessage) = 0;

	/**
	 * Check and return whether this condition is fulfilled.
	 * @return whether this condition is fulfilled.
	 */
	virtual bool isTrue() = 0;

protected:

	/** the system time when the condition was last checked, 0 for never. */
	time_t m_lastCheckTime;

	/** whether the condition was @a true during the last check. */
	bool m_isTrue;

};


/**
 * A simple @a Condition based on the value of one @a Message.
 */
class SimpleCondition : public Condition
{
public:

	/**
	 * Construct a new instance.
	 * @param condName the name of the condition.
	 * @param circuit the circuit name.
	 * @param name the message name, or empty for scan message.
	 * @param dstAddress the override destination address, or @a SYN (only for @a Message without specific destination as well as scan message).
	 * @param field the field name.
	 * @param hasValues whether a value has to be checked against.
	 */
	SimpleCondition(const string condName, const string circuit, const string name, const unsigned char dstAddress, const string field, const bool hasValues=false)
		: Condition(),
		  m_condName(condName), m_circuit(circuit), m_name(name), m_dstAddress(dstAddress), m_field(field), m_hasValues(hasValues), m_message(NULL) { }

	/**
	 * Destructor.
	 */
	virtual ~SimpleCondition() {}

	// @copydoc
	virtual SimpleCondition* derive(string valueList);

	// @copydoc
	virtual void dump(ostream& output);

	// @copydoc
	virtual CombinedCondition* combineAnd(Condition* other);

	/**
	 * Resolve the referred @a Message instance(s) and field index(es).
	 * @param messages the @a MessageMap instance for resolving.
	 * @param errorMessage a @a ostringstream to which to add optional error messages.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t resolve(MessageMap* messages, ostringstream& errorMessage);

	// @copydoc
	virtual bool isTrue();

	/**
	 * Return whether the condition is based on a numeric value.
	 * @return whether the condition is based on a numeric value.
	 */
	virtual bool isNumeric() { return true; }

protected:

	/**
	 * Check the values against the field in the @a Message.
	 * @param message the @a Message to check against.
	 * @param field the field name to check against, or empty for first field.
	 * @return whether the field matches one of the valid values.
	 */
	virtual bool checkValue(Message* message, const string field) { return true; }

private:

	/** the condition name. */
	const string m_condName;

	/** the circuit name. */
	const string m_circuit;

	/** the message name, or empty for scan message. */
	const string m_name;

	/** the override destination address, or @a SYN (only for @a Message without specific destination as well as scan message). */
	const unsigned char m_dstAddress;

	/** the field name, or empty for first field. */
	const string m_field;

	/** whether a value has to be checked against. */
	const bool m_hasValues;

	/** the resolved @a Message instance, or NULL. */
	shared_ptr<Message> m_message;

};


/**
 * A simple @a Condition based on the numeric value of one @a Message.
 */
class SimpleNumericCondition : public SimpleCondition
{
public:

	/**
	 * Construct a new instance.
	 * @param condName the name of the condition.
	 * @param circuit the circuit name.
	 * @param name the message name, or empty for scan message.
	 * @param dstAddress the override destination address, or @a SYN (only for @a Message without specific destination as well as scan message).
	 * @param field the field name.
	 * @param valueRanges the valid value ranges (pairs of from/to inclusive), empty for @a m_message seen check.
	 */
	SimpleNumericCondition(const string condName, const string circuit, const string name, const unsigned char dstAddress, const string field, const vector<unsigned int> valueRanges)
		: SimpleCondition(condName, circuit, name, dstAddress, field, true),
		  m_valueRanges(valueRanges) { }

	/**
	 * Destructor.
	 */
	virtual ~SimpleNumericCondition() {}

protected:

	// @copydoc
	virtual bool checkValue(Message* message, const string field);

private:

	/** the valid value ranges (pairs of from/to inclusive), empty for @a m_message seen check. */
	const vector<unsigned int> m_valueRanges;

};


/**
 * A simple @a Condition based on the string value of one @a Message.
 */
class SimpleStringCondition : public SimpleCondition
{
public:

	/**
	 * Construct a new instance.
	 * @param condName the name of the condition.
	 * @param circuit the circuit name.
	 * @param name the message name, or empty for scan message.
	 * @param dstAddress the override destination address, or @a SYN (only for @a Message without specific destination as well as scan message).
	 * @param field the field name.
	 * @param values the valid values.
	 */
	SimpleStringCondition(const string condName, const string circuit, const string name, const unsigned char dstAddress, const string field, const vector<string> values)
		: SimpleCondition(condName, circuit, name, dstAddress, field, true),
		  m_values(values) { }

	/**
	 * Destructor.
	 */
	virtual ~SimpleStringCondition() {}

	// @copydoc
	virtual bool isNumeric() { return false; }

protected:

	// @copydoc
	virtual bool checkValue(Message* message, const string field);

private:

	/** the valid values. */
	const vector<string> m_values;

};


/**
 * A @a Condition combining two or more @a SimpleCondition instances with a logical and.
 */
class CombinedCondition : public Condition
{
public:

	/**
	 * Construct a new instance.
	 */
	CombinedCondition()
		: Condition() { }

	/**
	 * Destructor.
	 */
	virtual ~CombinedCondition() {}

	// @copydoc
	virtual void dump(ostream& output);

	// @copydoc
	virtual CombinedCondition* combineAnd(Condition* other) { m_conditions.push_back(other); return this; }

	// @copydoc
	virtual result_t resolve(MessageMap* messages, ostringstream& errorMessage);

	// @copydoc
	virtual bool isTrue();

private:

	/** the @a Condition instances used. */
	vector<Condition*> m_conditions;

};


/**
 * An abstract instruction based on the value of one or more @a Message instances.
 */
class Instruction
{
public:

	/**
	 * Construct a new instance.
	 * @param condition the @a Condition this instruction requires, or null.
	 * @param singleton whether this @a Instruction belongs to a set of instructions of which only the first one may be executed for the same source file.
	 * @param defaultDest the default destination address, or empty.
	 * @param defaultCircuit the default circuit name, or empty.
	 * @param defaultSuffix the default circuit name suffix (starting with a "."), or empty.
	 */
	Instruction(Condition* condition, const bool singleton, const string& defaultDest, const string& defaultCircuit, const string& defaultSuffix)
		: m_condition(condition), m_singleton(singleton), m_defaultDest(defaultDest), m_defaultCircuit(defaultCircuit), m_defaultSuffix(defaultSuffix) { }

	/**
	 * Destructor.
	 */
	virtual ~Instruction() { }

	/**
	 * Factory method for creating a new instance.
	 * @param contextPath the path and/or filename context being loaded.
	 * @param defaultDest the default destination address, or empty.
	 * @param defaultCircuit the default circuit name, or empty.
	 * @param defaultSuffix the default circuit name suffix (starting with a "."), or empty.
	 * @param condition the @a Condition for the instruction, or NULL.
	 * @param type the type of the instruction.
	 * @param it the iterator to traverse for the definition parts.
	 * @param end the iterator pointing to the end of the definition parts.
	 * @param returnValue the variable in which to store the created instance.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	static result_t create(const string contextPath, const string& defaultDest, const string& defaultCircuit, const string& defaultSuffix,
		Condition* condition, const string type, vector<string>::iterator& it, const vector<string>::iterator end, Instruction*& returnValue);

	/**
	 * Return the @a Condition this instruction requires.
	 * @return the @a Condition this instruction requires, or null.
	 */
	Condition* getCondition() { return m_condition; }

	/**
	 * Return whether this @a Instruction belongs to a set of instructions of which only the first one may be executed for the same source file.
	 * @return whether this @a Instruction belongs to a set of instructions of which only the first one may be executed for the same source file.
	 */
	bool isSingleton() { return m_singleton; }

	/**
	 * Return a string describing the destination from the stored default values.
	 * @return a string describing the destination.
	 */
	string getDestination();

	/**
	 * Execute the instruction.
	 * @param messages the @a MessageMap.
	 * @param log the @a ostringstream to log success messages to (if necessary).
	 * @param loadInfoFunc the function to call for successful loading of a file for a certain address, or NULL.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	virtual result_t execute(MessageMap* messages, ostringstream& log, void (*loadInfoFunc)(MessageMap* messages, const unsigned char address, string filename)=NULL) = 0;

private:

	/** the @a Condition this instruction requires, or null. */
	Condition* m_condition;

	/** whether this @a Instruction belongs to a set of instructions of which only the first one may be executed for the same source file. */
	bool m_singleton;

protected:

	/** the default destination address, or empty. */
	const string m_defaultDest;

	/** the default circuit name, or empty. */
	const string m_defaultCircuit;

	/** the default circuit name suffix (starting with a "."), or empty. */
	const string m_defaultSuffix;

};


/**
 * An @a Instruction allowing to load another file.
 */
class LoadInstruction : public Instruction
{
public:

	/**
	 * Construct a new instance.
	 * @param condition the @a Condition this instruction requires, or null.
	 * @param singleton whether this @a Instruction belongs to a set of instructions of which only the first one may be executed for the same source file.
	 * @param defaultDest the default destination address (may be overwritten by file name), or empty.
	 * @param defaultCircuit the default circuit name (may be overwritten by file name), or empty.
	 * @param defaultSuffix the default circuit name suffix (starting with a ".", may be overwritten by file name), or empty.
	 * @param filename the name of the file to load.
	 */
	LoadInstruction(Condition* condition, const bool singleton, const string& defaultDest, const string& defaultCircuit, const string& defaultSuffix, const string filename)
		: Instruction(condition, singleton, defaultDest, defaultCircuit, defaultSuffix), m_filename(filename) { }

	/**
	 * Destructor.
	 */
	virtual ~LoadInstruction() { }

	// @copydoc
	virtual result_t execute(MessageMap* messages, ostringstream& log, void (*loadInfoFunc)(MessageMap* messages, const unsigned char address, string filename)=NULL);

private:

	/** the name of the file to load. */
	const string m_filename;

};


/**
 * Holds a map of all known @a Message instances.
 */
class MessageMap : public FileReader
{
public:

	/**
	 * Construct a new instance.
	 * @param addAll whether to add all messages, even if duplicate.
	 */
	MessageMap(const bool addAll=false) : FileReader::FileReader(true),
		m_addAll(addAll), m_maxIdLength(0), m_messageCount(0), m_conditionalMessageCount(0), m_passiveMessageCount(0)
	{
		m_scanMessage = make_shared<Message>("scan", "ident", false, false, 0x07, 0x04, DataFieldSet::getIdentFields(), true);
	}

	/**
	 * Destructor.
	 */
	virtual ~MessageMap() {
		clear();
	}

	/**
	 * Add a @a Message instance to this set.
	 * @param message the @a Message instance to add.
	 * @param storeByName whether to store the @a Message by name.
	 * @return @a RESULT_OK on success, or an error code.
	 * Note: the caller may not free the added instance on success.
	 */
	result_t add(shared_ptr<Message> message, bool storeByName=true);

	// @copydoc
	virtual result_t addDefaultFromFile(vector< vector<string> >& defaults, vector<string>& row,
		vector<string>::iterator& begin, string defaultDest, string defaultCircuit, string defaultSuffix,
		const string& filename, unsigned int lineNo);

	/**
	 * Read the @a Condition instance(s) from the types field.
	 * @param types the field from which to read the @a Condition instance(s).
	 * @param filename the name of the file being read.
	 * @param condition the variable in which to store the result.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	result_t readConditions(string& types, const string& filename, Condition*& condition);

	// @copydoc
	virtual result_t addFromFile(vector<string>::iterator& begin, const vector<string>::iterator end,
		vector< vector<string> >* defaults, const string& defaultDest, const string& defaultCircuit, const string& defaultSuffix,
		const string& filename, unsigned int lineNo);

	/**
	 * Get the scan @a Message instance for the specified address.
	 * @param dstAddress the destination address, or @a SYN for the base scan @a Message.
	 * @return the scan @a Message instance, or NULL if the dstAddress is no slave.
	 */
	shared_ptr<Message> getScanMessage(const unsigned char dstAddress=SYN);

	/**
	 * Resolve all @a Condition instances.
	 * @param verbose whether to verbosely add all problems to the error message.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	result_t resolveConditions(bool verbose=false);

	/**
	 * Resolve a @a Condition.
	 * @param condition the @a Condition to resolve.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	result_t resolveCondition(Condition* condition);

	/**
	 * Run all executable @a Instruction instances.
	 * @param log the @a ostringstream to log success messages to (if necessary).
	 * @param loadInfoFunc the function to call for successful loading of a file for a participant, or NULL.
	 * @return @a RESULT_OK on success, or an error code.
	 */
	result_t executeInstructions(ostringstream& log, void (*loadInfoFunc)(MessageMap* messages, const unsigned char address, string file)=NULL);

	/**
	 * Add a loaded file to a participant.
	 * @param address the slave address.
	 * @param file the name of the file from which a configuration part was loaded for the participant.
	 */
	void addLoadedFile(unsigned char address, string file);

	/**
	 * Get the loaded files for a participant.
	 * @param address the slave address.
	 * @return the name of the file(s) loaded for the participant (separated by comma and enclosed in double quotes), or empty.
	 */
	string getLoadedFiles(unsigned char address);

	/**
	 * Get the stored @a Message instances for the key.
	 * @param key the key of the @a Message.
	 * @return the found @a Message instances, or NULL.
	 * Note: the caller may not free the returned instances.
	 */
	vector<shared_ptr<Message>>* getByKey(const unsigned long long key);

	/**
	 * Find the @a Message instance for the specified circuit and name.
	 * @param circuit the optional circuit name.
	 * @param name the message name.
	 * @param isWrite whether this is a write message.
	 * @param isPassive whether this is a passive message.
	 * @return the @a Message instance, or NULL.
	 * Note: the caller may not free the returned instance.
	 */
	shared_ptr<Message> find(const string& circuit, const string& name, const bool isWrite, const bool isPassive=false);

	/**
	 * Find all active get @a Message instances for the specified circuit and name.
	 * @param circuit the circuit name, or empty for any.
	 * @param name the message name, or empty for any.
	 * @param completeMatch false to also include messages where the circuit and name matches only a part of the given circuit and name (default true).
	 * @param withRead true to include read messages (default true).
	 * @param withWrite true to include write messages (default false).
	 * @param withPassive true to include passive messages (default false).
	 * @return the found @a Message instances.
	 * Note: the caller may not free the returned instances.
	 */
	deque<shared_ptr<Message>> findAll(const string& circuit, const string& name, const bool completeMatch=true,
		const bool withRead=true, const bool withWrite=false, const bool withPassive=false);

	/**
	 * Find the @a Message instance for the specified master data.
	 * @param master the master @a SymbolString for identifying the @a Message.
	 * @param anyDestination true to only return messages without a particular destination.
	 * @param withRead true to include read messages (default true).
	 * @param withWrite true to include write messages (default true).
	 * @param withPassive true to include passive messages (default true).
	 * @return the @a Message instance, or NULL.
	 * Note: the caller may not free the returned instance.
	 */
	shared_ptr<Message> find(SymbolString& master, bool anyDestination=false,
		const bool withRead=true, const bool withWrite=true, const bool withPassive=true);

	/**
	 * Invalidate cached data of the @a Message and all other instances with a matching name key.
	 * @param message the @a Message to invalidate.
	 */
	void invalidateCache(shared_ptr<Message> message);

	/**
	 * Add a @a Message to the list of instances to poll.
	 * @param message the @a Message to poll.
	 * @param toFront whether to add the @a Message to the very front of the poll queue.
	 */
	void addPollMessage(shared_ptr<Message> message, bool toFront=false);

	/**
	 * Removes all @a Message instances.
	 */
	void clear();

	/**
	 * Get the number of all stored @a Message instances.
	 * @return the the number of all stored @a Message instances.
	 */
	size_t size() { return m_messageCount; }

	/**
	 * Get the number of stored conditional @a Message instances.
	 * @return the the number of stored conditional @a Message instances.
	 */
	size_t sizeConditional() { return m_conditionalMessageCount; }

	/**
	 * Get the number of stored passive @a Message instances.
	 * @return the the number of stored passive @a Message instances.
	 */
	size_t sizePassive() { return m_passiveMessageCount; }

	/**
	 * Get the number of stored @a Message instances with a poll priority.
	 * @return the the number of stored @a Message instances with a poll priority.
	 */
	size_t sizePoll() { return m_pollMessages.size(); }

	/**
	 * Get the next @a Message to poll.
	 * @return the next @a Message to poll, or NULL.
	 * Note: the caller may not free the returned instance.
	 */
	shared_ptr<Message> getNextPoll();

	/**
	 * Get the number of stored @a Condition instances.
	 * @return the number of stored @a Condition instances.
	 */
	size_t sizeConditions() { return m_conditions.size(); }

	/**
	 * Get the stored @a Condition instances.
	 * @return the @a Condition instances by filename and condition name.
	 */
	map<string, Condition*>& getConditions() { return m_conditions; }

	/**
	 * Write the message definitions to the @a ostream.
	 * @param output the @a ostream to append the formatted messages to.
	 * @param withConditions whether to include the optional conditions prefix.
	 */
	void dump(ostream& output, bool withConditions=false);

private:

	/** whether to add all messages, even if duplicate. */
	const bool m_addAll;

	/** the @a Message instance used for scanning. */
	shared_ptr<Message> m_scanMessage;

	/** the loaded configuration files by slave address. */
	map<unsigned char, string> m_loadedFiles;

	/** the maximum ID length used by any of the known @a Message instances. */
	unsigned char m_maxIdLength;

	/** the number of distinct @a Message instances stored in @a m_messagesByName. */
	size_t m_messageCount;

	/** the number of conditional @a Message instances part of @a m_messageCount. */
	size_t m_conditionalMessageCount;

	/** the number of distinct passive @a Message instances stored in @a m_messagesByKey. */
	size_t m_passiveMessageCount;

	/** the known @a Message instances by lowercase circuit and name. */
	map<string, vector<shared_ptr<Message>> > m_messagesByName;

	/** the known @a Message instances by key. */
	map<unsigned long long, vector<shared_ptr<Message>> > m_messagesByKey;

	/** the known @a Message instances to poll, by priority. */
	MessagePriorityQueue m_pollMessages;

	/** the @a Condition instances by filename and condition name. */
	map<string, Condition*> m_conditions;

	/** the list of @a Instruction instances by filename. */
	map<string, vector<Instruction*> > m_instructions;

};

#endif // LIBEBUS_MESSAGE_H_
