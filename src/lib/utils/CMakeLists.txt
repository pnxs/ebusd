
set(SOURCES
        log.cpp log.h
        tcpsocket.cpp tcpsocket.h
        thread.cpp thread.h
        clock.cpp clock.h
        queue.h
        notify.h
        cppconfig.h
        )

add_library(utils ${SOURCES})