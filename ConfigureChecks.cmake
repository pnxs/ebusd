include(CheckFunctionExists)

set(PACKAGE ${PACKAGE_NAME})
set(VERSION ${PACKAGE_VERSION})

check_function_exists(ppoll HAVE_PPOLL)
check_function_exists(pselect HAVE_PSELECT)
check_function_exists(pthread_setname_np HAVE_PTHREAD_SETNAME_NP)
