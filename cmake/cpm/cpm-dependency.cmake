# logging
set(SPDLOG_OPTIONS "SPDLOG_BUILD_PIC ON")
if(APPLE)
    list(APPEND SPDLOG_OPTIONS "SPDLOG_FWRITE_UNLOCKED OFF")
endif()

CPMAddPackage(
    NAME "spdlog"
    VERSION "1.17.0"
    GITHUB_REPOSITORY "gabime/spdlog"
    OPTIONS ${SPDLOG_OPTIONS}
)

# json
CPMAddPackage("gh:nlohmann/json@3.12.0")

# openssl (built from source via openssl-cmake)
# resolve target architecture
if(CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 _ossl_arch)
else()
    set(_ossl_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# determine OpenSSL target platform and extra configure options
set(_ossl_target_platform "")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        if(_ossl_arch STREQUAL "x86_64")
            set(_ossl_target_platform "iossimulator-x86_64-xcrun")
        else()
            set(_ossl_target_platform "iossimulator-arm64-xcrun")
        endif()
    else()
        set(_ossl_target_platform "ios64-xcrun")
    endif()
elseif(ANDROID)
    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
        set(_ossl_target_platform "android-arm64")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
        set(_ossl_target_platform "android-arm")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
        set(_ossl_target_platform "android-x86_64")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
        set(_ossl_target_platform "android-x86")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(_ossl_arch MATCHES "arm64|aarch64")
        set(_ossl_target_platform "darwin64-arm64-cc")
    else()
        set(_ossl_target_platform "darwin64-x86_64-cc")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(_ossl_arch MATCHES "arm64|aarch64")
        set(_ossl_target_platform "linux-aarch64")
    else()
        set(_ossl_target_platform "linux-x86_64")
    endif()
elseif(WIN32)
    if(_ossl_arch MATCHES "ARM64|aarch64|arm64")
        set(_ossl_target_platform "VC-WIN64-ARM")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_ossl_target_platform "VC-WIN64A")
    else()
        set(_ossl_target_platform "VC-WIN32")
    endif()
endif()

set(_ossl_cpm_options "OPENSSL_TARGET_VERSION 3.6.1")
if(_ossl_target_platform)
    list(APPEND _ossl_cpm_options "OPENSSL_TARGET_PLATFORM ${_ossl_target_platform}")
endif()

# Android: pass API level to OpenSSL Configure and disable unnecessary modules
if(ANDROID)
    set(ENV{ANDROID_API} ${ANDROID_NATIVE_API_LEVEL})
    set(ENV{ANDROID_NDK_ROOT} ${CMAKE_ANDROID_NDK})
    list(APPEND _ossl_cpm_options "OPENSSL_CONFIGURE_OPTIONS no-ui-console\\;no-engine")
endif()

CPMAddPackage(
    NAME openssl-cmake
    URL https://github.com/jimmy-park/openssl-cmake/archive/main.tar.gz
    OPTIONS ${_ossl_cpm_options}
)

# macOS: patch the generated OpenSSL Makefile to add -isysroot if missing
# (required on macOS 15+ / Xcode 16+ where the toolchain cc doesn't auto-set the SDK)
if(APPLE)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE _ossl_sdk
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    set(_ossl_makefile "${CMAKE_BINARY_DIR}/_deps/openssl-source-build/Makefile")
    if(_ossl_sdk AND EXISTS "${_ossl_makefile}")
        file(READ "${_ossl_makefile}" _ossl_mk)
        string(FIND "${_ossl_mk}" "isysroot" _ossl_has_sysroot)
        if(_ossl_has_sysroot EQUAL -1)
            string(REGEX REPLACE
                "(CNF_CFLAGS=[^\n]*)"
                "\\1 -isysroot ${_ossl_sdk}"
                _ossl_mk "${_ossl_mk}")
            file(WRITE "${_ossl_makefile}" "${_ossl_mk}")
            message(STATUS "IonClaw: patched OpenSSL Makefile with -isysroot ${_ossl_sdk}")
        endif()
    endif()
endif()

set(IONCLAW_HAS_SSL TRUE)

# http and networking (poco)
if(WIN32)
    set(POCO_NETSSL_OPTIONS
        "ENABLE_NETSSL OFF"
        "ENABLE_NETSSL_WIN ON"
    )
else()
    set(POCO_NETSSL_OPTIONS
        "ENABLE_NETSSL ON"
        "ENABLE_NETSSL_WIN OFF"
    )
endif()

CPMAddPackage(
    NAME "Poco"
    VERSION "1.15.0"
    GITHUB_REPOSITORY "pocoproject/poco"
    GIT_TAG "poco-1.15.0-release"
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "ENABLE_FOUNDATION ON"
        "ENABLE_NET ON"
        ${POCO_NETSSL_OPTIONS}
        "ENABLE_CRYPTO ON"
        "ENABLE_UTIL ON"
        "ENABLE_JSON OFF"
        "ENABLE_XML ON"
        "ENABLE_MONGODB OFF"
        "ENABLE_DATA OFF"
        "ENABLE_DATA_SQLITE OFF"
        "ENABLE_DATA_MYSQL OFF"
        "ENABLE_DATA_POSTGRESQL OFF"
        "ENABLE_DATA_ODBC OFF"
        "POCO_ENABLE_SQL OFF"
        "ENABLE_REDIS OFF"
        "ENABLE_PROMETHEUS OFF"
        "ENABLE_ENCODINGS OFF"
        "ENABLE_ENCODINGS_COMPILER OFF"
        "ENABLE_PAGECOMPILER OFF"
        "ENABLE_PAGECOMPILER_FILE2PAGE OFF"
        "ENABLE_ACTIVERECORD OFF"
        "ENABLE_ACTIVERECORD_COMPILER OFF"
        "ENABLE_ZIP ON"
        "ENABLE_JWT OFF"
        "ENABLE_APACHECONNECTOR OFF"
        "ENABLE_TESTS OFF"
        "ENABLE_SAMPLES OFF"
)

# yaml parser
CPMAddPackage(
    NAME "yaml-cpp"
    VERSION "0.8.0"
    GITHUB_REPOSITORY "jbeder/yaml-cpp"
    GIT_TAG "0.8.0"
    OPTIONS
        "YAML_CPP_BUILD_TESTS OFF"
        "YAML_CPP_BUILD_TOOLS OFF"
)

# stb image for local image generation (png output)
CPMAddPackage(
    NAME "stb"
    GITHUB_REPOSITORY "nothings/stb"
    GIT_TAG "master"
    DOWNLOAD_ONLY YES
)

# jwt token (header-only, download only to avoid nlohmann json conflict)
CPMAddPackage(
    NAME "jwt-cpp"
    VERSION "0.7.0"
    GITHUB_REPOSITORY "Thalhammer/jwt-cpp"
    DOWNLOAD_ONLY YES
)

if(jwt-cpp_ADDED)
    add_library(jwt-cpp INTERFACE)
    target_include_directories(jwt-cpp INTERFACE ${jwt-cpp_SOURCE_DIR}/include)
    target_link_libraries(jwt-cpp INTERFACE nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(jwt-cpp INTERFACE JWT_DISABLE_PICOJSON)
endif()

# SSL link targets
if(WIN32)
    set(IONCLAW_SSL_LIBS Poco::NetSSLWin)
else()
    set(IONCLAW_SSL_LIBS Poco::NetSSL)
endif()

list(APPEND IONCLAW_SSL_LIBS Poco::Crypto OpenSSL::SSL OpenSSL::Crypto jwt-cpp)

if(stb_ADDED)
    target_include_directories(ionclaw-lib PRIVATE ${stb_SOURCE_DIR})
    target_compile_definitions(ionclaw-lib PUBLIC IONCLAW_HAS_STB_IMAGE_WRITE)

    if(IONCLAW_BUILD_SHARED)
        target_include_directories(ionclaw-shared PRIVATE ${stb_SOURCE_DIR})
        target_compile_definitions(ionclaw-shared PUBLIC IONCLAW_HAS_STB_IMAGE_WRITE)
    endif()
endif()

# local LLM inference via llama.cpp
if(IONCLAW_LLAMA_CPP)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_CURL OFF CACHE BOOL "" FORCE)

    CPMAddPackage(
        NAME llama.cpp
        GITHUB_REPOSITORY ggml-org/llama.cpp
        GIT_TAG master
        PATCH_COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_LIST_DIR}/../llama-cpp-patch.cmake"
        OPTIONS
            "LLAMA_BUILD_TESTS OFF"
            "LLAMA_BUILD_EXAMPLES OFF"
            "LLAMA_BUILD_SERVER OFF"
            "LLAMA_CURL OFF"
    )

    if(llama.cpp_ADDED)
        target_compile_definitions(ionclaw-lib PUBLIC IONCLAW_HAS_LLAMA_CPP)
        target_link_libraries(ionclaw-lib PRIVATE llama)

        if(IONCLAW_BUILD_SHARED)
            target_compile_definitions(ionclaw-shared PUBLIC IONCLAW_HAS_LLAMA_CPP)
            target_link_libraries(ionclaw-shared PRIVATE llama)
        endif()

        message(STATUS "IonClaw: llama.cpp enabled for local LLM inference")
    else()
        message(WARNING "IonClaw: llama.cpp was requested but could not be added")
    endif()
endif()

# link dependencies to ionclaw targets
target_link_libraries(ionclaw-lib PUBLIC
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Poco::Foundation
    Poco::Net
    Poco::Util
    Poco::XML
    Poco::Zip
    yaml-cpp
    ${IONCLAW_SSL_LIBS}
)

target_compile_definitions(ionclaw-lib PUBLIC IONCLAW_HAS_SSL)

if(IONCLAW_BUILD_SHARED)
    target_link_libraries(ionclaw-shared PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
    )

    target_link_libraries(ionclaw-shared PRIVATE
        Poco::Foundation
        Poco::Net
        Poco::Util
        Poco::XML
        Poco::Zip
        yaml-cpp
        ${IONCLAW_SSL_LIBS}
    )

    target_compile_definitions(ionclaw-shared PUBLIC IONCLAW_HAS_SSL)
else()
    target_link_libraries(ionclaw-server PRIVATE ionclaw-lib)
endif()
