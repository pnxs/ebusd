#pragma once

#include <cstdint>

namespace libebus {

class Address {
public:
    Address() = default;
    Address(uint8_t binAddr);

    /**
    * Returns whether the address is one of the 25 master addresses.
    * @return <code>true</code> if the specified address is a master address.
    */
    bool isMaster() const;

    /**
    * Returns whether the address is a slave address of one of the 25 masters.
    * @return <code>true</code> if the specified address is a slave address of a master.
    */
    bool isSlaveMaster() const;

    /**
    * Returns the master address associated with the specified address (master or slave).
    * @return the master address, or SYN if the specified address is neither a master address nor a slave address of a master.
    */
    Address getMasterAddress() const;

    /**
    * Returns the number of the master if the address is a valid bus address.
    * @return the number of the master if the address is a valid bus address (1 to 25), or 0.
    */
    uint8_t getMasterNumber() const;

    /**
    * Returns whether the address is a valid bus address.
    * @param allowBroadcast whether to also allow @a addr to be the broadcast address (default true).
    * @return <code>true</code> if the specified address is a valid bus address.
    */
    bool isValid(bool allowBroadcast = true) const;

    uint8_t binAddr() const { return m_binAddr; }

    bool operator==(const Address& raddr) const;
    bool operator!=(const Address& raddr) const;

    /**
    * Returns the index of the upper or lower 4 bits of a master address.
    * @param bits the upper or lower 4 bits of the address.
    * @return the 1-based index of the upper or lower 4 bits of a master address (1 to 5), or 0.
    */
    static uint8_t getMasterPartIndex(uint8_t bits);


private:
    uint8_t m_binAddr = 0;
};

}