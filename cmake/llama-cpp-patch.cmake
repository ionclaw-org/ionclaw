# Patch llama.cpp for subdirectory usage:
# - Guard project() calls (prevent re-init when used as subdirectory)
# - Remove CMAKE_BUILD_TYPE FORCE overrides
# - Force BUILD_SHARED_LIBS OFF

# --- llama.cpp/CMakeLists.txt ---
set(_llama_cmake "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
file(READ "${_llama_cmake}" _content)

# guard project() call
string(REPLACE
    "project(\"llama.cpp\" C CXX)"
    "if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)\n    project(\"llama.cpp\" C CXX)\nendif()"
    _content "${_content}")

# remove CMAKE_BUILD_TYPE FORCE block
string(REGEX REPLACE
    "if \\(NOT XCODE AND NOT MSVC AND NOT CMAKE_BUILD_TYPE\\)[^)]*endif\\(\\)\n*"
    ""
    _content "${_content}")

# remove debug message
string(REGEX REPLACE "message\\(\"CMAKE_BUILD_TYPE=\\\$\\{CMAKE_BUILD_TYPE\\}\"\\)\n*" "" _content "${_content}")

# force BUILD_SHARED_LIBS OFF
string(REPLACE
    "option(BUILD_SHARED_LIBS \"build shared libraries\" \${BUILD_SHARED_LIBS_DEFAULT})"
    "option(BUILD_SHARED_LIBS \"build shared libraries\" OFF)"
    _content "${_content}")

# guard find_package(OpenSSL) to avoid conflicts with pre-built openssl-cmake targets
string(REGEX REPLACE
    "(find_package\\(OpenSSL[^)]*\\))"
    "if(NOT TARGET OpenSSL::SSL)\n    \\1\nendif()"
    _content "${_content}")

file(WRITE "${_llama_cmake}" "${_content}")

# --- ggml/CMakeLists.txt ---
set(_ggml_cmake "${CMAKE_CURRENT_SOURCE_DIR}/ggml/CMakeLists.txt")
if(EXISTS "${_ggml_cmake}")
    file(READ "${_ggml_cmake}" _content)

    # guard project() call, remove ASM (causes sysroot interference on macOS)
    string(REPLACE
        "project(\"ggml\" C CXX ASM)"
        "if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)\n    project(\"ggml\" C CXX ASM)\nendif()"
        _content "${_content}")

    # remove CMAKE_BUILD_TYPE FORCE block
    string(REGEX REPLACE
        "if \\(NOT XCODE AND NOT MSVC AND NOT CMAKE_BUILD_TYPE\\)[^)]*endif\\(\\)\n*"
        ""
        _content "${_content}")

    # force BUILD_SHARED_LIBS OFF
    string(REPLACE
        "option(BUILD_SHARED_LIBS           \"ggml: build shared libraries\" \${BUILD_SHARED_LIBS_DEFAULT})"
        "option(BUILD_SHARED_LIBS           \"ggml: build shared libraries\" OFF)"
        _content "${_content}")

    # guard find_package(OpenSSL) to avoid conflicts with pre-built openssl-cmake targets
    string(REGEX REPLACE
        "(find_package\\(OpenSSL[^)]*\\))"
        "if(NOT TARGET OpenSSL::SSL)\n    \\1\nendif()"
        _content "${_content}")

    file(WRITE "${_ggml_cmake}" "${_content}")
endif()

message(STATUS "IonClaw: llama.cpp patched for subdirectory usage")
