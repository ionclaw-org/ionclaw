# Deploy

IonClaw is a C++ AI agent orchestrator that runs anywhere as a single native binary. It supports one-click deploy on multiple cloud platforms. Each platform reads specific configuration files from the repository to build and run the application automatically.

All platforms use the same `Dockerfile` to build the application. The build is a multi-stage process:

1. **Build stage** — compiles the C++ binary using CMake on Ubuntu 24.04 with build-essential and OpenSSL
2. **Runtime stage** — copies the binary into a minimal Ubuntu image with only OpenSSL runtime libraries

The container listens on the port defined by the `PORT` environment variable (defaults to `8080`).

After deploying on any platform, you need to:

1. Ensure the project has a `config.yml` (e.g. from `ionclaw init` or mounted volume). Set environment variables for secrets (e.g. `ANTHROPIC_API_KEY`, `AUTH_SECRET_KEY`) in the platform's dashboard so they are available when the server loads config. You can reference them in `config.yml` as `${ANTHROPIC_API_KEY}`.
2. Access the web interface at the URL provided by the platform.
3. Log in with the credentials defined in your config (default admin/admin if you use the auto-created credential).

---

## Shared Files

### `Dockerfile`

Multi-stage Docker build used by all platforms. The build stage uses `ubuntu:24.04` with CMake and build-essential to compile the C++ binary. The runtime stage uses a minimal `ubuntu:24.04` image with only OpenSSL runtime libraries.

The `CMD` initializes the project (if not already initialized) and starts the server:

```
CMD ["sh", "-c", "ionclaw-server init /data && ionclaw-server start --project /data --host 0.0.0.0 --port ${PORT:-8080}"]
```

Project data is stored in `/data` and should be mounted as a volume for persistence.

### `.dockerignore`

Excludes development files, build artifacts, docs, Flutter code, and other non-runtime files from the Docker build context to keep the image small and the build fast.

### `docker-compose.yml`

Used by Hostinger and for local Docker deployments. Builds the image and mounts a named volume (`ionclaw-data`) at `/data` for project persistence.

---

## Render

**Website:** https://render.com

**Config file:** `render.yaml`

**Deploy button:**

```
https://render.com/deploy?repo=https://github.com/ionclaw-org/ionclaw
```

### How it works

1. User clicks the deploy button in the README
2. Render reads `render.yaml` (Blueprint spec) from the repository
3. It builds the Docker image using the `Dockerfile`
4. The service starts on the `free` plan with a public URL
5. Render sets the `PORT` environment variable to `8080`

### Configuration

| Field | Value | Description |
|---|---|---|
| `type` | `web` | Web service with HTTP routing |
| `runtime` | `docker` | Uses the Dockerfile for building |
| `dockerfilePath` | `./Dockerfile` | Path to the Dockerfile |
| `plan` | `free` | Uses the free tier |
| `envVars.PORT` | `8080` | Port the server listens on |

### Environment variables

Set these in the Render dashboard under your service's **Environment** tab:

| Variable | Required | Description |
|---|---|---|
| `PORT` | No (default `8080`) | Port the server listens on |

---

## Heroku

**Website:** https://heroku.com

**Config files:** `app.json` + `heroku.yml`

**Deploy button:**

```
https://www.heroku.com/deploy?template=https://github.com/ionclaw-org/ionclaw
```

### How it works

1. User clicks the deploy button in the README
2. Heroku reads `app.json` for app metadata and detects `"stack": "container"`
3. It reads `heroku.yml` to know how to build the Docker image
4. The Docker image is built using the `Dockerfile`
5. The service starts and Heroku assigns a public URL
6. Heroku dynamically sets the `PORT` environment variable

### Configuration

**`app.json`** — defines the app for the deploy button:

| Field | Value | Description |
|---|---|---|
| `name` | `IonClaw` | Application name |
| `stack` | `container` | Uses Docker instead of buildpacks |
| `repository` | GitHub URL | Source repository |

**`heroku.yml`** — tells Heroku how to build:

| Field | Value | Description |
|---|---|---|
| `build.docker.web` | `Dockerfile` | Dockerfile for the web process |

### Environment variables

Set these in the Heroku dashboard under **Settings > Config Vars**:

| Variable | Required | Description |
|---|---|---|
| `PORT` | No (set by Heroku) | Port the server listens on |

---

## DigitalOcean App Platform

**Website:** https://cloud.digitalocean.com

**Config file:** `.do/deploy.template.yaml`

**Deploy button:**

```
https://cloud.digitalocean.com/apps/new?repo=https://github.com/ionclaw-org/ionclaw/tree/main
```

### How it works

1. User clicks the deploy button in the README
2. DigitalOcean reads `.do/deploy.template.yaml` from the repository
3. It builds the Docker image using the `Dockerfile`
4. The service starts on a `basic-xxs` instance with a public URL
5. DigitalOcean routes HTTP traffic to port `8080`

### Configuration

| Field | Value | Description |
|---|---|---|
| `spec.name` | `ionclaw` | Application name |
| `spec.services[0].name` | `web` | Service name |
| `spec.services[0].git.branch` | `main` | Branch to deploy |
| `spec.services[0].git.repo_clone_url` | GitHub `.git` URL | Source repository |
| `spec.services[0].dockerfile_path` | `Dockerfile` | Path to the Dockerfile |
| `spec.services[0].http_port` | `8080` | Port the app listens on |
| `spec.services[0].instance_count` | `1` | Number of instances |
| `spec.services[0].instance_size_slug` | `basic-xxs` | Smallest instance size |

### Environment variables

Set these in the DigitalOcean dashboard under your app's **Settings > App-Level Environment Variables**:

| Variable | Required | Description |
|---|---|---|
| `PORT` | No (default `8080`) | Port the server listens on |

---

## Hostinger

**Website:** https://www.hostinger.com

**Config file:** `docker-compose.yml`

**Deploy button:**

```
https://www.hostinger.com/docker-hosting?compose_url=https://raw.githubusercontent.com/ionclaw-org/ionclaw/main/docker-compose.yml
```

### How it works

1. User clicks the deploy button in the README
2. Hostinger provisions a VPS with Docker pre-installed
3. It pulls the `docker-compose.yml` from the raw GitHub URL
4. Docker Compose clones the repository and builds the image using the `Dockerfile`
5. The service starts on port `8080`

### Configuration

| Field | Value | Description |
|---|---|---|
| `services.ionclaw.build` | `.` | Builds the Dockerfile from the repository |
| `services.ionclaw.ports` | `8080:8080` | Maps container port to host |
| `services.ionclaw.volumes` | `ionclaw-data:/data` | Persists project data across restarts |
| `services.ionclaw.environment.PORT` | `8080` | Port the server listens on |
| `services.ionclaw.restart` | `unless-stopped` | Restarts automatically on crash |

### Environment variables

After deploying, set environment variables in the `docker-compose.yml` or via the Hostinger panel:

| Variable | Required | Description |
|---|---|---|
| `PORT` | No (default `8080`) | Port the server listens on |

---

## File Summary

| File | Platform | Purpose |
|---|---|---|
| `Dockerfile` | All | Multi-stage build (C++ compilation + minimal runtime) |
| `.dockerignore` | All | Excludes dev files from the Docker build context |
| `docker-compose.yml` | Hostinger | Docker Compose service definition |
| `render.yaml` | Render | Blueprint service definition |
| `app.json` | Heroku | App metadata and stack declaration for the deploy button |
| `heroku.yml` | Heroku | Docker build instructions |
| `.do/deploy.template.yaml` | DigitalOcean | App Platform service definition |
