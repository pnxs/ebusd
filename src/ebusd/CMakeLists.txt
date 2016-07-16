
set(SOURCES bushandler.cpp
bushandler.h
network.cpp
network.h
mainloop.cpp
mainloop.h
main.h
main.cpp
)

add_executable(ebusd ${SOURCES})
target_link_libraries(ebusd utils ebus pthread)
add_definitions(-DHAVE_CONFIG_H -DSYSCONFDIR=\"$(sysconfdir)\" -DLOCALSTATEDIR=\"$(localstatedir)\")

# TODO: find out how to create directories on install
#install(TARGET ebusd DIRECTORY $(DESTDIR)$(sysconfdir)/ebusd)