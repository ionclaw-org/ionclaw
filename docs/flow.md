# How IonClaw Works

This document explains the complete execution flow of IonClaw, from the moment you start the server to the delivery of an AI response. Each step follows the actual code execution sequence.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Server Startup](#2-server-startup)
3. [Configuration Loading](#3-configuration-loading)
4. [Authentication](#4-authentication)
5. [Message Entry](#5-message-entry)
6. [Message Bus](#6-message-bus)
7. [Session Queue](#7-session-queue)
8. [Message Routing](#8-message-routing)
9. [Context Building](#9-context-building)
10. [The .md Files](#10-the-md-files)
11. [Skills](#11-skills)
12. [Agent Loop](#12-agent-loop)
13. [Provider Selection](#13-provider-selection)
14. [Tools](#14-tools)
15. [Subagents](#15-subagents)
16. [Memory and Consolidation](#16-memory-and-consolidation)
17. [Sessions](#17-sessions)
18. [Task Management](#18-task-management)
19. [WebSocket Events](#19-websocket-events)
20. [Heartbeat Service](#20-heartbeat-service)
21. [Scheduler (Cron)](#21-scheduler-cron)
22. [Output Routing](#22-output-routing)
23. [Hook System](#23-hook-system)
24. [Server Shutdown](#24-server-shutdown)
25. [Complete Cycle](#25-complete-cycle)

---

## 1. Overview

IonClaw is an AI agent orchestrator built in C++. When you run `ionclaw-server start`, a Poco HTTP server boots up and creates all necessary components. One binary — no Python, no Node, no Docker.

```
User (Browser / Telegram) ──► Input Channel
                                  │
                          WebSocket or REST API
                                  │
                                  ▼
                          Message Bus (Inbound)
                                  │
                                  ▼
                          Session Queue (mode resolution)
                                  │
                                  ▼
                              Orchestrator
                                  │
                         Agent Classifier (multi-agent)
                                  │
                                  ▼
                          Hook Runner (lifecycle hooks)
                                  │
                                  ▼
                              Agent Loop
                           ╱     │     ╲
                        LLM    Tools   Memory
                           ╲     │     ╱
                        Tool Loop Detector
                                  │
                         Compaction (context management)
                                  │
                                  ▼
                              Response
                                  │
                          Message Bus (Outbound)
                                  │
                       Event Dispatcher ──► WebSocket ──► User
```

Everything is wired together in `ServerInstance::start()`. The `ServerInstance` class handles component creation, and the startup sequence orchestrates dependency injection and service initialization.

---

## 2. Server Startup

When you run `ionclaw-server start --project /path/to/project`, the CLI parses arguments and calls `ServerInstance::start()`. The startup creates components in this order:

```
main() ──► Application::cmdStart()
               │
               ▼
         Parse CLI args (--project, --host, --port, --debug)
               │
               ▼
         Configure logging (spdlog)
               │
               ▼
         ServerInstance::start()
               │
               ├── 1.  Resolve project path (canonicalize)
               ├── 2.  Load config.yml (ConfigLoader::load)
               ├── 3.  Generate default credentials (JWT secret, web login)
               ├── 4.  Apply CLI overrides (host, port)
               ├── 5.  Resolve agent workspaces (relative → absolute)
               ├── 6.  Create Config (shared_ptr)
               ├── 7.  Create EventDispatcher
               ├── 8.  Create MessageBus
               ├── 9.  Create SessionManager (workspace/sessions)
               ├── 10. Create TaskManager (workspace/tasks.jsonl)
               ├── 11. Create ToolRegistry
               ├── 12. Create WebSocketManager
               ├── 13. Create Auth (JWT)
               ├── 14. Load persisted tasks + recover stale tasks (DOING → ERROR)
               ├── 15. Wire EventDispatcher → WebSocketManager (broadcast handler)
               ├── 16. Create Orchestrator (bus, dispatcher, session, task, tools, config)
               ├── 17. Resolve web resources (embedded or filesystem)
               ├── 18. Create ChannelManager + start enabled channels
               ├── 19. Create HeartbeatService + start
               ├── 20. Create CronService + start
               ├── 21. Create Routes + HttpServer
               ├── 22. Start Orchestrator (background worker thread)
               └── 23. Start HttpServer (Poco listener)
               │
               ▼
         Set signal handlers (SIGINT, SIGTERM)
               │
               ▼
         Wait for shutdown signal
```

Each agent gets its own `AgentLoop` with:
- Its own LLM provider (Anthropic, OpenAI, or FailoverProvider)
- Its own workspace directory (resolved at startup)
- A `ToolRegistry` filtered by the agent's `tools` whitelist and `tool_policy`
- A shared `SubagentRegistry` for spawn tracking
- A shared `HookRunner` for lifecycle hooks

During startup (step 14), the TaskManager loads persisted tasks and recovers any that were interrupted by a previous shutdown. Tasks left in the DOING state are reset to ERROR. Additionally, when the Orchestrator starts, it recovers stale subagent runs and sets abort cutoffs on active sessions.

---

## 3. Configuration Loading

`ConfigLoader::load()` does the following:

1. **Reads `config.yml`**: Parses the YAML file
2. **Expands environment variables**: Substitutes `${VAR}` patterns using `std::getenv()`, recursively (max 20 iterations to prevent infinite loops)
3. **Type-safe extraction**: `expandStr()`, `expandInt()`, `expandBool()` with error handling for malformed values

Environment variables referenced in `config.yml` must be set in the process environment before starting IonClaw. You can use a `.env` file with your shell or deployment tool — IonClaw itself reads from the process environment via `std::getenv()`.

```yaml
# config.yml
credentials:
  anthropic:
    type: simple
    key: ${ANTHROPIC_API_KEY}  # replaced with the actual value
```

### Important Defaults

| Setting | Default |
|---|---|
| Server | `host: 0.0.0.0`, `port: 8080` |
| Agent params | `max_iterations: 40`, `memory_window: 100`, `max_concurrent: 1` |
| Model params | `{}` (empty — set per provider or agent) |
| Exec timeout | `60` seconds |
| Queue mode | `collect` with 1000ms debounce |
| Heartbeat | `enabled: false`, `interval: 1800` (30 minutes) |

### Default Credentials

On first startup, IonClaw creates two default credentials if missing:

1. **Server JWT secret** — a random 64-character hex string for signing tokens
2. **Web client login** — `admin`/`admin` for the web panel

Both are persisted to `config.yml` so tokens remain valid across restarts.

---

## 4. Authentication

The authentication system uses **JWT (JSON Web Tokens)** to protect API routes.

```
User (Browser)                              Server
      │                                        │
      ├── POST /api/auth/login ──────────────► │ Validate credentials
      │   {username, password}                 │
      │ ◄──────────────────── {token: "eyJ.."} │ JWT valid for 24h
      │                                        │
      ├── GET /api/tasks ────────────────────► │ Verify JWT
      │   Header: Bearer eyJ...                │
      │ ◄────────────────────── [task list]    │
      │                                        │
      ├── WebSocket /ws?token=eyJ... ────────► │ Verify JWT via query param
      │ ◄───────────────── Connection accepted │
```

### Flow Details

1. **Login**: `POST /api/auth/login` with username and password. Returns a JWT signed with HS256, valid for **24 hours**
2. **API routes**: All routes under `/api/` require the `Authorization: Bearer {token}` header
3. **WebSocket**: Token passed as query parameter (`/ws?token=...`) and validated on connection
4. **Public routes**: `/api/auth/login`, `/api/health`, `/app/*` (static frontend), `/public/*` (static files). WebSocket (`/ws`) bypasses API auth entirely — it handles token verification internally via query parameter

---

## 5. Message Entry

A message enters the system through three paths:

### Via WebSocket (Browser)

1. The user opens the browser at `http://localhost:8080`
2. The web client connects via **WebSocket** at `/ws?token={jwt}`
3. The `WebSocketHandler` upgrades the HTTP connection and registers it with the `WebSocketManager`
4. When the user sends a message, the client sends a JSON frame:

```json
{
  "type": "chat.send",
  "data": {
    "message": "Hello, how are you?",
    "session_id": "direct"
  }
}
```

5. WebSocket `chat.send` messages are logged for debugging. The actual chat sending happens through the REST API (`POST /api/chat`), which the web client calls alongside the WebSocket connection

### Via REST API

```
POST /api/chat
{
  "message": "Hello",
  "session_id": "direct"
}
```

The route does the following before returning:

1. Creates a task and injects the `task_id` into the message metadata
2. Ensures the session exists: `sessionManager->ensureSession("web:{chatId}")`
3. Persists the user message immediately: `sessionManager->addMessage(sessionKey, userMsg)`
4. Broadcasts `sessions:updated` so connected clients see the new session right away
5. Publishes the `InboundMessage` to the bus with `message_saved: true` in metadata

Returns `{"task_id": "abc123", "session_id": "web:direct"}`. Because the session is saved before the response, a page refresh always shows the session and the user's message — even if the agent hasn't started processing yet.

**Steer optimization**: If the session has an active agent turn and the queue mode is steer-compatible, the message is injected directly into the `SessionQueue`, bypassing the MessageBus for lower latency.

### Via Telegram

1. The `TelegramRunner` performs periodic polling on the Telegram API to fetch new messages
2. Upon receiving a message, it creates an `InboundMessage` with `channel="telegram"` and Telegram metadata
3. If the message contains media, files are downloaded to `public/media/` and paths added to the `media` list
4. The message is published to the same bus inbound queue

### InboundMessage Structure

In all cases, the message becomes an `InboundMessage`:

| Field | Description |
|---|---|
| `channel` | Source: `"web"`, `"telegram"`, `"cron"`, or `"heartbeat"` |
| `senderId` | Who sent it (e.g., `"web-user"`, Telegram user ID) |
| `chatId` | Conversation identifier |
| `content` | Message text |
| `media` | List of attached file paths |
| `metadata` | Extra data (`task_id`, `message_saved`, etc.) |
| `queueMode` | Optional override for queue mode resolution |

The session key is derived as `channel:chatId` (e.g., `web:direct`).

---

## 6. Message Bus

The Message Bus is the central communication channel. It has two thread-safe queues:

```
Web (WebSocket) ──► ┌─────────────────────┐
Telegram ──────────►│  Inbound Queue      │──► Orchestrator
Cron ──────────────►│                     │
Heartbeat ─────────►└─────────────────────┘

                    ┌─────────────────────┐
Orchestrator ──────►│  Outbound Queue     │──► Channel Manager
                    │                     │      ├──► WebSocket
                    └─────────────────────┘      └──► Telegram API
```

The bus uses C++ `std::queue` with mutex and condition variable synchronization. The producer does not wait for the consumer — they are independent threads.

### Deduplication

Before enqueuing, the bus checks for duplicates using a fingerprint: `channel:chatId:senderId:hash(content)`. Messages with the same fingerprint within a 5-second window are dropped. Synthetic messages (e.g., subagent completions) bypass dedup.

The bus **decouples** channels from the agent. Telegram knows nothing about the AgentLoop, and the AgentLoop knows nothing about Telegram. They only know the bus.

---

## 7. Session Queue

When a message arrives while the agent is already processing a request for the same session, the `SessionQueue` handles the collision. Five modes control the behavior:

| Mode | Behavior |
|------|----------|
| `steer` | Inject into the active turn between tool iterations |
| `followup` | Enqueue and process as a separate turn after the current one completes |
| `collect` | Batch multiple messages into a single prompt after a debounce period |
| `steer-backlog` | Try steer first; if not streaming, fall back to followup |
| `interrupt` | Abort the current turn, clear the queue, process the new message immediately |

### Mode Resolution

The queue mode is resolved with this priority:

1. **Inline mode** from the message itself (explicit override)
2. **Per-channel override**: `config.messages.queue.byChannel[channel]`
3. **Global default**: `config.messages.queue.mode` (default: `collect`)

### Drop Policy

When the queue depth exceeds the configured cap (default: 20), a drop policy kicks in:

| Policy | Behavior |
|--------|----------|
| `old` | Drop oldest items |
| `new` | Reject new items |
| `summarize` | Drop oldest but keep summary lines (collapsed to 160 chars) for context |

---

## 8. Message Routing

The `Orchestrator` runs in a background worker thread, consuming messages from the inbound queue:

```cpp
while (running.load())
{
    if (bus->consumeInbound(message, 500))  // 500ms timeout
    {
        handleMessage(message);
    }
}
```

The 500ms timeout ensures the stop flag is checked regularly for graceful shutdown.

### Routing Steps

1. **Extract session key**: `message.sessionKey()` → `"web:direct"`
2. **Fire hook**: `MessageReceived`
3. **Resolve queue settings**: inline → per-channel → global
4. **Check if session is active**: maintained in `activeTurns_` map
5. **If NOT active**: process immediately via `processMessageDirect()`
6. **If ACTIVE**: route by queue mode (steer, followup, collect, etc.)

### Agent Classification (Multi-Agent)

When multiple agents are configured, the `Classifier` uses an LLM call to determine the best agent. It analyzes the user's message plus recent conversation history and returns the target agent name. For single-agent setups, messages go directly to the default agent with no classification overhead.

**Session affinity**: Once an agent handles a session, the affinity is stored in `session.liveState["agentAffinity"]`. Subsequent messages to the same session go to the same agent — the classifier is not called again for sessions that already have affinity.

### Per-Agent Concurrency

Each agent has a configurable `maxConcurrent` limit (default: 1). If the agent is already processing the maximum number of concurrent turns, the new message is queued as a followup.

### Direct Processing Flow

`processMessageDirect()` executes the following sequence:

1. Drain announce queue (collect subagent completion results)
2. Resolve target agent (affinity → classify → fallback to first)
3. Enforce per-agent concurrency limits
4. Create `ActiveTurnHandle` (tracks agent name, task ID, start time, abort flag)
5. Fire `AgentTurnStart` hook
6. Fire `BeforePromptBuild` hook
7. Build system prompt via `ContextBuilder`
8. Call `AgentLoop::processMessage()` with event broadcast callback
9. Clear active turn
10. Fire `MessageSent` and `AgentTurnEnd` hooks
11. Handle subagent completion (if this was a subagent turn)
12. Drain session queue (process followup/collect items)

---

## 9. Context Building

The `ContextBuilder` assembles the system prompt — the text that tells the LLM who it is, what it knows, and what rules it follows. The prompt is built in this order:

```
1.  Identity ("You are IonClaw, a personal AI assistant.")
2.  Agent name (if multi-agent: "You are acting as the **research** agent.")
3.  Safety section (no harm, no manipulation, follow laws) [Full only]
4.  Current date and time
5.  Runtime (platform, OS version)
6.  Channel guidance (web: Markdown freely; telegram: under 4096 chars)
7.  Public URL (if configured)
8.  Directory context (workspace, public paths) [Full only]
9.  Response guidelines (be concise, use Markdown, etc.) [Full only]
10. Available tools list
11. Bootstrap files (AGENTS.md, SOUL.md, USER.md, TOOLS.md) [Full only]
12. Memory (MEMORY.md content) [Full only]
13. Always-on skills [Full only]
14. Available skills summary [Full only]
15. Agent instructions (from config)
```

### Prompt Modes

| Mode | When Used | Includes |
|------|-----------|----------|
| `Full` | Main agents | All 15 sections |
| `Minimal` | Subagents | Identity, agent name, date/time, runtime, channel guidance, public URL, directory context, tools, agent instructions |

The minimal mode skips safety, response guidelines, bootstrap files, memory, and skills to save tokens for subagent calls.

### Bootstrap File Truncation

Large bootstrap files are truncated to prevent them from consuming the context window:

- **70% head + 20% tail** of each file (preserves beginning and ending context)
- **20K max per file**
- **80K max total** across all bootstrap files
- UTF-8-safe truncation at all cut points

### Prompt Injection Sanitization

Runtime values (bot name, agent name, workspace paths, public URL) are sanitized with `sanitizeForPrompt()`, which strips Unicode control characters, format characters, line/paragraph separators, and zero-width characters before embedding them in the prompt.

---

## 10. The .md Files

These files reside at the project root (where `config.yml` lives) and define the agent's personality and context. They are read in this fixed order: AGENTS.md, SOUL.md, USER.md, TOOLS.md.

### AGENTS.md — Agent Descriptions

Describes the available agents:

```markdown
## Main Agent
General-purpose assistant capable of file operations, web search, and code execution.
```

### SOUL.md — The Agent's Personality

Defines personality, tone, and behavior rules:

```markdown
You are a helpful AI assistant.
- Always be polite and professional
- Respond in the same language as the user
- When unsure, ask for clarification
```

### USER.md — User Information

Information about the user for personalized responses:

```markdown
- Name: Paulo
- Preferred language: Portuguese
- Timezone: America/Sao_Paulo
```

### TOOLS.md — Tools Documentation

Additional instructions on tool usage:

```markdown
## File Operations
When reading files, always check if the file exists first.
```

All these files are **optional**. If they don't exist, the agent works with the base identity. They are read **every time** a message is processed, so edits take effect immediately — no restart needed.

---

## 11. Skills

Skills extend the agent with extra knowledge and workflows, written in Markdown.

### Skill Discovery (Priority Order)

Skills are loaded from three sources. Higher-priority sources override lower ones by name:

1. **Embedded** — built-in skills compiled into the binary (lowest)
2. **Project** — `{project}/skills/` (shared across all agents)
3. **Workspace** — `{workspace}/skills/` (agent-specific, highest)

### SKILL.md Format

Each skill is a folder containing a `SKILL.md` file with YAML frontmatter:

```markdown
---
name: code-review
description: Reviews code for bugs and improvements
always: false
platform: [linux, macos, windows]
---

When asked to review code, analyze it for:
1. Bugs and logic errors
2. Security vulnerabilities
3. Performance issues
```

### Frontmatter Fields

| Field | Description |
|---|---|
| `name` | Skill identifier |
| `description` | Short description (shown in skills summary) |
| `always` | `true` (included in every prompt automatically) or `false`/absent (available on demand) |
| `platform` | Restrict to specific platforms (e.g., `ios`, `[linux, macos, windows]`) |

### How Skills Are Used

- **Always-on skills** (`always: true`) are automatically injected into the system prompt
- **On-demand skills** (`always: false` or absent) appear in the available skills summary. The LLM can read the full skill file when needed
- Skills can declare a `platform` to restrict visibility to specific operating systems

### Marketplace

A built-in marketplace lets you browse and install community skills from the web UI — at project level or per agent — without editing files.

---

## 12. Agent Loop

The Agent Loop is the core processing engine. It implements the **ReAct** pattern (Reason + Act): the LLM thinks, calls tools, observes results, and repeats.

### processMessage()

When the Orchestrator calls `AgentLoop::processMessage()`, the following happens:

1. Update task state to **DOING**
2. Broadcast user message event (if not pre-saved)
3. Ensure session exists
4. Handle special commands (`/new` clears session, `/help` lists commands)
5. Create `ToolContext` (workspace paths, service pointers, hook runner)
6. Resolve media (images → base64 content blocks, audio → transcription)
7. Save user message to session (if not pre-saved by REST API)
8. Build message history from session (respecting `memory_window`)
9. Handle abort recovery (trim messages past crash cutoff)
10. Prepend working directory context: `[cwd: /path/to/workspace]`
11. Assemble messages: `[system prompt] + [history] + [user message]`
12. Validate prompt size (compact if > 10MB)
13. Run the agent loop

### runAgentLoop()

The main iteration loop (default: up to 40 iterations):

```
FOR EACH ITERATION:
    │
    ├── Check abort flags (stopped, active turn aborted)
    ├── Poll steer messages (inject between iterations)
    ├── Trim history (max_history limit)
    ├── Repair tool use/result pairing (fix orphans after trim)
    ├── Check context guard (force compact if critical)
    ├── Compact if approaching context limit (85% threshold)
    ├── Clean context:
    │     ├── Strip thinking from older messages
    │     ├── Prune images (keep last 4)
    │     └── Enforce tool result budget (60% of context)
    │
    ├── Broadcast "chat:typing" event
    ├── Stream response from LLM (consumeStream)
    │
    ├── IF ERROR:
    │     ├── context_overflow → compact + retry (up to 3 attempts)
    │     ├── role_ordering → clear session + return error
    │     ├── thinking_constraint → downgrade (high→medium→low→off) + retry
    │     ├── timeout → reduce thinking + retry with 2.5s delay
    │     └── transient (500/502/503, network errors) → single retry with 2.5s delay
    │
    ├── IF TOOL CALLS:
    │     FOR EACH TOOL CALL:
    │         ├── Fire BeforeToolCall hook (can block or modify args)
    │         ├── Execute tool via ToolRegistry
    │         ├── Fire AfterToolCall hook
    │         ├── Append assistant + tool result messages
    │         ├── Detect tool loops
    │         └── Broadcast tool events
    │     CONTINUE LOOP
    │
    └── IF TEXT ONLY:
          BREAK (end of turn)
```

After the loop:

1. Handle `[SILENT]` response (use last sent content)
2. Save assistant response to session
3. Publish outbound message (for non-WebSocket channels)
4. Update task to **DONE**
5. Emit final `chat:message` event

### Token Streaming

The agent loop uses streaming by default. As the LLM generates tokens:

- `content` tokens are broadcast immediately as `chat:stream` events
- `reasoning` tokens are accumulated and broadcast as `chat:thinking` after the stream ends
- `tool_call` deltas are assembled into complete tool calls
- `usage` stats are tracked by the `UsageTracker`

### Extended Thinking

Some models support extended thinking — exposing internal chain-of-thought. The thinking budget is configured via `model_params.thinking`:

| Level | Description |
|-------|-------------|
| `off` | No extended thinking (default) |
| `low` | Light reasoning for simple tasks |
| `medium` | Balanced reasoning for moderate tasks |
| `high` | Deep reasoning for complex tasks |

Reasoning content is broadcast once for real-time display, stored in the session for history, but **stripped before sending to the LLM** on subsequent turns. It is not re-injected into future calls.

**Thinking constraint fallback**: If the model rejects a thinking level, IonClaw automatically downgrades: high → medium → low → off. Timeout errors also trigger a thinking downgrade before retry.

### Context Window Management

Long conversations can exceed the LLM's context window. The agent loop handles this automatically:

1. **Token estimation** — chars/4 heuristic with safety margin
2. **Compaction trigger** — when estimated tokens exceed 85% of the model's limit
3. **Compaction process**:
   - Separate system messages from conversation
   - Take oldest messages for summarization
   - Strip tool results to 2000 chars, remove reasoning content
   - Call LLM with summarization prompt (preserves key decisions, file paths, IDs, URLs, errors)
   - Return `[system] + [summary] + [acknowledgment] + [recent messages]`
4. **3-tier fallback**: full summary → exclude oversized messages → text-only note
5. **Error recovery** — if context overflow still occurs, compact and retry (up to 3 attempts, with progressive tool result truncation on retries)

### Pre-Compaction Memory Flush

Before compaction runs (once per compaction cycle), the agent loop attempts a memory flush: a single LLM call with only the `memory_save` tool. This lets the model extract and persist important facts before older messages are summarized and discarded.

### Tool Loop Detection

Four detection strategies prevent infinite loops:

| Detector | Description |
|----------|-------------|
| **Generic Repeat** | Same tool with identical arguments. Escalates: Warning → Critical → Circuit Breaker |
| **Ping-Pong** | Alternating A-B-A-B pattern between two tools |
| **Poll No-Progress** | Known polling tools (`exec`, `http_client`, `web_fetch`, `browser`) returning identical results |
| **Global No-Progress** | Any tool producing identical results consecutively |

Default thresholds: Warning at 10, Critical at 20, Circuit Breaker at 30. Warning emissions are bucketed (every 10 occurrences) to avoid flooding logs. Each detector can be individually enabled/disabled via `ToolLoopConfig`.

### Iteration Limit

The loop has a maximum iteration limit (default: **40**, configurable via `agent_params.max_iterations`). If the agent hits this limit without reaching a final response, processing stops.

---

## 13. Provider Selection

IonClaw supports multiple LLM providers. The `ProviderFactory` maps provider names to implementations:

| Provider Name | Implementation | API |
|---|---|---|
| `anthropic` | `AnthropicProvider` | Native Anthropic API |
| All others | `OpenAiProvider` | OpenAI-compatible API |

This means `openai`, `gemini`, `grok`, `openrouter`, `deepseek`, `kimi`, `ollama`, `minimax`, `together`, and any custom provider all use the same `OpenAiProvider` with different base URLs and credentials.

### Provider Resolution

When a model string like `anthropic/claude-sonnet-4-20250514` is used:

1. Extract the prefix before `/` (e.g., `anthropic`)
2. Look up the provider config by name
3. Resolve credentials from the config
4. Create the appropriate provider instance

### Model Parameters Merge

Parameters are resolved with a three-level merge chain:

1. **Provider-level** `model_params` — base defaults for all agents using this provider
2. **Agent-level** `model_params` — overrides provider defaults
3. **Profile-level** `model_params` — overrides agent defaults (failover profiles only)

### Failover Provider

When an agent has multiple profiles, `ProviderFactory` creates a `FailoverProvider` that wraps them:

- Profiles are tried in priority order (lower number = higher priority)
- Exponential backoff between retries: 1s → 2s → 4s → 8s max, with random jitter (0–500ms)
- Rate-limit errors use a 2s base delay instead of 1s
- Each provider has a 60-second cooldown after failure
- Stream requests that already delivered content are **not** retried (prevents duplicate partial content)
- Each profile applies its own `model_params` before delegating to the underlying provider

### Prompt Caching (Anthropic)

For Anthropic models, the system prompt is sent with `cache_control: { type: "ephemeral" }`. This enables prompt caching — subsequent requests reuse the cached system prompt instead of re-processing it, reducing latency and token costs.

### Error Classification

LLM API errors are classified into actionable categories:

| Category | Trigger | Recovery |
|----------|---------|----------|
| `context_overflow` | Context too large | Compact and retry (up to 3 attempts) |
| `rate_limit` | 429, too many requests | Exponential backoff (FailoverProvider) |
| `billing` | Quota exceeded | Fail with error |
| `auth` | 401, invalid key | Fail with error |
| `model_not_found` | Model does not exist | Fail with error |
| `timeout` | Request timeout | Downgrade thinking, retry |
| `transient` | 500, 502, 503, connection errors | Single retry with 2.5s delay |
| `role_ordering` | Roles must alternate | Clear session, return error |
| `thinking_constraint` | Reasoning budget rejected | Downgrade thinking level, retry |

Error messages are automatically **redacted** — API keys, tokens, and bearer credentials are stripped before reaching the user or logs.

---

## 14. Tools

Tools are capabilities that the agent uses to interact with the world. Each tool is a C++ class that inherits from `Tool` and implements `execute()`.

### Tool Registry

The `ToolRegistry` manages all tools:

- **Registration**: Filtered by platform at registration time (e.g., `exec` excluded on iOS/Android)
- **Parameter validation**: Against JSON schema before execution
- **Policy enforcement**: Per-agent allow/deny lists (deny takes precedence)
- **Output truncation**: Context-aware, proportional to model's context window
- **Error handling**: Appends "[Analyze the error and try a different approach]" hint

### Built-in Tools

| Tool | Description | Platforms |
|---|---|---|
| `read_file` | Read file contents | All |
| `write_file` | Write content to a file | All |
| `edit_file` | Edit parts of an existing file | All |
| `list_dir` | List files and folders | All |
| `exec` | Execute shell commands | Desktop only |
| `web_search` | Search the web (Brave, DuckDuckGo) | All |
| `web_fetch` | Fetch content from a URL | All |
| `http_client` | HTTP requests with download/upload | All |
| `rss_reader` | Read RSS/Atom feeds | All |
| `message` | Send intermediate messages to the user | All |
| `spawn` | Create subagents for parallel tasks | Desktop only |
| `subagents` | List, kill, check status of subagent runs | Desktop only |
| `cron` | Schedule recurring or one-time tasks | Desktop only |
| `memory_save` | Save information to long-term memory | All |
| `memory_read` | Read MEMORY.md or HISTORY.md on demand | All |
| `memory_search` | Search across memory files | All |
| `browser` | Browser automation (Chrome via CDP) | Desktop only |
| `generate_image` | Generate images via AI | All |
| `image_ops` | Local image operations (resize, draw, watermark) | All |
| `invoke_platform` | Call host platform functions (FFI bridge) | All |

### Workspace Restriction

File tools, HTTP client, and shell execution are restricted to:

1. The agent's own workspace directory
2. The shared `public/` directory at the project root

Paths outside these boundaries raise an error. In multi-agent setups, agents cannot access each other's workspaces.

### Tool Policy

Per-agent runtime access control, separate from the `tools` whitelist:

```yaml
agents:
  main:
    tool_policy:
      deny: [exec, spawn]   # block dangerous tools
  coder:
    tool_policy:
      allow: [read_file, write_file, edit_file, list_dir, exec]
      deny: [spawn]          # allow exec but not spawn
```

---

## 15. Subagents

Subagents are independent agents that run in parallel to execute specific tasks. The main agent creates them using the `spawn` tool.

### Spawn Flow

```
User ──► Agent Loop: "Research topic X and Y"
              │
              ├── spawn(task="research topic X")
              │     └── SubagentRegistry creates run (status=Pending)
              │     └── Publishes InboundMessage to bus
              │     └── Returns "Subagent spawned (task: abc123)"
              │
              ├── spawn(task="research topic Y")
              │     └── Same flow (task: def456)
              │
              └── "I've started two research tasks."
                    (Main agent's task is DONE)

         ... Minutes later ...

    Subagent completes ──► AnnounceQueue ──► Parent session
              │
              └── "[Subagent abc123 completed]: findings about X..."
                    (New InboundMessage to parent session)
                    (Agent reads full history, sees its spawn decision)
                    (Formulates response using the result)
```

The agent "remembers" because of the **session history** — not because of any callback or state machine.

### Subagent Limits

| Limit | Default | Description |
|---|---|---|
| `MAX_DEPTH` | 5 | Maximum nesting depth |
| `MAX_CHILDREN` | 5 | Maximum concurrent children per session |
| `DEFAULT_TIMEOUT` | 300s | Per-spawn timeout (configurable) |

### Differences from Main Agent

| Feature | Main Agent | Subagent |
|---|---|---|
| System prompt | Full (all 15 sections) | Minimal (identity, tools, workspace, runtime) |
| Session history | Yes | No |
| Memory | Yes | No |
| Max iterations | Configurable (default 40) | Configurable per agent |
| Tools | All configured | Same `tool_policy` pipeline applies |
| Model | Configurable per agent | Uses target agent's model (can be overridden via `spawn` tool) |
| Prompt mode | Full | Minimal |

### Kill Cascade

When a subagent is killed (timeout or manual), the kill cascades through all descendants via BFS through the `childSessionKey → requesterSessionKey` tree.

### Subagent Hooks

- **SubagentSpawning** — fired before spawn, can **block** (with reason)
- **SubagentSpawned** — fired after successful spawn
- **SubagentEnded** — fired when a subagent run completes

---

## 16. Memory and Consolidation

The memory system allows the agent to remember important information across conversations.

### Structure

Memory resides in the `memory/` folder of the workspace:

```
workspace/
  memory/
    MEMORY.md     # Long-term memory (facts, preferences)
    HISTORY.md    # Consolidated history (conversation summaries)
    topic-1.md    # Optional topic files
```

### How Memory Is Used

Memory is loaded **on every message**, not once at startup. The `ContextBuilder` reads `memory/MEMORY.md` from disk each time it assembles the system prompt. Changes to the file (from consolidation, the `memory_save` tool, or manual editing) take effect immediately on the next message.

The content is injected as a `# Memory` section in the system prompt, after the bootstrap files and before the skills.

| File | Purpose | How It Changes |
|---|---|---|
| `MEMORY.md` | Active long-term memory. Included in every system prompt | Overwritten on consolidation. Also written by `memory_save` tool |
| `HISTORY.md` | Chronological conversation summaries. Not included in the prompt | Appended to on each consolidation |

Only `MEMORY.md` affects the agent's behavior. `HISTORY.md` is purely archival.

### Automatic Consolidation

Consolidation runs automatically when unconsolidated messages exceed `memory_window` (default: **100 messages**):

1. Extract unconsolidated messages (only user and assistant text — tool calls/results excluded)
2. Create a temporary consolidation agent with access only to `memory_save`
3. LLM analyzes the messages and calls `memory_save(history_entry, updated_memory)`
4. `HISTORY.md` gets a new timestamped summary appended
5. `MEMORY.md` is overwritten with updated content
6. Session's `lastConsolidated` index is updated

### Memory Search

The `memory_search` tool provides full-text search across memory files:

- **Multi-language support**: stop word filtering for EN, PT, ES, ZH, JA, KO, AR
- **CJK tokenization**: character unigrams + bigrams for Chinese/Japanese, Hangul support
- **Temporal decay**: half-life of 30 days (recently modified files score higher)
- **Evergreen files**: MEMORY.md and HISTORY.md do not decay
- **Keyword ratio scoring** (`matched_keywords / total_keywords * decay_multiplier`)
- Returns top 20 results sorted by relevance

---

## 17. Sessions

The `SessionManager` provides JSONL-based conversation persistence.

### Session Key Format

Sessions are identified by `channel:chatId` (e.g., `web:direct`, `telegram:12345`).

### Persistence

Each session is stored as a `.jsonl` file (one JSON message per line). On load, corrupt lines are automatically skipped with a `.bak` backup created for recovery.

### Thread Safety

- **Global mutex** protects the session cache map
- **Per-session mutex** protects individual session read/write operations
- `getOrCreate()` returns sessions by **value** (copy), not reference — prevents use-after-free from concurrent cache eviction
- `ensureSession()` is available for callers that only need creation (no copy overhead)

### Session Features

- **Abort recovery**: On startup, `setAbortCutoffAll()` marks sessions that were active during the prior shutdown as aborted
- **Idle eviction**: Sessions are evicted from cache after configurable TTL
- **Disk budget**: `SessionSweeper` enforces total disk usage with high-water ratio cleanup
- **Per-channel history limits**: Different channels can have different `max_history` values

---

## 18. Task Management

The `TaskManager` tracks every agent execution as a task.

### Task States

```
Todo ──► Doing ──► Done
                └──► Error
```

### Task Lifecycle

1. **Creation**: REST API creates a task before publishing the message. Returns `task_id` to the client
2. **Processing**: Agent loop sets task to DOING and updates `agent_name`
3. **Iteration tracking**: Each agent loop iteration increments the task's iteration counter
4. **Completion**: Task set to DONE with a result preview (first 200 chars)
5. **Error**: If the agent loop throws, task set to ERROR with the error message

### Persistence

Tasks are stored in a JSONL file (`workspace/tasks.jsonl`). On startup, tasks left in DOING are recovered to ERROR.

### Event Broadcasting

Task creation and state changes are broadcast via the EventDispatcher, so the web panel updates the task board in real time.

---

## 19. WebSocket Events

The `EventDispatcher` broadcasts events to all connected WebSocket clients. Events include:

| Event | Description |
|---|---|
| `chat:typing` | Agent is processing (show typing indicator) |
| `chat:stream` | Streaming content tokens (accumulated by frontend) |
| `chat:stream_end` | End of streaming response |
| `chat:message` | Final complete response (content block array) |
| `chat:thinking` | Reasoning/thinking step |
| `chat:tool_use` | Tool invocation (human-readable name + description) |
| `chat:user_message` | Non-web user message (e.g., Telegram, cron) |
| `chat:transcription` | Audio transcription result |
| `chat:warning` | Warning from agent loop (e.g., tool loop detected) |
| `task:created` | New task |
| `task:updated` | Task state change |
| `sessions:updated` | Session list changed (reload sidebar) |

All `chat:*` events include `agent_name` for multi-agent identification and `chat_id` for session routing.

### Thread Safety

The EventDispatcher copies its handler list under a mutex and invokes handlers outside the lock. This prevents deadlocks from re-entrant hooks and ensures handler exceptions don't affect other handlers.

---

## 20. Heartbeat Service

The `HeartbeatService` sends periodic check-in messages to the agent. When enabled:

1. Publishes an `InboundMessage` with `channel="heartbeat"` at the configured interval (default: 1800 seconds / 30 minutes)
2. The agent processes it like any other message, allowing it to perform scheduled self-checks

---

## 21. Scheduler (Cron)

The `CronService` manages scheduled tasks. It supports:

- **Cron expressions** — standard cron syntax (e.g., `0 9 * * *` for daily at 9am)
- **Intervals** — run every N seconds/minutes/hours
- **One-shot** — run once at a specific time

When a scheduled task triggers, the CronService publishes an `InboundMessage` with `channel="cron"` to the bus. The orchestrator processes it like any other message, creating a task and routing to the appropriate agent.

Scheduled tasks appear on the task board with full tracking — you see every execution, its status, and the result.

---

## 22. Output Routing

After the agent produces a response, it flows through two paths:

### WebSocket (Browser)

Events are broadcast in real time during processing (`chat:stream`, `chat:thinking`, `chat:tool_use`). The final response is delivered as a `chat:message` event. No outbound queue needed — events go directly to connected clients.

### Other Channels (Telegram, etc.)

The response is published as an `OutboundMessage` to the bus outbound queue. The `ChannelManager` consumes outbound messages and routes them to the appropriate channel handler (e.g., Telegram API).

---

## 23. Hook System

IonClaw provides a 14-point lifecycle hook system for observing and controlling agent behavior.

### Hook Points

| Hook Point | Location | Can Block | Description |
|---|---|---|---|
| `BeforeModelResolve` | Orchestrator | No | Before provider/model resolution |
| `BeforeAgentStart` | Orchestrator | No | After agent loop is wired |
| `BeforePromptBuild` | Orchestrator | No | Before system prompt construction |
| `AgentTurnStart` | Orchestrator | No | At the start of each turn |
| `AgentTurnEnd` | Orchestrator | No | After turn completes |
| `MessageReceived` | Orchestrator | No | When an inbound message arrives |
| `MessageSent` | Orchestrator | No | When an outbound message is sent |
| `BeforeToolCall` | AgentLoop | **Yes** | Before executing a tool |
| `AfterToolCall` | AgentLoop | No | After tool execution |
| `BeforeCompaction` | AgentLoop | No | Before context compaction |
| `AfterCompaction` | AgentLoop | No | After compaction completes |
| `SubagentSpawning` | SpawnTool | **Yes** | Before a subagent is spawned |
| `SubagentSpawned` | SpawnTool | No | After a subagent is spawned |
| `SubagentEnded` | Orchestrator | No | When a subagent run completes |

### Blocking Hooks

`BeforeToolCall` and `SubagentSpawning` can set `blocked = true` with an optional `blockReason`. When blocked:
- **BeforeToolCall**: The tool is not executed; the block reason is returned as the tool result
- **SubagentSpawning**: The subagent is not spawned; the block reason is returned to the LLM

### Parameter Modification

`BeforeToolCall` can modify `data["arguments"]` in the hook context. The tool receives the modified arguments.

### Thread Safety

The `HookRunner` copies its callback list under a mutex and invokes callbacks outside the lock, preventing deadlocks from re-entrant hooks.

---

## 24. Server Shutdown

When a shutdown signal is received (SIGINT or SIGTERM), `ServerInstance::stop()` executes:

```
ServerInstance::stop()
    │
    ├── 1.  Mark active sessions as aborted (setAbortCutoffAll)
    ├── 2.  Stop HTTP server
    ├── 3.  Stop all channels (Telegram, etc.)
    ├── 4.  Stop heartbeat service
    ├── 5.  Stop cron service
    ├── 6.  Stop orchestrator (signals worker thread to exit)
    ├── 7.  Persist tasks to JSONL
    └── 8.  Reset all components in reverse creation order
```

The abort cutoff ensures that sessions interrupted by the shutdown can be properly recovered on the next startup.

---

## 25. Complete Cycle

Here is the full lifecycle of a single user message, from entry to response:

```
User types "List the files in docs/" in the browser
    │
    ▼
WebSocket frame → WebSocketHandler
    │
    ▼
ChatRoutes::handleChatSend
    ├── Create task (state: TODO)
    ├── Save user message to session
    ├── Broadcast "sessions:updated"
    └── Publish InboundMessage to bus
    │
    ▼
MessageBus::publishInbound
    ├── Dedup check (5s window)
    └── Push to inbound queue
    │
    ▼
Orchestrator::run (worker thread)
    ├── Consume message
    ├── Fire "MessageReceived" hook
    ├── Session not active → processMessageDirect
    ├── Resolve agent: "main"
    ├── Create ActiveTurnHandle
    ├── Build system prompt (Full mode)
    └── Call AgentLoop::processMessage
    │
    ▼
AgentLoop::processMessage
    ├── Task state → DOING
    ├── Build history from session
    ├── Assemble messages: [system + history + user]
    └── runAgentLoop
    │
    ▼
Iteration 1:
    ├── Broadcast "chat:typing"
    ├── Stream from LLM → LLM decides to call list_dir(path="docs/")
    ├── Fire "BeforeToolCall" hook
    ├── Execute list_dir → "architecture.md\nconfiguration.md\nflow.md\n..."
    ├── Fire "AfterToolCall" hook
    ├── Broadcast "chat:tool_use"
    └── Add assistant message + tool result to messages
    │
    ▼
Iteration 2:
    ├── Broadcast "chat:typing"
    ├── Stream from LLM → "The files in the docs/ folder are: ..."
    ├── Broadcast "chat:stream" (tokens arrive in real time)
    ├── No tool calls → loop ends
    └── Broadcast "chat:stream_end"
    │
    ▼
Back to processMessage:
    ├── Save assistant response to session
    ├── Broadcast "sessions:updated"
    ├── Task state → DONE
    └── Emit "chat:message" (final response)
    │
    ▼
User sees the response in the browser
```

This cycle repeats for every message. The same flow applies whether the message comes from the browser, Telegram, a scheduled cron job, or the heartbeat service — the only difference is the entry point into the bus.
