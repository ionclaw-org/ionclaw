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

## Communication

- `message(content)` - Send a message to the user during processing
- `spawn(task, label)` - Delegate a task to a background subagent

## Scheduling

- `cron(action, message, every_seconds, cron_expr, at, tz)` - Manage scheduled jobs

## Memory

- `memory_save(history_entry, updated_memory)` - Save to long-term memory
- `memory_read(file, max_lines)` - Read memory or history files
- `memory_search(query)` - Search across memory and history
