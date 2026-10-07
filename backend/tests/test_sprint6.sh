#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$ROOT/backend/build/extreamfs"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_sprint6_$$"
mkdir -p "$TEST_DIR"
API_PID=""
cleanup(){
  if [ -n "$API_PID" ] && kill -0 "$API_PID" 2>/dev/null; then kill "$API_PID" 2>/dev/null || true; wait "$API_PID" 2>/dev/null || true; fi
  rm -rf "$TEST_DIR"
}
trap cleanup EXIT

pass(){ PASS=$((PASS+1)); echo "  PASS: $1"; }
fail(){ FAIL=$((FAIL+1)); echo "  FAIL: $1${2:+ -> $2}"; }
assert_contains(){ local d="$1" o="$2" r="$3"; if printf '%s\n' "$o" | grep -qE "$r"; then pass "$d"; else fail "$d" "$o"; fi; }
assert_file(){ local d="$1" f="$2"; if [ -s "$f" ]; then pass "$d"; else fail "$d" "archivo ausente/vacio: $f"; fi; }
run_multi(){ printf "%s\n" "$1" | "$BIN" 2>&1; }

printf '%s\n' "============================================"
printf '%s\n' " Sprint 6 - Pruebas integrales y entrega"
printf '%s\n' "============================================"

# 1) Compilacion limpia y herramientas requeridas
if cmake -S "$ROOT/backend" -B "$ROOT/backend/build" >/dev/null 2>&1 && cmake --build "$ROOT/backend/build" >/dev/null 2>&1; then pass "1. backend compila con CMake"; else fail "1. backend compila con CMake"; fi
if command -v dot >/dev/null 2>&1; then pass "2. Graphviz disponible"; else fail "2. Graphviz disponible" "instalar graphviz"; fi
if [ -f "$ROOT/frontend/package.json" ] && grep -q '"react"' "$ROOT/frontend/package.json"; then pass "3. frontend React presente"; else fail "3. frontend React presente"; fi
if [ -f "$ROOT/frontend/src/App.jsx" ] && grep -q "endsWith('.smia')" "$ROOT/frontend/src/App.jsx"; then pass "4. frontend valida extension .smia"; else fail "4. frontend valida extension .smia"; fi
if grep -q 'POST /api/execute' "$ROOT/backend/src/api.cpp" && grep -q 'GET  /api/health' "$ROOT/backend/src/api.cpp"; then pass "5. endpoints REST presentes"; else fail "5. endpoints REST presentes"; fi

# 2) Flujo integral por consola
DISK="$TEST_DIR/final test.mia"
REPORTS="$TEST_DIR/reportes finales"
HOSTFILE="$TEST_DIR/host.txt"
printf 'Contenido cargado desde archivo host.\nSegunda linea.' > "$HOSTFILE"
CMDS="# comentario preservado de script
mkdisk -size=16 -unit=M -fit=FF -path=\"$DISK\"
fdisk -size=11 -unit=M -path=\"$DISK\" -name=Principal -fit=WF
mount -path=\"$DISK\" -name=Principal
mounted
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkgrp -name=devs
mkusr -user=daniel -pass=clave -grp=devs
mkdir -p -path=/home/daniel/docs
mkfile -path=/home/daniel/docs/a.txt -size=900
mkfile -path=/home/daniel/docs/host.txt -cont=\"$HOSTFILE\"
cat -file1=/home/daniel/docs/a.txt -file2=/home/daniel/docs/host.txt
rep -id=441A -path=\"$REPORTS/mbr.png\" -name=mbr
rep -id=441A -path=\"$REPORTS/disk.pdf\" -name=disk
rep -id=441A -path=\"$REPORTS/tree.png\" -name=tree
rep -id=441A -path=\"$REPORTS/users.txt\" -name=file -path_file_ls=/users.txt
rep -id=441A -path=\"$REPORTS/ls.png\" -name=ls -path_file_ls=/home/daniel/docs"
OUT=$(run_multi "$CMDS")
assert_contains "6. comentarios de script llegan a salida" "$OUT" '# comentario preservado de script'
assert_contains "7. MKDISK integral" "$OUT" '\[OK\] MKDISK'
assert_contains "8. FDISK integral" "$OUT" '\[OK\] FDISK'
assert_contains "9. MOUNT integral" "$OUT" 'id=441A'
assert_contains "10. MKFS integral" "$OUT" '\[OK\] MKFS'
assert_contains "11. LOGIN integral" "$OUT" '\[OK\] LOGIN'
assert_contains "12. usuarios/grupos integral" "$OUT" '\[OK\] MKUSR'
assert_contains "13. MKDIR integral" "$OUT" '\[OK\] MKDIR'
assert_contains "14. MKFILE integral" "$OUT" '\[OK\] MKFILE'
assert_contains "15. CAT concatena archivos" "$OUT" 'Contenido cargado desde archivo host'
assert_contains "16. REP integral" "$OUT" '\[OK\] REP'

# El disco no debe crecer luego de operaciones
SIZE=$(stat -c%s "$DISK" 2>/dev/null || echo 0)
if [ "$SIZE" = "$((16*1024*1024))" ]; then pass "17. archivo .mia conserva tamano fisico"; else fail "17. archivo .mia conserva tamano fisico" "obtenido=$SIZE"; fi
assert_file "18. reporte MBR generado" "$REPORTS/mbr.png"
assert_file "19. reporte DISK generado" "$REPORTS/disk.pdf"
assert_file "20. reporte TREE generado" "$REPORTS/tree.png"
assert_file "21. reporte FILE generado" "$REPORTS/users.txt"
assert_file "22. reporte LS generado" "$REPORTS/ls.png"
if head -c 4 "$REPORTS/disk.pdf" | grep -q '%PDF'; then pass "23. DISK es PDF real"; else fail "23. DISK es PDF real"; fi
if grep -q '1,U,root,root,123' "$REPORTS/users.txt" && grep -q 'daniel' "$REPORTS/users.txt"; then pass "24. FILE users.txt refleja usuarios"; else fail "24. FILE users.txt refleja usuarios"; fi

# 3) API REST real y estado persistente
PORT=$((18080 + ($$ % 1000)))
"$BIN" --server "$PORT" >"$TEST_DIR/api.log" 2>&1 &
API_PID=$!
for _ in $(seq 1 50); do
  if curl -s "http://127.0.0.1:$PORT/api/health" >/dev/null 2>&1; then break; fi
  sleep 0.1
done
HEALTH=$(curl -s "http://127.0.0.1:$PORT/api/health" 2>/dev/null || true)
assert_contains "25. health API final" "$HEALTH" '"ok":true'
API_DISK="$TEST_DIR/api.mia"
BODY=$(python3 - <<PY
import json
print(json.dumps({"commands":"mkdisk -size=8 -unit=M -path=$API_DISK\\nfdisk -size=4 -unit=M -path=$API_DISK -name=Web\\nmount -path=$API_DISK -name=Web\\nmkfs -id=441A"}))
PY
)
API_OUT=$(curl -s -H 'Content-Type: application/json' -d "$BODY" "http://127.0.0.1:$PORT/api/execute" 2>/dev/null || true)
assert_contains "26. API ejecuta flujo multi-comando" "$API_OUT" '\[OK\] MKFS'
BODY2='{"commands":"login -user=root -pass=123 -id=441A\nmkdir -p -path=/api/prueba\nmkfile -path=/api/prueba/test.txt -size=15\ncat -file1=/api/prueba/test.txt"}'
API_OUT2=$(curl -s -H 'Content-Type: application/json' -d "$BODY2" "http://127.0.0.1:$PORT/api/execute" 2>/dev/null || true)
assert_contains "27. sesion y EXT2 persisten entre requests" "$API_OUT2" '012345678901234'

# 4) Documentacion y frontend de entrega
assert_file "28. Manual Tecnico PDF" "$ROOT/docs/Manual_Tecnico_ExtreamFS.pdf"
assert_file "29. Manual de Usuario PDF" "$ROOT/docs/Manual_Usuario_ExtreamFS.pdf"
assert_file "30. Diagrama de Flujo PDF" "$ROOT/docs/Diagrama_Flujo_ExtreamFS.pdf"
assert_file "31. Informe de Impacto PDF" "$ROOT/docs/Informe_Impacto_ExtreamFS.pdf"
assert_file "32. Plan de Mantenimiento PDF" "$ROOT/docs/Plan_Mantenimiento_ExtreamFS.pdf"
if grep -q 'Arquitectura' "$ROOT/README.md" && grep -q 'Graphviz' "$ROOT/README.md"; then pass "33. README final documentado"; else fail "33. README final documentado"; fi

if [ -d "$ROOT/frontend/node_modules" ]; then
  if (cd "$ROOT/frontend" && npm run build >/dev/null 2>&1); then pass "34. frontend compila en produccion"; else fail "34. frontend compila en produccion"; fi
else
  echo "  INFO: frontend/node_modules no existe; prueba 34 omitida (npm install)."
fi

printf '%s\n' "============================================"
printf ' Resultados: %d pasaron, %d fallaron\n' "$PASS" "$FAIL"
if [ "$FAIL" -eq 0 ]; then
  echo " Todas las pruebas integrales pasaron"
  exit 0
else
  echo " Hay pruebas integrales fallidas"
  exit 1
fi
