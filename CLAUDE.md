# IonClaw

C++20 AI-agent orchestrator. A Poco HTTP server exposes a REST + WebSocket API and an MCP endpoint, drives an agent loop over pluggable LLM providers, and bridges the same agents onto messaging channels. The engine also builds as a shared library for the iOS/tvOS/watchOS xcframework and the Android aar.

Dependencies come through CPM: Poco (HTTP/TLS/XML/Zip), nlohmann/json, yaml-cpp, spdlog, OpenSSL, jwt-cpp, hnswlib, and llama.cpp (isolated). User language for chat is Brazilian Portuguese; code and comments are English.

## Build and test

- `make build` — release server (`build/bin/ionclaw-server`).
- `make test` — configure with `-DIONCLAW_BUILD_TESTS=ON` and run the doctest suite (`build/bin/ionclaw-tests`).
- `make build-web` — Vue client into `main/resources/web` (gitignored, embedded via CMake). Rebuild it before a cmake build when the web changed, then re-run cmake so the embed picks up the new asset hashes.
- `make build-lib` / `make build-xcframework` / `make build-android` — shared-library targets.

CI builds all six platforms on every push: macOS arm64, macOS x86_64, Linux x86_64, Windows x86_64, iOS xcframework, Android aar. A change is only done when all six are green.

## Code standard (strict)

Code and comments are in English. The code must read as if an experienced product C++ engineer wrote it, never like generated output. Do a change only when it makes sense and is genuinely needed, never to show work.

### Philosophy

- No fallbacks, no backward-compatibility shims, no legacy code, no gambiarras, no dirty code, no dead code.
- Never keep a branch that exists only because the code used to work differently. Write the new, final version and refactor, remove, or rewrite whatever that requires, without worrying about compatibility.
- No generic fallback and no implicit unexpected behavior. Do not add an `else` for an unknown case that would produce surprising behavior. Handle the known cases explicitly and surface anything else as a clear error.
- Every error or failure is reported clearly. A failure the AI or the operator must see is logged and returned, never swallowed.

### Naming and structure

- All functions are class methods. Never a free function in a namespace, and never a function in an anonymous namespace. Generic or shared logic goes on a helper class as a `private static` or public `static` method (`StringHelper::foo`, `ToolHelper::bar`).
- No member `_` suffix. Names carry the meaning, so correct naming replaces most comments.
- Keep headers, implementation, namespaces, types, names, and responsibilities consistent with each other.
- When a method takes on too much responsibility, extract small objective methods. Do not extract just to shrink a function, and avoid artificial abstractions that obscure the main flow.

### Comments

- Comments are rare and used only where naming cannot carry the meaning. If it is obvious, decorative, or restates the code, delete it.
- A comment explains intent or context, not the literal code.
- Never add artificial section separators like `helpers`, `validators`, or `public methods`. Never write header comments that describe a method, a member, or a section.
- A `//` or `#` one-line comment is lowercase and one objective sentence, with no semicolon splitting one sentence into clauses.
- If a comment needs more than one line, finish each sentence with punctuation before the next line. Never continue one sentence onto the next line, and keep comments objective and natural rather than verbose or narrative.
- In a complex method, a short one-line comment can mark the intent of each meaningful block.

### Logging

- Every log message carries a `[Component]` prefix in PascalCase, e.g. `[BrowserTool]`, `[VectorStore]`, matching the class or subsystem that emits it.
- The message after the prefix starts with a capital letter and reads as a sentence: `spdlog::warn("[AgentLoop] No provider found for '{}'", name)`. This is the opposite of the lowercase rule for `//` comments, which never carry a prefix.
- Report every failure the operator or the AI must see; never swallow it. Keep the message objective and free of semicolon-split clauses, same as comments.

### Formatting and flow

- Preserve the project's existing visual, structural, and architectural pattern. Keep it compact, professional, and consistent.
- Use only the vertical spacing needed to separate reading contexts, and always separate blocks of different responsibility with one blank line.
- Never clump multiple `if`s, validations, loops, state mutations, and returns together. A method should have a visually identifiable beginning, middle, and end and be understandable at a glance.
- Prefer early returns and never write an `else` after a `return`. Avoid unnecessary nesting.
- Keep includes clean, direct, and organized.
- Lambdas do not format well under clang-format, so wrap each lambda in `// clang-format off` / `// clang-format on` with the code already correctly formatted between the tags.

### Safety

- Prefer `const`, references, and smart pointers where they add clarity, safety, or correct ownership.
- Avoid macros, unsafe casts, and raw pointers when a safer alternative consistent with the project exists.
- Read untrusted JSON defensively. `nlohmann::json::value(key, default)` throws on a present-but-null or wrong-typed field, so guard with `is_string()` / `is_number_integer()` before `get<T>()`.
- Timestamps are UTC. Engine state lives at the project root, `workspace/` holds only agent-generated files, and YAML config keys and json state files use dashes, not underscores.

## Architecture

Entry: `server/ServerInstance` wires the components and owns them as statics. Request flow: `HttpServer` → `RequestHandlerFactory` picks a handler (`ApiHandler` for `/api/*`, `McpHandler` for MCP, `WebhookHandler` for `/webhook/*`, `WebSocketHandler`, `PublicFileHandler` for `/public/*`, `WebAppHandler` for the SPA). `ApiHandler` dispatches into `Routes` (split across `server/routes/*`). `Auth` gates everything except public and webhook paths.

- **Providers** (`provider/`) — `ProviderFactory` resolves a `provider/model` string. `AnthropicProvider` is the native Anthropic client (extended thinking with signature replay before tool_use, redacted-thinking preserved, prompt-caching breakpoint gated by capability). `OpenAiProvider` serves every openai-compatible provider, including the thin-config providers whose base URLs the factory knows. `LlamaProvider` runs a local gguf via llama.cpp. `ClaudeCliProvider` drives the local claude binary. `FailoverProvider` wraps auth profiles with backoff. `ModelCapabilities` loads the embedded litellm capability table (~2900 models) and drops unsupported params (reasoning, vision tool, tools payload, cache_control) before a request goes out.
- **Agent loop** (`agent/`) — `Orchestrator` serializes turns on one worker thread; `AgentLoop` runs the tool loop, streaming, compaction, and context management (`ContextBuilder`, `ContextWindow`, `Compaction`, `ToolLoopDetector`).
- **Tools** (`tool/builtin/`) — registered in `ToolRegistry`, which validates params before dispatch and catches exceptions. Tools receive untrusted model-generated JSON args.
- **Channels** (`channel/`) — the logical channels are `web`, `telegram`, `whatsapp`, and `mcp`. Inbound arrives on `MessageBus`; the agent reply is delivered per channel. `web` is answered over the WebSocket (`EventDispatcher` broadcasts), so it takes no outbound queue. `telegram` and `whatsapp` have runner threads that drain a per-channel outbound queue. WhatsApp has two providers behind the one `whatsapp` channel: `whatsapp_zapi` and `whatsapp_meta`, with Meta taking precedence. A reply to a non-web session reaches both the web ui (broadcast) and the origin channel (runner). `DeliveryParser` turns inline markers (`[[image:path]]`, `[[audio:path]]`, `[[video:path]]`, `[[document:path]]`, `[[media:path]]`, `[[break]]`) into real attachments and message splits.
- **Embeddings and semantic memory** (`embedding/`, `agent/SemanticMemory`) — `EmbeddingProvider` has an openai-compatible http implementation and a local llama.cpp implementation (in the isolated `ionclaw-llama` target, gated by `IONCLAW_HAS_LLAMA_CPP`). `VectorStore` keeps vectors in an hnswlib index over normalized inner product (cosine), persisted with a metadata sidecar. `SemanticMemory` chunks memory files with `TextChunker` and re-embeds only files whose content hash changed. `memory_search` uses semantic search when an embeddings model is configured and its provider is available, otherwise keyword search.
- **Config** (`config/`) — `ConfigStore` owns a `std::shared_ptr<const Config>` behind a mutex: `snapshot()` hands out the current immutable config for lock-free reads, `replace()` swaps a new one, `update()` copies-mutates-commits for the editing endpoints. Every component holds the store and snapshots at the top of an operation, so `/api/config/restart` cannot race a reader. YAML config files use dashes, not underscores.
- **State layout** — engine state lives at the project root (`sessions/`, `tasks.json`, `cron-jobs.json`, `subagent-runs/`, `memory/`), not under `workspace/`. `workspace/` holds only agent-generated files. `workspace/public` is served at `{server.public_url}/public`.

Deeper reference lives in `docs/` (architecture, flow, configuration, custom-providers, whatsapp, mcp, llama, tools, known-limitations).

## Concurrency model

Threads: the Poco HTTP thread pool (concurrent request handlers), one orchestrator worker thread that runs every turn to completion, the telegram poll/outbound/typing threads, the whatsapp outbound thread, the session sweeper, the heartbeat, the cron scheduler, and detached compaction threads. Shared state reached from more than one of these must be immutable after construction, atomic, or guarded by a mutex. Config is read only through `ConfigStore` snapshots. `MessageBus` and `SessionQueue` are internally synchronized.

## Gotchas

- nlohmann `json::value(key, default)` only substitutes the default when the key is absent — on an explicit `null` (or a wrong type) it calls `get<T>()` and throws. Provider responses, stream chunks, tool args, webhook payloads, and request bodies routinely carry `null` fields, so read them with an `is_string()` / `is_number_integer()` guard, never bare `value()`. The same applies to `nlohmann obj.value("k", object()).items()`, whose temporary dangles inside a range-for — iterate a named member instead.
- The web asset filenames are content-hashed; a stale cmake configure embeds the old names and the build fails. Rebuild the web, then re-run cmake.
