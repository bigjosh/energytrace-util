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

# MSP430 library
if(WIN32)
    # On Windows, link against MSP430.dll via import library
    # Users can set -DMSP430_LIBRARY=<path> to override
    if(NOT MSP430_LIBRARY)
        find_library(MSP430_LIBRARY NAMES MSP430 msp430
            PATHS
            "C:/ti/msp430/MSP Debug Stack/lib"
            "C:/ti/ccs/ccs_base/DebugServer/drivers"
            "$ENV{TI_MSPDS_DIR}/lib"
            "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()
    if(MSP430_LIBRARY)
        target_link_libraries(energytrace ${MSP430_LIBRARY})
    else()
        message(FATAL_ERROR "MSP430 library not found. Set -DMSP430_LIBRARY=<path>")
    endif()
else()
    target_link_libraries(energytrace msp430)
endif()

# Install target
install(TARGETS energytrace DESTINATION bin)
