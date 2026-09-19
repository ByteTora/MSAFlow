add_library(msaflow_simulator STATIC simulated_backend.cpp)
target_link_libraries(msaflow_simulator PUBLIC msaflow_core)
target_include_directories(msaflow_simulator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(msaflow_simulator PRIVATE -Wall -Wextra)
