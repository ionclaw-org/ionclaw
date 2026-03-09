# Agents

## Main Agent

The main agent handles all conversations. It has full access to tools and workspace.

## Subagents

Subagents are spawned via the `spawn` tool for background tasks. They run asynchronously in a child session with their own context. Subagents have access to the same tools as their parent agent (determined by the agent's `tools` configuration). They can also spawn nested subagents up to the configured depth limit.

### Limits

- **Max depth:** 5 levels of nested subagent spawning (configurable via `subagent_limits.max_depth`).
- **Max children:** 5 concurrent subagents per parent session (configurable via `subagent_limits.max_children`).
- **Timeout:** 300 seconds by default (configurable via `subagent_limits.default_timeout_seconds`). Expired runs are automatically killed.

### Recovery

On server restart, any subagent runs that were Active or Pending are automatically marked as Errored with outcome "Interrupted by server restart".
