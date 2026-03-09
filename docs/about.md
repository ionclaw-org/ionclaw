# About IonClaw

IonClaw is an AI agent orchestrator built from the ground up in C++. It compiles to a single native binary that runs directly on any device — no runtime, no interpreter, no container required.

On a server (Linux, macOS, Windows), it starts with one command and serves a full web panel. On a phone or laptop (iOS, Android), the Flutter app embeds the same C++ engine and runs everything locally on the device. Same codebase, same capabilities, everywhere.

Because it is native C++, IonClaw has fast startup, low memory footprint, zero external dependencies, and true portability. The entire platform — web panel, project templates, built-in skills — is compiled into the binary. You deploy one file and it just works.

Rather than being just another autonomous AI agent, IonClaw provides a controlled ecosystem where agents operate inside structured environments, with isolation, governance, and clear operational transparency. It combines orchestration, real-time monitoring, multi-agent architecture, integrated communication, and native tooling into a unified platform.

## Security

IonClaw is designed with security as a priority. It avoids unnecessary port exposure and is architected to be controlled and predictable. Each agent runs inside an isolated workspace with sandbox restrictions, limiting file access and tool permissions. File operations are always confined to the agent's workspace and the shared public directory.

## Ease of installation and use

The platform is built to be simple to install and start. The server is started with an explicit command (`ionclaw start`). No complex infrastructure is required. Configuration is driven by a single `config.yml` file with environment variable expansion for secrets.

## Not just for developers

The fully responsive web panel (desktop, tablet, and mobile) allows anyone to manage, monitor, and configure the system. Visual configuration editing with YAML validation makes advanced configuration safer without breaking the system. Sensitive values are masked in the UI and preserved when saving.

## Multi-agent orchestration

IonClaw can manage multiple agents. Each agent can have its own workspace, model, and tool set. When multiple agents are configured, an optional classifier routes each message to the best-suited agent. This creates a scalable, structured architecture for complex projects and automations.

## Real-time web dashboard

The integrated real-time dashboard lets you:

- See activity in real time
- Manage agents and configuration
- Monitor tasks and executions
- Track logs and execution duration
- Inspect errors and status

All of this is accessible from desktop, tablet, and mobile.

## Integrated file browser

A native file browser in the web panel separates public and private files. File operations occur inside sandboxed environments, ensuring safe access and preventing unintended system exposure.

## Skill system and marketplace

Skills extend the agent with specialized knowledge and workflows. You can add and edit skills through the web dashboard. A built-in marketplace lets you browse and install community skills from the web UI, at project level or per agent.

## Web chat

A built-in web chat works without external applications. It supports text and media. Messages are delivered in real time via WebSocket (streaming, tool use, typing indicators). Sessions are persisted and listed in the sidebar.

## Native tools system

IonClaw includes a native C++ tools system. Built-in capabilities include:

- Sandboxed file operations (read, write, edit, list)
- Secure shell execution (exec)
- Full HTTP client (GET, POST, etc., with auth and file download/upload)
- Web search (configurable providers: Brave, DuckDuckGo)
- Web fetch and RSS reader
- AI image generation (provider-routed: Gemini native API, OpenAI-compatible)
- Local image operations (create, resize, draw, overlay, watermark)
- Subagent spawning and cron scheduling
- Persistent memory and session handling

## Multi-provider LLM support

Multiple LLM providers are supported (Anthropic, OpenAI, Gemini, Grok, OpenRouter, DeepSeek, and others). Model configuration uses the `provider/model` format. Credentials are stored by name and referenced from providers and tools.

## Summary

IonClaw provides:

- Runs anywhere — macOS, Linux, Windows, iOS, Android
- Zero dependencies — single binary with everything embedded
- Security-first architecture and sandboxed agent isolation
- Single-command start, everything from the browser, no coding required
- Complete real-time visual dashboard
- Multi-agent support with optional classifier
- Multi-provider LLM flexibility
- Native tool ecosystem (files, HTTP, search, images, memory, cron)
- Skill marketplace and in-app skill editing
- Full browser-based control and mobile-friendly UI
