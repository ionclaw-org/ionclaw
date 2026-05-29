# Local Inference (llama.cpp)

IonClaw can run GGUF models fully offline through an embedded [llama.cpp](https://github.com/ggml-org/llama.cpp) backend. The `llama` provider links `libllama` directly into the binary — there is no separate process, no HTTP server, and no API key. This is different from running llama.cpp (or Ollama / LM Studio / vLLM) as a local OpenAI-compatible server, which is covered in [custom-providers.md](custom-providers.md).

---

## How It Works

The provider is selected by the `llama/` prefix in an agent's model string. The model file is a local `.gguf` resolved from the provider config:

- The model **and** its inference context are loaded lazily on the first request and kept resident for the provider's lifetime. Subsequent turns reuse the warm context and only clear the previous turn's token cache, so the model is never torn down and rebuilt between messages.
- Prompts are formatted with the model's own chat template through llama.cpp's `common` library, falling back to a generic template when the model defines none. Tool definitions are injected into the same template when the agent exposes tools.
- Tool calling is supported: when tools are present, sampling is constrained to the model's tool-call grammar and the structured calls are parsed out of the output. See [Tool Calling](#tool-calling).
- Tokens are streamed as they are generated, with partial UTF-8 sequences held back until they are complete.
- Cancellation is honored during both prompt processing and generation: a `/stop` (or server shutdown) interrupts the running decode immediately, rather than waiting for the response to finish.
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

Declare one provider per `.gguf` file, with the file path in `base_url`. The provider key is just a label — agents do not reference it directly. No credential is required.

```yaml
providers:
  qwen:
    base_url: "/path/to/models/qwen2.5-3b-instruct-q4_k_m.gguf"
    model_params:
      context_size: 8192      # n_ctx of the loaded model
      gpu_layers: -1          # -1 offloads all layers to the GPU, 0 keeps everything on CPU

agents:
  main:
    workspace: "workspace"
    model: "llama/qwen2.5-3b"   # "llama" picks local inference, the rest matches the file name
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

## Selecting a Model

The `llama/` prefix means "use a local model". The text after it is matched against the **start of the `.gguf` file name** in each provider's `base_url`, case-insensitively, so a short label like `qwen3` resolves to `qwen3-xl-instruct-q4_k_m.gguf` without spelling out the whole name.

To offer several models, declare several providers, one per file:

```yaml
providers:
  qwen3:
    base_url: "/path/to/models/qwen3-xl-instruct-q4_k_m.gguf"
    model_params:
      context_size: 8192
      gpu_layers: -1
  llama3:
    base_url: "/path/to/models/llama-3.1-8b-instruct-q4_k_m.gguf"

agents:
  fast:
    model: "llama/qwen3"        # resolves to qwen3-xl-instruct-q4_k_m.gguf
  reasoner:
    model: "llama/llama-3.1"    # resolves to llama-3.1-8b-instruct-q4_k_m.gguf
```

Resolution rules:

- Only providers whose `base_url` file name ends in `.gguf` are considered — remote providers pointing at a URL are skipped automatically.
- The match is on the file name only, so the directory part of the path is ignored.
- The first provider whose file name starts with the requested term wins.
- `llama` with no suffix (or a trailing `llama/`) selects the first model file found.
- A term that matches no model file is a configuration error and is reported at startup.

Each model file resolves to its own resident provider, so different agents can run different local models side by side.

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

## Tool Calling

Local models call tools through the official llama.cpp function-calling machinery, so the behavior matches what `llama-server` offers. When an agent exposes tools:

- The tool schemas are passed into the model's chat template, which renders them in the format the model was trained on (Hermes, Llama 3.x, Functionary, and others are detected automatically).
- Sampling is constrained by a grammar derived from those schemas, so the model can only emit syntactically valid calls. Lazy grammars are used where the template supports them, so plain text is unconstrained until the model starts a tool call.
- The raw output is parsed back into structured calls (name, arguments, id) and handed to the agent loop exactly like a remote provider's tool calls.

Tool calling quality depends on the model: pick a GGUF that was trained for it. A model with no tool support simply never emits calls and the grammar stays inactive.

---

## Notes

- Runs entirely offline — no network access and no credential.
- The model and its context are loaded once and reused across turns, so only the first request pays the load cost.
- Each distinct model file loads its own copy into memory, and requests on the same provider are serialized because a local model context is not thread-safe.
- A `llama` profile can take part in [provider failover](custom-providers.md#provider-failover) alongside remote providers, falling back to or from a local model on error.
