# IonClaw Architecture

IonClaw is a C++ AI agent orchestrator that runs anywhere as a single native binary — on server, desktop, and mobile. It provides the infrastructure for running autonomous AI agents that communicate through a web interface, execute tools, manage tasks, and maintain persistent sessions — all orchestrated through a message bus.

This document describes the system architecture, core components, and data flow.

---

## Table of Contents

- [High-Level Overview](#high-level-overview)
- [Message Flow](#message-flow)
- [Real-Time Updates](#real-time-updates)
- [Core Components](#core-components)
  - [Server](#server)
  - [Message Bus](#message-bus)
  - [Session Queue](#session-queue)
  - [Agent](#agent)
  - [Hook System](#hook-system)
  - [Platform Bridge](#platform-bridge)
  - [Providers](#providers)
  - [Tools](#tools)
  - [Tasks](#tasks)
  - [Sessions](#sessions)
  - [Memory](#memory)
  - [Config](#config)
  - [Web Client](#web-client)
- [Project Structure](#project-structure)
- [Startup Lifecycle](#startup-lifecycle)
- [Safety and Resilience](#safety-and-resilience)
- [Key Design Decisions](#key-design-decisions)

---

## High-Level Overview

IonClaw is composed of the following layers:

1. **HTTP Server** — Poco-based HTTP server handling REST API requests, static files, and WebSocket upgrades.
2. **Message Bus** — Async queue pair (`inbound` / `outbound`) that decouples channels from the agent.
3. **Session Queue** — Per-session message queuing with configurable modes (steer, followup, collect, interrupt) and drop policies.
4. **Orchestrator** — Consumes inbound messages, classifies them to the appropriate agent, and delegates processing.
5. **Agent Loop** — Runs an agentic LLM loop (call model, execute tools, repeat), and publishes the final response to the outbound queue.
6. **Hook System** — 14-point lifecycle hooks that allow observing and controlling agent behavior (blocking, parameter modification).
7. **Event Dispatcher** — Broadcasts real-time events (thinking, tool use, messages, task updates) through registered handlers (e.g. `WebSocketManager`).

---

## Message Flow

```
User (Web UI) → HTTP API → MessageBus (inbound)
                                ↓
                          SessionQueue (mode resolution)
                                ↓
                          Orchestrator
                                ↓
                     AgentClassifier (multi-agent)
                                ↓
                         HookRunner (BeforeAgentStart, AgentTurnStart)
                                ↓
                          AgentLoop
                       ↗     ↓     ↘
                   LLM    Tools   Memory
                       ↘     ↓     ↗
                      ToolLoopDetector
                                ↓
                 Compaction (with memory flush + hooks)
                                ↓
                         Response
                                ↓
                    MessageBus (outbound)
                                ↓
                  EventDispatcher → WebSocket → User
```

---

## Real-Time Updates

The `EventDispatcher` broadcasts events to all registered handlers. The `WebSocketManager` is one such handler — it pushes events to connected WebSocket clients.

Event types (server → client) include:
- `chat:typing` — agent is processing (show typing indicator)
- `chat:stream` — streaming content tokens
- `chat:stream_end` — end of stream
- `chat:message` — final response (content block array)
- `chat:thinking` — reasoning/thinking step
- `chat:tool_use` — tool invocation with human-readable description
- `chat:user_message` — non-web user message (e.g. from another channel)
- `chat:transcription` — audio transcription result
- `chat:warning` — warning from agent loop (e.g. tool loop detected)
- `task:created` — new task
- `task:updated` — task state change
- `sessions:updated` — session list changed (reload sidebar)

See [API Reference](api.md#websocket) for full payload shapes.

---

## Core Components

### Server

- **HttpServer** (`server/HttpServer.hpp`) — Poco HTTP server lifecycle (start, stop, port).
- **Handlers** (`server/handler/`) — HTTP request handlers split by responsibility: `RequestHandlerFactory` (URL dispatch), `ApiHandler` (REST API), `WebSocketHandler`, `WebAppHandler` (SPA at `/app/`), `PublicFileHandler`, `RedirectHandler`, `HttpHelper` (shared CORS/content-type utilities).
- **Routes** (`server/Routes.hpp`) — Route handler declarations. Implementations split by domain into `server/routes/`: AuthRoutes, ChatRoutes, TaskRoutes, AgentRoutes, ConfigRoutes, SystemRoutes, SkillRoutes, FileRoutes, ChannelRoutes, FormRoutes, SchedulerRoutes, MarketplaceRoutes.
- **Auth** (`server/Auth.hpp`) — JWT-based authentication for API and WebSocket connections.
- **WebSocketManager** (`server/WebSocketManager.hpp`) — Manages WebSocket connections and broadcasts events to clients.
- **ServerInstance** (`server/ServerInstance.hpp`) — Singleton that orchestrates startup and shutdown of all components.

### Message Bus

- **MessageBus** (`bus/MessageBus.hpp`) — Thread-safe async queue pair for inbound/outbound messages.
- **EventDispatcher** (`bus/EventDispatcher.hpp`) — Publishes typed events to registered handlers.
- **Events** (`bus/Events.hpp`) — Event type definitions, queue mode enums, and inbound/outbound message structures.

### Session Queue

- **SessionQueue** (`bus/SessionQueue.hpp`) — Per-session message queuing with five modes and three drop policies. Handles debounce timing, collect prompt building, and dropped message summarization.

**Queue modes** determine how messages arriving during an active agent turn are handled:

| Mode | Behavior |
|------|----------|
| `steer` | Inject into the active streaming turn between tool iterations |
| `followup` | Enqueue and process as a separate turn after current completes |
| `collect` | Batch multiple messages into a single prompt after debounce |
| `steer_backlog` | Try steer; if not streaming, fallback to followup |
| `interrupt` | Abort current turn, clear queue, process immediately |

**Drop policies** control what happens when queue depth exceeds the cap:

| Policy | Behavior |
|--------|----------|
| `old` | Drop oldest items |
| `new` | Reject new items |
| `summarize` | Drop oldest but keep summary lines for context |

Settings are resolved per channel: `inlineMode > byChannel > globalMode > default(collect)`.

### Agent

- **Orchestrator** (`agent/Orchestrator.hpp`) — Consumes messages from the bus, routes to agents, and publishes responses. On startup, performs recovery: marks stale subagent runs as errored, sets session abort cutoffs, and transitions stuck tasks from DOING to ERROR. Wires the hook runner to agent loops and fires lifecycle hooks (BeforeAgentStart, AgentTurnStart, AgentTurnEnd, BeforePromptBuild).
- **AgentLoop** (`agent/AgentLoop.hpp`) — Core agentic loop: call LLM, parse tool calls, execute tools, repeat until done. Includes context overflow recovery (up to 3 compaction retries with progressive tool result truncation), synthetic error flush for abandoned tool calls on abort, per-channel history limits, and pre-compaction memory flush.
- **Classifier** (`agent/Classifier.hpp`) — LLM-based agent classifier for multi-agent setups.
- **ContextBuilder** (`agent/ContextBuilder.hpp`) — Builds the system prompt and message context for each LLM call. Supports two prompt modes: `Full` (complete system prompt with safety, memory, skills, response guidelines, bootstrap files) and `Minimal` (lightweight prompt for subagents with identity, tools, workspace, and agent instructions only). Applies prompt injection sanitization to runtime values, enforces per-result and total tool result context budgets, and truncates oversized bootstrap files (70% head + 20% tail, 20K per file, 80K total).
- **Compaction** (`agent/Compaction.hpp`) — Summarizes old messages to reduce context size. Retries up to 3 times with exponential backoff (500ms–5s) and a 3-tier fallback strategy (full summary → exclude oversized messages → text-only note). Uses adaptive token-aware chunk sizing based on model context window.
- **ToolLoopDetector** (`agent/ToolLoopDetector.hpp`) — Detects repetitive tool call patterns with four detection strategies and configurable thresholds. Warning emission uses bucket-based throttling to prevent log spam.
- **SubagentRegistry** (`agent/SubagentRegistry.hpp`) — Tracks spawned subagent runs with lifecycle management. Supports run timeouts (default 300s), stale run recovery on startup, progress tracking (latest output snippet), and maximum depth/children limits (configurable per agent).
- **AnnounceQueue** (`agent/AnnounceQueue.hpp`) — Queues completion notifications from child subagents to their parent sessions. Supports idempotency (deduplication by announce ID), retry with backoff (up to 3 retries), and automatic expiry (300s TTL).

#### Tool Loop Detection

The `ToolLoopDetector` identifies four types of repetitive patterns:

| Detector | Description |
|----------|-------------|
| **Generic Repeat** | Same tool called with identical arguments. Severity escalates: Warning → Critical → CircuitBreaker |
| **Ping-Pong** | Alternating A-B-A-B pattern between two tools with no progress |
| **Known Poll No-Progress** | Tools known to poll (`exec`, `http_client`, `web_fetch`, `browser`) returning identical results |
| **Global No-Progress** | Any tool producing identical results consecutively |

Default thresholds: Warning at 10, Critical at 20, CircuitBreaker at 30. Each detector can be individually enabled/disabled via `ToolLoopConfig`. Warning emissions are bucketed (every 10 iterations) to avoid flooding logs.

#### Pre-Compaction Memory Flush

Before compaction runs (once per compaction cycle), the agent loop attempts a memory flush: a single LLM call with only the `memory_save` tool available. This allows the model to extract and persist important facts from the conversation before the context is summarized and older messages are lost. The flush runs at most once per compaction cycle to avoid excessive LLM calls.

#### Subagent Lifecycle

Subagents run on the same Orchestrator worker thread as the parent (sequential, not parallel). See [flow.md § 15](flow.md#15-subagents) for the complete step-by-step flow with examples.

1. **Spawn** — Parent calls `spawn` tool → validates depth/children limits → fires `SubagentSpawning` hook (can block) → creates SubagentRunRecord (Pending) → publishes InboundMessage to MessageBus → fires `SubagentSpawned` hook. Parent's turn continues normally.
2. **Active** — Orchestrator picks up child message from bus → updates status to Active → runs AgentLoop with minimal prompt. Progress tracked via `updateProgress()` (last 500-char snippet).
3. **Completion** — Child finishes → status updated to Completed/Errored → result enqueued in AnnounceQueue (with idempotency key) → `SubagentEnded` hook fires → if all siblings are terminal, a synthetic wake message is published to the parent's session.
4. **Wake** — Parent session receives the wake message → AnnounceQueue is drained → all child results are prepended to the message → AgentLoop processes and responds using full session history.
5. **Spawn-wake loop prevention** — Parent agents with the `spawn` tool receive orchestration guidance in the system prompt: push-based model (wait for completion events, do not poll), track expected children before answering, and reply `[SILENT]` if a completion arrives after the final answer was already sent. The `[SILENT]` token is detected by AgentLoop, which suppresses delivery and falls back to previously sent content.
6. **Timeout** — `checkTimeouts()` marks expired runs as Killed (with kill cascade to descendants) after `timeoutSeconds`.
7. **Recovery** — On startup, `recoverStaleRuns()` marks Active/Pending runs as Errored (no agent is running to complete them).

### Hook System

- **HookRunner** (`agent/HookRunner.hpp`) — Manages lifecycle hooks with 14 hook points. Hooks are registered as callbacks and executed synchronously at each point. Thread-safe via mutex.

**Hook points:**

| Hook Point | Location | Can Block | Description |
|------------|----------|-----------|-------------|
| `BeforeModelResolve` | Orchestrator | No | Before provider/model resolution |
| `BeforeAgentStart` | Orchestrator | No | After agent loop is wired, before processing |
| `BeforePromptBuild` | Orchestrator | No | Before system prompt construction |
| `AgentTurnStart` | Orchestrator | No | At the start of each agent turn |
| `AgentTurnEnd` | Orchestrator | No | After agent turn completes |
| `MessageReceived` | Orchestrator | No | When an inbound message arrives |
| `MessageSent` | Orchestrator | No | When an outbound message is sent |
| `BeforeToolCall` | AgentLoop | Yes | Before executing a tool — can block or modify arguments |
| `AfterToolCall` | AgentLoop | No | After tool execution with result and success flag |
| `BeforeCompaction` | AgentLoop | No | Before context compaction runs |
| `AfterCompaction` | AgentLoop | No | After compaction completes |
| `SubagentSpawning` | SpawnTool | Yes | Before a subagent is spawned — can block |
| `SubagentSpawned` | SpawnTool | No | After a subagent is successfully spawned |
| `SubagentEnded` | Orchestrator | No | When a subagent run completes |

**HookContext** carries `agentName`, `sessionKey`, `taskId`, `data` (JSON payload), and control fields `blocked` / `blockReason`. Blocking hooks set `blocked = true` and optionally `blockReason` to prevent the action from proceeding.

**BeforeToolCall** additionally supports parameter modification: if the hook modifies `data["arguments"]`, the tool receives the modified arguments.

### Providers

- **LlmProvider** (`provider/LlmProvider.hpp`) — Abstract base class for LLM API integration.
- **AnthropicProvider** (`provider/AnthropicProvider.hpp`) — Anthropic Claude API implementation. Supports text, image, and audio (document type) content blocks. Extended thinking via `thinking` model param.
- **OpenAiProvider** (`provider/OpenAiProvider.hpp`) — OpenAI-compatible API implementation. Supports text, image, and audio (`input_audio`) content blocks.
- **ProviderFactory** (`provider/ProviderFactory.hpp`) — Creates provider instances from config. Maps `anthropic` → AnthropicProvider, all others → OpenAiProvider.
- **FailoverProvider** (`provider/FailoverProvider.hpp`) — Wraps multiple auth profiles and retries with exponential backoff (1s–8s, 2s base for rate limits) and jitter on transient/rate-limit errors. Tracks per-profile cooldowns (60s). Each profile can define its own `model_params` that override agent-level defaults. Aborts stream retry if content was already delivered to prevent duplicate partial responses.
- **ProviderHelper** (`provider/ProviderHelper.hpp`) — Shared utilities: tool call ID sanitization (ASCII-safe, max 64 chars), malformed JSON argument repair, error classification, error message redaction (API keys, tokens), tool call input sanitization (drops entries with missing id or name), tool call name sanitization, and stream deduplication.

#### Error Classification

`ProviderHelper::classifyError()` categorizes LLM API errors into actionable types:

| Category | Trigger | Recovery Action |
|----------|---------|-----------------|
| `context_overflow` | Context length exceeded, too many tokens | Compact and retry (up to 3 attempts) |
| `rate_limit` | Rate limit hit, 429 status | Exponential backoff with 2s base |
| `billing` | Billing/quota exceeded | Fail with error |
| `auth` | Authentication failure, 401/403 | Fail with error |
| `model_not_found` | "model_not_found", "model not found", "does not exist" | Fail with error |
| `timeout` | Request timeout | Downgrade thinking level, retry |
| `transient` | 500, 502, 503, connection errors | Single retry with 2.5s delay |
| `role_ordering` | "Roles must alternate" | Clear session, retry |
| `thinking_constraint` | Reasoning budget constraint | Graduated downgrade: high→medium→low→off |
| `unknown` | Unrecognized error | Fail with error |

#### Prompt Caching (Anthropic)

For Anthropic models, the system prompt is sent with `cache_control: { type: "ephemeral" }`. This enables prompt caching — subsequent requests reuse the cached system prompt instead of re-processing it, reducing latency and token costs. Cache read/write tokens are tracked in the `UsageTracker`.

### Platform Bridge

- **PlatformBridge** (`platform/PlatformBridge.hpp`) — Singleton bridge between the C++ core and the host application. The host registers a handler via `ionclaw_set_platform_handler()` (FFI). When the `invoke_platform` tool is called, it delegates to `PlatformBridge::invoke()`, which forwards the function name and JSON params to the registered handler. If no handler is registered, it returns an error. Invocations are exception-safe to prevent crashes from handler failures.

#### Async execution flow

The platform bridge uses a blocking async pattern to integrate with the host application's event loop:

```
Agent calls invoke_platform("local-notification.send", {"title": "Hello"})
  │
  ▼
C++ InvokePlatformTool → PlatformBridge::invoke()
  │
  ▼
C++ handler lambda (set via FFI):
  1. Creates PendingRequest with mutex + condition_variable
  2. Calls Dart callback via NativeCallable.listener
  3. Blocks on condition_variable.wait_for(timeout)
  │
  ▼
Dart PlatformHandler._handle() runs on main isolate:
  1. Routes function name to the correct PlatformPlugin
  2. Plugin executes async work (await HTTP, sensors, UI, etc.)
  3. Returns result string
  │
  ▼
Dart calls ionclaw_platform_respond(requestId, result)
  │
  ▼
C++ unlocks condition_variable, returns result to agent
```

- The C++ thread blocks while Dart executes asynchronously — the agent receives the result as tool output.
- Timeout is configurable (default 30 seconds). If exceeded, returns an error.
- Thread-safe: each request gets its own `PendingRequest` with independent mutex/cv. Multiple concurrent calls are supported.
- Memory-safe: Dart allocates with `toNativeUtf8()` and frees with `calloc.free()`. C++ allocates with `std::malloc` and the caller frees with `ionclaw_free()`.

#### Flutter plugin system

The Dart side uses a plugin registry pattern (`PlatformHandler`) where each plugin declares which platforms it supports. Only plugins available on the current platform are initialized and registered.

```dart
abstract class PlatformPlugin {
  Set<String> get functions;    // function names this plugin handles
  Set<String> get platforms;    // platforms it supports: android, ios, macos, linux, windows
  bool get isAvailable;         // true if current platform is in the set
  Future<void> init();          // one-time initialization
  Future<String> handle(String function, String paramsJson);  // execute
}
```

**Built-in plugins:**

| Plugin | Functions | Platforms |
|--------|-----------|-----------|
| `_NotificationPlugin` | `local-notification.send` | android, ios, macos, linux |

**Registering custom plugins:**

```dart
// 1. Implement the plugin
class VibrationPlugin extends PlatformPlugin {
  @override Set<String> get functions => {'vibration.haptic', 'vibration.pattern'};
  @override Set<String> get platforms => {'android', 'ios'};

  @override
  Future<void> init() async {
    // request permissions, initialize SDKs, etc.
  }

  @override
  Future<String> handle(String function, String paramsJson) async {
    final params = jsonDecode(paramsJson) as Map<String, dynamic>;
    switch (function) {
      case 'vibration.haptic':
        await Vibration.vibrate(duration: params['duration'] ?? 200);
        return 'OK';
      case 'vibration.pattern':
        await Vibration.vibrate(pattern: List<int>.from(params['pattern']));
        return 'OK';
      default:
        return "Error: '$function' not implemented";
    }
  }
}

// 2. Register before initialize
PlatformHandler.register(VibrationPlugin());
await PlatformHandler.initialize();
```

The plugin's `handle()` method is fully async — it can `await` any number of Futures (HTTP calls, file I/O, camera, GPS, biometrics, database queries, etc.). The C++ side blocks until the response arrives or times out.

**Platform values** are always lowercase strings: `android`, `ios`, `macos`, `linux`, `windows`.

On platforms where no plugins are available (e.g., Windows with no registered plugins), the `PlatformHandler` does not register a handler with C++, and the `PlatformBridge` returns the default error message: `"Error: '{function}' is not implemented on {platform}."`.

#### Adding a platform skill

Each platform group should have a corresponding skill file that tells the agent which functions are available. Create a skill in `skills/` or `resources/skills/`:

```markdown
---
name: platform-ios
description: Platform functions available on iOS
type: always-on
platform: ios
---

## Available platform functions

- `local-notification.send` — Send a local push notification.
  - params: `{"title": "string", "message": "string"}`
- `vibration.haptic` — Trigger haptic feedback.
  - params: `{"duration": 200}`
```

This ensures the agent only attempts to call functions that exist on the current platform.

### Tools

- **ToolRegistry** (`tool/ToolRegistry.hpp`) — Registry of available tools. Filters by platform at registration time and supports per-agent tool policy (allow/deny lists).
- **Tool** (`tool/Tool.hpp`) — Base tool definition. Each tool declares `supportedPlatforms()` (default: all). `ToolContext` carries workspace path, session key, agent name, and pointers to shared services (config, task manager, message bus, event dispatcher, cron service, subagent registry, hook runner).
- **Platform** (`tool/Platform.hpp`) — Runtime detection returning lowercase OS name (`linux`, `macos`, `windows`, `ios`, `android`).
- **Builtin tools** (`tool/builtin/`) — Each tool in its own file. Tools declare supported platforms as string sets. Tools restricted to macOS/Linux/Windows (exec, spawn, browser) are excluded on iOS/Android.

#### Tool Policy

Each agent can configure fine-grained tool access via `tool_policy`:

- **allow** — Whitelist of tool names. Empty = all tools allowed.
- **deny** — Blacklist of tool names. Deny takes precedence over allow.

This is separate from the `tools` field (which controls tool registration). Tool policy is evaluated at runtime and allows more flexible control.

### Tasks

- **TaskManager** (`task/TaskManager.hpp`) — CRUD operations for tasks with JSONL-based persistence. Broadcasts events on task creation and status changes.
- **Task** (`task/Task.hpp`) — Task model with status tracking (TODO, DOING, DONE, ERROR).

### Sessions

- **SessionManager** (`session/SessionManager.hpp`) — Manages conversation sessions with JSONL-based persistence. Includes JSONL repair on load: corrupt lines are skipped with a `.bak` backup created automatically. Supports abort cutoff marking for recovery after crashes.
- **SessionSweeper** (`session/SessionSweeper.hpp`) — Manages session disk budget enforcement based on configurable high-water ratios.

### Memory

- **MemoryStore** (`agent/MemoryStore.hpp`) — Memory system with `MEMORY.md` (curated long-term knowledge) and daily logs (`YYYY-MM-DD.md`). Provides memory context for the system prompt and full-text search across all memory files.
- **Memory Search** — Full-text search across memory and history files with multi-language support. Features include:
  - Case-insensitive keyword extraction and matching
  - CJK (Chinese, Japanese, Korean) codepoint-level tokenization for languages without whitespace separation
  - Temporal decay scoring: recently modified files score higher
  - Stop word filtering for common English words
  - Context window around matched lines

### Config

- **Config** (`config/Config.hpp`) — All configuration structs including `AgentConfig`, `ToolPolicy`, `SubagentLimits`, `MessageQueueConfig`, `SessionBudgetConfig`.
- **ConfigLoader** (`config/ConfigLoader.hpp`) — YAML config file parser with environment variable substitution.
- **Thread safety** — Config mutations from HTTP handlers (ConfigRoutes, ChannelRoutes) are serialized via `configMutex_` in Routes to prevent concurrent read-modify-write races on `ConfigLoader::save`.

### Web Client

A pre-built web client is embedded in the `main/resources/web/` directory and served at `/app/`. It provides a chat interface, task board, and settings management.

---

## Project Structure

```
main/
├── lib/
│   ├── include/ionclaw/         # Public headers
│   │   ├── ionclaw.h            # C API for FFI (init, start, stop, platform handler, free)
│   │   ├── agent/               # Orchestrator, AgentLoop, Classifier, ContextBuilder, SkillsLoader,
│   │   │                        # SubagentRegistry, AnnounceQueue, HookRunner, ToolLoopDetector, MemoryStore
│   │   ├── bus/                  # EventDispatcher, MessageBus, Events, SessionQueue
│   │   ├── config/              # Config structs, ConfigLoader
│   │   ├── platform/            # PlatformBridge (FFI bridge to host)
│   │   ├── provider/            # LLM providers (Anthropic, OpenAI, Failover), ProviderHelper
│   │   ├── server/              # HttpServer, Routes, Auth, WebSocketManager, ServerInstance
│   │   │   └── handler/         # ApiHandler, WebSocketHandler, WebAppHandler, etc.
│   │   ├── session/             # SessionManager, SessionSweeper
│   │   ├── task/                # TaskManager, Task
│   │   ├── tool/                # ToolRegistry, Tool, Platform
│   │   │   └── builtin/         # ReadFileTool, ExecTool, BrowserTool, SpawnTool, etc.
│   │   └── util/                # HttpClient, JwtHelper, TimeHelper, StringHelper, ProcessRunner, EmbeddedResources
│   └── src/                     # Implementation files (mirrors include/ structure)
├── app/
│   └── src/main.cpp             # Desktop entry point (CLI)
└── resources/
    ├── web/                     # Embedded web client (gitignored, built from apps/web/)
    ├── skills/                  # Built-in skills (tracked in git)
    └── template/                # Project template (extracted on init)
```

---

## Startup Lifecycle

`ServerInstance::start()` performs the following steps:

1. Resolve and validate project path
2. Verify `config.yml` exists (returns error if not initialized)
3. Load `config.yml`
4. Apply command-line overrides (host, port)
5. Resolve agent workspaces
6. Create all components (dispatcher, bus, session manager, task manager, tool registry, WebSocket manager, auth)
7. Load persisted tasks and recover stuck tasks (DOING → ERROR)
8. Connect event dispatcher to WebSocket broadcast
9. Create orchestrator with all dependencies
10. Recover stale subagent runs (Active/Pending → Errored)
11. Set session abort cutoffs for any sessions interrupted by prior shutdown
12. Resolve web client (embedded resources or filesystem)
13. Create routes and HTTP server
14. Start orchestrator message processing loop
15. Start HTTP server

Project initialization is a separate step (`ionclaw_project_init` or `ionclaw-server init`). The server does not auto-create project files.

---

## Safety and Resilience

- **Prompt injection sanitization** — `StringHelper::sanitizeForPrompt()` strips Unicode control characters (Cc), format characters (Cf), line/paragraph separators, zero-width characters, and C1 controls from runtime values embedded in prompts. Applied to bot name, agent name, workspace paths, and public URL.
- **Safety guidelines** — The system prompt (Full mode) includes explicit safety constraints: no independent goals, no harm, no manipulation, no safeguard bypass, legal compliance.
- **UTF-8 safe truncation** — `StringHelper::utf8SafeTruncate()` prevents splitting multi-byte sequences when truncating strings. Applied at all truncation points across the codebase.
- **Context overflow recovery** — If the LLM returns a context overflow error, AgentLoop compacts the conversation and retries (up to 3 attempts). On subsequent attempts, tool results larger than 2000 chars are truncated before compaction.
- **Tool result context guard** — ContextBuilder enforces per-result caps (50% of context budget) and progressive oldest-first compaction to prevent tool results from consuming the entire context window. Results with error/diagnostic content in the tail receive a larger tail allocation (important-tail detection).
- **Abandoned tool call flush** — When the agent loop is stopped or aborted mid-batch, synthetic error results are injected for any tool calls that didn't execute, preventing orphaned tool call entries in the transcript.
- **Transcript repair** — After history trimming, `repairToolUseResultPairing()` fixes orphaned tool calls, duplicate results, and missing synthetic responses to maintain a valid message sequence. AnthropicProvider additionally merges consecutive tool result messages into a single user message (Anthropic requires all tool_results in one message immediately after the assistant's tool_use).
- **Error message redaction** — API keys, tokens, and bearer credentials are automatically redacted from error messages before they reach the user or logs.
- **Tool loop detection** — Four-strategy detector prevents infinite loops: generic repeat, ping-pong, poll-no-progress, and global-no-progress. Circuit breaker stops execution after 30 identical calls.
- **Spawn-wake loop prevention** — System prompt instructs parent agents with `spawn` tool to use a push-based model (wait for completion events, no polling), track expected children, and reply `[SILENT]` for late arrivals. AgentLoop detects `[SILENT]` and suppresses delivery, falling back to previously sent content.
- **Hook-based blocking** — BeforeToolCall and SubagentSpawning hooks can block execution, providing a policy enforcement layer for tool access control and subagent governance.
- **Pre-compaction memory flush** — Important facts are extracted from context via LLM call before compaction summarizes and discards older messages.
- **Thinking constraint fallback** — When the LLM rejects a thinking level (e.g. model doesn't support `high`), the agent automatically downgrades: high → medium → low → off. Timeout errors also trigger a thinking downgrade before retry.
- **Transient error retry** — HTTP 500/502/503 and connection errors trigger a single retry with a 2.5s delay.
- **Role ordering recovery** — If the LLM returns a "roles must alternate" error (malformed transcript), the session is cleared and the message is retried.
- **Stream deduplication** — Duplicate stream chunks from the provider are detected and dropped to prevent repeated content in the response.
- **Session JSONL repair** — Corrupt lines in session files are automatically skipped on load, with `.bak` backup created for recovery.
- **Stale run recovery** — Active/Pending subagent runs from prior server crashes are marked as Errored on startup.
- **Session abort recovery** — On startup, `setAbortCutoffAll()` marks all sessions that were active during the prior shutdown as aborted, preventing inconsistent state.

---

## Key Design Decisions

1. **Single binary** — No external runtime dependencies. The web client, server, and all agent logic are compiled into one executable.

2. **C API for FFI** — The library exposes `ionclaw_project_init`, `ionclaw_server_start`, `ionclaw_server_stop`, `ionclaw_set_platform_handler`, and `ionclaw_free` as C functions. This enables any host application to embed IonClaw via FFI. `ionclaw_project_init` extracts the embedded project template to create a new project directory. `ionclaw_set_platform_handler` registers a callback that receives platform function invocations from the `invoke_platform` tool — the callback receives the function name and params as JSON, and returns a malloc'd result string (freed by the C++ side).

3. **Static + shared library** — The same source builds as either a static library (linked into the desktop executable) or a shared library (for FFI), controlled by the `IONCLAW_BUILD_SHARED` CMake option.

4. **Poco HTTP** — Uses the Poco C++ Libraries for HTTP server, WebSocket, and networking. Mature, cross-platform, and well-suited for embedded server use.

5. **JSON-based C API** — The C functions return JSON strings instead of C structs. This simplifies FFI (the host just reads a string) and allows adding fields without ABI breaks.

6. **Event-driven architecture** — The EventDispatcher decouples event producers (orchestrator, task manager) from transport (WebSocket). New transports can be added by registering handlers.

7. **File-based persistence** — Sessions and tasks are stored as files in the workspace directory. No database required.

8. **Embedded resources** — Three resource sets are compiled into the binary at build time: web client (`main/resources/web/`), built-in skills (`main/resources/skills/`), and project template (`main/resources/template/`). Each is ZIP-compressed and embedded as a C++ byte array via `cmake/embed-resources.cmake`.

9. **Platform-aware tools** — Each tool declares which platforms it supports via string sets (e.g., `{"linux", "macos", "windows"}`). The `ToolRegistry` filters tools at registration time based on `Platform::current()`. Tools restricted to macOS/Linux/Windows (exec, spawn, browser) are excluded on iOS and Android. The system prompt includes the current platform and OS version so the LLM knows its runtime environment.

10. **3-tier skill loading** — Skills are loaded from three sources in priority order: embedded built-in skills (lowest), project-level skills (`{project}/skills/`), and workspace-level skills (`{workspace}/skills/`, highest). Higher priority sources override lower ones by name. Skills can also declare a `platform` frontmatter field to restrict visibility to specific operating systems (e.g., `ios`, `android`, `[linux, macos, windows]`).

11. **Platform bridge with plugin system** — The `PlatformBridge` singleton provides a clean separation between the C++ agent core and host platform capabilities. The host application registers a callback via FFI, and the `invoke_platform` tool delegates calls through the bridge. On the Flutter side, a plugin registry (`PlatformHandler`) allows each plugin to declare which platforms it supports — only plugins available on the current platform are initialized and registered with the C++ bridge. This enables platform-specific capabilities (e.g., notifications on mobile/desktop, vibration on mobile only, biometrics where available) while the agent uses a single unified `invoke_platform` tool. Platform skills tell the agent which functions exist on each platform. Platform values are always lowercase: `android`, `ios`, `macos`, `linux`, `windows`.

12. **Lifecycle hooks** — The 14-point hook system provides a non-invasive extensibility layer. Hooks observe and optionally control agent behavior without modifying core logic. Blocking hooks (BeforeToolCall, SubagentSpawning) enable policy enforcement. Parameter modification hooks enable argument rewriting.

13. **Session queue modes** — Five queue modes handle the complexity of concurrent message arrival during active agent turns. The mode hierarchy (inline > per-channel > global > default) allows fine-grained control per deployment scenario.
