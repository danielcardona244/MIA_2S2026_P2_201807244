#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
INSPECT="backend/build/test_sprint2"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_p2_sprint5_$$"
PORT=$((19500 + ($$ % 400)))
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
assert_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then pass "$d"; else fail "$d" "$o"; fi; }
assert_not_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then fail "$d" "$o"; else pass "$d"; fi; }
assert_eq(){ local d="$1" a="$2" e="$3"; if [ "$a" = "$e" ]; then pass "$d"; else fail "$d" "esperado=$e obtenido=$a"; fi; }
assert_file(){ local d="$1" p="$2"; if [ -s "$p" ]; then pass "$d"; else fail "$d" "archivo ausente/vacio: $p"; fi; }
kv(){ echo "$1" | grep "^$2=" | head -1 | cut -d= -f2-; }
run_multi(){ printf "%s\n" "$1" | "$BIN" 2>&1; }

if [ ! -x "$BIN" ] || [ ! -x "$INSPECT" ]; then
  echo "ERROR: compile primero con: cmake -S backend -B backend/build && cmake --build backend/build"
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "ERROR: curl es necesario para probar la API REST"
  exit 1
fi

echo "============================================"
echo " Proyecto 2 - Sprint 5: LOSS + Journaling GUI"
echo "============================================"

# ---------------------------------------------------------------------------
# 1) Validaciones basicas de LOSS
# ---------------------------------------------------------------------------
OUT=$(echo "loss" | "$BIN" 2>&1)
assert_contains "1. LOSS exige id" "$OUT" 'falta parametro -id'

OUT=$(echo "loss -id=999Z" | "$BIN" 2>&1)
assert_contains "2. LOSS valida id montado" "$OUT" '\[ERROR\] LOSS'

OUT=$(echo "loss -id=441A -foo=bar" | "$BIN" 2>&1)
assert_contains "3. scanner rechaza parametro extra en LOSS" "$OUT" 'parametro no reconocido -foo'

D2="$TEST_DIR/ext2.mia"
OUT=$(run_multi "mkdisk -size=8 -unit=M -path=$D2
fdisk -size=3 -unit=M -path=$D2 -name=P2
mount -path=$D2 -name=P2
mkfs -id=441A -fs=2fs
loss -id=441A")
assert_contains "4. LOSS rechaza EXT2" "$OUT" 'la particion no utiliza EXT3'

# ---------------------------------------------------------------------------
# 2) LOSS real en EXT3: preserva SB+journal y limpia las cuatro areas
# ---------------------------------------------------------------------------
D3="$TEST_DIR/ext3_loss.mia"
PREP=$(run_multi "mkdisk -size=12 -unit=M -path=$D3
fdisk -size=6 -unit=M -path=$D3 -name=P3
mount -path=$D3 -name=P3
mkfs -id=441A -fs=3fs
login -user=root -pass=123 -id=441A
mkdir -p -path=/home/docs
mkfile -path=/home/docs/a.txt -size=900
mkgrp -name=devs
mkusr -user=daniel -pass=secreto -grp=devs")
assert_contains "5. preparacion EXT3 para LOSS" "$PREP" '\[OK\] MKFILE'

BEFORE=$($INSPECT loss_inspect "$D3" P3)
if [ "$(kv "$BEFORE" inode_bitmap_nonzero)" -gt 0 ] 2>/dev/null; then pass "6. bitmap de inodos tenia datos antes"; else fail "6. bitmap de inodos tenia datos antes" "$BEFORE"; fi
if [ "$(kv "$BEFORE" block_bitmap_nonzero)" -gt 0 ] 2>/dev/null; then pass "7. bitmap de bloques tenia datos antes"; else fail "7. bitmap de bloques tenia datos antes" "$BEFORE"; fi
if [ "$(kv "$BEFORE" inode_area_nonzero)" -gt 0 ] 2>/dev/null; then pass "8. area de inodos tenia datos antes"; else fail "8. area de inodos tenia datos antes" "$BEFORE"; fi
if [ "$(kv "$BEFORE" block_area_nonzero)" -gt 0 ] 2>/dev/null; then pass "9. area de bloques tenia datos antes"; else fail "9. area de bloques tenia datos antes" "$BEFORE"; fi
JBEFORE=$(kv "$BEFORE" journal_count)
if [ -n "$JBEFORE" ] && [ "$JBEFORE" -ge 5 ] 2>/dev/null; then pass "10. journal tenia transacciones antes"; else fail "10. journal tenia transacciones antes" "$BEFORE"; fi

SIZE_BEFORE=$(stat -c%s "$D3")
LOSS_OUT=$(run_multi "mount -path=$D3 -name=P3
loss -id=441A")
assert_contains "11. LOSS muestra bitmap de inodos antes" "$LOSS_OUT" 'bitmap inodos antes:.*1'
assert_contains "12. LOSS muestra bitmap de bloques antes" "$LOSS_OUT" 'bitmap bloques antes:.*1'
assert_contains "13. LOSS muestra bitmap de inodos despues" "$LOSS_OUT" 'bitmap inodos despues: 0{20}'
assert_contains "14. LOSS muestra bitmap de bloques despues" "$LOSS_OUT" 'bitmap bloques despues: 0{20}'
assert_contains "15. LOSS confirma perdida simulada" "$LOSS_OUT" '\[OK\] LOSS: perdida EXT3 simulada'

AFTER=$($INSPECT loss_inspect "$D3" P3)
assert_eq "16. LOSS conserva tipo EXT3" "$(kv "$AFTER" fs_type)" "3"
assert_eq "17. LOSS conserva magic EF53" "$(kv "$AFTER" magic)" "61267"
assert_eq "18. bitmap de inodos queda en cero binario" "$(kv "$AFTER" inode_bitmap_nonzero)" "0"
assert_eq "19. bitmap de bloques queda en cero binario" "$(kv "$AFTER" block_bitmap_nonzero)" "0"
assert_eq "20. area de inodos queda en cero binario" "$(kv "$AFTER" inode_area_nonzero)" "0"
assert_eq "21. area de bloques queda en cero binario" "$(kv "$AFTER" block_area_nonzero)" "0"
JAFTER=$(kv "$AFTER" journal_count)
assert_eq "22. LOSS agrega una transaccion al journal" "$JAFTER" "$((JBEFORE+1))"
assert_eq "23. ultima transaccion es LOSS" "$(kv "$AFTER" last_journal_operation)" "loss"
SIZE_AFTER=$(stat -c%s "$D3")
assert_eq "24. LOSS no cambia tamano fisico del disco" "$SIZE_AFTER" "$SIZE_BEFORE"

OUT=$(run_multi "mount -path=$D3 -name=P3
journaling -id=441A")
assert_contains "25. journaling previo sobrevive a LOSS" "$OUT" 'mkfile \| /home/docs/a.txt'
assert_contains "26. journaling registra LOSS" "$OUT" 'loss \| / \| system loss'

# Un nuevo proceso ya no debe poder autenticar con users.txt destruido.
OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=root -pass=123 -id=441A")
assert_contains "27. datos de usuarios se pierden con LOSS" "$OUT" '\[ERROR\] LOGIN'

# Los reportes de bitmap siguen disponibles y evidencian la perdida.
BMI="$TEST_DIR/bm_inode_after.txt"
BMB="$TEST_DIR/bm_block_after.txt"
OUT=$(run_multi "mount -path=$D3 -name=P3
rep -id=441A -path=$BMI -name=bm_inode
rep -id=441A -path=$BMB -name=bm_block")
assert_file "28. reporte bitmap inodos posterior existe" "$BMI"
assert_file "29. reporte bitmap bloques posterior existe" "$BMB"
if awk '{for(i=1;i<=NF;i++) if($i != 0) exit 1}' "$BMI"; then pass "30. reporte bm_inode evidencia solo ceros"; else fail "30. reporte bm_inode evidencia solo ceros"; fi
if awk '{for(i=1;i<=NF;i++) if($i != 0) exit 1}' "$BMB"; then pass "31. reporte bm_block evidencia solo ceros"; else fail "31. reporte bm_block evidencia solo ceros"; fi

# LOSS puede ejecutarse otra vez y el journal sigue intacto.
OUT=$(run_multi "mount -path=$D3 -name=P3
loss -id=441A
journaling -id=441A")
assert_contains "32. segundo LOSS sigue siendo valido" "$OUT" '\[OK\] LOSS'
assert_contains "33. journal permanece legible tras segundo LOSS" "$OUT" '\[OK\] JOURNALING'
AFTER2=$($INSPECT loss_inspect "$D3" P3)
assert_eq "34. segundo LOSS agrega otra transaccion" "$(kv "$AFTER2" journal_count)" "$((JAFTER+1))"

# ---------------------------------------------------------------------------
# 3) API dedicada de Journaling para la interfaz grafica
# ---------------------------------------------------------------------------
"$BIN" --server "$PORT" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!
READY=0
for _ in $(seq 1 50); do
  if curl -sS "http://127.0.0.1:$PORT/api/health" >/dev/null 2>&1; then READY=1; break; fi
  sleep 0.1
done
if [ "$READY" -ne 1 ]; then
  echo "ERROR: la API no inicio correctamente"
  cat "$SERVER_LOG"
  exit 1
fi

DA="$TEST_DIR/api_ext3.mia"
BODY=$(printf '{"commands":"mkdisk -size=8 -unit=M -path=%s\\nfdisk -size=4 -unit=M -path=%s -name=API3\\nmount -path=%s -name=API3\\nmkfs -id=441A -fs=3fs\\nlogin -user=root -pass=123 -id=441A\\nmkdir -p -path=/api/docs\\nmkfile -path=/api/docs/x.txt -size=25"}' "$DA" "$DA" "$DA")
EXEC=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary "$BODY" "http://127.0.0.1:$PORT/api/execute")
assert_contains "35. API prepara EXT3" "$EXEC" '\[OK\] MKFILE'

JAPI=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"id":"441A"}' "http://127.0.0.1:$PORT/api/journaling")
assert_contains "36. endpoint journaling responde ok" "$JAPI" '"ok":true'
assert_contains "37. endpoint devuelve arreglo entries" "$JAPI" '"entries":\['
assert_contains "38. endpoint expone operacion" "$JAPI" '"operation":"mkfile"'
assert_contains "39. endpoint expone ruta" "$JAPI" '"path":"/api/docs/x.txt"'
assert_contains "40. endpoint expone contenido" "$JAPI" '"content":"0123456789012345678901234"'
assert_contains "41. endpoint expone fecha" "$JAPI" '"date":"[0-9]{4}-[0-9]{2}-[0-9]{2}'

BAD=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{}' "http://127.0.0.1:$PORT/api/journaling")
assert_contains "42. endpoint exige id" "$BAD" '"ok":false'
BAD=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"id":"999Z"}' "http://127.0.0.1:$PORT/api/journaling")
assert_contains "43. endpoint valida id montado" "$BAD" '"ok":false'
STATUS=$(curl -sS -o /dev/null -w '%{http_code}' -X GET "http://127.0.0.1:$PORT/api/journaling")
assert_eq "44. endpoint journaling restringe metodo" "$STATUS" "405"

LOSS_API=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"commands":"loss -id=441A"}' "http://127.0.0.1:$PORT/api/execute")
assert_contains "45. LOSS funciona por API execute" "$LOSS_API" '\[OK\] LOSS'
JAPI2=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"id":"441A"}' "http://127.0.0.1:$PORT/api/journaling")
assert_contains "46. GUI API ve LOSS en journaling" "$JAPI2" '"operation":"loss"'

# ---------------------------------------------------------------------------
# 4) Frontend: apartado dedicado y proteccion destructiva
# ---------------------------------------------------------------------------
APP=$(cat frontend/src/App.jsx 2>/dev/null || true)
assert_contains "47. frontend tiene apartado Journaling EXT3" "$APP" 'Journaling EXT3'
assert_contains "48. frontend consume endpoint dedicado" "$APP" '/api/journaling'
assert_contains "49. frontend muestra Operacion" "$APP" '<th>Operacion</th>'
assert_contains "50. frontend muestra Ruta" "$APP" '<th>Ruta</th>'
assert_contains "51. frontend muestra Contenido" "$APP" '<th>Contenido</th>'
assert_contains "52. frontend muestra Fecha y hora" "$APP" '<th>Fecha y hora</th>'
assert_contains "53. frontend confirma LOSS destructivo" "$APP" 'window.confirm'
assert_contains "54. frontend detecta comando LOSS" "$APP" 'containsLoss'

if [ -d frontend/node_modules ]; then
  if (cd frontend && npm run build >/dev/null 2>&1); then
    pass "55. frontend compila con visor de journaling"
  else
    fail "55. frontend compila con visor de journaling" "npm run build fallo"
  fi
else
  echo "  INFO: frontend/node_modules no existe; prueba 55 omitida."
  echo "        En el equipo local: cd frontend && npm install && npm run build"
fi

echo "============================================"
echo " Resultados: $PASS pasaron, $FAIL fallaron"
if [ "$FAIL" -eq 0 ]; then
  echo " Todas las pruebas de Proyecto 2 Sprint 5 pasaron"
  exit 0
fi
echo " Hay pruebas fallidas en Proyecto 2 Sprint 5"
exit 1
