include_directories (
  ./include
)

set (SOURCES
    ./src/Base64.cpp
)

add_library("encryption" ${SOURCES})

if( BUILD_SHARED_LIBS )
  install (FILES ${CMAKE_CURRENT_BINARY_DIR}/libencryption.so DESTINATION lib)
endif()
