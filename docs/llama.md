# Local Inference (llama.cpp)

IonClaw can run GGUF models fully offline through an embedded [llama.cpp](https://github.com/ggml-org/llama.cpp) backend. The `llama` provider links `libllama` directly into the binary — there is no separate process, no HTTP server, and no API key. This is different from running llama.cpp (or Ollama / LM Studio / vLLM) as a local OpenAI-compatible server, which is covered in [custom-providers.md](custom-providers.md).

---

## How It Works

The provider is selected by the `llama/` prefix in an agent's model string. The model file is a local `.gguf` pointed to by `base_url`:

- The model is loaded lazily on the first request and kept resident for the provider's lifetime.
- Prompts are formatted with the model's built-in chat template, falling back to ChatML when the model has none.
- Tokens are streamed as they are generated, with partial UTF-8 sequences held back until they are complete.
- Generation honors the same cancellation path as the API providers (server shutdown and `/stop`).
- When a prompt exceeds the context size, the provider reports `context_overflow` and the agent loop compacts and retries, exactly like the remote providers.

---

## Building

Local inference is gated by the `IONCLAW_LLAMA_CPP` CMake option. It defaults to **ON** on every platform except the Apple watch/tv/vision targets, where llama.cpp is not supported.

| Platform | Default |
|----------|---------|
| Linux, macOS, Windows, iOS, Android | ON |
| tvOS, watchOS, visionOS | OFF |

```bash
# default build already includes local inference on supported platforms
cmake -B build
cmake --build build

# disable it explicitly
cmake -B build -DIONCLAW_LLAMA_CPP=OFF
```

> The first build fetches and compiles llama.cpp and ggml, so it takes considerably longer than a normal build. The dependency is pinned to a specific upstream commit for reproducible builds.

GPU acceleration follows the llama.cpp platform defaults — Metal and Accelerate on Apple Silicon — and the required frameworks are linked transitively through the `llama` target, so no GPU configuration is needed at build time. CPU code is compiled for the architecture baseline (`GGML_NATIVE OFF`) rather than `-mcpu=native`, which keeps the binary portable across machines.

---

## Configuration

Declare the `llama` provider with the path to the `.gguf` file, then point an agent at it. No credential is required.

```yaml
providers:
  llama:
    base_url: "/path/to/models/qwen2.5-3b-instruct-q4_k_m.gguf"
    model_params:
      context_size: 8192      # n_ctx of the loaded model
      gpu_layers: -1          # -1 offloads all layers to the GPU, 0 keeps everything on CPU

agents:
  main:
    workspace: "workspace"
    model: "llama/qwen2.5-3b"   # the "llama" prefix selects the provider, the rest is a free label
    description: "Local offline assistant"
    instructions: ""
    tools:
      - read_file
      - write_file
      - exec
    model_params:
      temperature: 0.7
      top_p: 0.95
      top_k: 40
      repeat_penalty: 1.1
      repeat_last_n: 64
      max_tokens: 2048
```

---

## Model Parameters

Load-time parameters configure how the model is loaded and belong on the **provider** entry. Sampling parameters apply per request and belong on the **agent** (or a failover profile).

| Parameter | Level | Type | Description |
|-----------|-------|------|-------------|
| `context_size` | provider | int | Context window (`n_ctx`) for the loaded model (default: 4096) |
| `gpu_layers` | provider | int | Layers offloaded to the GPU: `-1` for all, `0` for CPU-only (default: -1) |
| `max_tokens` | agent | int | Maximum tokens generated per response |
| `temperature` | agent | float | Sampling temperature, `<= 0` switches to greedy decoding |
| `top_p` | agent | float | Nucleus sampling threshold |
| `top_k` | agent | int | Top-k sampling cutoff |
| `repeat_penalty` | agent | float | Repetition penalty, `1.0` disables it |
| `repeat_last_n` | agent | int | Window of recent tokens the repeat penalty applies to, `0` disables it |

---

## Notes

- Runs entirely offline — no network access and no credential.
- Each agent that references a `llama/` model loads its own copy of the file into memory, and requests on the same provider are serialized because a local model context is not thread-safe.
- A `llama` profile can take part in [provider failover](custom-providers.md#provider-failover) alongside remote providers, falling back to or from a local model on error.
