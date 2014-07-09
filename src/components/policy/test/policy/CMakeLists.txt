include_directories(
  ./
  ./include
  ${CMAKE_SOURCE_DIR}/src/thirdPartyLibs/gmock-1.7.0/include
  ${CMAKE_SOURCE_DIR}/src/thirdPartyLibs/gmock-1.7.0/gtest/include
  ${CMAKE_SOURCE_DIR}/src/thirdPartyLibs/jsoncpp/include/
  ${CMAKE_SOURCE_DIR}/src/components/policy/src/policy/include/
  ${CMAKE_BINARY_DIR}/src/components/policy/src/policy/
  ${CMAKE_SOURCE_DIR}/src/components/rpc_base/include
  ${CMAKE_SOURCE_DIR}/src/components/policy/src/policy/sqlite_wrapper/include
  ${CMAKE_SOURCE_DIR}/src/components/policy/src/policy/usage_statistics/include
  ${CMAKE_SOURCE_DIR}/src/components/utils/include/
)

set(LIBRARIES
  gtest
  gtest_main
  gmock
  gmock_main
  dl
  Policy
  ConfigProfile
)

if (CMAKE_SYSTEM_NAME STREQUAL "QNX")
  list(REMOVE_ITEM LIBRARIES dl)
endif()

set(SHARED_LIBRARY_SOURCES
  ./src/test_shared_library.cc
)

set(SQL_PT_REPRESENTATION_SOURCES
  ./src/test_sql_pt_representation.cc
)

if (EXTENDED_POLICY_FLAG)
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SQL_PT_EXT_REPRESENTATION_SOURCES
        ./src/test_sql_pt_ext_representation.cc
    )
    create_test("test_SQLPTExtRepresentation" "${SQL_PT_EXT_REPRESENTATION_SOURCES}" "${LIBRARIES}")
  endif ()
endif ()

set(GENERATED_CODE_SOURCES
  ./src/generated_code_with_sqlite_test.cc
)

set(POLICY_MANAGER_IMPL_SOURCES
  ./src/test_policy_manager_impl.cc
)

set(STRESS_POLICY_MANAGER_IMPL_SOURCES
  ./src/test_stress_policy_manager_impl.cc
)

add_subdirectory(usage_statistics)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # --- Tests for SQLite Wrapper
    add_subdirectory(sqlite_wrapper)
    create_test("test_generated_code_with_sqlite" "${GENERATED_CODE_SOURCES}" "${LIBRARIES}")
    create_test("test_SQLPTRepresentation" "${SQL_PT_REPRESENTATION_SOURCES}" "${LIBRARIES}")
endif()

create_test("test_SharedLibrary" "${SHARED_LIBRARY_SOURCES}" "${LIBRARIES}")
create_test("test_PolicyManagerImpl" "${POLICY_MANAGER_IMPL_SOURCES}" "${LIBRARIES}")
create_test("test_stress_PolicyManagerImpl" "${STRESS_POLICY_MANAGER_IMPL_SOURCES}" "${LIBRARIES}")

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/log4cxx.properties DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
