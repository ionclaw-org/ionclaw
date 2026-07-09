# Custom Providers (OpenAI-Compatible)

IonClaw supports any LLM provider that exposes an OpenAI-compatible chat completions API. This includes self-hosted models (Ollama, LM Studio, vLLM, llama.cpp) and third-party services (MiniMax, Together AI, Fireworks, Groq, etc.).

> To run a GGUF model offline without a separate server, use the built-in `llama` provider instead — it links llama.cpp directly into the binary. See [Local Inference](llama.md).

---

## How It Works

IonClaw routes requests based on the provider prefix in the model string (`provider/model-name`). Any provider name that is not `anthropic` is treated as OpenAI-compatible and uses the standard `/chat/completions` endpoint.

To add a custom provider, you need three things in your `config.yml`:

1. A **credential** entry (API key)
2. A **provider** entry (base URL)
3. A **model** reference in your agent (`provider/model-name`)

---

## Built-in Providers

These provider names ship with a default base URL, so you can omit `base_url` and just supply a credential (the local ones need none). Any other name is still accepted as a custom provider as long as you give it a `base_url`.

| Provider prefix | Default base URL |
|-----------------|------------------|
| `anthropic` | `https://api.anthropic.com` (native, not OpenAI-compatible) |
| `openai` | `https://api.openai.com/v1` |
| `openrouter` | `https://openrouter.ai/api/v1` |
| `deepseek` | `https://api.deepseek.com/v1` |
| `grok` / `xai` | `https://api.x.ai/v1` |
| `google` / `gemini` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| `kimi` / `moonshot` | `https://api.moonshot.cn/v1` |
| `groq` | `https://api.groq.com/openai/v1` |
| `mistral` | `https://api.mistral.ai/v1` |
| `together` / `togetherai` | `https://api.together.xyz/v1` |
| `fireworks` / `fireworks_ai` | `https://api.fireworks.ai/inference/v1` |
| `deepinfra` | `https://api.deepinfra.com/v1/openai` |
| `perplexity` | `https://api.perplexity.ai` |
| `cerebras` | `https://api.cerebras.ai/v1` |
| `sambanova` | `https://api.sambanova.ai/v1` |
| `nvidia` / `nvidia_nim` | `https://integrate.api.nvidia.com/v1` |
| `ollama` | `http://localhost:11434/v1` (local, no credential) |
| `lm_studio` | `http://localhost:1234/v1` (local, no credential) |

```yaml
credentials:
  groq: { type: simple, key: "gsk-..." }

providers:
  groq: { credential: groq }   # base_url resolved automatically

agents:
  main:
    model: "groq/llama-3.3-70b-versatile"
```

---

## Examples

### Ollama (Local)

[Ollama](https://ollama.com) runs models locally and exposes an OpenAI-compatible API on port 11434. `ollama` is a built-in provider: it needs no credential and defaults to `http://localhost:11434/v1`, so a bare provider entry is enough.

```yaml
providers:
  ollama: {}   # defaults to http://localhost:11434/v1, no credential needed

agents:
  main:
    model: "ollama/llama3.3"
```

Pull the model first with `ollama pull llama3.3`, then check what is installed with `ollama list`. Point a remote Ollama server at a different host by setting `base_url`.

### LM Studio (Local)

[LM Studio](https://lmstudio.ai) runs models locally with an OpenAI-compatible server on port 1234.

```yaml
credentials:
  lmstudio:
    type: simple
    key: "lm-studio"

providers:
  lmstudio:
    credential: lmstudio
    base_url: http://localhost:1234/v1

agents:
  main:
    model: "lmstudio/qwen2.5-32b"
```

### MiniMax

```yaml
credentials:
  minimax:
    type: simple
    key: ${MINIMAX_API_KEY}

providers:
  minimax:
    credential: minimax
    base_url: https://api.minimax.chat/v1

agents:
  main:
    model: "minimax/MiniMax-M1"
```

### Together AI

```yaml
credentials:
  together:
    type: simple
    key: ${TOGETHER_API_KEY}

providers:
  together:
    credential: together
    base_url: https://api.together.xyz/v1

agents:
  main:
    model: "together/meta-llama/Llama-3.3-70B-Instruct-Turbo"
```

### Groq

```yaml
credentials:
  groq:
    type: simple
    key: ${GROQ_API_KEY}

providers:
  groq:
    credential: groq
    base_url: https://api.groq.com/openai/v1

agents:
  main:
    model: "groq/llama-3.3-70b-versatile"
```

### Fireworks AI

```yaml
credentials:
  fireworks:
    type: simple
    key: ${FIREWORKS_API_KEY}

providers:
  fireworks:
    credential: fireworks
    base_url: https://api.fireworks.ai/inference/v1

agents:
  main:
    model: "fireworks/accounts/fireworks/models/llama-v3p3-70b-instruct"
```

### vLLM (Self-Hosted)

```yaml
credentials:
  vllm:
    type: simple
    key: "token"

providers:
  vllm:
    credential: vllm
    base_url: http://your-server:8000/v1

agents:
  main:
    model: "vllm/meta-llama/Llama-3.3-70B-Instruct"
```

---

## Claude Code CLI (Subscription)

The built-in `claude-cli` provider drives the local `claude` binary (Claude Code) instead of the API, so responses come from your logged-in subscription without spending API tokens. It is resolved by the `claude-cli/` prefix and needs no credential or provider entry.

```yaml
agents:
  main:
    model: "claude-cli/opus"
    tools: []   # the cli answers in a single text turn, so leave tools empty
```

Install and authenticate Claude Code first (run `claude` and sign in). The provider runs the binary with the prompt on stdin and API-key environment variables stripped, so the subscription answers. It is text-only and single-turn: it does not stream tokens or call tools, so agents using it must have no tools configured. The model after the prefix is passed to `--model` (e.g. `opus`, `sonnet`, `haiku`, or a full model id).

---

## Provider Failover

You can configure multiple providers as fallback profiles. If the primary provider fails (rate limit, timeout, error), IonClaw automatically retries with the next profile.

```yaml
agents:
  main:
    model: "ollama/llama3.3"
    model_params:
      temperature: 0.5
    profiles:
      - model: "ollama/llama3.3"
        credential: ollama
        priority: 1
        model_params:
          temperature: 0.3
      - model: "minimax/MiniMax-M1"
        credential: minimax
        priority: 2
        model_params:
          max_tokens: 8192
      - model: "together/meta-llama/Llama-3.3-70B-Instruct-Turbo"
        credential: together
        priority: 3
```

Profiles are tried in priority order. IonClaw applies exponential backoff between retries and tracks provider health automatically.

Each profile can define its own `model_params` that override the agent-level defaults. In this example, the Ollama profile uses `temperature: 0.3` instead of `0.5`, while the MiniMax profile adds `max_tokens: 8192`. The Together AI profile inherits the agent-level params as-is.

Failover is triggered by these error categories: `rate_limit`, `auth`, `model_not_found`, `host_not_found`, `timeout`, and `transient`. Non-failoverable errors (`context_overflow`, `billing`, `thinking_constraint`) are handled locally without switching providers. See [architecture.md](architecture.md#error-classification) for the full error classification table.

---

## Provider Options

Each provider entry supports these fields:

```yaml
providers:
  my-provider:
    credential: my-credential    # Required. Credential name from the credentials section.
    base_url: https://...        # Required for custom providers. The OpenAI-compatible base URL.
    timeout: 60                  # Optional. Request timeout in seconds (default: 60).
    request_headers:             # Optional. Custom HTTP headers sent with every request.
      X-Custom-Header: "value"
    model_params:                # Optional. Default model parameters for this provider.
      temperature: 0.7
      max_tokens: 4096
```

---

## Model Parameters Merge Order

Model parameters are resolved with a three-level merge chain. Each level overrides the one before it:

1. **Provider-level** `model_params` — base defaults for all agents using this provider
2. **Agent-level** `model_params` — overrides provider defaults
3. **Profile-level** `model_params` — overrides agent defaults (failover profiles only)

```yaml
providers:
  ollama:
    credential: ollama
    base_url: http://localhost:11434/v1
    model_params:
      temperature: 0.5
      max_tokens: 2048

agents:
  main:
    model: "ollama/llama3.3"
    model_params:
      temperature: 0.8    # overrides provider's 0.5
      # max_tokens: 2048  — inherited from provider
    profiles:
      - model: "ollama/llama3.3"
        credential: ollama
        priority: 1
        model_params:
          temperature: 0.3  # overrides agent's 0.8 for this profile
      - model: "minimax/MiniMax-M1"
        credential: minimax
        priority: 2
        # inherits agent-level params: temperature 0.8, max_tokens 2048
```

In this example:
- The first profile resolves to `temperature: 0.3` (profile override) and `max_tokens: 2048` (inherited from provider → agent).
- The second profile resolves to `temperature: 0.8` (from agent) and `max_tokens: 2048` (from provider), since it has no profile-level overrides.

Supported model parameters:

| Parameter | Type | Description |
|-----------|------|-------------|
| `temperature` | float | Sampling temperature (0.0 - 2.0) |
| `max_tokens` | int | Maximum response tokens |
| `top_p` | float | Nucleus sampling threshold |
| `thinking` | string | Extended thinking level: `off`, `low`, `medium`, `high` |
| `context_window` | int | Override context window size for this model |

---

## Custom Headers

Some providers require additional headers (e.g., OpenRouter). Use `request_headers`:

```yaml
providers:
  openrouter:
    credential: openrouter
    base_url: https://openrouter.ai/api/v1
    request_headers:
      HTTP-Referer: "https://your-app.com"
      X-Title: "My App"
```

---

## Notes

- The provider name in `config.yml` is arbitrary — it just needs to match the prefix in your model string.
- All OpenAI-compatible providers support text and image input, streaming, and tool use.
- Audio input is supported for providers that implement the OpenAI audio format.
- Capabilities are gated by the embedded model table: for a model the table marks as lacking a feature, the reasoning/thinking params, the `vision` tool, and the `tools` payload are dropped before the request goes out (with a logged warning) instead of being sent and rejected with a 400. Unknown models are treated permissively (features left enabled).
- Environment variables (`${VAR_NAME}`) can be used anywhere in the config. Use a `.env` file to keep keys out of version control.
