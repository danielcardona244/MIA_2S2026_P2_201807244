#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_sprint3_$$"
PORT=$((18080 + ($$ % 1000)))
SERVER_LOG="$TEST_DIR/api.log"
SERVER_PID=""

mkdir -p "$TEST_DIR"

cleanup() {
  if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -rf "$TEST_DIR"
}
trap cleanup EXIT

pass(){ PASS=$((PASS+1)); echo "  PASS: $1"; }
fail(){ FAIL=$((FAIL+1)); echo "  FAIL: $1${2:+ -> $2}"; }

assert_contains(){
  local description="$1"
  local output="$2"
  local regex="$3"
  if echo "$output" | grep -qE "$regex"; then
    pass "$description"
  else
    fail "$description" "$output"
  fi
}

assert_file(){
  local description="$1"
  local path="$2"
  if [ -f "$path" ]; then
    pass "$description"
  else
    fail "$description" "no existe $path"
  fi
}

if [ ! -x "$BIN" ]; then
  echo "ERROR: primero ejecute:"
  echo "  cmake -S backend -B backend/build"
  echo "  cmake --build backend/build"
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "ERROR: curl es necesario para probar la API REST"
  exit 1
fi

echo "============================================"
echo " Sprint 3 - Frontend + API REST"
echo "============================================"

"$BIN" --server "$PORT" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

READY=0
for _ in $(seq 1 40); do
  if curl -sS "http://127.0.0.1:$PORT/api/health" >/dev/null 2>&1; then
    READY=1
    break
  fi
  sleep 0.1
done

if [ "$READY" -ne 1 ]; then
  echo "ERROR: la API no inicio correctamente"
  cat "$SERVER_LOG"
  exit 1
fi

HEALTH=$(curl -sS "http://127.0.0.1:$PORT/api/health")
assert_contains "1. API responde health" "$HEALTH" '"ok":true'
assert_contains "2. health identifica ExtreamFS" "$HEALTH" '"service":"ExtreamFS"'

DISK="$TEST_DIR/s3.mia"
BODY="{\"commands\":\"mkdisk -size=8 -unit=M -path=$DISK\\nfdisk -size=3 -unit=M -path=$DISK -name=PartS3\\nmount -path=$DISK -name=PartS3\\nmkfs -id=441A\"}"

EXEC=$(curl -sS \
  -H "Content-Type: application/json" \
  -X POST \
  --data-binary "$BODY" \
  "http://127.0.0.1:$PORT/api/execute")

assert_contains "3. API ejecuta MKDISK" "$EXEC" '\[OK\] MKDISK'
assert_contains "4. API ejecuta FDISK" "$EXEC" '\[OK\] FDISK'
assert_contains "5. API ejecuta MOUNT" "$EXEC" 'id=441A'
assert_contains "6. API ejecuta MKFS" "$EXEC" '\[OK\] MKFS'

LOGIN=$(curl -sS \
  -H "Content-Type: application/json" \
  -X POST \
  --data-binary '{"commands":"login -user=root -pass=123 -id=441A"}' \
  "http://127.0.0.1:$PORT/api/execute")
assert_contains "7. sesion persiste entre requests" "$LOGIN" '\[OK\] LOGIN'

BAD_JSON=$(curl -sS \
  -H "Content-Type: application/json" \
  -X POST \
  --data-binary '{"otro":"valor"}' \
  "http://127.0.0.1:$PORT/api/execute")
assert_contains "8. API valida propiedad commands" "$BAD_JSON" '"ok":false'

CORS=$(curl -sSI -X OPTIONS "http://127.0.0.1:$PORT/api/execute" | tr -d '\r')
assert_contains "9. API expone CORS" "$CORS" 'Access-Control-Allow-Origin: \*'

assert_file "10. frontend React existe" "frontend/src/App.jsx"
assert_file "11. configuracion Vite existe" "frontend/vite.config.js"

PACKAGE=$(cat frontend/package.json 2>/dev/null || true)
if echo "$PACKAGE" | grep -q '"react"' && echo "$PACKAGE" | grep -q '"vite"'; then
  pass "12. frontend usa React + Vite"
else
  fail "12. frontend usa React + Vite" "dependencias no encontradas"
fi

if [ -d "frontend/node_modules" ]; then
  if (cd frontend && npm run build >/dev/null 2>&1); then
    pass "13. frontend compila con Vite"
  else
    fail "13. frontend compila con Vite" "npm run build fallo"
  fi
else
  echo "  INFO: frontend/node_modules no existe; omitiendo build web."
  echo "        Para validarlo: cd frontend && npm install && npm run build"
fi

echo "============================================"
echo " Resultados: $PASS pasaron, $FAIL fallaron"

if [ "$FAIL" -eq 0 ]; then
  echo " Todas las pruebas obligatorias pasaron"
  exit 0
fi

echo " Hay pruebas fallidas"
exit 1
