# IonClaw

<p align="center">
    <a href="https://github.com/ionclaw-org/ionclaw" target="_blank" rel="noopener noreferrer">
        <img width="280" src="extras/images/logo.png" alt="IonClaw Logo">
    </a>
</p>

<p align="center">
    <a href="https://opensource.org/licenses/MIT"><img src="https://img.shields.io/badge/license-MIT-yellow?style=flat-square" alt="License: MIT"></a>
    <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square" alt="C++17"></a>
    <a href="https://cmake.org/"><img src="https://img.shields.io/badge/CMake-3.20%2B-brightgreen?style=flat-square" alt="CMake 3.20+"></a>
</p>

<p align="center">
    <a href="https://github.com/ionclaw-org/ionclaw/actions/workflows/build-all.yml"><img src="https://img.shields.io/github/actions/workflow/status/ionclaw-org/ionclaw/build-all.yml?style=flat-square" alt="Build status"></a>
</p>

<p align="center">
    A C++ AI agent orchestrator that runs anywhere as a single native binary —<br>
    Linux, macOS, Windows, iOS, and Android — with zero external dependencies.
</p>

<p align="center">
    Multi-agent · Real-time task board · Web control panel · Skills system · Browser automation<br>
    Multi-provider · Scheduler · Subagents · Memory · File management<br>
    <strong>One command to start. Everything from the browser. No coding required.</strong>
</p>

---

## What is IonClaw?

IonClaw is an AI agent orchestrator built from the ground up in C++. It compiles to a single native binary that runs directly on any device — no runtime, no interpreter, no container required.

On a server (Linux, macOS, Windows), it starts with one command and serves a full web panel. On iOS and Android, the Flutter app embeds the same C++ engine and runs everything locally on the device. Same codebase, same capabilities, everywhere.

### Why C++?

Because native means fast startup, low memory, no dependencies, and true portability. The entire platform — web panel, project templates, built-in skills — is compiled into the binary. You deploy one file and it just works.

### What can it do?

- **Multi-agent** — run multiple agents with independent models, tools, and workspaces
- **Real-time task board** — track every agent task live, with full history and status
- **Web control panel** — configure agents, providers, credentials, and skills from the browser
- **Skills system** — extend agent capabilities with simple Markdown files
- **Browser automation** — agents can navigate, click, type, screenshot, and extract data from web pages
- **Multi-provider** — Anthropic, OpenAI, Gemini, Grok, OpenRouter, DeepSeek, Kimi, and any OpenAI-compatible endpoint
- **Scheduler** — cron expressions, intervals, and one-shot tasks with full board tracking
- **Subagents** — agents can spawn child agents for parallel work
- **Memory** — persistent memory with search-based recall across sessions
- **File management** — read, write, search, and organize files within sandboxed workspaces
- **Secure** — sandboxed workspaces, JWT auth, tool policy per agent, hook system for custom rules

## Screenshots

See [screenshots](docs/screenshots.md) for some platforms running IonClaw.

## Quick Start

**Requirements:** CMake 3.20+, C++17 compiler, Node.js 18+ (for web client).

```bash
git clone https://github.com/ionclaw-org/ionclaw.git
cd ionclaw

make setup-web
make build-web
make build
make install
```

Then initialize and start a project:

```bash
ionclaw-server init /path/to/your/project
ionclaw-server start --project /path/to/your/project
```

Open `http://localhost:8080` in your browser. The web panel is served automatically.

## Documentation

- [Architecture](docs/architecture.md) — System design and components
- [Build](docs/build.md) — Build system and Makefile targets
- [Configuration](docs/configuration.md) — Full config.yml reference
- [Flutter](docs/flutter.md) — Flutter app, release builds, and signing
- [Skills](docs/skills.md) — Creating and managing skills
- [Tools](docs/tools.md) — Built-in tools reference
- [Docker](docs/docker.md) — Docker build, run, and compose
- [Deploy](docs/deploy.md) — One-click deploy to cloud platforms

## License

MIT — see [LICENSE](LICENSE.md) for details.

## Links

- [GitHub](https://github.com/ionclaw-org/ionclaw) · [Issues](https://github.com/ionclaw-org/ionclaw/issues) · [Discussions](https://github.com/ionclaw-org/ionclaw/discussions)

Made by [Paulo Coutinho](https://github.com/ionclaw)
