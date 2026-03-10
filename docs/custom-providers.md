# Custom Providers (OpenAI-Compatible)

IonClaw supports any LLM provider that exposes an OpenAI-compatible chat completions API. This includes self-hosted models (Ollama, LM Studio, vLLM, llama.cpp) and third-party services (MiniMax, Together AI, Fireworks, Groq, etc.).

---

## How It Works

IonClaw routes requests based on the provider prefix in the model string (`provider/model-name`). Any provider name that is not `anthropic` is treated as OpenAI-compatible and uses the standard `/chat/completions` endpoint.

To add a custom provider, you need three things in your `config.yml`:

1. A **credential** entry (API key)
2. A **provider** entry (base URL)
3. A **model** reference in your agent (`provider/model-name`)

---

## Examples

### Ollama (Local)

[Ollama](https://ollama.com) runs models locally and exposes an OpenAI-compatible API on port 11434.

```yaml
credentials:
  ollama:
    type: simple
    key: "ollama"  # Ollama ignores the key, but a value is required

providers:
  ollama:
    credential: ollama
    base_url: http://localhost:11434/v1

agents:
  main:
    model: "ollama/llama3.3"
```

> Ollama does not require an API key. Use any non-empty string.

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
- Environment variables (`${VAR_NAME}`) can be used anywhere in the config. Use a `.env` file to keep keys out of version control.
