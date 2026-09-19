set(MSAFLOW_UNIT_TEST_SOURCES
  unit/test_types.cpp
  unit/test_query_registry.cpp
  unit/test_block_aggregator.cpp
  unit/test_priority_policy.cpp
  unit/test_cache.cpp
  unit/test_buffer.cpp
  unit/test_sim_backend.cpp
  unit/test_trace_reader.cpp
  unit/test_des.cpp
  unit/test_block_db.cpp
  unit/test_buffer_pool.cpp
  unit/test_pread_backend.cpp
  unit/test_golden.cpp
)
if(MSAFLOW_HAVE_IO_URING)
  list(APPEND MSAFLOW_UNIT_TEST_SOURCES unit/test_io_uring_backend.cpp)
endif()

add_executable(msaflow_unit_tests ${MSAFLOW_UNIT_TEST_SOURCES})
target_link_libraries(msaflow_unit_tests PRIVATE msaflow_core msaflow_simulator msaflow_storage GTest::gtest_main)
target_compile_definitions(msaflow_unit_tests PRIVATE MSAFLOW_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/golden")

include(GoogleTest)
gtest_discover_tests(msaflow_unit_tests)

if(Python3_Interpreter_FOUND)
  add_test(
    NAME integration.runtime_crosscheck
    COMMAND ${Python3_EXECUTABLE} -m unittest tests.integration.test_runtime_crosscheck
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
  set_tests_properties(integration.runtime_crosscheck PROPERTIES
    LABELS integration
    ENVIRONMENT "MSAFLOW_BUILD_DIR=${CMAKE_BINARY_DIR};MSAFLOW_SIM=$<TARGET_FILE:msaflow-sim>;MSAFLOW_RUNTIME=$<TARGET_FILE:msaflow-runtime>"
  )
endif()
