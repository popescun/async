cmake_minimum_required(VERSION 4.1)

project(test_async)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# todo: enable this for googletest
# Download and unpack external dependencies at configure time
#configure_file(Dependencies_CMakeLists deps/CMakeLists.txt)
#execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
#  RESULT_VARIABLE result
#  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/deps)
#if(result)
#  message(FATAL_ERROR "CMake step for external dependencies failed: ${result}")
#endif()
## The build step actually triggers cloning the repositories
#execute_process(COMMAND ${CMAKE_COMMAND} --build .
#  RESULT_VARIABLE result
#  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/deps)
#if(result)
#  message(FATAL_ERROR "Build step for dependencies failed: ${result}")
#endif()

#include headers directories
include_directories(
  ../
)

#add source files
set(SOURCE_FILES test_async.cpp)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

if (UNIX)
  target_link_libraries(${PROJECT_NAME} -lpthread)
endif (UNIX)
