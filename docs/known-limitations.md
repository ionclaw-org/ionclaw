# Known Limitations

Items intentionally left as known limitations after the functional audit, with rationale and workarounds. Each is bounded (no crash or unbounded resource use) and safe.

## Providers

- **Anthropic native extended thinking + tools (multi-turn).** Anthropic requires the prior assistant turn's `thinking` block (with its `signature`) to be replayed when a thinking-enabled turn made a tool call. The runtime does not yet persist/replay the thinking signature, so a thinking-enabled Anthropic agent that calls tools can get a 400 on the following turn. **Workaround:** use extended thinking through an OpenAI-compatible/OpenRouter route, or disable `thinking` for Anthropic agents that use tools. (Single-shot thinking with no tools works.)
- **reasoning_effort on non-supporting OpenAI-compatible backends.** When a `thinking` level is set, the flat `reasoning_effort` field is sent to OpenAI-compatible providers; a backend that does not support it (some Ollama/DeepSeek/gateway setups) may return 400. It is opt-in via `model_params.thinking`.
- **Truncated stream detection.** A stream that drops without a `[DONE]` sentinel is treated as a clean `stop`; a partial response can be accepted as final. In-band error events are now surfaced; silent socket drops are not distinguished from completion.
- **CLI / local text-only providers.** `claude-cli` and `llama` are text-only: tools, tool-call history, and images are dropped (documented model constraint).

## Concurrency / queueing

The runtime executes one turn at a time on a single worker thread, which guarantees per-session ordering and prevents double-execution on the normal path. The following are bounded edge cases in the mid-turn *steer bypass*:

- **Per-session send rate limit.** There is no per-session message rate limit; the global inbound backlog cap and the per-session queue ceiling provide the backpressure that keeps a flood from breaking the server.
- **`queue_mode=interrupt`** only takes effect between turns (the bus is drained between turns). For an immediate stop, use the `/stop` endpoint, which aborts the running turn directly.
- **Collect debounce / `max_concurrent`.** Because turns are serialized, the collect-debounce batching and the per-agent `max_concurrent` gate rarely engage on the bus path; rapid messages are processed as sequential turns.
- **Steer-bypass timing.** A steer message enqueued at the exact instant a turn ends may be delayed until a later turn; `steer_backlog`'s followup backup guarantees the message is still processed.

## Persistence

- **Task log growth.** `tasks.jsonl` is an append log compacted only at startup/shutdown; a very long-running server accumulates lines until the next compaction.

## Browser tool

- **Process-global browser.** Chrome and the "current tab" are process-global singletons shared across sessions; concurrent browser use from two sessions is not isolated. Intended for single-user/interactive use.

## Style / cosmetic

- A few `path + "/"` concatenations remain (functional on Windows, which accepts `/`), and some file-scope `static` helpers in `BrowserTool.cpp` are not yet folded into a helper class.
