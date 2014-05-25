include_directories (
  ./include
  ../utils/include/
  ../config_profile/include/
  ../../thirdPartyLibs/MessageBroker/include/
)

set (SOURCES
    ./src/watchdog.cc
    ./src/request_watchdog.cc
    ./src/request_info.cc
)

add_library("RequestWatchdog" ${SOURCES})
target_link_libraries ("RequestWatchdog" Utils)

if( BUILD_SHARED_LIBS )
  install (FILES ${CMAKE_CURRENT_BINARY_DIR}/libRequestWatchdog.so DESTINATION lib)
endif()
