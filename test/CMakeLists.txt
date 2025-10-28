cmake_minimum_required(VERSION 4.1)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(test_async)

# Download and unpack actuator at configure time
configure_file(CMakeLists.txt.in actuator-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/actuator-download)
if(result)
  message(FATAL_ERROR "CMake step for actuator failed: ${result}")
endif()

#include headers directories
include_directories(../../actuator ../)

#add source files
set(SOURCE_FILES test_async.cpp)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

if (UNIX)
  target_link_libraries(${PROJECT_NAME} -lpthread)
endif (UNIX)
