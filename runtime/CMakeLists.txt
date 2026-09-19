add_executable(msaflow-runtime runtime.cpp)
target_link_libraries(msaflow-runtime PRIVATE msaflow_simulator msaflow_storage)
target_compile_options(msaflow-runtime PRIVATE -Wall -Wextra)
