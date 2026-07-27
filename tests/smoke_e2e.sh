#!/usr/bin/env bash
#
# End-to-end smoke test: drives a real ionclaw-server through the chat API and
# checks that an agent produces an assistant reply. Exercises the claude-cli and
# ollama providers when their prerequisites are available, and skips them cleanly
# otherwise. This is a manual smoke (it needs the claude binary and/or a running
# Ollama), not part of the ctest suite.
#
# Usage: tests/smoke_e2e.sh [path-to-ionclaw-server]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER="${1:-$ROOT/build/release/bin/ionclaw-server}"
PORT=8971
BASE="http://127.0.0.1:$PORT"
WORKDIR="$(mktemp -d)"
PROJECT="$WORKDIR/proj"
SERVER_PID=""

cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

[ -x "$SERVER" ] || { echo "server binary not found at $SERVER (build it first)"; exit 1; }

token() {
    curl -s -X POST "$BASE/api/auth/login" -H 'Content-Type: application/json' \
        -d '{"username":"admin","password":"admin"}' \
        | python3 -c 'import sys,json;print(json.load(sys.stdin)["token"])'
}

# sends a message and waits for the assistant reply, printing it or failing
run_case() {
    local label="$1" model="$2" chat="$3" prompt="$4"
    echo "--- case: $label ($model) ---"

    "$SERVER" init "$PROJECT" >/dev/null 2>&1 || true
    cat > "$PROJECT/config.yml" <<YAML
server:
  host: 127.0.0.1
  port: $PORT
credentials:
  web_client:
    type: login
    username: admin
    password: admin
providers:
  ollama: {}
web_client:
  credential: web_client
agents:
  main:
    workspace: workspace
    model: "$model"
    instructions: "Reply concisely."
    tools: []
    model_params:
      max_tokens: 512
    agent_params:
      max_iterations: 4
YAML

    "$SERVER" start --project "$PROJECT" >"$WORKDIR/server.log" 2>&1 &
    SERVER_PID=$!

    for _ in $(seq 1 20); do
        curl -sf "$BASE/api/health" >/dev/null 2>&1 && break
        sleep 0.5
    done

    local tok
    tok="$(token)"
    curl -s -X POST "$BASE/api/chat" -H "Authorization: Bearer $tok" \
        -H 'Content-Type: application/json' \
        -d "{\"message\":\"$prompt\",\"session_id\":\"$chat\"}" >/dev/null

    local reply=""
    for _ in $(seq 1 40); do
        sleep 2
        reply="$(curl -s "$BASE/api/chat/sessions/web:$chat" -H "Authorization: Bearer $tok" \
            | python3 -c 'import sys,json
d=json.load(sys.stdin)
a=[m for m in d.get("messages",[]) if m.get("role")=="assistant" and m.get("content")]
print(a[-1]["content"] if a else "")')"
        [ -n "$reply" ] && break
    done

    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""

    if [ -z "$reply" ]; then
        echo "FAIL: no assistant reply for $label"
        tail -20 "$WORKDIR/server.log"
        return 1
    fi

    echo "PASS: $label -> ${reply:0:120}"
}

pass=0
if command -v claude >/dev/null 2>&1; then
    run_case "claude-cli" "claude-cli/sonnet" "cli1" "Reply with exactly: CLAUDE_CLI_OK" && pass=$((pass+1))
else
    echo "SKIP claude-cli: claude binary not in PATH"
fi

if command -v ollama >/dev/null 2>&1 && ollama list 2>/dev/null | grep -q .; then
    model="$(ollama list | awk 'NR==2{print $1}')"
    run_case "ollama" "ollama/$model" "oll1" "Say hello in one short sentence." && pass=$((pass+1))
else
    echo "SKIP ollama: no server or no models pulled"
fi

echo "=== smoke done, $pass case(s) passed ==="
