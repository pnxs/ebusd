
set(SOURCES
        result.cpp result.h
        symbol.cpp symbol.h
        filereader.h
        data.cpp data.h
        device.cpp device.h
        message.cpp message.h
        Address.cpp Address.h)

add_library(ebus ${SOURCES})