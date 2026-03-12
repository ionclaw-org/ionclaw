# MCP Server

IonClaw implements the [Model Context Protocol](https://modelcontextprotocol.io/) (MCP) using the Streamable HTTP transport. Supported protocol versions: **2024-11-05**, **2025-03-26**, **2025-11-25**. This lets any MCP-compatible AI client (Claude Code, GitHub Copilot, Cursor, etc.) connect directly to IonClaw to chat with agents and manage sessions.

## Transport

All MCP traffic goes through a single endpoint on the IonClaw HTTP server:

```
POST   http://<host>:<port>/mcp   — JSON-RPC request / response
GET    http://<host>:<port>/mcp   — SSE keepalive stream
DELETE http://<host>:<port>/mcp   — close MCP session
```

The port is whatever `server.port` is set to in `config.yml` (default: `8080`).

## Authentication

Authentication is **optional**. When enabled, every request must include a static Bearer token:

```
Authorization: Bearer <token>
```

### Setup

1. **Settings → Credentials** → Add a credential of type `Simple`. Use the generate button (↺) on the **Key** field to fill it with a UUID.
2. **Settings → Channels → MCP** → Enable **Require Token Authentication**, select the credential, Save.
3. Configure the client with `Authorization: Bearer <the-key-value>`.

The token is verified by direct comparison against the credential's `key` — no login or JWT flow required.

### Configuration reference

```yaml
channels:
  mcp:
    enabled: false
    require_auth: true
    credential: mcp_token   # credential name whose key = the Bearer token
```

## Security

The server validates the `Origin` header on all requests per MCP spec 2025-11-25 to prevent DNS rebinding attacks:

- **With auth enabled**: all origins are allowed (the Bearer token provides access control).
- **Without auth**: only local origins (`localhost`, `127.0.0.1`, `[::1]`) are allowed. Non-local origins receive `403 Forbidden`.

## Session Lifecycle

1. Client sends `POST /mcp` with method `initialize` (no `MCP-Session-Id` header).
2. Server creates a session and responds `200 OK` with `MCP-Session-Id: <uuid>` header plus capabilities.
3. Client sends `POST /mcp` with method `notifications/initialized` and the session ID header → server responds `202 Accepted`.
4. All subsequent requests must include `MCP-Session-Id: <uuid>`.
5. If the session is not found, server responds `404 Not Found` — client must start a new session with a fresh `initialize`.
6. Client sends `DELETE /mcp` to close the session cleanly (optional).

## Protocol

Requests and responses follow [JSON-RPC 2.0](https://www.jsonrpc.org/specification).

### Supported methods

| Method | Description |
|---|---|
| `initialize` | Handshake — negotiates protocol version and returns capabilities |
| `notifications/initialized` | Client confirms initialization |
| `ping` | Returns an empty result |
| `tools/list` | Lists available tools |
| `tools/call` | Executes a tool |
| `resources/list` | Lists available static resources |
| `resources/templates/list` | Lists URI templates |
| `resources/read` | Reads a resource by URI |

### Error codes

| Code | Name | Meaning |
|---|---|---|
| `-32700` | Parse error | Request body is not valid JSON |
| `-32600` | Invalid request | Malformed JSON-RPC or missing session |
| `-32601` | Method not found | Unknown method or tool name |
| `-32602` | Invalid params | Required parameter is missing |
| `-32603` | Internal error | Unexpected server error |

## Streaming

For `tools/call`, include `Accept: text/event-stream` to receive a streaming SSE response. The spec recommends including both `application/json` and `text/event-stream` in the Accept header.

The server sends `notifications/progress` events for each text chunk, followed by the final JSON-RPC result:

```
event: message
data: {"jsonrpc":"2.0","method":"notifications/progress","params":{"progressToken":"<token>","message":"<chunk>"}}

event: message
data: {"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"<full response>"}],"isError":false}}
```

Without `Accept: text/event-stream`, the server blocks until the full response is ready and returns it as `application/json`.

The `progressToken` is taken from `_meta.progressToken` in the request params if provided, otherwise defaults to the internal task ID. The original type is preserved: if the client sends a number (e.g. Cursor), the server echoes it back as a number.

Both streaming and non-streaming modes have a **300-second idle timeout**. If the agent does not respond within this window, the server returns an error.

## Tools

### `chat`

Send a message to the AI agent and receive a response.

**Parameters**

| Name | Type | Required | Description |
|---|---|---|---|
| `message` | string | yes | The message to send |
| `session_id` | string | no | Chat session ID. Uses the MCP session's own chat if omitted. May be a full key (`mcp:uuid`) or bare UUID |
| `agent` | string | no | Target agent name. If omitted, the classifier routes to the best agent |

**Returns** — plain text: the agent's full response.

**Streaming** — supports `Accept: text/event-stream`; tokens arrive as `notifications/progress` events.

---

### `abort`

Abort an active agent session mid-turn.

**Parameters**

| Name | Type | Required | Description |
|---|---|---|---|
| `session_id` | string | yes | Session key to abort (e.g. `mcp:uuid` or bare UUID) |

**Returns** — `{"status": "aborted", "session_id": "..."}` or `{"status": "no_active_turn", ...}`.

---

### `list_sessions`

List all chat sessions across all channels.

**Parameters** — none.

**Returns**

```json
{
  "sessions": [
    {
      "key": "mcp:abc123",
      "channel": "mcp",
      "display_name": "Hello, how are...",
      "created_at": "2025-01-01T00:00:00Z",
      "updated_at": "2025-01-01T00:01:00Z"
    }
  ],
  "count": 1
}
```

---

### `get_session`

Get session details and full message history.

**Parameters**

| Name | Type | Required | Description |
|---|---|---|---|
| `session_id` | string | yes | Full session key (e.g. `mcp:uuid` or `web:uuid`) |

**Returns**

```json
{
  "key": "mcp:abc123",
  "messages": [
    {"role": "user", "content": "Hello", "timestamp": "..."},
    {"role": "assistant", "content": "Hi!", "timestamp": "..."}
  ],
  "created_at": "...",
  "updated_at": "..."
}
```

---

### `delete_session`

Delete a chat session and its persisted history.

**Parameters**

| Name | Type | Required | Description |
|---|---|---|---|
| `session_id` | string | yes | Full session key to delete |

**Returns** — `{"status": "deleted", "session_id": "..."}`.

---

### `list_agents`

List all configured agents.

**Parameters** — none.

**Returns**

```json
{
  "agents": [
    {"name": "main", "description": "General-purpose assistant", "model": "anthropic/claude-sonnet-4-20250514"}
  ]
}
```

---

### `list_tasks`

List recent tasks.

**Parameters**

| Name | Type | Required | Description |
|---|---|---|---|
| `limit` | integer | no | Maximum tasks to return (default: 50) |

**Returns**

```json
{
  "tasks": [
    {
      "id": "task_abc",
      "title": "Hello",
      "state": "done",
      "channel": "mcp",
      "created_at": "...",
      "result": "Hi!"
    }
  ],
  "count": 1
}
```

---

### `get_task`

Get full task details by ID.

**Parameters**

| Name | Type | Required | Description |
|---|---|---|---|
| `task_id` | string | yes | Task ID |

**Returns** — full task object (same fields as `list_tasks` entries, plus `usage`, `iteration_count`, etc.).

## Resources

Resources expose IonClaw state as read-only URI-addressed data.

### `ionclaw://sessions`

List of all sessions. Returns the same structure as `list_sessions`.

### `ionclaw://sessions/{id}`

Session messages for a specific chat ID (bare UUID, without channel prefix).

### `ionclaw://agents`

List of all configured agents. Same structure as `list_agents`.

## Configuration

```yaml
channels:
  mcp:
    enabled: false        # auto-start on server boot (default: false)
    require_auth: false   # require Bearer token on all requests (default: false)
    credential: ""        # credential name whose key = the Bearer token
```

### `enabled` vs running state

`enabled` is a **persistent config flag** — it controls whether the MCP channel starts automatically when the server boots. It is saved to `config.yml` and only changes when you explicitly save the channel config.

The **running state** is transient — it reflects whether the channel is currently active. Starting or stopping the channel does not affect `enabled`.

- To configure auto-start: toggle `enabled` and click **Save** in the web app.
- To start/stop the channel at runtime without affecting the config: use the **Start/Stop** button or the API.

When the MCP channel is stopped, all in-memory MCP sessions are cleared. Clients must re-initialize after a restart.

### API

```
PUT  /channels/mcp          — update config (persists to config.yml)
POST /channels/mcp/start    — start channel (runtime only)
POST /channels/mcp/stop     — stop channel (runtime only)
```

## Client Setup

### Claude Code

Add to `.mcp.json` in your project root or `~/.claude.json` for user-level config:

```json
{
  "mcpServers": {
    "ionclaw": {
      "type": "http",
      "url": "http://localhost:8080/mcp",
      "headers": {
        "Authorization": "Bearer <your-token>"
      }
    }
  }
}
```

### GitHub Copilot (VS Code)

In VS Code `settings.json`:

```json
{
  "github.copilot.chat.mcpServers": {
    "ionclaw": {
      "type": "http",
      "url": "http://localhost:8080/mcp",
      "headers": {
        "Authorization": "Bearer <your-token>"
      }
    }
  }
}
```

### Cursor

In Cursor's MCP settings (`~/.cursor/mcp.json`):

```json
{
  "mcpServers": {
    "ionclaw": {
      "type": "http",
      "url": "http://localhost:8080/mcp",
      "headers": {
        "Authorization": "Bearer <your-token>"
      }
    }
  }
}
```

If `require_auth` is `false`, omit the `headers` block.

## Chat Session Mapping

Each MCP connection gets its own chat session. The session key in IonClaw follows the pattern `mcp:<uuid>` where `<uuid>` is the `MCP-Session-Id` value. Sessions appear in the web app alongside web and Telegram sessions and share the same history, memory, and agent context.

### Display Names

Sessions automatically get a display name derived from the first user message (truncated to ~50 characters at a word boundary). This makes MCP sessions easy to identify in the web app sidebar — e.g., "Hello, can you help me with..." instead of a raw UUID. If no user message has been sent yet, the sidebar shows a short ID like "MCP f4ec566c".

## Error Messages

When a provider is unreachable or misconfigured, IonClaw returns clear, actionable error messages instead of raw exception text:

| Error Type | Example Message |
|---|---|
| Host not found | Could not connect to provider 'llama': the host was not found. Please check that the provider's base_url is correct and the service is reachable. (model: llama/local) |
| Authentication | Authentication failed for model 'anthropic/claude-sonnet-4-20250514'. Please check that the API key is valid and has the required permissions. |
| Model not found | Model 'openai/gpt-5-turbo' was not found by the provider. Please check the model name in the agent configuration. |
