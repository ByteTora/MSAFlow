add_executable(msaflow_unit_tests
  unit/test_types.cpp
  unit/test_query_registry.cpp
  unit/test_block_aggregator.cpp
  unit/test_priority_policy.cpp
  unit/test_cache.cpp
  unit/test_buffer.cpp
  unit/test_sim_backend.cpp
  unit/test_trace_reader.cpp
  unit/test_des.cpp
  unit/test_golden.cpp
)
target_link_libraries(msaflow_unit_tests PRIVATE msaflow_core msaflow_simulator GTest::gtest_main)
target_compile_definitions(msaflow_unit_tests PRIVATE MSAFLOW_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/golden")

include(GoogleTest)
gtest_discover_tests(msaflow_unit_tests)
