# Testing Checklist

A comprehensive manual QA checklist for the agent runtime. It is grouped by area; each item names the behavior to verify and the edge cases that must not break. Nothing here should fail, and rapid or malformed input must never crash the server or leave a task stuck.

## 1. Text conversations

- [ ] Send a plain text message and receive a complete streamed reply.
- [ ] Send an empty message with no media — rejected with a clear error, no task created stuck.
- [ ] Send a very long message (hundreds of KB) — accepted, truncated where shown, no crash.
- [ ] Send messages with emoji and multi-byte UTF-8 — never truncated mid-codepoint anywhere (titles, previews, summaries).
- [ ] A turn that needs many tool calls terminates cleanly at the iteration cap with a clear "processing limit" message.
- [ ] The loop detector stops a genuinely looping turn (same tool+args repeated) before the hard cap.

## 2. Images (vision)

- [ ] Attach one image and ask about it — the model uses the vision tool and answers.
- [ ] Attach several images in one message.
- [ ] Attach an oversized image (> the tool cap) — clear error returned to the model, no crash.
- [ ] Attach an unsupported/undecodable format (svg, tiff) — graceful error, turn does not die silently.
- [ ] Vision-heavy turn on a small-context model triggers compaction instead of a provider context-overflow.
- [ ] Screenshot/vision tool result on an **OpenAI-compatible** provider — image is carried in a following user message, request is accepted (no 400 on images in a tool message).
- [ ] Same on Anthropic — image in tool_result accepted.

## 3. Audio (transcription)

- [ ] Send a voice note with a valid transcription model configured — transcription is prepended and the model answers.
- [ ] With **no** transcription model configured — the model is told the audio was ignored (not silently dropped).
- [ ] With an **invalid** transcription key/quota — the failure is surfaced to the model as a media note, not swallowed, and the reply does not claim the audio was understood.
- [ ] Empty/corrupt audio file — clear media note, no crash.
- [ ] `.opus` / `.oga` files carry the correct mime type to the transcription endpoint.
- [ ] Mixed text + image + audio in one message — all three are handled.

## 4. Rapid / concurrent messages (must not break)

- [ ] Send many messages rapidly to one session — memory stays bounded; once the inbound backlog cap is hit, further sends get a clear "server busy" 429 and their task is marked errored (never left pending).
- [ ] Double-click send (identical text twice within 5s) — the duplicate is ignored and its task is resolved, not left hanging.
- [ ] Flood a session with steer/steer_backlog messages — the per-session queue is capped (hard ceiling), excess rejected; no unbounded growth.
- [ ] `steer_backlog`: when the steer copy is injected mid-turn, the followup backup is dropped (message processed once, not twice).
- [ ] Two messages to the same session arrive together — processed in order, one turn at a time, session file never interleaved/torn.
- [ ] Adversarial chat ids that sanitize to the same filename share one write lock (no torn session file).
- [ ] Press Stop mid-turn — the turn ends, the saved assistant message reads only "[Request interrupted by the user]" (no contradictory "processing limit" text), task marked Stopped.
- [ ] Shut the server down while a turn is running — clean drain, no crash, no misleading saved reply.

## 5. Sessions, compaction, persistence

- [ ] Long conversation crosses the context window — compaction fires, keeps the system prompt and the latest user message, and never ships an orphaned tool_use without its tool_result.
- [ ] Kill -9 the server mid-write, restart — session/task/cron/subagent files are intact (atomic temp+rename), no truncated file, history preserved.
- [ ] Disk-budget sweeper evicts old sessions when over budget; the active session being written is not deleted.
- [ ] Restart after a crash — a session that was mid-turn trims its in-flight tail (abort cutoff persisted on graceful shutdown).
- [ ] A compaction failure (summary provider errors) is logged clearly; the turn still makes progress via deterministic pruning.

## 6. Providers (text / tools / reasoning)

- [ ] Anthropic with extended thinking at **medium** and **high** — no 400; max_tokens exceeds the thinking budget.
- [ ] OpenAI reasoning model (o1/o3/o4/gpt-5) — request uses `max_completion_tokens`, omits a custom temperature; no 400.
- [ ] OpenAI-compatible provider returns an in-band `data:{"error":...}` mid-stream — surfaced as an error (and failover fires), not a silent empty "stop".
- [ ] Failover across profiles preserves images/tools across the retry.
- [ ] NVIDIA / OpenRouter / Ollama chat-completions still work for plain text + tools.
- [ ] reasoning/thinking level maps correctly per provider (Anthropic budget, OpenRouter reasoning.effort, OpenAI-compat reasoning_effort; `adaptive`→medium).

## 7. Tools

- [ ] `read_file`/`write_file`/`edit_file`/`list_dir` stay within the workspace; `..` and absolute escapes rejected.
- [ ] Concurrent `write_file`/`edit_file` to the same path — unique temp files, no corruption, no orphan `.tmp`.
- [ ] `http_client` download — atomic write; a write failure is reported, not a false success.
- [ ] `http_client` `upload_file` to a DNS-rebinding host — blocked by the connect-time peer recheck.
- [ ] `web_fetch` on a huge/adversarial HTML page — bounded (no catastrophic regex backtracking), output capped.
- [ ] `rss_reader` on a feed containing a DOCTYPE/entity bomb — rejected, no memory blowup.
- [ ] `image_ops` overlay with an invalid resize — clear error, not a silently wrong output.
- [ ] `browser` screenshots/pdf go to unique temp names; old temp files are pruned (no unbounded garbage). On Windows shutdown, only the launched Chrome is terminated (not every chrome.exe). On POSIX, a dead Chrome is detected (no zombie).
- [ ] `cron` add/update: `tz` only accepted with `cron_expr`; a cron job without `tz` inherits the configured timezone; next-run stored as UTC.

## 8. Scheduling / timezone

- [ ] Create a cron job in a non-UTC IANA zone (e.g. America/Sao_Paulo) — fires at the correct local wall-clock across a DST boundary.
- [ ] The assistant prompt shows the configured timezone's current date/time.
- [ ] `every_seconds` below the tick minimum is rejected.

## 9. Storage layout

- [ ] After running, confirm on disk: `workspace/sessions/`, `workspace/tasks.jsonl`, `workspace/cron_jobs.json`, `workspace/memory/`, `workspace/subagent-runs.json` all present under the project. No project data under a hidden `.ionclaw` directory. (Chrome profile under `~/.ionclaw/` is machine cache, expected.)

## 10. Apps (visual + behavior)

- [ ] Web, Apple (iOS/tvOS/watchOS), Android, Flutter all use the shared brand palette consistently.
- [ ] Copy-to-clipboard "copied" state resets after ~2s in every app that has it.
- [ ] Web: logout closes the WebSocket; tool-running spinner never gets stuck; TaskCard never renders NaN; marketplace target resets after install.
- [ ] Android: WebView is destroyed on dispose; mic permission grant is scoped; invalid server port rejected.
- [ ] Flutter: async UI guarded with `mounted`; FFI calls wrapped; downloads sanitize the filename.

## 11. Multi-platform build

- [ ] `make build` + `make test` on macOS.
- [ ] CI green on macOS (arm64/x86_64), Linux, Windows, iOS (xcframework), Android — including the outbound-HTTPS regression test.
