add_executable(msaflow_unit_tests unit/test_types.cpp)
target_link_libraries(msaflow_unit_tests PRIVATE msaflow_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(msaflow_unit_tests)
