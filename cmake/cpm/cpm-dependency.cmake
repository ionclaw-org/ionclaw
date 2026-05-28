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
include(${CMAKE_CURRENT_LIST_DIR}/openssl.cmake)

# http and networking (poco)
# tvos and watchos prohibit fork/exec, so poco's Process launch is compiled out
if(CMAKE_SYSTEM_NAME MATCHES "^(tvOS|watchOS)$")
    add_compile_definitions(POCO_NO_FORK_EXEC)
endif()

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
    VERSION "1.15.3"
    GITHUB_REPOSITORY "pocoproject/poco"
    GIT_TAG "poco-1.15.3-release"
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
    VERSION "0.9.0"
    GITHUB_REPOSITORY "jbeder/yaml-cpp"
    GIT_TAG "yaml-cpp-0.9.0"
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
    VERSION "0.7.2"
    GITHUB_REPOSITORY "Thalhammer/jwt-cpp"
    DOWNLOAD_ONLY YES
)

if(jwt-cpp_ADDED)
    add_library(jwt-cpp INTERFACE)
    target_include_directories(jwt-cpp INTERFACE ${jwt-cpp_SOURCE_DIR}/include)
    target_link_libraries(jwt-cpp INTERFACE nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(jwt-cpp INTERFACE JWT_DISABLE_PICOJSON)
endif()

# ssl link targets
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

# local llm inference via llama.cpp
if(IONCLAW_LLAMA_CPP)
    CPMAddPackage(
        NAME llama.cpp
        GITHUB_REPOSITORY ggml-org/llama.cpp
        GIT_TAG 19e92c33ef974661e4b1e43dd48be231d07be5ed
        OPTIONS
            "BUILD_SHARED_LIBS OFF"
            "LLAMA_BUILD_COMMON OFF"
            "LLAMA_BUILD_TESTS OFF"
            "LLAMA_BUILD_EXAMPLES OFF"
            "LLAMA_BUILD_TOOLS OFF"
            "LLAMA_BUILD_SERVER OFF"
            "LLAMA_OPENSSL OFF"
            # build for the architecture baseline instead of -mcpu=native, which keeps the binary portable and avoids host cpus the compiler may not know
            "GGML_NATIVE OFF"
    )

    if(NOT llama.cpp_ADDED)
        message(FATAL_ERROR "IonClaw: IONCLAW_LLAMA_CPP is ON but llama.cpp could not be fetched")
    endif()

    target_compile_definitions(ionclaw-lib PUBLIC IONCLAW_HAS_LLAMA_CPP)
    target_link_libraries(ionclaw-lib PRIVATE llama)

    if(IONCLAW_BUILD_SHARED)
        target_compile_definitions(ionclaw-shared PUBLIC IONCLAW_HAS_LLAMA_CPP)
        target_link_libraries(ionclaw-shared PRIVATE llama)
    endif()

    message(STATUS "IonClaw: llama.cpp enabled for local LLM inference")
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
