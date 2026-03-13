# IonClaw Configuration

IonClaw is a C++ AI agent orchestrator that runs anywhere as a single native binary — on server, desktop, and mobile. It is configured through a `config.yml` file located in the project root directory. This document describes every configuration section, field, default value, and provides practical examples.

---

## Environment Variables

Environment variables can be referenced anywhere in `config.yml` using the `${VAR_NAME}` syntax. IonClaw automatically loads a `.env` file from the project directory if one is present, so you can keep secrets out of version control.

Example `.env` file:

```
ANTHROPIC_API_KEY=sk-ant-...
```

Referencing in `config.yml`:

```yaml
credentials:
  anthropic:
    type: simple
    key: ${ANTHROPIC_API_KEY}
  web_client:
    type: login
    username: admin
    password: changeme
```

---

## Complete YAML Reference

```yaml
# ---------------------------------------------------------------------------
# Bot
# ---------------------------------------------------------------------------
bot:
  name: "IonClaw"                 # str  -- Display name of the bot.
  description: ""                 # str  -- Short description.

# ---------------------------------------------------------------------------
# Server
# ---------------------------------------------------------------------------
server:
  host: "0.0.0.0"                # str  -- Bind address for the HTTP server.
  port: 8080                     # int  -- Port the server listens on.
  public_url: ""                 # str  -- Public URL for external access.
  credential: "server"           # str  -- Credential name for JWT signing.

# ---------------------------------------------------------------------------
# Agents
# ---------------------------------------------------------------------------
agents:
  main:
    workspace: "./workspace"     # str  -- Working directory for this agent.
    model: "anthropic/claude-sonnet-4-20250514"  # str  -- Model in "provider/model" format.
    description: ""              # str  -- Agent description for classifier routing.
    instructions: ""             # str  -- Agent-specific system prompt instructions.
    tools: []                    # list -- Tool whitelist. Empty = all tools.
    tool_policy:
      allow: []                  # list -- Fine-grained tool allow list. Empty = all.
      deny: []                   # list -- Tool deny list. Deny overrides allow.
    agent_params:
      max_iterations: 40         # int  -- Maximum agentic loop iterations per request.
      max_concurrent: 1          # int  -- Parallel message processing limit.
      max_history: 500           # int  -- Maximum messages before trimming. 0 = no limit.
      context_tokens: 0          # int  -- Context window cap in tokens. 0 = use model limit.
      channel_history_limits:    # dict -- Per-channel override for max_history.
        # telegram: 200          #         Key = channel prefix, value = max messages.
        # web: 1000              #         Channels not listed use max_history.
    model_params:                # dict -- Model parameters passed to the LLM provider.
      # temperature: 0.7         # float -- Sampling temperature.
      # max_tokens: 4096         # int  -- Maximum response tokens.
      # thinking: "medium"       # str  -- Extended thinking level: off, low, medium, high.
      # context_window: 128000   # int  -- Override context window size for this model.
    profiles:                    # list -- Auth profile failover. When set, agent retries
      []                         #         across profiles on transient/rate-limit errors.
      # - model: "anthropic/claude-sonnet-4-20250514"
      #   credential: "anthropic-key-1"
      #   priority: 1
      #   model_params:          # dict -- Per-profile model params. Overrides agent-level.
      #     temperature: 0.5
      # - model: "anthropic/claude-sonnet-4-20250514"
      #   credential: "anthropic-key-2"
      #   priority: 2
    subagent_limits:
      max_depth: 5                 # int  -- Maximum subagent nesting depth.
      max_children: 5              # int  -- Maximum concurrent child subagents.
      default_timeout_seconds: 0   # int -- Subagent run timeout. 0 = use registry default (300s).
      allow_agents: []             # list -- Which agents subagents can route to. Empty = same agent.

# ---------------------------------------------------------------------------
# Classifier
# ---------------------------------------------------------------------------
classifier:
  model: ""                      # str  -- Model for agent classification. Empty = default agent's model.

# ---------------------------------------------------------------------------
# Credentials
# ---------------------------------------------------------------------------
credentials:
  anthropic:
    type: "simple"               # str  -- Credential type (simple, login, bearer, oauth1).
    key: ""                      # str  -- API key or token.

# ---------------------------------------------------------------------------
# Providers
# ---------------------------------------------------------------------------
providers:
  anthropic:
    credential: ""               # str  -- Credential name for this provider.
    base_url: ""                 # str  -- Custom base URL for OpenAI-compatible APIs.
    timeout: 60                  # int  -- Request timeout in seconds.
    request_headers: {}          # dict -- Custom HTTP headers.
    model_params: {}             # dict -- Default model parameters (merged as base; agent-level overrides).

# ---------------------------------------------------------------------------
# Web Client
# ---------------------------------------------------------------------------
web_client:
  credential: "web_client"      # str  -- Credential name (type: login) for web UI auth.

# ---------------------------------------------------------------------------
# Image Generation
# ---------------------------------------------------------------------------
image:
  model: ""                     # str  -- Model in "provider/model" format.
  aspect_ratio: ""              # str  -- Default aspect ratio.
  size: ""                      # str  -- Default image size.
  # Provider is auto-detected from model prefix: gemini/, openai/, grok/
  # Each provider has a dedicated generator with API-specific parameters.
  # See docs/image-generation.md for full provider details.

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
tools:
  exec_timeout: 60              # int  -- Max execution time for exec tool (seconds).
  web_search_provider: brave    # str  -- Search provider: "brave" or "duckduckgo".
  web_search_credential: brave  # str  -- Credential name for the search API.
  # Nested form (alternative, same effect):
  # web_search:
  #   provider: brave
  #   credential: brave

# ---------------------------------------------------------------------------
# Channels
# ---------------------------------------------------------------------------
channels:
  telegram:
    enabled: false              # bool -- Enable Telegram bot.
    credential: ""              # str  -- Credential name (type: simple) for Bot API token.
    allowed_users: []           # list -- Telegram usernames or user IDs; empty = allow all.

# ---------------------------------------------------------------------------
# Storage
# ---------------------------------------------------------------------------
storage:
  type: "local"                 # str  -- Storage provider type. Only "local" is supported for now.

# ---------------------------------------------------------------------------
# Heartbeat
# ---------------------------------------------------------------------------
heartbeat:
  enabled: false                # bool -- Enable periodic heartbeat.
  # interval: 1800              # int  -- Seconds between checks. Default: 1800.

# ---------------------------------------------------------------------------
# Transcription
# ---------------------------------------------------------------------------
transcription:
  model: ""                      # str  -- Model in "provider/model" format for audio transcription.

# ---------------------------------------------------------------------------
# Message Queue
# ---------------------------------------------------------------------------
messages:
  queue:
    mode: "collect"              # str  -- Default queue mode: steer, followup, collect, steer_backlog, interrupt.
    by_channel:                  # dict -- Per-channel mode override.
      # telegram: "followup"     #         Key = channel prefix, value = queue mode.
    debounce_ms: 1000            # int  -- Collect debounce period in milliseconds.
    cap: 20                      # int  -- Max queued messages before drop policy kicks in.
    drop: "summarize"            # str  -- What to do when cap exceeded: old, new, summarize.

# ---------------------------------------------------------------------------
# Session Budget
# ---------------------------------------------------------------------------
session_budget:
  max_disk_bytes: 0              # int  -- Max total disk for sessions. 0 = unlimited.
  high_water_ratio: 0.8          # float -- Trigger cleanup at this percentage of budget.
```

---

## Agent configuration details

### Workspace resolution

Each agent's workspace path is resolved at startup relative to the project root (where `config.yml` lives). Relative paths (e.g. `./workspace`) are resolved against the project root; absolute paths are used as-is. The workspace directory is created automatically if it does not exist.

### Workspace restriction

File-related tools are restricted to (restriction is always enforced by the backend):

1. The agent's own workspace directory.
2. The shared `public/` directory at the project root.

File tools (`read_file`, `write_file`, `edit_file`, `list_dir`), HTTP client `download_path`/`upload_file`, and the exec working directory respect these boundaries. Paths outside them raise an error.

### Tool whitelisting

The `tools` field on each agent is a list of tool names. When set, only tools whose name appears in this list are available to that agent. When empty (default), all tools are registered. Use this to limit an agent to a subset of tools (e.g. no `exec`, no `spawn`).

### Agent instructions and description

- **instructions** — Injected into the system prompt as a dedicated section. Use for behavioral rules, domain expertise, or output formatting.
- **description** — Used by the classifier when multiple agents are configured. Describe what the agent does so the classifier can route messages correctly.

### Per-channel history limits

The `channel_history_limits` map under `agent_params` allows overriding `max_history` per channel. The key is the channel prefix (the part before `:` in the session key, e.g. `web`, `telegram`). Sessions whose channel prefix matches a key use that limit; all others fall back to the global `max_history`.

```yaml
agent_params:
  max_history: 500
  channel_history_limits:
    telegram: 200   # Telegram sessions keep at most 200 messages
    web: 1000       # Web sessions keep at most 1000 messages
```

### Tool policy

The `tool_policy` on each agent provides fine-grained runtime tool access control, separate from the `tools` whitelist:

- **allow** — When set, only tools in this list are available. Empty = all tools.
- **deny** — Tools in this list are blocked. Deny takes precedence over allow.

```yaml
agents:
  main:
    tool_policy:
      deny: [exec, spawn]   # block dangerous tools for this agent
  coder:
    tool_policy:
      allow: [read_file, write_file, edit_file, list_dir, exec]
      deny: [spawn]          # allow exec but not spawn
```

The `tools` field controls which tools are *registered* (available in schema). The `tool_policy` field controls which registered tools can *execute* at runtime. Both can be used together.

### Subagent limits

Each agent can configure limits on subagent spawning:

```yaml
agents:
  main:
    subagent_limits:
      max_depth: 5          # max nesting depth (default 5)
      max_children: 5       # max concurrent children (default 5)
      allow_agents: []      # empty = same agent only, ["*"] = all agents
```

### Message queue

The `messages.queue` section controls how messages arriving during an active agent turn are handled:

```yaml
messages:
  queue:
    mode: "collect"           # default mode for all channels
    by_channel:
      telegram: "followup"    # telegram uses followup instead
      web: "steer_backlog"    # web uses steer with fallback
    debounce_ms: 1000         # collect mode debounce window
    cap: 20                   # max queued messages
    drop: "summarize"          # old, new, or summarize
```

**Modes:** `steer` (inject mid-turn), `followup` (next turn), `collect` (batch after debounce), `steer_backlog` (try steer, else followup), `interrupt` (abort and process).

**Drop policies:** When the queue exceeds `cap`, excess messages are handled per policy: `old` drops oldest, `new` rejects incoming, `summarize` drops oldest but keeps summary lines for context injection.

### Classifier (multi-agent)

When multiple agents are defined, the optional `classifier.model` specifies the model used to route each message. If empty, the default (first) agent's model is used. The classifier analyzes the user message and recent history to select the best agent.

### Context tokens

The `context_tokens` field under `agent_params` sets an upper bound on the context window size (in tokens). When set to a positive value, the agent's context window is capped at that number, even if the model supports more. When `0` (default), the model's known limit is used.

This is useful for controlling costs or when using models behind proxies that impose lower limits than the model's native capacity.

### Model parameters

Model parameters (`max_tokens`, `temperature`, etc.) are resolved with a three-level merge chain. Each level overrides the one before it:

1. **Provider-level** `model_params` — base defaults for all agents using this provider.
2. **Agent-level** `model_params` — overrides provider defaults.
3. **Profile-level** `model_params` — overrides agent defaults (failover profiles only).

See [Custom Providers — Model Parameters Merge Order](custom-providers.md#model-parameters-merge-order) for a detailed example.

Special model parameters:

- **`thinking`** — Extended thinking/reasoning level. Values: `off`, `low`, `medium`, `high`. For Anthropic models, this maps to `budget_tokens` scaling. For OpenRouter models, this maps to `reasoning.effort`.
- **`context_window`** — Override the context window size for a specific model. Useful for custom or fine-tuned models whose context window size is not in the built-in lookup table.

### Auth profile failover

When an agent has multiple `profiles` configured, IonClaw wraps the providers in a `FailoverProvider` that automatically retries across profiles on failure. Each profile specifies a model, credential, priority, and optionally its own `model_params`.

```yaml
agents:
  main:
    model: "anthropic/claude-sonnet-4-20250514"    # primary model
    model_params:
      temperature: 0.5
    profiles:
      - model: "anthropic/claude-sonnet-4-20250514"
        credential: "anthropic-key-1"
        priority: 1
      - model: "anthropic/claude-sonnet-4-20250514"
        credential: "anthropic-key-2"
        priority: 2
        model_params:
          temperature: 0.8    # override for this profile only
      - model: "openai/gpt-4o"
        credential: "openai"
        priority: 3
        model_params:
          max_tokens: 16384
```

**Failover behavior:**

- Each profile can define its own `model_params` that override the agent-level defaults for that profile only.
- Retries use exponential backoff: 1s → 2s → 4s → 8s max, with random jitter (0–500ms).
- Rate-limit errors use a 2s base delay instead of 1s.
- Each provider has a 60-second cooldown after failure before being retried.
- Providers are tried in priority order; lower priority number = higher priority.
- Stream requests that have already delivered content are NOT retried (prevents duplicate partial content).

---

## Saving configuration

### Advanced YAML editor

When you save the full YAML via the Advanced editor, sensitive values shown as `****` in the UI are **not** overwritten. The save process keeps existing secret values for any key that appears masked: the system reloads the config after the first write and replaces placeholders with the stored secrets before writing again.

### Section updates (PUT /api/config/{section})

Single-section updates (e.g. from the Settings UI) only touch the specified section. Sensitive fields are masked in API responses and preserved when merging updates.

---

## Examples

### Basic Setup with Anthropic

```yaml
bot:
  name: "MyAssistant"

server:
  host: "0.0.0.0"
  port: 8080

credentials:
  anthropic:
    type: simple
    key: ${ANTHROPIC_API_KEY}
  web_client:
    type: login
    username: admin
    password: ${AUTH_PASSWORD}

providers:
  anthropic:
    credential: anthropic

agents:
  main:
    model: "anthropic/claude-sonnet-4-20250514"

web_client:
  credential: web_client
```

### Multiple Agents

```yaml
credentials:
  anthropic:
    type: simple
    key: ${ANTHROPIC_API_KEY}
  openai:
    type: simple
    key: ${OPENAI_API_KEY}

providers:
  anthropic:
    credential: anthropic
  openai:
    credential: openai

agents:
  main:
    model: "anthropic/claude-sonnet-4-20250514"
    description: "General-purpose assistant"
  researcher:
    workspace: "./workspace/research"
    model: "openai/gpt-4o"
    description: "Research specialist"
    tools: [read_file, write_file, web_search, web_fetch]

classifier:
  model: "anthropic/claude-haiku-3"
```

### OpenAI-Compatible Endpoint

```yaml
credentials:
  custom:
    type: simple
    key: ${CUSTOM_API_KEY}

providers:
  custom:
    credential: custom
    base_url: "https://my-llm-server.example.com/v1"

agents:
  main:
    model: "custom/my-model"
```

---

## Supported providers

| Provider     | Description                                      |
|--------------|--------------------------------------------------|
| `anthropic`  | Anthropic Claude (native API)                    |
| `openai`     | OpenAI GPT (native API)                         |
| `gemini`     | Google Gemini (OpenAI-compatible or native)     |
| `grok`       | xAI Grok (OpenAI-compatible)                    |
| `openrouter`| Multi-model router (OpenAI-compatible)         |
| `deepseek`   | DeepSeek (OpenAI-compatible)                   |
| `kimi`       | Moonshot Kimi (OpenAI-compatible)               |
| Custom       | Any OpenAI-compatible endpoint via `base_url`  |

Model format is `provider/model-name` (e.g. `anthropic/claude-sonnet-4-20250514`).

---

## Credential types

Credentials are referenced by name from `providers`, `tools.web_search`, `server`, and `web_client`. Supported types include:

- **simple** — `key` (API key or token).
- **login** — `username`, `password` (web UI login).
- **bearer** — `token` (Bearer auth).
- **oauth1** — `consumer_key`, `consumer_secret`, `access_token`, `access_token_secret`.


---

## Notes

- **Defaults.** Only include sections and fields you want to override; defaults are applied automatically.
- **Workspace resolution.** Relative paths are resolved relative to the project root. Absolute paths are used as-is. Directories are created at startup if missing.
- **Server credential.** If `server.credential` is not set or the credential has no key, a random JWT secret is generated at startup and persisted to `config.yml` so tokens remain valid across restarts.
- **Web client auth.** If `web_client.credential` is not set or the login credential is missing, default credentials (admin/admin) are created and persisted to `config.yml` at startup.
