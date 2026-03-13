---
name: memory
description: Memory system with daily logs and keyword search. Use to save facts, search past events, or recall user preferences and project context across conversations.
always: true
---

# Memory

## Structure

- `memory/MEMORY.md` -- Long-term curated facts (preferences, project context). Always loaded into your context.
- `memory/YYYY-MM-DD.md` -- Daily logs. Append-only, one file per day. Search them with `memory_search`.

## Save Memories

Use `memory_save` to store durable facts and context. Content is appended to today's daily log automatically:

```
memory_save(content="User prefers dark mode. Project uses OAuth2 for auth.")
```

## Search Past Events

Use `memory_search` to find information across all memory files:

```
memory_search(query="keyword")
```

Returns matching lines with surrounding context, indicating which file each match came from.

## Read Memory Files

Use `memory_read` to read a specific memory file:

```
memory_read(file="MEMORY.md")
memory_read(file="2026-03-13.md")
memory_read(file="2026-03-13.md", max_lines=50)
memory_read(file="list")
```

Use `file="list"` to see all available memory files.

## Pre-Compaction Flush

When the conversation context approaches its limit, durable facts are automatically saved to today's daily log before compaction. MEMORY.md is not modified automatically.
