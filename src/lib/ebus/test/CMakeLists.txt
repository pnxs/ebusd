
add_executable(test_message test_message.cpp)
target_link_libraries(test_message ebus)

add_executable(test_data test_data.cpp)
target_link_libraries(test_data ebus)

add_executable(test_device test_device.cpp)
target_link_libraries(test_device ebus)

add_executable(test_filereader test_filereader.cpp)
target_link_libraries(test_filereader ebus)

add_executable(test_symbol test_symbol.cpp)
target_link_libraries(test_symbol ebus)
