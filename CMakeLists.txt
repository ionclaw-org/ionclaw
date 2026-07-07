cmake_minimum_required(VERSION 3.20.0)

# allow older cmake minimum version in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

# cmake project (c is required by the llama.cpp/ggml backend)
project(ionclaw LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
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
set(IONCLAW_VERSION "1.0.6" CACHE STRING "IonClaw Version")
option(IONCLAW_BUILD_SHARED "Build Shared Library (FFI)" OFF)

# split the version into numeric components for the windows executable resource
string(REGEX REPLACE "\\+.*$" "" IONCLAW_VERSION_NUMERIC "${IONCLAW_VERSION}")
string(REPLACE "." ";" IONCLAW_VERSION_PARTS "${IONCLAW_VERSION_NUMERIC}")
list(GET IONCLAW_VERSION_PARTS 0 IONCLAW_VERSION_MAJOR)
list(GET IONCLAW_VERSION_PARTS 1 IONCLAW_VERSION_MINOR)
list(GET IONCLAW_VERSION_PARTS 2 IONCLAW_VERSION_PATCH)

# llama.cpp
set(IONCLAW_LLAMA_CPP_DEFAULT ON)
if(CMAKE_SYSTEM_NAME MATCHES "^(tvOS|watchOS|visionOS)$")
    set(IONCLAW_LLAMA_CPP_DEFAULT OFF)
endif()
option(IONCLAW_LLAMA_CPP "Build With llama.cpp For Local LLM Inference" ${IONCLAW_LLAMA_CPP_DEFAULT})

# unit tests
option(IONCLAW_BUILD_TESTS "Build The Unit Test Suite" OFF)

# shared library requires all code (including dependencies) to be position-independent
if(IONCLAW_BUILD_SHARED)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

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

# unit tests (linked against the static library, so unavailable in shared-only builds)
if(IONCLAW_BUILD_TESTS AND NOT IONCLAW_BUILD_SHARED)
    enable_testing()
    add_subdirectory(tests)
endif()

# desktop installers via cpack — msi on windows, dmg on macos, deb on linux
if(NOT IONCLAW_BUILD_SHARED AND (WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(CPACK_PACKAGE_NAME "IonClaw")
    set(CPACK_PACKAGE_VENDOR "IonClaw")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "IonClaw AI Agent Orchestrator")
    set(CPACK_PACKAGE_VERSION "${IONCLAW_VERSION}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "IonClaw")
    set(CPACK_PACKAGE_CONTACT "https://github.com/ionclaw-org/ionclaw")

    # package only the server runtime so dependency headers and static libs stay out of the installer
    set(CPACK_COMPONENTS_ALL runtime)
    set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)

    if(WIN32)
        # bundle the msvc runtime so the installed server runs on a clean machine
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT runtime)
        include(InstallRequiredSystemLibraries)

        install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.md" DESTINATION . RENAME LICENSE.txt COMPONENT runtime)

        set(CPACK_GENERATOR "WIX")
        # a stable upgrade guid lets new installers replace older ones instead of installing side by side
        set(CPACK_WIX_UPGRADE_GUID "6F1B2C34-9D5E-4A78-8B90-1C2D3E4F5A6B")
        set(CPACK_WIX_PROPERTY_ARPHELPLINK "https://github.com/ionclaw-org/ionclaw")
        set(CPACK_WIX_ROOT_FEATURE_TITLE "IonClaw")
        # add the install bin directory to the system path
        set(CPACK_WIX_PATCH_FILE "${CMAKE_SOURCE_DIR}/cmake/wix-patch.xml")
    elseif(APPLE)
        install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.md" DESTINATION share/doc/ionclaw RENAME LICENSE.txt COMPONENT runtime)

        # flat installer package that drops the binary into a directory already on the path
        set(CPACK_GENERATOR "productbuild")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")

        # productbuild only accepts rtf, txt, or html for the license screen, so provide a txt copy
        configure_file("${CMAKE_SOURCE_DIR}/LICENSE.md" "${CMAKE_BINARY_DIR}/LICENSE.txt" COPYONLY)
        set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_BINARY_DIR}/LICENSE.txt")
    else()
        # debian package with automatically resolved shared-library dependencies
        install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.md" DESTINATION share/doc/ionclaw RENAME copyright COMPONENT runtime)

        set(CPACK_GENERATOR "DEB")
        set(CPACK_DEB_COMPONENT_INSTALL ON)
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "IonClaw")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        # shlibdeps cannot see the runtime zone database, so require tzdata explicitly for iana cron scheduling
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "tzdata")
        set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    endif()

    include(CPack)
endif()
