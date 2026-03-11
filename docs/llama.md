# Local LLM Inference (llama.cpp)

IonClaw integrates [llama.cpp](https://github.com/ggml-org/llama.cpp) for running LLM models locally on your device. This enables **fully offline operation** — no API keys, no internet connection, no cloud dependency. If your hardware can run the model, you can use IonClaw completely disconnected.

---

## Overview

The `llama` provider loads GGUF model files directly and runs inference on your local hardware. It supports:

- CPU inference on any platform (macOS, Linux, Windows, iOS, Android)
- GPU acceleration via Metal (Apple Silicon, iOS), CUDA (NVIDIA), Vulkan, and OpenCL
- Streaming and non-streaming responses
- Chat template auto-detection from the model file
- Full model_params support (temperature, top_p, top_k, repeat_penalty)
- Configurable context window size and GPU offloading

---

## Build Configuration

llama.cpp is an **optional** dependency, enabled by default. To disable it (e.g., for embedded/constrained devices like ESP32):

```bash
# build with llama.cpp (default)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# build without llama.cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release -DIONCLAW_LLAMA_CPP=OFF
```

When disabled, the `llama` provider is not available and the binary is smaller. All other providers continue to work normally.

### GPU Acceleration

llama.cpp auto-detects available backends. To explicitly enable specific acceleration:

```bash
# Apple Silicon (Metal) — enabled automatically on macOS
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGGML_METAL=ON

# NVIDIA CUDA
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGGML_CUDA=ON

# Vulkan (cross-platform GPU)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGGML_VULKAN=ON

# OpenBLAS (CPU acceleration)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGGML_BLAS=ON -DGGML_BLAS_VENDOR=OpenBLAS
```

---

## Configuration

Add the `llama` provider to your `config.yml`. Unlike cloud providers, it does not need credentials — only a path to the model file.

```yaml
providers:
  llama:
    base_url: "/path/to/models/llama-3.2-3b-instruct-q4_k_m.gguf"
    model_params:
      context_size: 4096
      gpu_layers: -1

agents:
  main:
    model: "llama/local"
    model_params:
      max_tokens: 2048
      temperature: 0.7
```

### Provider Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `base_url` | string | — | **Required.** Absolute path to the `.gguf` model file. |
| `model_params.context_size` | int | 4096 | Context window size in tokens. |
| `model_params.gpu_layers` | int | -1 | Number of layers to offload to GPU. `-1` = all layers (full GPU). `0` = CPU only. |

### Model Parameters

Standard model parameters work with the llama provider via the three-level merge chain (provider → agent → profile), just like cloud providers:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `max_tokens` | int | 4096 | Maximum tokens to generate per response. |
| `temperature` | float | 0.7 | Sampling temperature. `0` = greedy (deterministic). |
| `top_p` | float | 0.95 | Nucleus sampling threshold. |
| `top_k` | int | 40 | Top-K sampling. `0` = disabled. |
| `repeat_penalty` | float | 1.1 | Repetition penalty. `1.0` = disabled. |

---

## Model Format (GGUF)

llama.cpp uses the **GGUF** (GPT-Generated Unified Format) file format. GGUF files are quantized model weights optimized for efficient CPU/GPU inference.

### Quantization Levels

| Quantization | Size (7B) | Quality | Speed | Use Case |
|-------------|-----------|---------|-------|----------|
| Q2_K | ~2.7 GB | Low | Fastest | Very constrained devices |
| Q4_K_M | ~4.1 GB | Good | Fast | Recommended for most use cases |
| Q5_K_M | ~4.8 GB | Very good | Medium | Balance of quality and speed |
| Q6_K | ~5.5 GB | Excellent | Slower | High quality, enough RAM |
| Q8_0 | ~7.2 GB | Near-lossless | Slowest | Maximum quality |
| F16 | ~14 GB | Lossless | Slowest | Reference quality |

### Where to Get Models

Download GGUF models from [Hugging Face](https://huggingface.co/models?library=gguf). Popular choices:

- **Llama 3.2** (1B, 3B) — compact and fast
- **Llama 3.1** (8B, 70B) — strong general purpose
- **Mistral** (7B) — efficient and capable
- **Phi-3** (3.8B, 14B) — strong for its size
- **Qwen 2.5** (7B, 14B, 32B, 72B) — multilingual
- **Gemma 2** (2B, 9B, 27B) — Google's open models
- **DeepSeek-R1** (1.5B, 7B, 8B, 14B, 32B, 70B) — reasoning models

Search for `<model-name>-GGUF` on Hugging Face and download the quantization that fits your hardware.

### Hardware Requirements

A rough guide for RAM requirements:

| Model Size | Q4_K_M | Q8_0 |
|-----------|--------|------|
| 1B | ~1 GB | ~1.5 GB |
| 3B | ~2 GB | ~3.5 GB |
| 7B | ~4 GB | ~7.5 GB |
| 13B | ~8 GB | ~14 GB |
| 34B | ~20 GB | ~36 GB |
| 70B | ~40 GB | ~72 GB |

> The context window also consumes memory. A 4096-token context adds ~200 MB for 7B models.

---

## Fully Offline Operation

With the llama provider, IonClaw can operate **100% offline**. This is ideal for:

- **Air-gapped environments** — secure networks with no internet access
- **Privacy-sensitive deployments** — all data stays on your device, nothing leaves the machine
- **Edge devices** — laptops, workstations, or on-premise servers without reliable connectivity
- **Development** — no API costs, no rate limits, no latency to cloud services

To run fully offline, configure all agents to use the `llama` provider and disable any features that require network access (web search, external channels, etc.):

```yaml
providers:
  llama:
    base_url: "/models/qwen2.5-7b-instruct-q4_k_m.gguf"
    model_params:
      context_size: 8192
      gpu_layers: -1

agents:
  main:
    model: "llama/local"
    tools:
      - read_file
      - write_file
      - edit_file
      - list_dir
      - exec
      - memory_save
      - memory_read
      - memory_search
    model_params:
      max_tokens: 4096
      temperature: 0.3
```

> No credentials section is needed when running fully offline with only the llama provider.

---

## Failover (Local + Cloud)

You can use the llama provider as a fallback when cloud providers are unavailable, or use cloud as a fallback when local inference is too slow:

```yaml
providers:
  llama:
    base_url: "/models/llama-3.2-3b-instruct-q4_k_m.gguf"
    model_params:
      context_size: 4096
      gpu_layers: -1

  anthropic:
    credential: anthropic

agents:
  main:
    model: "anthropic/claude-sonnet-4-20250514"
    profiles:
      - model: "anthropic/claude-sonnet-4-20250514"
        credential: anthropic
        priority: 1
      - model: "llama/local"
        base_url: "/models/llama-3.2-3b-instruct-q4_k_m.gguf"
        priority: 2
        model_params:
          context_size: 4096
          gpu_layers: -1
```

In this setup, IonClaw uses Claude as the primary model and falls back to the local Llama model if Claude is unavailable (no internet, rate limited, etc.).

---

## Error Handling

The llama provider handles all common error scenarios:

| Error | Behavior |
|-------|----------|
| Model file not found | Throws error with file path, logged as `spdlog::error` |
| Insufficient memory | llama.cpp returns null context, provider throws descriptive error |
| Prompt exceeds context | Classified as `context_overflow`, triggers compaction in agent loop |
| Tokenization failure | Throws with error code, logged for debugging |
| Decode failure mid-generation | Stops generation gracefully, returns partial output |
| Concurrent requests | Serialized via mutex — one inference at a time per provider instance |

All errors are logged with the `[LlamaProvider]` prefix and classified using the same error taxonomy as cloud providers, so failover and retry logic works identically.

---

## Notes

- The model name after `llama/` (e.g., `llama/local`) is arbitrary — it is only used for display and logging. The actual model is determined by the `base_url` path.
- llama.cpp supports models with built-in chat templates (Llama 3, Mistral, Qwen, etc.). For models without a template, ChatML format is used as fallback.
- Inference is single-threaded per provider instance (one request at a time). For concurrent users, consider running multiple IonClaw instances or using a cloud provider for high-concurrency scenarios.
- Token usage is estimated (not exact) since llama.cpp does not report usage in the same format as cloud APIs.
