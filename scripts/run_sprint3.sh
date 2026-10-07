#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PORT="${1:-8080}"
API_PID=""

cleanup() {
  if [ -n "$API_PID" ] && kill -0 "$API_PID" 2>/dev/null; then
    kill "$API_PID" 2>/dev/null || true
    wait "$API_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if [ ! -x backend/build/extreamfs ]; then
  cmake -S backend -B backend/build
  cmake --build backend/build
fi

if [ ! -d frontend/node_modules ]; then
  echo "Instalando dependencias del frontend..."
  (cd frontend && npm install)
fi

backend/build/extreamfs --server "$PORT" &
API_PID=$!

echo
echo "Backend API: http://127.0.0.1:$PORT"
echo "Frontend:    http://127.0.0.1:5173"
echo "Ctrl+C para detener ambos."
echo

(cd frontend && npm run dev)
