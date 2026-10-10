#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_p2_sprint6_$$"
PORT=$((19900 + ($$ % 400)))
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

if [ ! -x "$BIN" ]; then
  echo "ERROR: compile primero con: cmake -S backend -B backend/build && cmake --build backend/build"
  exit 1
fi
if ! command -v curl >/dev/null 2>&1; then
  echo "ERROR: curl es necesario para probar la API REST"
  exit 1
fi

echo "============================================"
echo " Proyecto 2 - Sprint 6: Login + Explorador GUI"
echo "============================================"

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

BASE="http://127.0.0.1:$PORT"
D1="$TEST_DIR/visual1.mia"
D2="$TEST_DIR/visual2.mia"

HEALTH=$(curl -sS "$BASE/api/health")
assert_contains "1. API health disponible" "$HEALTH" '"ok":true'
assert_contains "2. health anuncia EXT2/EXT3" "$HEALTH" '"filesystem":"EXT2/EXT3"'

SESSION=$(curl -sS "$BASE/api/session")
assert_contains "3. sesion inicia inactiva" "$SESSION" '"active":false'

# Crear dos discos; solo el primero queda montado y formateado.
BODY=$(printf '{"commands":"mkdisk -size=12 -unit=M -path=%s\\nfdisk -size=8 -unit=M -path=%s -name=Datos\\nmount -path=%s -name=Datos\\nmkfs -id=441A -fs=3fs\\nmkdisk -size=6 -unit=M -path=%s\\nfdisk -size=3 -unit=M -path=%s -name=Reserva"}' "$D1" "$D1" "$D1" "$D2" "$D2")
PREP=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary "$BODY" "$BASE/api/execute")
assert_contains "4. terminal API crea discos" "$PREP" '\[OK\] MKDISK'
assert_contains "5. terminal API monta particion" "$PREP" 'id=441A'
assert_contains "6. terminal API formatea EXT3" "$PREP" 'formateada como EXT3'

DISKS=$(curl -sS "$BASE/api/disks")
assert_contains "7. endpoint discos responde ok" "$DISKS" '"ok":true'
assert_contains "8. visualizador lista primer disco" "$DISKS" '"name":"visual1.mia"'
assert_contains "9. visualizador lista segundo disco" "$DISKS" '"name":"visual2.mia"'
assert_contains "10. disco expone capacidad" "$DISKS" '"size":12582912'
assert_contains "11. disco expone fit" "$DISKS" '"fit":"FF"'
assert_contains "12. disco expone fecha" "$DISKS" '"created":"[0-9]{4}-[0-9]{2}-[0-9]{2}'
assert_contains "13. disco expone particiones montadas" "$DISKS" '"mountedCount":1'
assert_contains "14. disco expone cantidad de particiones" "$DISKS" '"partitionCount":1'

PART1=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary "{\"path\":\"$D1\"}" "$BASE/api/partitions")
assert_contains "15. endpoint particiones responde ok" "$PART1" '"ok":true'
assert_contains "16. particion muestra nombre" "$PART1" '"name":"Datos"'
assert_contains "17. particion muestra tipo" "$PART1" '"type":"primary"'
assert_contains "18. particion muestra tamano" "$PART1" '"size":8388608'
assert_contains "19. particion muestra fit" "$PART1" '"fit":"WF"'
assert_contains "20. particion muestra estado montado" "$PART1" '"mounted":true'
assert_contains "21. particion muestra id" "$PART1" '"id":"441A"'

PART2=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary "{\"path\":\"$D2\"}" "$BASE/api/partitions")
assert_contains "22. particion desmontada es visible" "$PART2" '"name":"Reserva"'
assert_contains "23. particion desmontada reporta estado" "$PART2" '"mounted":false'
assert_contains "24. particion desmontada no tiene id" "$PART2" '"id":""'

BAD_LOGIN=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"user":"root","password":"incorrecta","id":"441A"}' "$BASE/api/login")
assert_contains "25. login grafico rechaza password incorrecto" "$BAD_LOGIN" '"ok":false'
SESSION=$(curl -sS "$BASE/api/session")
assert_contains "26. login fallido no activa sesion" "$SESSION" '"active":false'

LOGIN=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"user":"root","password":"123","id":"441A"}' "$BASE/api/login")
assert_contains "27. login grafico responde ok" "$LOGIN" '"ok":true'
assert_contains "28. login devuelve usuario" "$LOGIN" '"user":"root"'
assert_contains "29. login devuelve grupo" "$LOGIN" '"group":"root"'
assert_contains "30. login devuelve id de particion" "$LOGIN" '"id":"441A"'
assert_contains "31. login devuelve uid/gid" "$LOGIN" '"uid":1.*"gid":1'
assert_not_contains "32. login nunca devuelve password" "$LOGIN" '123|password'

DUP=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"user":"root","password":"123","id":"441A"}' "$BASE/api/login")
assert_contains "33. API evita doble login" "$DUP" '"ok":false'

SESSION=$(curl -sS "$BASE/api/session")
assert_contains "34. sesion persiste entre requests" "$SESSION" '"active":true'
assert_contains "35. sesion persistente conserva id" "$SESSION" '"id":"441A"'

FILES=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"commands":"mkdir -p -path=/home/docs/sub\nmkfile -path=/home/docs/a.txt -size=25\nmkfile -path=/home/docs/sub/b.txt -size=80"}' "$BASE/api/execute")
assert_contains "36. sesion GUI sirve para MKDIR" "$FILES" '\[OK\] MKDIR'
assert_contains "37. sesion GUI sirve para MKFILE" "$FILES" '\[OK\] MKFILE'

ROOT=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"id":"441A","path":"/"}' "$BASE/api/fs/list")
assert_contains "38. explorador abre raiz" "$ROOT" '"path":"/"'
assert_contains "39. raiz muestra users.txt" "$ROOT" '"name":"users.txt"'
assert_contains "40. raiz muestra carpeta home" "$ROOT" '"name":"home".*"type":"folder"'

DOCS=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"id":"441A","path":"/home/docs"}' "$BASE/api/fs/list")
assert_contains "41. explorador navega carpetas" "$DOCS" '"path":"/home/docs"'
assert_contains "42. listado muestra archivo" "$DOCS" '"name":"a.txt".*"type":"file"'
assert_contains "43. listado muestra subcarpeta" "$DOCS" '"name":"sub".*"type":"folder"'
assert_contains "44. listado muestra permisos" "$DOCS" '"permissions":"664"'
assert_contains "45. listado muestra UID y GID" "$DOCS" '"uid":1.*"gid":1'
assert_contains "46. listado informa lectura" "$DOCS" '"readable":true'
assert_contains "47. listado muestra fecha modificada" "$DOCS" '"modified":"[0-9]{4}-[0-9]{2}-[0-9]{2}'

FILE=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"id":"441A","path":"/home/docs/a.txt"}' "$BASE/api/fs/file")
assert_contains "48. visualizador abre archivo" "$FILE" '"name":"a.txt"'
assert_contains "49. archivo muestra contenido" "$FILE" '"content":"0123456789012345678901234"'
assert_contains "50. archivo muestra tamano" "$FILE" '"size":25'
assert_contains "51. archivo muestra permisos" "$FILE" '"permissions":"664"'

BAD_PATH=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"id":"441A","path":"/no/existe"}' "$BASE/api/fs/list")
assert_contains "52. explorador valida ruta inexistente" "$BAD_PATH" '"ok":false'
BAD_ID=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"id":"442A","path":"/"}' "$BASE/api/fs/list")
assert_contains "53. explorador limita acceso a sesion activa" "$BAD_ID" '"ok":false'
assert_contains "54. error identifica particion de sesion" "$BAD_ID" 'sesion activa'

LOGOUT=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{}' "$BASE/api/logout")
assert_contains "55. logout grafico responde ok" "$LOGOUT" '"ok":true'
assert_contains "56. logout deja sesion inactiva" "$LOGOUT" '"active":false'
POST_LOGOUT=$(curl -sS -H 'Content-Type: application/json' -X POST --data-binary '{"id":"441A","path":"/"}' "$BASE/api/fs/list")
assert_contains "57. explorador exige login" "$POST_LOGOUT" '"ok":false'
assert_contains "58. explorador explica que debe iniciar sesion" "$POST_LOGOUT" 'iniciar sesion'

STATUS=$(curl -sS -o /dev/null -w '%{http_code}' -X POST "$BASE/api/disks")
assert_eq "59. endpoint discos restringe metodo" "$STATUS" "405"
STATUS=$(curl -sS -o /dev/null -w '%{http_code}' -X GET "$BASE/api/login")
assert_eq "60. endpoint login restringe metodo" "$STATUS" "405"

# Frontend: flujo solicitado por el enunciado.
APP=$(cat frontend/src/App.jsx 2>/dev/null || true)
assert_contains "61. frontend tiene pantalla de iniciar sesion" "$APP" 'Iniciar sesión'
assert_contains "62. frontend consume API login" "$APP" '/api/login'
assert_contains "63. frontend consume API logout" "$APP" '/api/logout'
assert_contains "64. frontend consulta sesion" "$APP" '/api/session'
assert_contains "65. frontend tiene visualizador" "$APP" 'Visualizador del sistema de archivos'
assert_contains "66. visualizador se declara solo lectura" "$APP" 'Solo lectura'
assert_contains "67. frontend consume discos" "$APP" '/api/disks'
assert_contains "68. frontend consume particiones" "$APP" '/api/partitions'
assert_contains "69. frontend navega directorios" "$APP" '/api/fs/list'
assert_contains "70. frontend visualiza archivos" "$APP" '/api/fs/file'
assert_contains "71. frontend muestra permisos" "$APP" 'Permisos'
assert_contains "72. frontend muestra UID/GID" "$APP" 'UID/GID'
assert_contains "73. frontend incluye cierre de sesion" "$APP" 'Cerrar sesión'
assert_contains "74. frontend mantiene Journaling EXT3" "$APP" 'Journaling EXT3'
assert_contains "75. frontend prepara URL configurable para AWS" "$APP" 'VITE_API_URL'

CSS=$(cat frontend/src/styles.css 2>/dev/null || true)
assert_contains "76. frontend tiene estilos del explorador" "$CSS" '\.explorer-grid'
assert_contains "77. frontend tiene estilos de particiones" "$CSS" '\.partition-card'
assert_contains "78. frontend tiene estilos de archivos" "$CSS" '\.file-table'
assert_contains "79. frontend es responsive" "$CSS" '@media.*max-width'

if [ -d frontend/node_modules ]; then
  if (cd frontend && npm run build >/dev/null 2>&1); then
    pass "80. frontend compila con login y explorador"
  else
    fail "80. frontend compila con login y explorador" "npm run build fallo"
  fi
else
  echo "  INFO: frontend/node_modules no existe; prueba 80 omitida."
  echo "        En el equipo local: cd frontend && npm install && npm run build"
fi

echo "============================================"
echo " Resultados: $PASS pasaron, $FAIL fallaron"
if [ "$FAIL" -eq 0 ]; then
  echo " Todas las pruebas de Proyecto 2 Sprint 6 pasaron"
  exit 0
fi
echo " Hay pruebas fallidas en Proyecto 2 Sprint 6"
exit 1
