cmake_minimum_required(VERSION 3.10)
project(energytrace C)

set(CMAKE_C_STANDARD 11)

add_executable(energytrace energytrace.c)

# MSP430 Debug Stack include directory
# Headers are vendored in Inc/, but users can override with -DMSP430_INCLUDE_DIR=<path>
if(NOT MSP430_INCLUDE_DIR)
    set(MSP430_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Inc")
endif()

message(STATUS "MSP430 headers: ${MSP430_INCLUDE_DIR}")
target_include_directories(energytrace PRIVATE ${MSP430_INCLUDE_DIR})

# MSP430 library linking
if(WIN32)
    # On Windows, MSP430.DLL is loaded at runtime via LoadLibrary/GetProcAddress.
    # No import library is needed. This avoids 32-bit __stdcall name decoration issues.
else()
    target_link_libraries(energytrace msp430)
endif()

# Install target
install(TARGETS energytrace DESTINATION bin)
