FROM node:22-slim AS web-builder

WORKDIR /build/apps/web
COPY apps/web/package.json .
RUN npm install
COPY apps/web/ .
RUN npm run build

FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY CMakeLists.txt .
COPY cmake/ cmake/
COPY main/ main/
COPY --from=web-builder /build/main/resources/web main/resources/web/

RUN cmake -B out -DCMAKE_BUILD_TYPE=Release \
    && cmake --build out --config Release -j$(nproc)

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3t64 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/out/bin/ionclaw-server /usr/local/bin/ionclaw-server

WORKDIR /data

VOLUME /data

EXPOSE 8080

CMD ["sh", "-c", "ionclaw-server init /data && ionclaw-server start --project /data --host 0.0.0.0 --port ${PORT:-8080}"]
