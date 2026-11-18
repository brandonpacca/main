cmake_minimum_required(VERSION 3.15)
project(madcatz_wheel CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(madcatz_wheel src/main.cpp src/vjoy_api.h)
target_compile_definitions(madcatz_wheel PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(madcatz_wheel PRIVATE winmm)

if(MSVC)
  set_property(TARGET madcatz_wheel PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_link_options(madcatz_wheel PRIVATE /SUBSYSTEM:CONSOLE)
else()
  target_link_options(madcatz_wheel PRIVATE -municode -static)
endif()

add_custom_command(TARGET madcatz_wheel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/madcatz_wheel.ini $<TARGET_FILE_DIR:madcatz_wheel>/madcatz_wheel.ini)
