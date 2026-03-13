cmake_minimum_required(VERSION 3.20.0)

# allow older cmake minimum version in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

# cmake project
project(ionclaw LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# policies
cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0076 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)

# build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# options
set(IONCLAW_VERSION "1.0.2" CACHE STRING "IonClaw Version")
option(IONCLAW_BUILD_SHARED "Build Shared Library (FFI)" OFF)

# Apple platform deployment targets
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "iOS deployment target")
elseif(CMAKE_SYSTEM_NAME STREQUAL "watchOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "8.0" CACHE STRING "watchOS deployment target")
elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "tvOS deployment target")
elseif(CMAKE_SYSTEM_NAME STREQUAL "visionOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "1.0" CACHE STRING "visionOS deployment target")
endif()

# output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib")

# ionclaw
add_subdirectory(main)

# dependencies via CPM
include(cmake/cpm/cpm-downloader.cmake)
include(cmake/cpm/cpm-dependency.cmake)
