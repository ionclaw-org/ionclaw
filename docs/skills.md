# Skills

Skills define what the agent knows and how it behaves in specific contexts. They are the primary mechanism for extending the agent with specialized knowledge, workflows, and domain expertise.

## Overview

A skill is a Markdown file with YAML frontmatter that provides instructions to the AI agent. Skills transform the platform from a general-purpose assistant into a specialized agent equipped with procedural knowledge for specific domains or tasks.

Skills are loaded by the skills loader and injected into the system prompt. The agent sees a summary of all available skills (name and description) in every request; when it decides to activate a skill, the full body is loaded into context.

## Skill locations

Skills live in three tiers, loaded in priority order:

1. **Built-in skills** — Shipped with the platform inside `resources/skills/`. Always available. Lowest priority.
2. **Project skills** — `<project_path>/skills/` in the project root (where `config.yml` lives). Shared across all agents. Overrides built-in.
3. **Agent workspace skills** — `<workspace>/skills/` in each agent's workspace directory. Scoped to that agent only. Highest priority.

Project and workspace skill directories support both flat (`skills/<name>/SKILL.md`) and nested (`skills/<source>/<name>/SKILL.md`) layouts (e.g. marketplace-installed skills with a publisher prefix).

**Precedence:** A higher-tier skill with the same name replaces the lower-tier entry entirely: workspace overrides project, project overrides built-in.

**Agent visibility:** Each agent sees built-in + project + its own workspace skills. Project skills are shared by all agents. Workspace skills are private to the agent whose workspace they live in.

## Directory structure

Each skill is a **directory** containing a required `SKILL.md` file and optional resource subdirectories:

```
skill-name/
├── SKILL.md              (required)
├── scripts/              (optional — executable code)
├── references/           (optional — documentation for context)
└── assets/               (optional — files used in output)
```

The directory name is the skill identifier. Use lowercase letters, digits, and hyphens. For nested skills (e.g. from the marketplace), the identifier includes the publisher prefix: `anthropic/pdf`, `community/web-scraper`. Two skills with the same name but different publishers are distinct.

Example layout:

```
project_root/
├── config.yml
├── skills/                        # project skills (shared by all agents)
│   ├── my-skill/
│   │   └── SKILL.md
│   └── anthropic/
│       └── pdf/
│           └── SKILL.md
└── workspace/
    └── skills/                    # workspace skills (private to this agent)
        └── custom-tool/
            └── SKILL.md
```

## SKILL.md format

Every `SKILL.md` has two parts: YAML frontmatter and a Markdown body.

### Frontmatter fields

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `name` | Yes | string | Skill identifier, used in the skills summary |
| `description` | Yes | string | What the skill does and when to use it. Primary trigger — the agent reads this to decide whether to activate the skill |
| `always` | No | boolean | If `true`, the skill body is always included in the system prompt (default: `false`) |
| `platform` | No | string or list | Restrict the skill to specific operating systems. Values: `linux`, `macos`, `windows`, `ios`, `android`. Accepts a single string or a YAML list. If omitted, the skill is available on all platforms |

### Platform filtering

The `platform` field restricts a skill to specific operating systems. Skills whose `platform` value does not include the current runtime OS are excluded entirely (not loaded, not listed).

Accepts a **single string** or a **YAML list** of OS names (case-insensitive):

- `ios` — iOS
- `android` — Android
- `macos` — macOS
- `linux` — Linux
- `windows` — Windows

If `platform` is omitted, the skill is available on all platforms.

```yaml
# single platform
---
name: platform-ios
description: Platform-specific functions available on iOS
always: true
platform: ios
---

# multiple platforms
---
name: browser
description: Automate browser interactions
platform: [linux, macos, windows]
---
```

### Writing effective descriptions

The `description` field is the main trigger for activation. The agent reads all skill descriptions to decide which skills to activate.

- Include **what** the skill does and **when** to use it.
- Be slightly broad to avoid under-triggering — include related phrases and contexts the user might use.
- Put "when to use" in the description; the body is only loaded after triggering.

**Weak:** `description: Process DOCX files`

**Strong:** `description: Create, edit, and analyze documents (.docx). Use when working with professional documents including creating new documents, modifying content, formatting, or text extraction.`

### Body

The Markdown body contains the actual instructions. This content is loaded into context only when the skill is activated (or always, if `always: true`).

**Guidelines:**

- Use imperative form ("Extract the data", not "The data should be extracted").
- Keep under ~500 lines to avoid context bloat.
- Explain the reasoning behind important choices.
- Prefer concise examples over long explanations.
- Start with the most common use case, then variations.

## How skills are loaded

1. **Discovery** — All three tiers are scanned. Flat skills use the directory name as key (`my-skill`); nested skills use `publisher/name` (`anthropic/pdf`).
2. **Listing** — The API and system prompt receive metadata for each skill: name, description, available, always, source (builtin/project/workspace), publisher. Skills marked `always: true` have their full body injected into every system prompt.
3. **On-demand** — When the agent activates a skill, the full body is loaded (frontmatter stripped) and added to context.

### Skills summary in the system prompt

The agent receives an XML or structured summary of available skills in every request, so it knows what skills exist and can request the full content when needed.

## Bundled resources

### Scripts (`scripts/`)

Executable code for tasks that need deterministic behavior or are repeated often. Include when the same code would be rewritten across invocations. Scripts can be run by the agent (e.g. via `exec`) or read for patching.

### References (`references/`)

Documentation loaded as needed. Keeps SKILL.md lean — put detailed schemas, API docs, and domain knowledge here. For large files, add a table of contents at the top. Reference them from SKILL.md with guidance on when to read each file.

### Assets (`assets/`)

Files used in output, not loaded into context — templates, images, boilerplate. The agent uses these paths in its output without reading them into context.

### What not to include

Do not add README.md, INSTALLATION_GUIDE.md, changelogs, or user-facing docs inside a skill directory. Only include what the agent needs.

## Progressive disclosure

1. **Metadata** (name + description) — always in context.
2. **SKILL.md body** — loaded when the skill triggers.
3. **Bundled resources** — loaded as needed by the agent.

When SKILL.md approaches ~500 lines, split content into reference files and point to them from SKILL.md.

## Built-in skills

The platform ships with built-in skills that document tools and workflows:

| Skill | Description |
|-------|-------------|
| **browser** | Automate browser interactions (CDP): navigate, snapshot, screenshot, click, type, etc. |
| **cron** | Schedule reminders and recurring tasks (add, list, remove). Interval, cron expression, one-time `at`. |
| **http-client** | Make HTTP requests (GET, POST, etc.) with auth, download, upload. |
| **image-generation** | Generate images from text using AI (provider-routed: Gemini native, OpenAI-compatible). |
| **local-image-generation** | Local image operations (create, resize, draw, overlay, watermark) via the `image_ops` tool. |
| **memory** | Two-layer memory: MEMORY.md and HISTORY.md, with search-based recall. |
| **rss-reader** | Read RSS/Atom feeds. |
| **skill-creator** | Guidance for creating and updating skills from within a conversation. |
| **weather** | Current weather and forecasts (e.g. wttr.in, Open-Meteo). |
| **platform-android**, **platform-ios**, **platform-desktop** | Platform-specific guidance (loaded only on matching OS: `android`, `ios`, `linux`, `macos`, `windows`). |

## Context files

In addition to skills, optional context files in the project root can shape agent behavior. If present, they are included in the system prompt:

| File | Purpose |
|------|---------|
| `SOUL.md` | Bot personality, tone, behavioral guidelines |
| `USER.md` | User preferences and personalization |
| `AGENTS.md` | Agent and subagent configuration and capabilities |
| `TOOLS.md` | Available tools documentation |

These files are optional.

## Creating a new skill

1. **Capture intent** — What should the skill enable? When should it trigger? What output format?
2. **Plan contents** — What scripts, references, or assets would help? Extract repeated logic into `scripts/`.
3. **Create the directory** — Under `skills/` (project) or `<workspace>/skills/` (agent-only). Use lowercase and hyphens.
4. **Write SKILL.md** — Frontmatter with `name` and `description`, then the body with instructions and examples.
5. **Test and iterate** — Use the skill in conversations, observe failures, refine description and body.

## Managing skills via API

- `GET /api/skills` — List all skills. Optional `?agent=` for scope (none = all, empty = built-in + project, name = + that agent's workspace).
- `GET /api/skills/{name}` — Get raw content. Use full name for nested skills (e.g. `anthropic/pdf`).
- `PUT /api/skills/{name}` — Update SKILL.md content. Built-in skills cannot be edited.
- `DELETE /api/skills/{name}` — Delete the skill directory. Built-in skills cannot be deleted.

## Design guidelines

- **Be concise.** Context is shared with history and other skills. Prefer concise examples over long text.
- **Explain the why.** Reasoning helps the agent more than rigid rules.
- **Write clear descriptions.** The description is the main trigger; include what and when. Slightly broad is better than too narrow.
- **Use progressive disclosure.** Keep SKILL.md lean; split to references when it grows.
- **Match specificity to fragility.** Use detailed steps for fragile operations, high-level guidance for flexible tasks.
- **Avoid duplication.** Keep information in one place — SKILL.md or a reference file, not both.
- **No extraneous files.** Only what the agent needs — no READMEs or changelogs inside the skill.
