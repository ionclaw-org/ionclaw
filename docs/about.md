# About IonClaw

IonClaw is an **AI agent orchestrator** built from the ground up in C++. If you are new to the project: an orchestrator is the layer that runs one or more AI agents, gives them tools (files, web, search, etc.), and lets you control and monitor everything from a single place. IonClaw compiles to a single native binary — no Python, no Node server, no Docker required. It runs on Linux, macOS, Windows, iOS, and Android with **zero external dependencies**.

On a server, you start it with one command and it serves a full web panel. On a phone (iOS or Android), the app embeds the same C++ engine and runs everything locally on your device. **It is the only AI agent orchestrator that runs on mobile**, so you get a true personal assistant with **privacy and security by design** — everything runs on your machine. Same codebase, same capabilities, everywhere.

Because it is native C++, IonClaw has fast startup, low memory use, and true portability. The whole platform — web panel, project templates, built-in skills — is compiled into the binary. You deploy one file and it just works.

Rather than being “just another” autonomous AI, IonClaw is a **controlled ecosystem**: agents run inside structured workspaces, with isolation, clear rules, and full visibility. You get orchestration, real-time monitoring, multi-agent setup, and native tooling in one product.

---

## Security

IonClaw is designed with security as a priority. It avoids unnecessary port exposure and stays predictable and controllable. Each agent runs in an isolated workspace with sandbox limits: file access and tool permissions are restricted. File operations stay inside the agent’s workspace and the shared public directory, so the rest of the system is protected.

---

## Ease of installation and use

The platform is built to be simple to install and start. You start the server with one explicit command (e.g. `ionclaw start`). No complex infrastructure. Configuration is a single `config.yml` file, with environment variable expansion for secrets, so you can keep credentials out of the file.

---

## Not just for developers

The web panel is fully responsive (desktop, tablet, mobile) so **anyone** can manage, monitor, and configure the system. You edit configuration visually with YAML validation, so advanced settings are safer and you are less likely to break the system. Sensitive values are masked in the UI and preserved when you save.

---

## Multi-agent orchestration

IonClaw can run **multiple agents**. Each agent can have its own workspace, model, and set of tools. When you have several agents, an optional classifier can route each message to the best-suited agent. That gives you a scalable, structured setup for complex projects and automations.

---

## Real-time web dashboard

The built-in real-time dashboard lets you:

- See activity as it happens
- Manage agents and configuration
- Monitor tasks and executions
- Track logs and execution duration
- Inspect errors and status

All of this works from desktop, tablet, and mobile.

---

## Integrated file browser

A file browser in the web panel separates public and private files. All file operations happen inside sandboxed environments, so access is safe and the rest of the system is not exposed.

---

## Skill system and marketplace

**Skills** extend an agent with extra knowledge and workflows (written in simple Markdown). You add and edit skills from the web dashboard. A built-in **marketplace** lets you browse and install community skills from the UI, at project level or per agent, without touching the codebase.

---

## Web chat

The built-in web chat works without any external app. It supports text and media. Messages are delivered in real time over WebSocket — including streaming, tool use, and typing indicators. Sessions are persisted and listed in the sidebar so you can continue conversations later.

---

## Native tools system

IonClaw includes a native C++ tools system. Built-in capabilities include:

- Sandboxed file operations (read, write, edit, list)
- Secure shell execution (exec)
- Full HTTP client (GET, POST, etc., with auth and file download/upload)
- Web search (configurable providers, e.g. Brave, DuckDuckGo)
- Web fetch and RSS reader
- AI image generation (provider-routed: e.g. Gemini native API, OpenAI-compatible)
- Local image operations (create, resize, draw, overlay, watermark)
- Subagent spawning and cron scheduling
- Persistent memory and session handling

---

## Multi-provider LLM support

Multiple LLM providers are supported (Anthropic, OpenAI, Gemini, Grok, OpenRouter, DeepSeek, and others). You choose models with the `provider/model` format. Credentials are stored by name and referenced from providers and tools, so you can switch or add providers without rewriting config.

---

## Summary

IonClaw gives you:

- **Runs anywhere** — macOS, Linux, Windows, iOS, Android
- **Zero dependencies** — one binary with everything embedded
- **Security-first** — sandboxed agents and isolated workspaces
- **One command to start** — everything from the browser, no coding required
- **Real-time dashboard** — full visibility over agents and tasks
- **Multi-agent** — optional classifier to route work
- **Multi-provider** — use the LLMs you already have
- **Native tools** — files, HTTP, search, images, memory, cron, etc.
- **Skill marketplace** — extend agents from the UI
- **Browser-based control** — one panel, works on mobile too

For more detail on setup, configuration, and features, see the rest of the [documentation](configuration.md).
