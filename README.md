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

### Docker

```bash
docker compose up
```

### From source

**Requirements:** CMake 3.20+, C++17 compiler, OpenSSL.

```bash
git clone https://github.com/ionclaw-org/ionclaw.git
cd ionclaw

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/bin/ionclaw-server init /path/to/your/project
./build/bin/ionclaw-server start --project /path/to/your/project
```

Open `http://localhost:8080` in your browser. The web panel is served automatically.

## One-Click Deploy

| Platform | |
|---|---|
| Render | [![Deploy to Render](https://render.com/images/deploy-to-render-button.svg)](https://render.com/deploy?repo=https://github.com/ionclaw-org/ionclaw) |
| Heroku | [![Deploy to Heroku](https://www.herokucdn.com/deploy/button.svg)](https://www.heroku.com/deploy?template=https://github.com/ionclaw-org/ionclaw) |
| DigitalOcean | [![Deploy to DigitalOcean](https://www.deploytodo.com/do-btn-blue.svg)](https://cloud.digitalocean.com/apps/new?repo=https://github.com/ionclaw-org/ionclaw/tree/main) |
| Hostinger | [![Deploy on Hostinger](https://assets.hostinger.com/vps/deploy.svg)](https://www.hostinger.com/docker-hosting?compose_url=https://raw.githubusercontent.com/ionclaw-org/ionclaw/main/docker-compose.yml) |

See the [deploy docs](docs/deploy.md) for details on each platform.

## Flutter App

IonClaw includes a native Flutter app for iOS, Android, macOS, Linux, and Windows. The app embeds the full C++ engine and runs everything locally on the device.

```bash
# Build the shared library
cmake -B build-shared -DIONCLAW_BUILD_SHARED=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-shared -j$(nproc)

# Run the app
cd flutter/runner
flutter run
```

## Documentation

- [Architecture](docs/architecture.md) — System design and components
- [Configuration](docs/configuration.md) — Full config.yml reference
- [Skills](docs/skills.md) — Creating and managing skills
- [Tools](docs/tools.md) — Built-in tools reference
- [Deploy](docs/deploy.md) — One-click deploy

## License

MIT — see [LICENSE](LICENSE.md) for details.

## Links

- [GitHub](https://github.com/ionclaw-org/ionclaw) · [Issues](https://github.com/ionclaw-org/ionclaw/issues) · [Discussions](https://github.com/ionclaw-org/ionclaw/discussions)

Made by [Paulo Coutinho](https://github.com/ionclaw)
