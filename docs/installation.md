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

Alternatively, download `ionclaw-macos-<arch>.pkg` from the [latest release](https://github.com/ionclaw-org/ionclaw/releases) and open it. It installs `ionclaw-server` to `/usr/local/bin` (already on `PATH`), so a new terminal can run it directly.

---

## Linux (.deb)

On Debian and Ubuntu, download `ionclaw_<version>_amd64.deb` from the [latest release](https://github.com/ionclaw-org/ionclaw/releases) and install it:

```bash
sudo dpkg -i ionclaw_*_amd64.deb
```

The server is installed to `/usr/bin/ionclaw-server` (on `PATH`). Initialize and start a project:

```bash
ionclaw-server init /path/to/your/project
ionclaw-server start --project /path/to/your/project
```

---

## Windows (MSI Installer)

Download `ionclaw-windows-x86_64.msi` from the [latest release](https://github.com/ionclaw-org/ionclaw/releases).

Because the file was downloaded from the internet, Windows tags it with the "mark of the web" and may block it. Before running, **right-click the `.msi` → Properties → check "Unblock" → Apply**. Then run the installer, or install it silently:

```powershell
# unblock from a terminal instead of the properties dialog
Unblock-File .\ionclaw-windows-x86_64.msi
msiexec /i ionclaw-windows-x86_64.msi /qn
```

The installer adds `ionclaw-server` to the system `PATH`, so open a new terminal and run it directly (installed under `C:\Program Files\IonClaw\bin`):

```powershell
ionclaw-server init C:\path\to\project
ionclaw-server start --project C:\path\to\project
```

Uninstall from **Settings → Apps → Installed apps**, or silently:

```powershell
msiexec /x ionclaw-windows-x86_64.msi /qn
```

---

## Build from Source

**Requirements:** CMake 3.25+, C++20 compiler, Perl (for the OpenSSL build), Node.js 20.19+ (for web client).

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

## Mobile and Apple platforms

IonClaw runs natively on iOS, tvOS, and watchOS via dedicated SwiftUI apps — see [Apple](apple.md) — and on Android via a native Kotlin/Compose app — see [Android](android.md). On desktop (macOS, Linux, Windows) it runs via the Flutter app — see [Flutter](flutter.md). They all embed the same C++ engine and run everything locally on the device.
