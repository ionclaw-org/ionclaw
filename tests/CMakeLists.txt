# doctest test framework
CPMAddPackage("gh:doctest/doctest@2.4.11")

set(IONCLAW_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/provider_factory_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/anthropic_thinking_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/openai_provider_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/config_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/config_store_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/provider_helper_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/string_helper_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session_key_utils_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tool_policy_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cron_parser_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http_client_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/model_capabilities_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/whatsapp_webhook_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/webhook_dedup_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/whatsapp_sender_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/delivery_parser_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/message_bus_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/usage_tracker_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text_chunker_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/time_helper_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ssrf_guard_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crypto_helper_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/environment_helper_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session_queue_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/subagent_registry_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/task_manager_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vector_store_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/heartbeat_service_test.cpp
)

add_executable(ionclaw-tests ${IONCLAW_TEST_SOURCES})
target_link_libraries(ionclaw-tests PRIVATE ionclaw-lib doctest::doctest)
target_include_directories(ionclaw-tests PRIVATE ${CMAKE_SOURCE_DIR}/main/lib/include)

add_test(NAME ionclaw-tests COMMAND ionclaw-tests)
