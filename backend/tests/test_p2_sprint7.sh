#!/usr/bin/env bash
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$ROOT/backend/build/extreamfs"
PASS=0
FAIL=0
SKIP=0
TMP="$(mktemp -d)"
SERVER_PID=""

cleanup() {
  if [[ -n "$SERVER_PID" ]]; then kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; fi
  rm -rf "$TMP"
}
trap cleanup EXIT

pass() { PASS=$((PASS+1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL+1)); echo "  FAIL: $1"; }
skip() { SKIP=$((SKIP+1)); echo "  SKIP: $1"; }
contains() { grep -qE -- "$2" "$1"; }

start_server() {
  local port="$1" host="$2" log="$3"
  "$BIN" --server "$port" "$host" >"$log" 2>&1 &
  SERVER_PID=$!
  for _ in $(seq 1 50); do
    if curl -fsS "http://127.0.0.1:${port}/api/health" >/dev/null 2>&1; then return 0; fi
    sleep 0.1
  done
  return 1
}
stop_server() {
  if [[ -n "$SERVER_PID" ]]; then kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; SERVER_PID=""; fi
}

echo "============================================"
echo " Proyecto 2 - Sprint 7: Preparacion AWS"
echo "============================================"

if [[ -x "$BIN" ]]; then pass "1. backend compilado"; else fail "1. backend compilado"; fi

contains "$ROOT/backend/include/api.h" 'bind_address = "127\.0\.0\.1"' && pass "2. bind local sigue siendo default" || fail "2. bind local sigue siendo default"
contains "$ROOT/backend/src/main.cpp" '--server \[puerto\] \[host\]' && pass "3. CLI documenta host configurable" || fail "3. CLI documenta host configurable"
contains "$ROOT/backend/src/api.cpp" 'inet_pton\(AF_INET, bind_address' && pass "4. backend valida IPv4 de bind" || fail "4. backend valida IPv4 de bind"

PORT1=18171
LOG1="$TMP/local.log"
if start_server "$PORT1" "127.0.0.1" "$LOG1"; then
  pass "5. API inicia en bind local explicito"
  HEALTH=$(curl -fsS "http://127.0.0.1:${PORT1}/api/health" 2>/dev/null || true)
  [[ "$HEALTH" == *'"ok":true'* ]] && pass "6. health responde en modo local" || fail "6. health responde en modo local"
  contains "$LOG1" "127\.0\.0\.1:${PORT1}" && pass "7. log reporta bind local" || fail "7. log reporta bind local"
else
  fail "5. API inicia en bind local explicito"; fail "6. health responde en modo local"; fail "7. log reporta bind local"
fi
stop_server

PORT2=18172
LOG2="$TMP/aws.log"
if start_server "$PORT2" "0.0.0.0" "$LOG2"; then
  pass "8. API inicia en 0.0.0.0 para EC2"
  HEALTH=$(curl -fsS "http://127.0.0.1:${PORT2}/api/health" 2>/dev/null || true)
  [[ "$HEALTH" == *'"service":"ExtreamFS"'* ]] && pass "9. health responde con bind de red" || fail "9. health responde con bind de red"
  contains "$LOG2" "0\.0\.0\.0:${PORT2}" && pass "10. log reporta bind de red" || fail "10. log reporta bind de red"
  contains "$LOG2" 'modo red' && pass "11. log avisa modo accesible por red" || fail "11. log avisa modo accesible por red"
  CORS=$(curl -sSI -X OPTIONS "http://127.0.0.1:${PORT2}/api/health" | tr -d '\r')
  [[ "$CORS" == *'Access-Control-Allow-Origin: *'* ]] && pass "12. CORS permite frontend S3" || fail "12. CORS permite frontend S3"
  [[ "$CORS" == *'Access-Control-Allow-Methods: GET, POST, OPTIONS'* ]] && pass "13. preflight permite metodos usados" || fail "13. preflight permite metodos usados"
else
  fail "8. API inicia en 0.0.0.0 para EC2"; fail "9. health responde con bind de red"; fail "10. log reporta bind de red"; fail "11. log avisa modo accesible por red"; fail "12. CORS permite frontend S3"; fail "13. preflight permite metodos usados"
fi
stop_server

BAD=$($BIN --server 18173 no-es-ip 2>&1 || true)
[[ "$BAD" == *'direccion IPv4 invalida'* ]] && pass "14. backend rechaza host de bind invalido" || fail "14. backend rechaza host de bind invalido"

contains "$ROOT/frontend/src/App.jsx" 'VITE_API_URL' && pass "15. frontend usa VITE_API_URL" || fail "15. frontend usa VITE_API_URL"
contains "$ROOT/frontend/.env.production.example" '^VITE_API_URL=http://EC2_PUBLIC_IP:8080$' && pass "16. existe ejemplo de entorno AWS" || fail "16. existe ejemplo de entorno AWS"

[[ -x "$ROOT/deploy/ec2/install_backend.sh" ]] && pass "17. instalador EC2 es ejecutable" || fail "17. instalador EC2 es ejecutable"
contains "$ROOT/deploy/ec2/install_backend.sh" 'build-essential cmake graphviz curl' && pass "18. instalador incluye dependencias backend" || fail "18. instalador incluye dependencias backend"
contains "$ROOT/deploy/ec2/install_backend.sh" '0\.0\.0\.0' && pass "19. instalador EC2 expone API por defecto" || fail "19. instalador EC2 expone API por defecto"
contains "$ROOT/deploy/ec2/install_backend.sh" 'systemctl enable --now' && pass "20. instalador habilita servicio systemd" || fail "20. instalador habilita servicio systemd"
contains "$ROOT/deploy/ec2/extreamfs.service.example" 'Restart=on-failure' && pass "21. servicio reinicia ante fallo" || fail "21. servicio reinicia ante fallo"

[[ -x "$ROOT/deploy/s3/build_frontend.sh" ]] && pass "22. script S3 es ejecutable" || fail "22. script S3 es ejecutable"
contains "$ROOT/deploy/s3/build_frontend.sh" 'npm ci' && pass "23. build S3 usa instalacion reproducible" || fail "23. build S3 usa instalacion reproducible"
contains "$ROOT/deploy/s3/build_frontend.sh" 'VITE_API_URL=' && pass "24. build S3 inyecta URL EC2" || fail "24. build S3 inyecta URL EC2"
contains "$ROOT/deploy/s3/build_frontend.sh" 'aws s3 sync' && pass "25. script permite sincronizar dist a S3" || fail "25. script permite sincronizar dist a S3"
contains "$ROOT/deploy/s3/bucket-policy.json.template" 's3:GetObject' && pass "26. plantilla S3 permite lectura del sitio" || fail "26. plantilla S3 permite lectura del sitio"
contains "$ROOT/deploy/s3/bucket-policy.json.template" '__BUCKET_NAME__' && pass "27. politica S3 exige sustituir bucket" || fail "27. politica S3 exige sustituir bucket"

contains "$ROOT/deploy/AWS_DEPLOY.md" 'Static website hosting' && pass "28. guia documenta S3 website" || fail "28. guia documenta S3 website"
contains "$ROOT/deploy/AWS_DEPLOY.md" 'Security Group' && pass "29. guia documenta Security Group EC2" || fail "29. guia documenta Security Group EC2"
contains "$ROOT/deploy/AWS_DEPLOY.md" 'Prueba de aceptacion en nube' && pass "30. guia incluye prueba integral AWS" || fail "30. guia incluye prueba integral AWS"
contains "$ROOT/README.md" 'Proyecto 2 - Sprint 7' && pass "31. README documenta Sprint 7" || fail "31. README documenta Sprint 7"
[[ -f "$ROOT/data/.gitkeep" ]] && pass "32. existe directorio persistente sugerido para discos" || fail "32. existe directorio persistente sugerido para discos"

if [[ -d "$ROOT/frontend/node_modules" ]]; then
  if (cd "$ROOT/frontend" && VITE_API_URL="http://203.0.113.10:8080" npm run build >/dev/null 2>&1); then
    pass "33. frontend compila con VITE_API_URL de produccion"
    if grep -Rqs '203\.0\.113\.10:8080' "$ROOT/frontend/dist"; then
      pass "34. build incorpora endpoint configurado"
    else
      fail "34. build incorpora endpoint configurado"
    fi
  else
    fail "33. frontend compila con VITE_API_URL de produccion"
    fail "34. build incorpora endpoint configurado"
  fi
else
  skip "33-34. frontend build AWS (node_modules no esta incluido en el ZIP)"
fi

echo "============================================"
echo " Resultados: $PASS pasaron, $FAIL fallaron, $SKIP omitidos"
if (( FAIL == 0 )); then
  echo " Todas las pruebas de Proyecto 2 Sprint 7 pasaron"
  exit 0
fi
exit 1
