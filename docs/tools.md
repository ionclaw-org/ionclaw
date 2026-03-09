# Tools Reference

Tools are the functions the AI agent can call during conversations. This document describes all built-in tools, their parameters, path resolution, and configuration.

## Overview

Tools are registered in the `ToolRegistry`. The registry stores tool instances, generates OpenAI-compatible function definitions for the LLM, executes tool calls by name with arguments, and returns string results to the agent loop. Tool results may be truncated to avoid context overflow.

Each agent can restrict which tools it has via the `tools` whitelist in its configuration. When the list is empty (default), all tools are available. When set, only tools whose name appears in the list are registered for that agent.

## Path resolution

All file-based tools (`read_file`, `write_file`, `edit_file`, `list_dir`, `http_client` with `download_path`/`upload_file`, `image_ops`) resolve paths through a shared validator that enforces directory restrictions.

**Behavior:**

1. Relative paths are resolved against the agent's workspace directory.
2. Paths under the project's `public/` directory are also allowed (e.g. `public/media/out.png`).
3. Any path outside the workspace or `public/` raises an error. Workspace restriction is always enforced.

The exec tool's working directory is the agent's workspace.

---

## Built-in tools

### File operations

#### read_file

Read the contents of a file. Supports line-based pagination for large files.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `path` | string | Yes | File path (relative to workspace or under public/) |
| `offset` | integer | No | Line number to start reading (1-based). Default: 1 |
| `limit` | integer | No | Maximum number of lines to read. Default: all |

Output may be capped per read; the response indicates when more content exists and the `offset` to use for the next chunk.

#### write_file

Create or overwrite a file. Creates parent directories automatically.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `path` | string | Yes | File path |
| `content` | string | Yes | Content to write |

#### edit_file

Search and replace within a file. The `old_text` must match exactly once. If it appears multiple times, the tool returns a warning asking for more context. If not found, a best-match diff may be shown.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `path` | string | Yes | File path |
| `old_text` | string | Yes | Exact text to find |
| `new_text` | string | Yes | Replacement text |

#### list_dir

List the contents of a directory. Items are formatted as `[dir] name (size bytes)` or `[file] name (size bytes)`.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `path` | string | Yes | Directory path |

---

### Shell

#### exec

Execute a shell command in the workspace directory.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `command` | string | Yes | The shell command to execute |
| `working_dir` | string | No | Working directory (relative to workspace). When restriction is enabled, locked to workspace |

Timeout is configured via `tools.exec_timeout` (default 60 seconds). Available on macOS, Linux, and Windows only.

---

### Web

#### web_search

Search the web. Returns titles, URLs, and descriptions. Provider and credential are configured in settings (e.g. Brave, DuckDuckGo).

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `query` | string | Yes | The search query |
| `count` | integer | No | Number of results (1–10). Default: 5 |

Requires `tools.web_search.provider` and `tools.web_search.credential` (or flat `web_search_provider` / `web_search_credential`) with a valid credential.

#### web_fetch

Fetch and extract content from a URL. Uses readability-style extraction when applicable.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `url` | string | Yes | The URL to fetch |
| `max_chars` | integer | No | Max characters to return (default 50000) |

---

### HTTP client

#### http_client

Make HTTP requests with control over method, headers, body, and optional file download/upload.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `method` | string | Yes | `GET`, `POST`, `PUT`, `DELETE`, `PATCH`, `HEAD`, `OPTIONS` |
| `url` | string | Yes | The URL to request |
| `headers` | object | No | Request headers as key-value pairs |
| `body` | string | No | Request body |
| `content_type` | string | No | Shorthand: `json`, `form`, `text`, `xml` (sets Content-Type) |
| `timeout` | integer | No | Timeout in seconds (default 30) |
| `follow_redirects` | boolean | No | Follow redirects (default true) |
| `download_path` | string | No | Save response body to this file path (resolved via path validator) |
| `upload_file` | string | No | File path to upload as multipart form data |
| `upload_field` | string | No | Form field name for the upload (default: `file`) |
| `auth` | string | No | Auth profile name from config for authenticated requests |

Response includes `status`, `headers`, and `body` (or file info when using `download_path`). Large bodies may be truncated.

---

### Communication

#### message

Send a message to the user via the current channel.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `content` | string | Yes | The message content |
| `channel` | string | No | Target channel (e.g. `web`, `telegram`). Defaults to current |
| `chat_id` | string | No | Target chat/user ID. Defaults to current |
| `media` | array | No | File paths to attach |

Only available to the main agent (not subagents).

---

### Subagent management

#### subagents

Manage spawned subagent runs: list active runs, check status, or kill a run. Available on macOS, Linux, and Windows only.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `action` | string | Yes | `list`, `kill`, or `status` |
| `run_id` | string | Conditional | Run ID (required for `kill` and `status`) |
| `cascade` | boolean | No | When killing, also kill all descendant subagent runs (default: true) |

- **list** — Shows all subagent runs for the current session (runId, task, status, depth).
- **status** — Detailed status of a specific run (includes progress, timestamps, outcome).
- **kill** — Kills a run and optionally cascades to all descendants via BFS traversal.

Not available to subagents — only the main (parent) agent can manage runs.

---

### Background tasks and scheduling

#### spawn

Launch a subagent for an independent background task. The subagent runs asynchronously in a child session with its own context.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `task` | string | Yes | Description of the task for the subagent |
| `label` | string | No | Short label for the task (default: truncated task description) |
| `model` | string | No | Override model for the subagent (e.g. `anthropic/claude-haiku-3`) |
| `thinking` | string | No | Thinking level for the subagent: `low`, `medium`, `high` |
| `runTimeoutSeconds` | number | No | Timeout in seconds for this run (0 or omit = use agent default) |

Available on macOS, Linux, and Windows only. Creates a child task on the task board.

**Limits:** Subagent depth and concurrent children are configurable per agent via `subagent_limits` (default: 5 depth, 5 children). The `SubagentSpawning` hook can block spawns based on custom policy.

**Lifecycle:** Spawned → Active → Completed/Errored/Killed. Progress is tracked via content snippets. Runs exceeding `timeoutSeconds` (default 300s) are automatically killed.

#### cron

Schedule reminders and recurring tasks.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `action` | string | Yes | `add`, `list`, or `remove` |
| `message` | string | Conditional | Message or task description (required for `add`) |
| `every_seconds` | integer | Conditional | Interval in seconds for recurring tasks (min 5) |
| `cron_expr` | string | Conditional | Cron expression (e.g. `0 9 * * *`) |
| `at` | string | Conditional | ISO datetime for one-time execution |
| `job_id` | string | Conditional | Job ID (required for `remove`) |
| `tz` | string | No | IANA timezone for cron expressions |

Only available to the main agent when scheduler is enabled.

---

### Memory

#### memory_save

Save conversation history and updated long-term memory (used during consolidation).

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `history_entry` | string | Yes | Summary of recent conversation for HISTORY.md |
| `updated_memory` | string | Yes | Updated long-term memory content for MEMORY.md |

#### memory_read

Read memory files on demand.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `file` | string | Yes | `memory` for MEMORY.md, `history` for HISTORY.md |
| `max_lines` | integer | No | Max lines from the end (default: all). Useful for history |

#### memory_search

Search across memory and history for specific information. Supports multi-language queries including CJK (Chinese, Japanese, Korean) with codepoint-level tokenization.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `query` | string | Yes | Search term (case-insensitive keyword match) |

Returns matching lines with context, ranked by relevance score. Scoring considers keyword frequency, temporal decay (recently modified files score higher), and stop word filtering.

---

### RSS reader

#### rss_reader

Read RSS and Atom feeds and return the latest entries.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `url` | string | Yes | RSS/Atom feed URL |
| `count` | integer | No | Maximum entries to return (default 10, max 50) |

Supports RSS 2.0 and Atom. HTML is stripped from summary fields.

---

### Image generation and operations

#### generate_image

Generate images using AI models. Provider is chosen by `image.model` (e.g. `gemini/*`, `openai/*`). Saves to the public media directory.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `prompt` | string | Yes | Image generation prompt |
| `filename` | string | Yes | Output filename (e.g. `image.png`) |
| `reference_images` | array | No | Paths to reference images sent as visual context before the prompt |
| `aspect_ratio` | string | No | e.g. `1:1`, `16:9`, `9:16`, `3:4`, `4:3` |
| `size` | string | No | `1K`, `2K`, or `4K` (resolution) |
| `style` | string | No | Style hint for the image |
| `negative_prompt` | string | No | What to avoid in the generated image |
| `google_search` | boolean | No | Enable Google Search grounding for real-time data |

Requires `image.model` and a configured credential for the model's provider. Gemini models use the native API (aspect ratio, reference image); others use an OpenAI-compatible endpoint.

#### image_ops

Local image operations (no AI): create, resize, draw rectangles, overlay/watermark. Paths can be under workspace or public (e.g. `public/media/out.png`).

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `operation` | string | Yes | `create`, `resize`, `draw_rect`, `overlay` |
| `output_path` | string | Yes | Output path (e.g. `public/media/result.png`) |
| `input_path` | string | Conditional | Input image (resize, draw_rect, overlay) |
| `overlay_path` | string | Conditional | Overlay/watermark image (overlay only) |
| `width` | integer | Conditional | Width (create/resize/draw_rect) |
| `height` | integer | Conditional | Height (create/resize/draw_rect) |
| `x` | integer | Conditional | X position (draw_rect, overlay) |
| `y` | integer | Conditional | Y position (draw_rect, overlay) |
| `overlay_width` | integer | No | Overlay: width to resize overlay to before pasting |
| `overlay_height` | integer | No | Overlay: height to resize overlay to before pasting |
| `background` | string | No | create: `gradient` or `solid` |
| `color` | string | No | Hex color RRGGBB (create solid background; draw_rect fill) |

- **create** — New image (width, height, background, color).
- **resize** — Change dimensions (input_path, output_path, width, height).
- **draw_rect** — Fill a rectangle with color on an existing image (input_path, output_path, x, y, width, height, color).
- **overlay** — Paste an image on top at (x, y). Optional `overlay_width`/`overlay_height` to resize the overlay first.

---

### Browser

#### browser

Chrome automation via the Chrome DevTools Protocol (CDP): navigate, snapshot, screenshot, click, type, press, inspect, evaluate, wait. Requires Chrome installed. Available on macOS, Linux, and Windows only.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `action` | string | Yes | `navigate`, `snapshot`, `screenshot`, `click`, `type`, `press`, `inspect`, `evaluate`, `wait` |
| `url` | string | Conditional | Required for `navigate` |
| `selector` | string | Conditional | CSS selector (click, type) |
| `text` | string | Conditional | Text to type (type) |
| `key` | string | Conditional | Key name (press): Enter, Tab, Escape, etc. |
| `script` | string | Conditional | JavaScript expression (evaluate) |
| `seconds` | integer | No | Seconds to wait (wait; default 2) |
| `max_chars` | integer | No | Max characters for snapshot (default 50000) |
| `headless` | boolean | No | Run headless (default true) |

---

### Platform

#### invoke_platform

Invoke a platform-specific function on the host system. Functions are routed through the PlatformBridge to a registered handler (set via FFI by the host application). Available functions depend on the current platform — not all functions exist on all platforms.

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `function` | string | Yes | The platform function to invoke (e.g. `local-notification.send`) |
| `params` | object | No | Parameters for the function as a JSON object |

Available on all platforms. Returns a string result from the platform handler.

**How it works:**

1. The agent calls `invoke_platform` with a function name and params.
2. The C++ core forwards the call to the registered handler via FFI.
3. The host application (e.g. Flutter) routes the call to the appropriate plugin.
4. The plugin executes asynchronously (can await HTTP, sensors, UI, etc.).
5. The result string is returned to the agent as tool output.

The call is **synchronous from the agent's perspective** — the agent waits for the result before continuing. A configurable timeout (default 30s) prevents indefinite blocking.

**Built-in functions:**

| Function | Platforms | Description |
|----------|-----------|-------------|
| `local-notification.send` | android, ios, macos, linux | Send a local push notification |

Parameters for `local-notification.send`:

| Parameter | Type | Required | Description |
|----------|------|----------|-------------|
| `title` | string | No | Notification title (default: "IonClaw") |
| `message` | string | No | Notification body |

**Behavior when unavailable:**

- If no handler is registered (e.g. standalone server without Flutter): returns `"Error: '{function}' is not implemented on {platform}."`
- If the function is not supported on the current platform: returns `"Error: '{function}' is not implemented on this platform."`
- If the plugin failed to initialize: returns a descriptive error.

**Custom plugins:** The Flutter host application supports a plugin system where each plugin declares which functions it handles and which platforms it supports. See [Platform Bridge](architecture.md#platform-bridge) for details on creating custom plugins.

Platform values are always **lowercase**: `android`, `ios`, `macos`, `linux`, `windows`.

---

## Tool configuration

### Exec timeout

```yaml
tools:
  exec_timeout: 60   # seconds
```

### Web search

```yaml
tools:
  web_search:
    provider: brave      # or duckduckgo
    credential: brave

credentials:
  brave:
    type: simple
    key: ${BRAVE_SEARCH_API_KEY}
```

### Workspace restriction

Workspace restriction is always enforced. File tools and the exec working directory are limited to the agent's workspace and the `public/` directory.

---

## Per-agent tool whitelisting

Each agent can restrict which tools it has via the `tools` field:

```yaml
agents:
  main:
    tools: []   # empty = all tools
  researcher:
    tools: [read_file, write_file, web_search, web_fetch, http_client, message]
  coder:
    tools: [read_file, write_file, edit_file, list_dir, exec]
```

Only tools in the list are registered for that agent. The system prompt and LLM function definitions only include whitelisted tools.

---

## Tool policy (allow/deny)

In addition to the `tools` whitelist (which controls registration), each agent supports a `tool_policy` for fine-grained runtime access control:

```yaml
agents:
  main:
    tool_policy:
      allow: []              # empty = all registered tools allowed
      deny: [exec, spawn]   # deny overrides allow
  assistant:
    tool_policy:
      allow: [read_file, write_file, web_search, message]
      deny: []
```

- **allow** — When set, only these tools can execute. Empty = all allowed.
- **deny** — These tools are blocked regardless of the allow list. Deny always takes precedence.

The `tools` field and `tool_policy` serve different purposes:
- `tools` controls which tools are *registered* (included in schema sent to the LLM).
- `tool_policy` controls which registered tools can *execute* at runtime.

---

## Hook-based tool control

The `BeforeToolCall` hook provides dynamic, programmatic tool control beyond static configuration. Hooks can:

- **Block execution** — Set `blocked = true` and optionally `blockReason` to prevent a tool call.
- **Modify arguments** — Change `data["arguments"]` to rewrite tool parameters before execution.

The `AfterToolCall` hook fires after execution with the result and a success flag, enabling logging and monitoring.

---

## API

**GET /api/tools** — Returns the list of registered tools with name, description, and parameters (JSON Schema). Requires authentication.
