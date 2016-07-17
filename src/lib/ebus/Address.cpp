#include "Address.h"
#include "symbol.h"

namespace libebus {

Address::Address(uint8_t binAddr)
:m_binAddr(binAddr)
{
}

bool Address::isMaster() const {
    unsigned char addrHi = (m_binAddr & 0xF0) >> 4;
    unsigned char addrLo = (m_binAddr & 0x0F);

    return ((addrHi == 0x0) || (addrHi == 0x1) || (addrHi == 0x3) || (addrHi == 0x7) || (addrHi == 0xF))
           && ((addrLo == 0x0) || (addrLo == 0x1) || (addrLo == 0x3) || (addrLo == 0x7) || (addrLo == 0xF));

}

bool Address::isSlaveMaster() const {
    return Address(m_binAddr + 256 - 5).isMaster();
}

Address Address::getMasterAddress() const {
    if (isMaster())
        return *this;

    Address addr(m_binAddr + 256 - 5);
    if (addr.isMaster())
        return addr;

    return Address(SYN);
}

uint8_t Address::getMasterNumber() const {
    unsigned char priority = getMasterPartIndex(m_binAddr & 0x0F);
    if (priority==0) return 0;
    unsigned char index = getMasterPartIndex((m_binAddr & 0xF0) >> 4);
    if (index==0) return 0;
    return (unsigned char)(5*(priority-1) + index);
}

bool Address::isValid(bool allowBroadcast) const
{
    return m_binAddr != SYN && m_binAddr != ESC && (allowBroadcast || m_binAddr != BROADCAST);
}

/**
* Returns the index of the upper or lower 4 bits of a master address.
* @param bits the upper or lower 4 bits of the address.
* @return the 1-based index of the upper or lower 4 bits of a master address (1 to 5), or 0.
*/
uint8_t Address::getMasterPartIndex(uint8_t bits) {
    switch (bits)
    {
        case 0x0:
            return 1;
        case 0x1:
            return 2;
        case 0x3:
            return 3;
        case 0x7:
            return 4;
        case 0xF:
            return 5;
        default:
            return 0;
    }
}

    bool Address::operator==(const Address &raddr) const {
        return m_binAddr == raddr.m_binAddr;
    }

    bool Address::operator!=(const Address &raddr) const {
        return ! (*this == raddr);
    }


}