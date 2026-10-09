#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
INSPECT="backend/build/test_sprint2"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_p2_sprint4_$$"
mkdir -p "$TEST_DIR"
trap 'rm -rf "$TEST_DIR"' EXIT

pass(){ PASS=$((PASS+1)); echo "  PASS: $1"; }
fail(){ FAIL=$((FAIL+1)); echo "  FAIL: $1${2:+ -> $2}"; }
assert_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE -- "$r"; then pass "$d"; else fail "$d" "$o"; fi; }
assert_not_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE -- "$r"; then fail "$d" "$o"; else pass "$d"; fi; }
assert_eq(){ local d="$1" a="$2" e="$3"; if [ "$a" = "$e" ]; then pass "$d"; else fail "$d" "esperado=$e obtenido=$a"; fi; }
kv(){ echo "$1" | grep "^$2=" | head -1 | cut -d= -f2-; }
run_multi(){ printf "%s\n" "$1" | "$BIN" 2>&1; }

if [ ! -x "$BIN" ] || [ ! -x "$INSPECT" ]; then
  echo "ERROR: compile primero con: cmake -S backend -B backend/build && cmake --build backend/build"
  exit 1
fi

echo "============================================"
echo " Proyecto 2 - Sprint 4: COPY + MOVE + FIND"
echo "============================================"

# ---------------------------------------------------------------------------
# 1) Sesion y validaciones del scanner
# ---------------------------------------------------------------------------
OUT=$(echo "copy -path=/a -destino=/b" | "$BIN" 2>&1)
assert_contains "1. COPY exige sesion" "$OUT" '\[ERROR\] COPY: necesita iniciar sesion'
OUT=$(echo "move -path=/a -destino=/b" | "$BIN" 2>&1)
assert_contains "2. MOVE exige sesion" "$OUT" '\[ERROR\] MOVE: necesita iniciar sesion'
OUT=$(echo "find -path=/ -name=*" | "$BIN" 2>&1)
assert_contains "3. FIND exige sesion" "$OUT" '\[ERROR\] FIND: necesita iniciar sesion'
OUT=$(echo "copy -path=/a -destino=/b -foo=1" | "$BIN" 2>&1)
assert_contains "4. scanner rechaza parametro extra en COPY" "$OUT" 'parametro no reconocido -foo'
OUT=$(echo "move -path=/a -destino=/b -foo=1" | "$BIN" 2>&1)
assert_contains "5. scanner rechaza parametro extra en MOVE" "$OUT" 'parametro no reconocido -foo'
OUT=$(echo "find -path=/ -name=* -foo=1" | "$BIN" 2>&1)
assert_contains "6. scanner rechaza parametro extra en FIND" "$OUT" 'parametro no reconocido -foo'

# ---------------------------------------------------------------------------
# 2) COPY de archivo y carpeta recursiva
# ---------------------------------------------------------------------------
D1="$TEST_DIR/copy_basic.mia"
OUT=$(run_multi "mkdisk -size=14 -unit=M -path=$D1
fdisk -size=9 -unit=M -path=$D1 -name=P1
mount -path=$D1 -name=P1
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkdir -p -path=/src/sub
mkdir -p -path=/dest
mkfile -path=/src/a.txt -size=25
mkfile -path=/src/sub/b.txt -size=900
copy -path=/src/a.txt -destino=/dest
cat -file1=/dest/a.txt
copy -path=/src/sub -destino=/dest
cat -file1=/dest/sub/b.txt")
assert_contains "7. COPY copia archivo" "$OUT" '\[OK\] COPY: /src/a.txt copiado en /dest'
assert_contains "8. archivo copiado conserva contenido" "$OUT" '0123456789012345678901234'
assert_contains "9. COPY copia carpeta recursivamente" "$OUT" '\[OK\] COPY: /src/sub copiado en /dest'
assert_contains "10. archivo grande copiado se puede leer" "$OUT" '012345678901234567890123456789'

BEFORE=$($INSPECT inspect "$D1" P1)
BI=$(kv "$BEFORE" free_inodes); BB=$(kv "$BEFORE" free_blocks)
OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
mkdir -p -path=/extra
copy -path=/src/a.txt -destino=/extra")
AFTER=$($INSPECT inspect "$D1" P1)
AI=$(kv "$AFTER" free_inodes); AB=$(kv "$AFTER" free_blocks)
if [ "$AI" -lt "$BI" ]; then pass "11. COPY consume un nuevo inodo"; else fail "11. COPY consume un nuevo inodo" "$BI -> $AI"; fi
if [ "$AB" -lt "$BB" ]; then pass "12. COPY consume nuevos bloques"; else fail "12. COPY consume nuevos bloques" "$BB -> $AB"; fi

OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
copy -path=/src/a.txt -destino=/dest")
assert_contains "13. COPY rechaza nombre duplicado en destino" "$OUT" 'ya existe un archivo o carpeta con el nombre a.txt'
OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
copy -path=/src -destino=/src/sub")
assert_contains "14. COPY evita copiar carpeta dentro de si misma" "$OUT" 'no se puede copiar una carpeta dentro de si misma'
OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
copy -path=/noexiste -destino=/dest")
assert_contains "15. COPY valida origen inexistente" "$OUT" 'no existe la ruta solicitada'
OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
copy -path=/src/a.txt -destino=/noexiste")
assert_contains "16. COPY valida destino inexistente" "$OUT" 'el destino no existe o no es accesible'
OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
copy -path=/src/a.txt")
assert_contains "17. COPY exige -destino" "$OUT" 'requiere -path y -destino'
OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
copy -path=/ -destino=/dest")
assert_contains "18. COPY protege la raiz" "$OUT" 'no se puede copiar la carpeta raiz'

# ---------------------------------------------------------------------------
# 3) COPY y permisos: omite solo el hijo que no puede leerse
# ---------------------------------------------------------------------------
D2="$TEST_DIR/copy_perms.mia"
OUT=$(run_multi "mkdisk -size=14 -unit=M -path=$D2
fdisk -size=9 -unit=M -path=$D2 -name=P2
mount -path=$D2 -name=P2
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkgrp -name=devs
mkusr -user=daniel -pass=123 -grp=devs
mkdir -p -path=/home/docs
mkdir -p -path=/images
mkfile -path=/home/docs/a.txt -size=7
mkfile -path=/home/docs/b.txt -size=8
chown -path=/home -user=daniel -r
chown -path=/images -user=daniel
chmod -path=/home/docs/b.txt -ugo=200
logout
login -user=daniel -pass=123 -id=441A
copy -path=/home/docs -destino=/images
cat -file1=/images/docs/a.txt
cat -file1=/images/docs/b.txt")
assert_contains "19. COPY reporta elemento omitido por lectura" "$OUT" '1 omitidos por permisos de lectura'
assert_contains "20. COPY conserva hijo legible" "$OUT" '0123456'
assert_contains "21. COPY omite hijo sin lectura" "$OUT" 'CAT: /images/docs/b.txt: no existe la ruta solicitada'

OUT=$(run_multi "mount -path=$D2 -name=P2
login -user=root -pass=123 -id=441A
chmod -path=/images -ugo=444
logout
login -user=daniel -pass=123 -id=441A
copy -path=/home/docs/a.txt -destino=/images")
assert_contains "22. COPY exige escritura en destino" "$OUT" 'no tiene permiso de escritura sobre la carpeta destino'

OUT=$(run_multi "mount -path=$D2 -name=P2
login -user=root -pass=123 -id=441A
chmod -path=/home/docs -ugo=200
logout
login -user=daniel -pass=123 -id=441A
copy -path=/home/docs -destino=/")
assert_contains "23. COPY exige lectura en origen" "$OUT" 'no tiene permiso de lectura sobre /home/docs'

# ---------------------------------------------------------------------------
# 4) MOVE cambia referencias sin copiar datos
# ---------------------------------------------------------------------------
D3="$TEST_DIR/move_basic.mia"
OUT=$(run_multi "mkdisk -size=14 -unit=M -path=$D3
fdisk -size=9 -unit=M -path=$D3 -name=P3
mount -path=$D3 -name=P3
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkdir -p -path=/origen/docs/sub
mkdir -p -path=/destino
mkfile -path=/origen/docs/sub/data.txt -size=850")
assert_contains "24. preparacion MOVE" "$OUT" '\[OK\] MKFILE'
BASE=$($INSPECT inspect "$D3" P3)
BASE_I=$(kv "$BASE" free_inodes); BASE_B=$(kv "$BASE" free_blocks)
OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=root -pass=123 -id=441A
move -path=/origen/docs -destino=/destino
cat -file1=/destino/docs/sub/data.txt
cat -file1=/origen/docs/sub/data.txt")
assert_contains "25. MOVE mueve carpeta" "$OUT" '\[OK\] MOVE: /origen/docs movido a /destino'
assert_contains "26. contenido permanece accesible en nueva ruta" "$OUT" '012345678901234567890123456789'
assert_contains "27. ruta anterior deja de existir" "$OUT" 'CAT: /origen/docs/sub/data.txt: no existe la ruta solicitada'
FINAL=$($INSPECT inspect "$D3" P3)
assert_eq "28. MOVE no asigna ni libera inodos" "$(kv "$FINAL" free_inodes)" "$BASE_I"
assert_eq "29. MOVE no asigna ni libera bloques" "$(kv "$FINAL" free_blocks)" "$BASE_B"

OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=root -pass=123 -id=441A
move -path=/destino/docs -destino=/destino/docs/sub")
assert_contains "30. MOVE evita ciclo hacia descendiente" "$OUT" 'no se puede mover una carpeta dentro de si misma'
OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=root -pass=123 -id=441A
mkdir -p -path=/otro/docs
move -path=/destino/docs -destino=/otro")
assert_contains "31. MOVE rechaza duplicado en destino" "$OUT" 'ya existe un archivo o carpeta con el nombre docs'
OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=root -pass=123 -id=441A
move -path=/noexiste -destino=/otro")
assert_contains "32. MOVE valida origen inexistente" "$OUT" 'no existe el archivo o carpeta indicado'
OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=root -pass=123 -id=441A
move -path=/destino/docs -destino=/noexiste")
assert_contains "33. MOVE valida destino inexistente" "$OUT" 'el destino no existe o no es accesible'
OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=root -pass=123 -id=441A
move -path=/ -destino=/otro")
assert_contains "34. MOVE protege la raiz" "$OUT" 'no se puede mover la carpeta raiz'
OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=root -pass=123 -id=441A
move -path=/destino/docs")
assert_contains "35. MOVE exige -destino" "$OUT" 'requiere -path y -destino'

# ---------------------------------------------------------------------------
# 5) MOVE y permisos: fuente + destino; no inspecciona hijos internos
# ---------------------------------------------------------------------------
D4="$TEST_DIR/move_perms.mia"
OUT=$(run_multi "mkdisk -size=14 -unit=M -path=$D4
fdisk -size=9 -unit=M -path=$D4 -name=P4
mount -path=$D4 -name=P4
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkgrp -name=devs
mkusr -user=daniel -pass=123 -grp=devs
mkdir -p -path=/home/docs
mkdir -p -path=/images
mkfile -path=/home/docs/b.txt -size=6
chown -path=/home -user=daniel -r
chown -path=/images -user=daniel
chmod -path=/home/docs/b.txt -ugo=444
logout
login -user=daniel -pass=123 -id=441A
move -path=/home/docs -destino=/images
cat -file1=/images/docs/b.txt")
assert_contains "36. MOVE permite mover carpeta aunque un hijo no tenga escritura" "$OUT" '\[OK\] MOVE: /home/docs movido a /images'
assert_contains "37. hijo sin escritura viaja con la carpeta" "$OUT" '012345'

OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
chmod -path=/images/docs -ugo=444
logout
login -user=daniel -pass=123 -id=441A
move -path=/images/docs -destino=/home")
assert_contains "38. MOVE exige escritura sobre objeto origen" "$OUT" 'no tiene permiso de escritura sobre /images/docs'

OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
chmod -path=/images/docs -ugo=664
chmod -path=/home -ugo=444
logout
login -user=daniel -pass=123 -id=441A
move -path=/images/docs -destino=/home")
assert_contains "39. MOVE exige escritura en destino" "$OUT" 'no tiene permiso de escritura sobre la carpeta destino'

# ---------------------------------------------------------------------------
# 6) FIND exacto, ?, * y permisos de lectura
# ---------------------------------------------------------------------------
D5="$TEST_DIR/find.mia"
OUT=$(run_multi "mkdisk -size=14 -unit=M -path=$D5
fdisk -size=9 -unit=M -path=$D5 -name=P5
mount -path=$D5 -name=P5
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkdir -p -path=/find/sub
mkfile -path=/find/a.txt -size=1
mkfile -path=/find/b.txt -size=2
mkfile -path=/find/a.jpg -size=3
mkfile -path=/find/abcd.jpg -size=4
mkfile -path=/find/sub/c.txt -size=5
find -path=/find -name=a.txt")
assert_contains "40. FIND exacto encuentra archivo" "$OUT" '1 coincidencias para a.txt'
assert_contains "41. FIND exacto muestra a.txt" "$OUT" '\|_ a.txt'
assert_not_contains "42. FIND exacto no muestra b.txt" "$OUT" '\|_ b.txt'

OUT=$(run_multi "mount -path=$D5 -name=P5
login -user=root -pass=123 -id=441A
find -path=/find -name=?.*")
assert_contains "43. FIND ? y * encuentran cuatro nombres de un caracter con extension" "$OUT" '4 coincidencias para \?\.\*'
assert_contains "44. FIND wildcard incluye a.txt" "$OUT" '\|_ a.txt'
assert_contains "45. FIND wildcard incluye a.jpg" "$OUT" '\|_ a.jpg'
assert_contains "46. FIND wildcard incluye c.txt recursivo" "$OUT" '\|_ c.txt'
assert_not_contains "47. FIND wildcard excluye abcd.jpg" "$OUT" '\|_ abcd.jpg'

OUT=$(run_multi "mount -path=$D5 -name=P5
login -user=root -pass=123 -id=441A
find -path=/find -name=*")
assert_contains "48. FIND * recorre contenido recursivamente" "$OUT" '6 coincidencias para \*'
assert_contains "49. FIND * incluye subcarpeta" "$OUT" '\|_ sub'
assert_contains "50. FIND * incluye archivo profundo" "$OUT" '\|_ c.txt'

OUT=$(run_multi "mount -path=$D5 -name=P5
login -user=root -pass=123 -id=441A
find -path=/find -name=noexiste*")
assert_contains "51. FIND informa sin coincidencias" "$OUT" 'sin coincidencias'
OUT=$(run_multi "mount -path=$D5 -name=P5
login -user=root -pass=123 -id=441A
find -path=/find/a.txt -name=*")
assert_contains "52. FIND exige carpeta en -path" "$OUT" '-path debe indicar una carpeta'
OUT=$(run_multi "mount -path=$D5 -name=P5
login -user=root -pass=123 -id=441A
find -path=/noexiste -name=*")
assert_contains "53. FIND valida ruta inexistente" "$OUT" 'no existe la ruta solicitada'
OUT=$(run_multi "mount -path=$D5 -name=P5
login -user=root -pass=123 -id=441A
find -path=/find")
assert_contains "54. FIND exige -name" "$OUT" 'requiere -path y -name'

# FIND omite objetos sin lectura para usuario normal.
OUT=$(run_multi "mount -path=$D5 -name=P5
login -user=root -pass=123 -id=441A
mkgrp -name=devs
mkusr -user=daniel -pass=123 -grp=devs
chown -path=/find -user=daniel -r
chmod -path=/find/abcd.jpg -ugo=200
logout
login -user=daniel -pass=123 -id=441A
find -path=/find -name=*")
assert_not_contains "55. FIND omite archivo sin permiso de lectura" "$OUT" '\|_ abcd.jpg'
assert_contains "56. FIND conserva resultados legibles" "$OUT" '\|_ a.txt'

# ---------------------------------------------------------------------------
# 7) EXT3: journaling para COPY, MOVE y FIND
# ---------------------------------------------------------------------------
D6="$TEST_DIR/journal.mia"
OUT=$(run_multi "mkdisk -size=14 -unit=M -path=$D6
fdisk -size=9 -unit=M -path=$D6 -name=P6
mount -path=$D6 -name=P6
mkfs -id=441A -fs=3fs
login -user=root -pass=123 -id=441A
mkdir -p -path=/src
mkdir -p -path=/dst
mkdir -p -path=/moved
mkfile -path=/src/a.txt -size=5
copy -path=/src/a.txt -destino=/dst
move -path=/dst/a.txt -destino=/moved
find -path=/ -name=a.txt
journaling -id=441A")
assert_contains "57. EXT3 journal registra COPY" "$OUT" 'copy \| /src/a.txt \| /dst'
assert_contains "58. EXT3 journal registra MOVE" "$OUT" 'move \| /dst/a.txt \| /moved'
assert_contains "59. EXT3 journal registra FIND" "$OUT" 'find \| / \| a.txt'

# ---------------------------------------------------------------------------
# 8) Estructuras binarias base no cambian
# ---------------------------------------------------------------------------
SZ=$($INSPECT sizes)
assert_eq "60. Superblock conserva 80 bytes" "$(kv "$SZ" SB)" "80"
assert_eq "61. Inode conserva 104 bytes" "$(kv "$SZ" INODE)" "104"
assert_eq "62. bloque conserva 64 bytes" "$(kv "$SZ" BLOCK)" "64"

echo "============================================"
echo " Resultados: $PASS pasaron, $FAIL fallaron"
if [ "$FAIL" -eq 0 ]; then
  echo " Todas las pruebas de Proyecto 2 Sprint 4 pasaron"
  exit 0
fi
echo " Hay pruebas fallidas en Proyecto 2 Sprint 4"
exit 1
