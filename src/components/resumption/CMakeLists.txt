include_directories (
  ./include
  ../utils/include/
  ../config_profile/include/
  ${CMAKE_SOURCE_DIR}/src/thirdPartyLibs/jsoncpp/include
)

set (SOURCES
  ./src/last_state.cc
)

add_library("Resumption" ${SOURCES})

if( BUILD_SHARED_LIBS )
  install (FILES ${CMAKE_CURRENT_BINARY_DIR}/libResumption.so DESTINATION lib)
endif()
