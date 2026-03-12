---
name: mcp-client
description: Connect to external MCP servers to use their tools and read their resources. Use when the user wants to interact with a remote MCP-compatible service (e.g. another IonClaw instance, a database MCP server, a filesystem MCP server, or any third-party MCP server).
---

# MCP Client

Use the `mcp_client` tool to connect to external MCP servers and interact with their tools and resources using the Model Context Protocol (Streamable HTTP transport).

## Tool: `mcp_client`

### Workflow

Every interaction follows the same pattern:

1. **Initialize** — connect to the server (may return a session ID)
2. **Use** — list tools, call tools, list resources, read resources
3. **Close** — end the session when done

If `initialize` returns a `session_id`, pass it to all subsequent calls. Some servers are stateless and return an empty session ID — in that case, omit it.

### Actions

| Action | Description |
|--------|-------------|
| `initialize` | Connect to the MCP server and negotiate a session |
| `list_tools` | List all tools available on the remote server |
| `call_tool` | Call a specific tool on the remote server |
| `list_resources` | List available resources on the remote server |
| `read_resource` | Read a specific resource by URI |
| `ping` | Check if the server is alive |
| `close` | Close the MCP session |

### Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `action` | string | Yes | Action to perform |
| `url` | string | Yes | MCP server endpoint URL |
| `session_id` | string | No | MCP session ID from initialize (if server provides one) |
| `auth_token` | string | No | Bearer token for authenticated servers |
| `tool_name` | string | call_tool | Name of the tool to call |
| `tool_arguments` | object | No | Arguments for the tool call |
| `resource_uri` | string | read_resource | URI of the resource to read |
| `timeout` | integer | No | Request timeout in seconds (default: 30) |

## Examples

### Connect to a local MCP server

```
mcp_client(action="initialize", url="http://localhost:8080/mcp")
```

Returns:

```json
{
  "session_id": "abc-123-def",
  "protocol_version": "2025-03-26",
  "server_info": {"name": "IonClaw", "version": "1.0.0"},
  "capabilities": {"tools": {"listChanged": false}}
}
```

### Connect with authentication

```
mcp_client(action="initialize", url="http://localhost:8080/mcp", auth_token="your-bearer-token")
```

### List available tools

```
mcp_client(action="list_tools", url="http://localhost:8080/mcp", session_id="abc-123-def")
```

### Call a remote tool

```
mcp_client(action="call_tool", url="http://localhost:8080/mcp", session_id="abc-123-def", tool_name="chat", tool_arguments={"message": "Hello, what can you do?"})
```

### List resources

```
mcp_client(action="list_resources", url="http://localhost:8080/mcp", session_id="abc-123-def")
```

### Read a resource

```
mcp_client(action="read_resource", url="http://localhost:8080/mcp", session_id="abc-123-def", resource_uri="ionclaw://agents")
```

### Ping the server

```
mcp_client(action="ping", url="http://localhost:8080/mcp", session_id="abc-123-def")
```

### Close the session

```
mcp_client(action="close", url="http://localhost:8080/mcp", session_id="abc-123-def")
```

## Complete workflow example

A typical session looks like this:

```
# 1. connect
mcp_client(action="initialize", url="http://localhost:9090/mcp")
# → session_id: "abc-123"

# 2. discover what the server offers
mcp_client(action="list_tools", url="http://localhost:9090/mcp", session_id="abc-123")
# → tools: [{name: "query_db", ...}, {name: "insert_record", ...}]

# 3. use a tool
mcp_client(action="call_tool", url="http://localhost:9090/mcp", session_id="abc-123", tool_name="query_db", tool_arguments={"sql": "SELECT * FROM users LIMIT 5"})
# → {tool: "query_db", text: "...", isError: false}

# 4. read a resource
mcp_client(action="read_resource", url="http://localhost:9090/mcp", session_id="abc-123", resource_uri="myapp://schema/users")
# → {uri: "myapp://schema/users", text: "..."}

# 5. close when done
mcp_client(action="close", url="http://localhost:9090/mcp", session_id="abc-123")
# → {status: "closed"}
```

## Tips

- Always call `initialize` first to negotiate the protocol version and capabilities.
- If the server returns a `session_id`, pass it to all subsequent calls. If it returns empty, omit it.
- Use `list_tools` to discover what the remote server can do before calling tools.
- The `session_id` is specific to one server — if you connect to multiple servers, track each session ID separately.
- Use `auth_token` when the server requires authentication (Bearer token).
- For long-running tool calls, increase the `timeout` parameter.
- Always `close` the session when you are done to free server resources.
- The tool uses the MCP Streamable HTTP transport (JSON-RPC 2.0 over HTTP POST).
