# library sources
set(IONCLAW_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/config/Config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/config/ConfigLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/TimeHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/UniqueId.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/JwtHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/Base64.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/StbImageWrite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/StbImage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/StbImageResize.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/HttpClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/ProcessRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/MimeType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/SsrfGuard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/StringHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/util/EmbeddedResources.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/bus/Events.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/bus/MessageBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/bus/EventDispatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/provider/AnthropicProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/provider/OpenAiProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/provider/ProviderFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/provider/ProviderHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/session/SessionManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/task/Task.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/task/TaskManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/Tool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/Platform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/ToolRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/platform/PlatformBridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/search/SearchProviderRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/search/BraveSearchProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/search/DuckDuckGoSearchProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/image/ImageGeneratorHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/image/ImageGeneratorRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/image/GeminiImageGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/image/GrokImageGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/image/OpenAIImageGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/image/LocalImageGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/transcription/TranscriptionProviderRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/transcription/OpenAITranscriptionProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/transcription/LocalTranscriptionProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/ToolHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/ReadFileTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/WriteFileTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/EditFileTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/ListDirTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/ExecTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/MessageTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/WebSearchTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/WebFetchTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/HttpClientTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/McpClientTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/RssReaderTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/GenerateImageTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/ImageOpsTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/MemorySaveTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/MemoryReadTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/MemorySearchTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/InvokePlatformTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/SpawnTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/SubagentsTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/CronTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/BrowserTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/tool/builtin/VisionTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/ContextBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/AgentLoop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/Orchestrator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/Classifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/MemoryStore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/SkillsLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/ContextWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/Compaction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/ToolLoopDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/HookRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/SubagentRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/agent/AnnounceQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/provider/FailoverProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/session/SessionSweeper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/bus/SessionQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/Auth.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/WebSocketManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/Routes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/HttpServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/ServerInstance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/handler/HttpHelper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/handler/ApiHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/handler/WebSocketHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/handler/WebAppHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/handler/PublicFileHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/handler/RedirectHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/handler/RequestHandlerFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/AuthRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/ChatRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/TaskRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/AgentRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/ConfigRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/SystemRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/SkillRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/MarketplaceRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/FileRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/heartbeat/HeartbeatService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/cron/CronService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/cron/CronParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/channel/ChannelManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/channel/TelegramRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/ChannelRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/FormRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/routes/SchedulerRoutes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/mcp/McpDispatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/ionclaw/server/handler/McpHandler.cpp
)

# --- embed resources (shared by server and shared library) ---
# web client
file(GLOB_RECURSE IONCLAW_WEB_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/*")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded_web.cpp
           ${CMAKE_CURRENT_BINARY_DIR}/embedded_web.hpp
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/resources/web
        -DOUTPUT_CPP=${CMAKE_CURRENT_BINARY_DIR}/embedded_web.cpp
        -DOUTPUT_HPP=${CMAKE_CURRENT_BINARY_DIR}/embedded_web.hpp
        -DNAMESPACE=ionclaw_embedded_web
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed-resources.cmake
    DEPENDS ${IONCLAW_WEB_FILES}
    COMMENT "Embedding web resources..."
)

# built-in skills
file(GLOB_RECURSE IONCLAW_SKILLS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/skills/*")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded_skills.cpp
           ${CMAKE_CURRENT_BINARY_DIR}/embedded_skills.hpp
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/resources/skills
        -DOUTPUT_CPP=${CMAKE_CURRENT_BINARY_DIR}/embedded_skills.cpp
        -DOUTPUT_HPP=${CMAKE_CURRENT_BINARY_DIR}/embedded_skills.hpp
        -DNAMESPACE=ionclaw_embedded_skills
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed-resources.cmake
    DEPENDS ${IONCLAW_SKILLS_FILES}
    COMMENT "Embedding skills resources..."
)

# project template
file(GLOB_RECURSE IONCLAW_TEMPLATE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/template/*")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded_template.cpp
           ${CMAKE_CURRENT_BINARY_DIR}/embedded_template.hpp
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/resources/template
        -DOUTPUT_CPP=${CMAKE_CURRENT_BINARY_DIR}/embedded_template.cpp
        -DOUTPUT_HPP=${CMAKE_CURRENT_BINARY_DIR}/embedded_template.hpp
        -DNAMESPACE=ionclaw_embedded_template
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed-resources.cmake
    DEPENDS ${IONCLAW_TEMPLATE_FILES}
    COMMENT "Embedding template resources..."
)

add_custom_target(ionclaw-embedded-resources DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_web.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_skills.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_template.hpp
)

# static library (includes embedded resources)
add_library(ionclaw-lib STATIC
    ${IONCLAW_LIB_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_web.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_skills.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_template.cpp
)
target_include_directories(ionclaw-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/include)
target_include_directories(ionclaw-lib PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(ionclaw-lib PRIVATE IONCLAW_EMBEDDED_RESOURCES)
target_compile_definitions(ionclaw-lib PUBLIC IONCLAW_VERSION_STRING="${IONCLAW_VERSION}")
add_dependencies(ionclaw-lib ionclaw-embedded-resources)

if(IONCLAW_BUILD_SHARED)
    # shared library for FFI (includes embedded resources)
    add_library(ionclaw-shared SHARED
        ${IONCLAW_LIB_SOURCES}
        ${CMAKE_CURRENT_BINARY_DIR}/embedded_web.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/embedded_skills.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/embedded_template.cpp
    )
    set_target_properties(ionclaw-shared PROPERTIES
        OUTPUT_NAME "ionclaw"
        POSITION_INDEPENDENT_CODE ON
    )

    # iOS: build as framework so symbols are available via DynamicLibrary.process()
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set_target_properties(ionclaw-shared PROPERTIES
            FRAMEWORK TRUE
            MACOSX_FRAMEWORK_IDENTIFIER "com.ionclaw.lib"
            MACOSX_FRAMEWORK_BUNDLE_VERSION "${IONCLAW_VERSION}"
            MACOSX_FRAMEWORK_SHORT_VERSION_STRING "${IONCLAW_VERSION}"
            MACOSX_FRAMEWORK_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/ios-framework-info.plist.in"
            XCODE_ATTRIBUTE_DEFINES_MODULE YES
            XCODE_ATTRIBUTE_BUILD_LIBRARY_FOR_DISTRIBUTION YES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
        )
    endif()
    target_include_directories(ionclaw-shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/include)
    target_include_directories(ionclaw-shared PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(ionclaw-shared PRIVATE IONCLAW_EMBEDDED_RESOURCES)
    target_compile_definitions(ionclaw-shared PUBLIC IONCLAW_VERSION_STRING="${IONCLAW_VERSION}")
    add_dependencies(ionclaw-shared ionclaw-embedded-resources)
else()
    # desktop executable
    add_executable(ionclaw-server ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main.cpp)
    target_include_directories(ionclaw-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/include)
    install(TARGETS ionclaw-server RUNTIME DESTINATION bin)
endif()
