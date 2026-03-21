# IonClaw API Reference

Complete REST API and WebSocket reference for IonClaw.

---

## Authentication

All endpoints except `/api/auth/login` and `/api/health` require a valid JWT token in the `Authorization` header:

```
Authorization: Bearer <token>
```

---

## REST API

All endpoints are prefixed with `/api/`.

---

### Auth

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/auth/login` | Authenticate and obtain a JWT token |

**POST /api/auth/login**

No authentication required.

Request body:

```json
{
  "username": "string",
  "password": "string"
}
```

Response:

```json
{
  "token": "string",
  "username": "string"
}
```

---

### System

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/health` | Health check |
| GET | `/api/version` | Server version |
| GET | `/api/system/info` | System information (OS, CPU, memory, disk) |

**GET /api/health**

Response:

```json
{
  "status": "ok",
  "version": "string"
}
```

**GET /api/version**

Response:

```json
{
  "version": "string"
}
```

**GET /api/system/info**

Response: `os`, `cpu`, `memory` (e.g. `total_gb`), and `version` are always present. `disk` (e.g. `total_gb`, `used_gb`, `free_gb`, `percent`) may be omitted on some platforms.

```json
{
  "os": { "system": "string", "release": "string", "version": "string", "machine": "string" },
  "cpu": { "processor": "string", "cores": 0 },
  "memory": { "total_gb": 0.0 },
  "version": "string"
}
```

---

### Chat

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/chat` | Send a message to the agent |
| POST | `/api/chat/upload` | Upload media files (images, audio) |
| GET | `/api/chat/sessions` | List all chat sessions |
| GET | `/api/chat/sessions/{session_id}` | Get a single session with messages |
| DELETE | `/api/chat/sessions/{session_id}` | Delete a session |

**POST /api/chat**

Sends a message asynchronously. The response is delivered via WebSocket events.

Request body:

```json
{
  "message": "string",
  "session_id": "string (optional)",
  "media": ["string (optional, file paths)"],
  "queue_mode": "string (optional, override queue mode for this message)"
}
```

The message is persisted to the session immediately (before the async agent loop processes it). A page refresh always shows the session and the user's message, even if the agent has not responded yet.

Response:

```json
{
  "task_id": "string",
  "session_id": "string"
}
```

**POST /api/chat/upload**

Upload media files. Accepts multipart form data. Files are stored under `public/media/` with a generated filename.

Request: `multipart/form-data` with one or more file fields.

Response:

```json
{
  "paths": ["public/media/abc123.jpg"]
}
```

The returned paths can be passed in the `media` field when sending messages (if supported).

**GET /api/chat/sessions**

Response:

```json
[
  {
    "key": "string",
    "display_name": "string",
    "channel": "string",
    "created_at": "string",
    "updated_at": "string"
  }
]
```

**GET /api/chat/sessions/{session_id}**

Session key resolution:

- If the id contains `:`, it is used as the session key (e.g. `web:abc123`)
- Otherwise it is prefixed with `web:` (standard user sessions)

Response:

```json
{
  "key": "string",
  "messages": [],
  "live_state": {},
  "created_at": "string",
  "updated_at": "string"
}
```

**DELETE /api/chat/sessions/{session_id}**

Uses the same key resolution as GET.

Response:

```json
{
  "status": "deleted"
}
```

---

### Tasks

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/tasks` | List tasks |
| GET | `/api/tasks/{task_id}` | Get a single task |
| PUT or PATCH | `/api/tasks/{task_id}` | Update task state |

**GET /api/tasks**

Returns tasks (TODO, DOING, DONE, ERROR). Each task object includes:

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Task identifier |
| `title` | string | Short title |
| `state` | string | `TODO`, `DOING`, `DONE`, or `ERROR` |
| `agent_name` | string | Name of the agent handling the task |
| `tool_count` | int | Number of tool calls executed |
| `iteration_count` | int | Number of LLM call iterations |
| `usage` | object | Token usage: `{ prompt_tokens, completion_tokens, total_tokens }` (optional) |
| `duration_ms` | int \| null | Milliseconds from start to completion |

**PUT or PATCH /api/tasks/{task_id}**

Request body:

```json
{
  "state": "TODO | DOING | DONE | ERROR",
  "result": "string (optional)"
}
```

Returns `400` with a descriptive message if `state` is missing or not one of the valid values.

---

### Files

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/files` | Get workspace file tree |
| GET | `/api/files/{path}` | Read file metadata or content |
| GET | `/api/files/download/{path}` | Download raw file |
| POST | `/api/files/create/{path}` | Create an empty file |
| POST | `/api/files/mkdir/{path}` | Create a directory |
| POST | `/api/files/upload/{path}` | Upload files to a directory |
| POST | `/api/files/upload` | Upload files to the root directory |
| POST | `/api/files/rename/{path}` | Rename a file or directory |
| PUT | `/api/files/{path}` | Write content to a file |
| DELETE | `/api/files/{path}` | Delete a file or directory (recursive) |

**GET /api/files**

Returns a recursive tree of the workspace directory (excluding hidden and protected files such as `config.yml`).

**GET /api/files/{path}**

Returns file info. For text files the response includes `path`, `type` (`text`), and `content`. For media and binary files it includes `path`, `type` (`image` \| `video` \| `audio` \| `binary`), `mime`, `size`, and `url` (e.g. `/public/{path}` for public files or `/api/files/download/{path}` for others).

**PUT /api/files/{path}**

Request body:

```json
{
  "content": "string"
}
```

**POST /api/files/rename/{path}**

Rename a file or directory. Hidden paths and protected files cannot be renamed. Names with path separators or leading dots are rejected.

Request body:

```json
{
  "name": "string"
}
```

**DELETE /api/files/{path}**

Deletes a file or directory recursively.

---

### Public Files

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/public/{path}` | Serve files from the `public/` directory |

No authentication required. Files under the project's `public/` directory are served directly for use in `<img>`, `<video>`, `<audio>` tags.

---

### Skills

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/skills` | List all skills |
| GET | `/api/skills/{name}` | Get a skill's raw content |
| PUT | `/api/skills/{name}` | Update a skill's SKILL.md |
| DELETE | `/api/skills/{name}` | Delete a skill directory |

For nested skills, the name includes the publisher prefix (e.g. `/api/skills/anthropic/pdf`). An optional `?agent=` query parameter may be supported to scope results to a specific agent's workspace.

**GET /api/skills**

Returns all discovered skills (built-in, project, and workspace). When `agent` filtering is supported: no parameter or `agent=<name>` returns all; `agent=` (empty) returns built-in and project only.

Response: array of objects with `name`, `description`, `always`, `available`, `source` (builtin | project | workspace), and `publisher`.

**GET /api/skills/{name}**

Response:

```json
{
  "name": "string",
  "content": "string"
}
```

**PUT /api/skills/{name}** (POST is also accepted)

Request body:

```json
{
  "content": "string"
}
```

Built-in skills cannot be edited.

**DELETE /api/skills/{name}**

Built-in skills cannot be deleted.

---

### Marketplace

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/marketplace/targets` | List install targets (project + agents) |
| GET | `/api/marketplace/check/{source}/{name}` | Check if a skill is already installed |
| POST | `/api/marketplace/install` | Install a skill from the marketplace |

**GET /api/marketplace/targets**

Response:

```json
[
  { "label": "Project", "value": "" },
  { "label": "main", "value": "main" }
]
```

**GET /api/marketplace/check/{source}/{name}**

Supports `?agent=` to check a specific location.

Response:

```json
{
  "installed": "boolean"
}
```

**POST /api/marketplace/install**

Request body:

```json
{
  "source": "string",
  "name": "string",
  "agent": "string (optional, default: project)"
}
```

If the skill already exists at the target, it is replaced. Response: `{}` on success or `{ "error": "string" }` on failure.

---

### Config

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/config` | Get full configuration (sensitive values masked) |
| GET | `/api/config/yaml` | Get full configuration as YAML (sensitive values masked) |
| POST | `/api/config/validate` | Validate a YAML configuration string |
| PATCH | `/api/config` | Update multiple configuration sections in a single call |
| PUT | `/api/config/{section}` | Update a single configuration section |
| DELETE | `/api/config/{section}/{name}` | Delete a named item (agent, credential, or provider) |
| POST | `/api/config/restart` | Reload configuration and restart services |

**POST /api/config/validate**

Validates a YAML configuration string. Checks both YAML syntax and structural validity (all sections, types, and fields).

Request body:

```json
{
  "yaml": "string"
}
```

Response:

```json
{
  "valid": true
}
```

Or on error:

```json
{
  "valid": false,
  "error": "string"
}
```

**PUT /api/config/{section}**

Valid sections: `bot`, `server`, `agents`, `image`, `web_client`, `tools`, `storage`, `credentials`, `providers`, `channels`, `advanced`.

For the `advanced` section, the body can contain a `yaml` key with the full YAML string. When saving advanced YAML, masked placeholders (`****`) for sensitive fields are preserved (replaced with stored values before write).

**DELETE /api/config/{section}/{name}**

Delete a named item from a configuration section. Supported sections: `agents`, `credentials`, `providers`. The item is removed from both in-memory config and `config.yml`. Returns 404 if the item does not exist.

---

### Providers

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/providers` | List all LLM providers |

---

### Agents

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/agents` | List all configured agents |

---

### Tools

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/tools` | List all registered tools |

Response: array of objects with `name`, `description`, and `parameters` (JSON Schema).

---

### Forms

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/forms` | Get dynamic form schemas for the frontend |

Returns form schema definitions used by the Settings UI. Each field has `name`, `type`, `label`, and optional `required`, `placeholder`, `options` (for select), etc. Supported types include `text`, `int`, `bool`, `select`, `credential`, `provider`, and others.

---

### Scheduler

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/scheduler/jobs` | List scheduled jobs |
| POST | `/api/scheduler/jobs` | Create a job |
| PUT | `/api/scheduler/jobs/{job_id}` | Update a job (patch-style) |
| DELETE | `/api/scheduler/jobs/{job_id}` | Delete a job |

---

### Channels

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/channels` | Get status of all channels |
| GET | `/api/channels/{name}` | Get a specific channel's configuration and status |
| PUT | `/api/channels/{name}` | Update a channel's configuration |
| POST | `/api/channels/{name}/start` | Start a channel |
| POST | `/api/channels/{name}/stop` | Stop a channel |

**POST /api/channels/{name}/start**

Starts the named channel (e.g. `telegram`). Returns error if already running or not configured.

**POST /api/channels/{name}/stop**

Stops the named channel. Returns error if not running.

---

## WebSocket

### Connection

Connect with a valid JWT token as a query parameter:

```
ws://host:port/ws?token=JWT_TOKEN
```

### Events (server → client)

| Event | Payload | Description |
|-------|---------|-------------|
| `chat:typing` | `{ task_id, chat_id, agent_name }` | Agent is processing (show typing indicator) |
| `chat:stream` | `{ task_id, chat_id, content, agent_name }` | Real-time token stream — one chunk per update |
| `chat:stream_end` | `{ task_id, chat_id, agent_name }` | End of streaming response |
| `chat:message` | `{ content, chat_id, task_id, agent_name }` | Final AI response; `content` is a string |
| `chat:thinking` | `{ task_id, chat_id, content, agent_name }` | Agent reasoning/thinking step |
| `chat:tool_use` | `{ task_id, chat_id, tool, description, agent_name }` | Tool invocation with human-readable description |
| `chat:user_message` | `{ chat_id, content, channel }` | Non-web user message (e.g. from another channel) |
| `chat:transcription` | `{ chat_id, content, agent_name }` | Audio transcription result |
| `chat:warning` | `{ chat_id, content, agent_name }` | Warning from agent loop (e.g. tool loop detected) |
| `task:created` | Task object | New task created |
| `task:updated` | Task object | Task state changed |
| `sessions:updated` | `{}` | Session list changed (reload sidebar) |

**chat:stream**

Accumulate `content` chunks and render progressively. When `chat:stream_end` is received, finalize the message display.

**chat:message**

The `content` string is the authoritative complete response. Use it directly instead of client-accumulated stream chunks to guarantee completeness.

**chat:tool_use**

The `description` field contains a short, human-readable summary of the tool call (e.g. "Http Client GET https://api.example.com/...") for display in the chat UI.

Only events matching the active session's `chat_id` should be displayed; others can be ignored until the user switches session.

### Events (client → server)

| Event | Payload | Description |
|-------|---------|-------------|
| `chat.send` | `{ data: { message, session_id, metadata? } }` | Send a chat message (alternative to POST /api/chat) |

---

## Static files

| Path | Description |
|------|-------------|
| `/app/` | Embedded web client |
| `/public/` | User's public directory (no auth) |
