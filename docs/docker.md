# Docker

## Build

```bash
docker build -t ionclaw .
```

The `Dockerfile` uses a multi-stage build:

1. **web-builder** — compiles the Vue.js web client using Node.js 22
2. **builder** — compiles the C++ binary using CMake on Ubuntu 24.04
3. **runtime** — copies the binary into a minimal Ubuntu image with only OpenSSL runtime libraries

## Run

```bash
docker run -p 8080:8080 -v ionclaw-data:/data ionclaw
```

The container:

- Initializes a project in `/data` on first run (if not already initialized)
- Starts the server on the port defined by `PORT` (default `8080`)
- Stores all project data in `/data` — mount a volume for persistence

## Docker Compose

```bash
docker compose up
```

The `docker-compose.yml` at the project root builds the image, maps port `8080`, and mounts a named volume for data persistence.

```yaml
services:
  ionclaw:
    build: .
    ports:
      - "8080:8080"
    volumes:
      - ionclaw-data:/data
    environment:
      - PORT=8080
    restart: unless-stopped

volumes:
  ionclaw-data:
```

## Environment Variables

Pass environment variables to configure the server and inject secrets:

```bash
docker run -p 8080:8080 \
  -e PORT=8080 \
  -e ANTHROPIC_API_KEY=sk-... \
  -e AUTH_SECRET_KEY=my-secret \
  -v ionclaw-data:/data \
  ionclaw
```

Or add them to `docker-compose.yml`:

```yaml
environment:
  - PORT=8080
  - ANTHROPIC_API_KEY=sk-...
  - AUTH_SECRET_KEY=my-secret
```

Reference them in `config.yml` as `${ANTHROPIC_API_KEY}`.
