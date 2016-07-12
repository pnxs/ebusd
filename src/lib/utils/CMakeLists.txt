
set(SOURCES log.cpp
log.h
tcpsocket.cpp
tcpsocket.h
thread.cpp
thread.h
clock.h
clock.cpp
queue.h
notify.h
        cppconfig.h)

add_library(utils ${SOURCES})