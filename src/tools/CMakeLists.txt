
add_executable(ebusctl ebusctl.cpp)
target_link_libraries(ebusctl utils)

add_executable(ebusfeed ebusfeed.cpp)
target_link_libraries(ebusfeed utils ebus)

add_definitions(-DHAVE_CONFIG_H -DSYSCONFDIR=\"$(sysconfdir)\" -DLOCALSTATEDIR=\"$(localstatedir)\")