# Installation

## macOS (Homebrew)

```bash
brew tap ionclaw-org/tap
brew install ionclaw
```

To install the latest development version from `main`:

```bash
brew install --HEAD ionclaw
```

After installing, initialize and start a project:

```bash
mkdir -p ~/my-agent && cd ~/my-agent
ionclaw-server --init
ionclaw-server
```

To update:

```bash
brew upgrade ionclaw
```

---

## Build from Source

**Requirements:** CMake 3.20+, C++17 compiler, Node.js 18+ (for web client).

### Linux / macOS / Windows

```bash
git clone https://github.com/ionclaw-org/ionclaw.git
cd ionclaw

make setup-web
make build-web
make build
make install
```

The `make install` command creates a symlink in `~/.local/bin`. Make sure it's in your `PATH`.

Then initialize and start a project:

```bash
ionclaw-server init /path/to/your/project
ionclaw-server start --project /path/to/your/project
```

### Build Targets

Run `make help` for a full list. Common targets:

| Command | Description |
|---|---|
| `make build` | Build server (release) |
| `make build-debug` | Build server (debug) |
| `make build-web` | Build web client |
| `make build-all` | Build everything (server + web + shared lib) |
| `make install` | Build and install to `~/.local/bin` |
| `make run` | Build and run server |

See [Build](build.md) for the complete reference.

---

## Docker

```bash
docker run -p 8080:8080 -v ionclaw-data:/data ghcr.io/ionclaw-org/ionclaw
```

Or build from source:

```bash
docker build -t ionclaw .
docker run -p 8080:8080 -v ionclaw-data:/data ionclaw
```

See [Docker](docker.md) for compose setup and configuration.

---

## One-Click Cloud Deploy

| Platform | |
|---|---|
| Render | [![Deploy to Render](https://render.com/images/deploy-to-render-button.svg)](https://render.com/deploy?repo=https://github.com/ionclaw-org/ionclaw) |
| Heroku | [![Deploy to Heroku](https://www.herokucdn.com/deploy/button.svg)](https://www.heroku.com/deploy?template=https://github.com/ionclaw-org/ionclaw) |
| DigitalOcean | [![Deploy to DigitalOcean](https://www.deploytodo.com/do-btn-blue.svg)](https://cloud.digitalocean.com/apps/new?repo=https://github.com/ionclaw-org/ionclaw/tree/main) |

See [Deploy](deploy.md) for detailed instructions per platform.

---

## Mobile

IonClaw runs natively on iOS and Android via the Flutter app. See [Flutter](flutter.md) for build and release instructions.
