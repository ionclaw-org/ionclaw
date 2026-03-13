---
name: skill-creator
description: Create or update IonClaw skills. Use when users want to create a new skill, modify an existing one, or need guidance on skill structure, frontmatter, bundled resources, or best practices. Also use when the user says "turn this into a skill" or wants to capture a workflow as a reusable skill.
platform: [linux, macos, windows]
---

# Skill Creator

Guide for creating effective IonClaw skills.

## What Skills Are

Skills are **instructions** that teach the agent how to perform specific tasks using its existing tools. A skill is not a standalone program — it is a document that guides the agent's reasoning and actions.

Think of a skill as a recipe: it tells the agent what tools to use, in what order, with what parameters. The agent already has powerful tools (`http_client`, `generate_image`, `web_fetch`, `exec`, `read_file`, `write_file`, etc.). Skills teach it how to combine them for specific workflows.

**What skills provide:**

1. Specialized workflows — multi-step procedures for specific domains
2. Tool integrations — instructions for working with specific APIs, file formats, or services
3. Domain expertise — company-specific knowledge, schemas, business logic
4. Bundled resources — scripts, references, and assets for complex and repetitive tasks (only when existing tools are not enough)

## Tools-First Principle

Before creating any skill, evaluate what the agent can already do with its existing tools. Most skills should be **instructions only** — a SKILL.md file that tells the agent which tools to call and how.

**When to create only SKILL.md (most cases):**
- the workflow can be done entirely with existing tools (http_client, generate_image, web_fetch, exec, etc.)
- the user provides API documentation, credentials, or workflow steps
- the task is a sequence of tool calls with decision logic the agent can reason about

**When to bundle scripts (rare):**
- the same complex code would be rewritten identically every invocation
- the task requires deterministic binary processing (image manipulation, PDF parsing, data transformation)
- existing tools cannot accomplish the task at all

If the user provides everything needed (API docs, tokens, endpoints), the skill should contain clear instructions for using existing tools — not reimplementations in Python/Bash.

### Good Example: Social Media Posting Skill

The user wants to post to social media via a scheduling API. The agent already has `http_client` and `generate_image`. The skill should be:

```markdown
---
name: social-post
description: Post to social media channels via SocialPost API.
---

# Social Post

Post content to social media using the SocialPost API via `http_client`.

## Required Information

- **text**: the post content (required)
- **channel**: SocialPost channel ID (required)
- **token**: SocialPost API access token (required)
- **image**: set to true to generate an image for the post (optional)
- **prompt**: custom image generation prompt (optional, used when image is true)
- **reference**: reference image path for generation (optional, default: public/logo.png)

If any required info is missing, ask the user before proceeding.

## Posting

Use the SocialPost API to create a post:

\```
http_client(method="POST", url="https://api.socialpost.example/v1/posts",
  content_type="form",
  body="text=URL_ENCODED_TEXT&profile_ids[]=CHANNEL_ID&access_token=TOKEN")
\```

If image generation was requested, first generate the image with `generate_image`,
then attach the media URL to the post.

## Output

Report back: post ID, text sent, channel used, image URL (if generated), and any
errors from the API.
```

No Python files. No test scripts. No README. Just instructions.

### Bad Example

Creating `main.py`, `test.py`, `example.md`, `README.md`, `INSTALL.md` — this is a standalone project, not a skill. The agent already has the tools to do everything; it just needs to know the API endpoints and parameters.

## Skill Locations

- **Builtin skills**: shipped with the project, read-only
- **Project skills**: `<project>/skills/<skill-name>/SKILL.md` — shared across all agents
- **Workspace skills**: `<workspace>/skills/<skill-name>/SKILL.md` — agent-specific
- **Marketplace skills**: `<project>/skills/<source>/<skill-name>/SKILL.md` — installed from marketplace

When creating a new skill, place it under `<workspace>/skills/` for agent-specific skills or `<project>/skills/` for skills shared across all agents.

## Public Files

When generating files that should be publicly accessible (media, images, documents), store them under `<project>/public/`. This directory is at the project root, not inside the agent workspace. Use subdirectories like `public/media/` or `public/documents/` as needed.

## Anatomy of a Skill

```
skill-name/
├── SKILL.md           (required)
│   ├── YAML frontmatter (required)
│   └── Markdown body    (required)
└── Bundled Resources    (optional)
    ├── scripts/         - executable code (Python/Bash/etc.)
    ├── references/      - documentation loaded into context as needed
    └── assets/          - files used in output (templates, icons, fonts)
```

Most skills are a single SKILL.md file. Only create resource directories when truly needed.

### SKILL.md Frontmatter

The frontmatter is the primary mechanism for skill discovery and triggering.

```yaml
---
name: skill-name                # required - identifier
description: What it does...    # required - triggers skill selection
platform: [linux, macos]        # optional - restrict to specific OS(es)
always: true                    # optional - always loaded into context (default: false)
requires:                       # optional - prerequisites
  bins:                         # optional - required binaries on PATH
    - some-binary
  env:                          # optional - required environment variables
    - SOME_API_KEY
---
```

**Fields:**

- `name` — skill identifier, shown in the skills list
- `description` — the primary triggering mechanism. The agent reads this to decide when to use the skill. Include both what the skill does AND when to use it. All "when to use" info must go here, not in the body (the body is only loaded after triggering). Make descriptions slightly "broad" in scope to avoid under-triggering — include related phrases and contexts the user might use even if they don't explicitly name the skill. Example: instead of "Process DOCX files", write "Create, edit, and analyze documents (.docx). Use when working with professional documents: creating new documents, modifying content, tracked changes, comments, formatting, or text extraction"
- `platform` — restrict to specific operating systems. Accepts a single string or a list. Values: `linux`, `macos`, `windows`, `ios`, `android`. If omitted, the skill is available on all platforms. Examples: `platform: ios`, `platform: [linux, macos, windows]`
- `always` — when `true`, the skill body is always loaded into the agent's context regardless of triggering. Use sparingly — only for skills that must always be active (e.g., memory, cron)
- `requires.bins` — list of binary names that must exist on PATH (checked at startup). If any is missing, the skill is marked unavailable
- `requires.env` — list of environment variable names that must be set. If any is empty/unset, the skill is marked unavailable

### SKILL.md Body

Markdown instructions loaded only after the skill triggers. Use imperative form ("Extract the data", not "You should extract the data"). Keep under 500 lines.

The body should tell the agent:
1. what information it needs from the user (and what to ask if missing)
2. which existing tools to use and how (with concrete examples)
3. what to report back when done

### Bundled Resources

#### Scripts (`scripts/`)

Executable code for tasks that require deterministic reliability or are repeatedly rewritten.

- scripts are token-efficient, deterministic, and can be executed without loading into context
- include when the same code would be rewritten repeatedly across invocations
- **signal to bundle a script**: if you find yourself writing the same helper code every time the skill runs, extract it into a script
- only bundle scripts when existing tools (http_client, exec, etc.) cannot accomplish the task — if `http_client` can call the API, don't write a Python script that calls the API

#### References (`references/`)

Documentation loaded as needed to inform the agent's reasoning.

- keeps SKILL.md lean — move detailed schemas, API docs, and domain knowledge here
- for large files (>300 lines), include a table of contents at the top
- reference clearly from SKILL.md with guidance on when to read each file
- avoid duplication: information should live in either SKILL.md or references, not both

#### Assets (`assets/`)

Files used in output, not intended to be loaded into context.

- templates, images, icons, boilerplate code, fonts, sample documents
- the agent uses these files directly in its output without reading them into context

### What NOT to Include

Only essential files. Do NOT create:

- README.md, INSTALLATION_GUIDE.md, QUICK_REFERENCE.md, CHANGELOG.md
- test files, example files, setup guides
- scripts that duplicate what existing tools already do
- standalone programs or project scaffolding
- user-facing documentation or testing procedures

## Core Principles

### Use Existing Tools

The agent has: `http_client`, `generate_image`, `web_fetch`, `web_search`, `exec`, `read_file`, `write_file`, `edit_file`, `list_dir`, `vision`, `rss_reader`, `browser`, `mcp_client`, `memory_save`, `memory_read`, `memory_search`, `spawn`, `cron`, `message`.

Before bundling a script, check if any combination of these tools solves the problem. A skill that says "use http_client with these headers and this endpoint" is better than a skill that bundles a Python HTTP client.

### Concise is Key

The context window is shared. Skills compete for space with the system prompt, conversation history, other skills' metadata, and the user's request.

**The agent is already very smart.** Only add context it doesn't already have. Challenge each piece: "Does the agent really need this?" and "Does this paragraph justify its token cost?"

Prefer concise examples over verbose explanations.

### Explain the Why

Explain **why** things are important instead of heavy-handed constraints. The agent has good theory of mind — when given clear reasoning, it can go beyond rote instructions. If you find yourself writing ALWAYS or NEVER in all caps, reframe and explain the reasoning instead. That's more effective than rigid rules.

### Set Appropriate Degrees of Freedom

Match specificity to the task's fragility:

- **High freedom** (text instructions) — multiple approaches are valid, decisions depend on context
- **Medium freedom** (pseudocode/scripts with parameters) — a preferred pattern exists, some variation is acceptable
- **Low freedom** (specific scripts, few parameters) — operations are fragile, consistency is critical

Think of the agent exploring a path: a narrow bridge with cliffs needs guardrails, an open field allows many routes.

### Ask Before Assuming

If the user's request is missing required information (API tokens, channel IDs, specific parameters), the skill should instruct the agent to ask — not guess or generate placeholder values.

### Research Before Building

If you don't know how an API, service, or library works, research it first. Use `web_fetch` to read official documentation, `web_search` to find current API references, and check for the latest versions of endpoints, parameters, and authentication methods. Never guess API formats or build skills based on outdated knowledge — always verify against the official and current documentation. APIs change frequently; using deprecated endpoints or old parameter formats will cause the skill to fail in production.

### Progressive Disclosure

Skills use a three-level loading system:

1. **Metadata** (name + description) — always in context (~100 words)
2. **SKILL.md body** — loaded when skill triggers (<500 lines)
3. **Bundled resources** — loaded as needed by the agent (unlimited)

When SKILL.md approaches 500 lines, split content into reference files with clear pointers about when to read them.

**Pattern: high-level guide with references**

```markdown
# PDF Processing

## Quick start
Extract text with a PDF library:
[concise example]

## Advanced features
- **Form filling**: See references/forms.md for complete guide
- **API reference**: See references/api.md for all methods
```

The agent loads reference files only when needed.

**Pattern: domain-specific organization**

```
analytics-skill/
├── SKILL.md (overview and navigation)
└── references/
    ├── finance.md (revenue, billing metrics)
    ├── sales.md (opportunities, pipeline)
    └── product.md (API usage, features)
```

When the user asks about sales, the agent only reads `sales.md`.

**Pattern: conditional details**

```markdown
# DOCX Processing

## Creating documents
Use a DOCX library for new documents. See references/docx-guide.md.

## Editing documents
For simple edits, modify the XML directly.
**For tracked changes**: See references/redlining.md
```

## Skill Creation Process

### Step 1: Capture Intent

Understand the skill's purpose through concrete examples. If the current conversation already contains a workflow the user wants to capture, extract answers from the history first — the tools used, the sequence of steps, corrections made, input/output formats observed.

Key questions to resolve:

1. What should this skill enable the agent to do?
2. When should this skill trigger? (what user phrases/contexts)
3. What's the expected output format?
4. Are there edge cases or constraints?

Don't ask too many questions at once. Start with the most important and follow up as needed.

### Step 2: Research and Evaluate Tools

If the skill involves an external API or service, fetch the official documentation first using `web_fetch` or `web_search`. Verify the current API version, endpoints, authentication method, and request/response formats. Do not rely on memory — always confirm against live documentation to ensure the skill uses up-to-date APIs and libraries.

For each step in the workflow, identify which existing tool handles it:

- API calls → `http_client`
- Image creation → `generate_image`
- Web scraping → `web_fetch` or `browser`
- File operations → `read_file`, `write_file`, `edit_file`
- Shell commands → `exec`
- Web Search → `web_search`

Only if no existing tool can handle a step, consider bundling a script.

### Step 3: Plan Reusable Contents

Analyze each concrete example:

1. How would you execute this from scratch?
2. What scripts, references, or assets would help when repeating this?

**Examples:**

- Building a `pdf-editor` skill for "rotate this PDF" → a `scripts/rotate_pdf.py` avoids rewriting the same code each time
- Building a `webapp-builder` skill for "build me a todo app" → an `assets/template/` with boilerplate project files
- Building an `analytics` skill for "how many users logged in today?" → a `references/schema.md` documenting table schemas

Most skills need no bundled resources — just clear instructions in SKILL.md.

### Step 4: Create the Skill

Create the skill directory under `<workspace>/skills/` (or `<project>/skills/` for shared skills):

```
<workspace>/skills/skill-name/
├── SKILL.md
├── scripts/      (if needed)
├── references/   (if needed)
└── assets/       (if needed)
```

Only create resource directories that are actually needed.

### Step 5: Write the SKILL.md

#### Frontmatter

Write a clear `name` and comprehensive `description`:

- `description` is the primary triggering mechanism — include both what the skill does AND specific triggers/contexts
- make descriptions slightly "broad" in scope to avoid under-triggering — include related phrases and contexts the user might use even if they don't explicitly name the skill
- example: instead of "Process DOCX files", write "Create, edit, and analyze documents (.docx). Use when working with professional documents: creating new documents, modifying content, tracked changes, comments, formatting, or text extraction"

Only add `always`, `requires.bins`, or `requires.env` when actually needed.

#### Body

Write instructions for using the skill and its bundled resources. Guidelines:

- use imperative form ("Extract the data", not "The data should be extracted")
- explain the reasoning behind important choices, not just the rules
- show concrete tool call examples with realistic parameters
- specify what to do when information is missing (ask the user)
- start with the most common use case, then cover variations
- reference bundled resources with guidance on when to read them
- keep under 500 lines — split to references if approaching this limit

### Step 6: Iterate

After using the skill on real tasks:

1. Notice struggles or inefficiencies
2. Identify what to update in SKILL.md or bundled resources
3. Look for repeated work across invocations — if the agent keeps writing the same helper code, bundle it as a script
4. Implement changes and test again

### Skill Naming

- lowercase letters, digits, and hyphens only
- normalize user titles to hyphen-case (e.g., "Plan Mode" → `plan-mode`)
- under 64 characters
- prefer short, verb-led phrases that describe the action
- name the folder exactly after the skill name
