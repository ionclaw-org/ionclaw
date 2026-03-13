# Tools

## File Operations

- `read_file(path, offset, limit)` - Read file contents
- `write_file(path, content)` - Create or overwrite a file
- `edit_file(path, old_text, new_text)` - Search and replace text in a file
- `list_dir(path)` - List directory contents

## Shell

- `exec(command)` - Execute a shell command

## Web

- `web_search(query, count)` - Search the web (Brave Search)
- `web_fetch(url, max_chars)` - Extract text content from a URL
- `http_client(method, url, headers, body, auth)` - Make HTTP requests with optional auth profiles
- `rss_reader(url, count)` - Read RSS/Atom feeds

## Media

- `generate_image(prompt, filename)` - Generate an image from a text prompt
- `vision(path, url, base64, question, mime_type)` - Analyze and describe images using AI vision

## MCP Client

- `mcp_client(action, url, session_id, auth_token, tool_name, tool_arguments, resource_uri, timeout)` - Connect to external MCP servers to use their tools and resources

## Communication

- `message(content)` - Send a message to the user during processing
- `spawn(task, label)` - Delegate a task to a background subagent

## Scheduling

- `cron(action, message, every_seconds, cron_expr, at, tz)` - Manage scheduled jobs

## Memory

- `memory_save(content)` - Append to today's daily memory log
- `memory_read(file, max_lines)` - Read any memory file (use "list" to see available)
- `memory_search(query)` - Search across all memory files
