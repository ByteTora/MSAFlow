add_library(msaflow_storage STATIC
  sync/pread_backend.cpp
)
target_link_libraries(msaflow_storage PUBLIC msaflow_core)
target_include_directories(msaflow_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(msaflow_storage PRIVATE -Wall -Wextra)

# Optional Phase 2B io_uring backend. liburing is built locally under
# third_party/liburing/_install (gitignored) so no system package is required.
set(_liburing_prefix "${CMAKE_SOURCE_DIR}/third_party/liburing/_install")
find_path(LIBURING_INCLUDE_DIR liburing.h PATHS "${_liburing_prefix}/include" NO_DEFAULT_PATH)
set(_saved_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
find_library(LIBURING_LIBRARY NAMES uring PATHS "${_liburing_prefix}/lib" NO_DEFAULT_PATH)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_saved_suffixes})

if(LIBURING_INCLUDE_DIR AND LIBURING_LIBRARY)
  target_sources(msaflow_storage PRIVATE local_nvme/io_uring_backend.cpp)
  target_include_directories(msaflow_storage PUBLIC ${LIBURING_INCLUDE_DIR})
  target_link_libraries(msaflow_storage PUBLIC ${LIBURING_LIBRARY})
  target_compile_definitions(msaflow_storage PUBLIC MSAFLOW_HAVE_IO_URING=1)
  set(MSAFLOW_HAVE_IO_URING ON PARENT_SCOPE)
  message(STATUS "MSAFlow: io_uring backend enabled (${LIBURING_LIBRARY})")
else()
  message(STATUS "MSAFlow: io_uring backend disabled (liburing not found under ${_liburing_prefix})")
endif()
