# One-Click Deploy

IonClaw supports one-click deploy on multiple cloud platforms. Each platform reads configuration files from the repository to build and run the application automatically using Docker.

After deploying on any platform:

1. Set environment variables for secrets (e.g. `ANTHROPIC_API_KEY`, `AUTH_SECRET_KEY`) in the platform's dashboard
2. Access the web interface at the URL provided by the platform
3. Log in with the credentials defined in your config

| Platform | |
|---|---|
| Render | [![Deploy to Render](https://render.com/images/deploy-to-render-button.svg)](https://render.com/deploy?repo=https://github.com/ionclaw-org/ionclaw) |
| Heroku | [![Deploy to Heroku](https://www.herokucdn.com/deploy/button.svg)](https://www.heroku.com/deploy?template=https://github.com/ionclaw-org/ionclaw) |
| DigitalOcean | [![Deploy to DigitalOcean](https://www.deploytodo.com/do-btn-blue.svg)](https://cloud.digitalocean.com/apps/new?repo=https://github.com/ionclaw-org/ionclaw/tree/main) |
| Hostinger | [![Deploy on Hostinger](https://assets.hostinger.com/vps/deploy.svg)](https://www.hostinger.com/docker-hosting?compose_url=https://raw.githubusercontent.com/ionclaw-org/ionclaw/main/docker-compose.yml) |

---

## Render

**Website:** https://render.com

**Config file:** `render.yaml`

### How it works

1. Render reads `render.yaml` (Blueprint spec) from the repository
2. Builds the Docker image using the `Dockerfile`
3. Starts on the `free` plan with a public URL

### Configuration

| Field | Value | Description |
|---|---|---|
| `type` | `web` | Web service with HTTP routing |
| `runtime` | `docker` | Uses the Dockerfile for building |
| `plan` | `free` | Uses the free tier |
| `envVars.PORT` | `8080` | Port the server listens on |

---

## Heroku

**Website:** https://heroku.com

**Config files:** `app.json` + `heroku.yml`

### How it works

1. Heroku reads `app.json` for metadata and detects `"stack": "container"`
2. Reads `heroku.yml` to build the Docker image
3. Starts the service and assigns a public URL

### Configuration

**`app.json`** — defines the app for the deploy button:

| Field | Value | Description |
|---|---|---|
| `name` | `IonClaw` | Application name |
| `stack` | `container` | Uses Docker instead of buildpacks |

**`heroku.yml`** — tells Heroku how to build:

| Field | Value | Description |
|---|---|---|
| `build.docker.web` | `Dockerfile` | Dockerfile for the web process |

---

## DigitalOcean App Platform

**Website:** https://cloud.digitalocean.com

**Config file:** `.do/deploy.template.yaml`

### How it works

1. DigitalOcean reads `.do/deploy.template.yaml` from the repository
2. Builds the Docker image using the `Dockerfile`
3. Starts on a `basic-xxs` instance with a public URL

### Configuration

| Field | Value | Description |
|---|---|---|
| `spec.name` | `ionclaw` | Application name |
| `dockerfile_path` | `Dockerfile` | Path to the Dockerfile |
| `http_port` | `8080` | Port the app listens on |
| `instance_size_slug` | `basic-xxs` | Smallest instance size |

---

## Hostinger

**Website:** https://www.hostinger.com

**Config file:** `docker-compose.yml`

### How it works

1. Hostinger provisions a VPS with Docker pre-installed
2. Pulls the `docker-compose.yml` from the raw GitHub URL
3. Docker Compose builds the image and starts the service on port `8080`

---

## Platform Files

| File | Platform | Purpose |
|---|---|---|
| `Dockerfile` | All | Multi-stage build (C++ compilation + minimal runtime) |
| `.dockerignore` | All | Excludes dev files from Docker build context |
| `docker-compose.yml` | Hostinger | Docker Compose service definition |
| `render.yaml` | Render | Blueprint service definition |
| `app.json` | Heroku | App metadata for deploy button |
| `heroku.yml` | Heroku | Docker build instructions |
| `.do/deploy.template.yaml` | DigitalOcean | App Platform service definition |
