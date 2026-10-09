#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
INSPECT="backend/build/test_sprint2"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_p2_sprint3_$$"
mkdir -p "$TEST_DIR"
trap 'rm -rf "$TEST_DIR"' EXIT

pass(){ PASS=$((PASS+1)); echo "  PASS: $1"; }
fail(){ FAIL=$((FAIL+1)); echo "  FAIL: $1${2:+ -> $2}"; }
assert_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then pass "$d"; else fail "$d" "$o"; fi; }
assert_not_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then fail "$d" "$o"; else pass "$d"; fi; }
assert_eq(){ local d="$1" a="$2" e="$3"; if [ "$a" = "$e" ]; then pass "$d"; else fail "$d" "esperado=$e obtenido=$a"; fi; }
kv(){ echo "$1" | grep "^$2=" | head -1 | cut -d= -f2-; }
run_multi(){ printf "%s\n" "$1" | "$BIN" 2>&1; }

if [ ! -x "$BIN" ] || [ ! -x "$INSPECT" ]; then
  echo "ERROR: compile primero con: cmake -S backend -B backend/build && cmake --build backend/build"
  exit 1
fi

echo "============================================"
echo " Proyecto 2 - Sprint 3: REMOVE + RENAME"
echo "============================================"

# ---------------------------------------------------------------------------
# 1) Validaciones de sesion y REMOVE basico
# ---------------------------------------------------------------------------
OUT=$(echo "remove -path=/a.txt" | "$BIN" 2>&1)
assert_contains "1. REMOVE exige sesion" "$OUT" '\[ERROR\] REMOVE: necesita iniciar sesion'
OUT=$(echo "rename -path=/a.txt -name=b.txt" | "$BIN" 2>&1)
assert_contains "2. RENAME exige sesion" "$OUT" '\[ERROR\] RENAME: necesita iniciar sesion'

D1="$TEST_DIR/remove_basic.mia"
OUT=$(run_multi "mkdisk -size=10 -unit=M -path=$D1
fdisk -size=6 -unit=M -path=$D1 -name=P1
mount -path=$D1 -name=P1
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkdir -p -path=/home/docs
mkfile -path=/home/docs/a.txt -size=80")
assert_contains "3. preparacion REMOVE basico" "$OUT" '\[OK\] MKFILE'
BEFORE=$($INSPECT inspect "$D1" P1)
FREE_I_BEFORE=$(kv "$BEFORE" free_inodes)
FREE_B_BEFORE=$(kv "$BEFORE" free_blocks)

OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
remove -path=/home/docs/a.txt
cat -file1=/home/docs/a.txt")
assert_contains "4. REMOVE elimina archivo" "$OUT" '\[OK\] REMOVE: eliminado /home/docs/a.txt'
assert_contains "5. archivo eliminado deja de resolverse" "$OUT" 'CAT: /home/docs/a.txt: no existe la ruta solicitada'
AFTER=$($INSPECT inspect "$D1" P1)
FREE_I_AFTER=$(kv "$AFTER" free_inodes)
FREE_B_AFTER=$(kv "$AFTER" free_blocks)
if [ "$FREE_I_AFTER" -gt "$FREE_I_BEFORE" ]; then pass "6. REMOVE libera inodo"; else fail "6. REMOVE libera inodo" "$FREE_I_BEFORE -> $FREE_I_AFTER"; fi
if [ "$FREE_B_AFTER" -gt "$FREE_B_BEFORE" ]; then pass "7. REMOVE libera bloques"; else fail "7. REMOVE libera bloques" "$FREE_B_BEFORE -> $FREE_B_AFTER"; fi

OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
remove -path=/no-existe")
assert_contains "8. REMOVE valida ruta inexistente" "$OUT" 'no existe el archivo o carpeta indicado'
OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
remove -path=/")
assert_contains "9. REMOVE protege la raiz" "$OUT" 'no se puede eliminar la carpeta raiz'
OUT=$(run_multi "mount -path=$D1 -name=P1
login -user=root -pass=123 -id=441A
remove")
assert_contains "10. REMOVE exige -path" "$OUT" 'falta parametro -path'
OUT=$(echo "remove -path=/a -foo=1" | "$BIN" 2>&1)
assert_contains "11. scanner rechaza parametro extra en REMOVE" "$OUT" 'parametro no reconocido -foo'

# ---------------------------------------------------------------------------
# 2) Eliminacion recursiva completa
# ---------------------------------------------------------------------------
D2="$TEST_DIR/remove_tree.mia"
OUT=$(run_multi "mkdisk -size=12 -unit=M -path=$D2
fdisk -size=8 -unit=M -path=$D2 -name=P2
mount -path=$D2 -name=P2
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkdir -p -path=/tree/a/b
mkfile -path=/tree/a/b/uno.txt -size=20
mkfile -path=/tree/a/dos.txt -size=30
remove -path=/tree
cat -file1=/tree/a/b/uno.txt")
assert_contains "12. REMOVE elimina carpeta recursivamente" "$OUT" '\[OK\] REMOVE: eliminado /tree'
assert_contains "13. contenido recursivo deja de existir" "$OUT" 'CAT: /tree/a/b/uno.txt: no existe la ruta solicitada'

# ---------------------------------------------------------------------------
# 3) Permisos: conserva padres cuando queda un hijo no eliminable
#    El ejemplo del enunciado elimina el hijo escribible, conserva b.txt y los padres.
# ---------------------------------------------------------------------------
D3="$TEST_DIR/remove_perms.mia"
OUT=$(run_multi "mkdisk -size=14 -unit=M -path=$D3
fdisk -size=9 -unit=M -path=$D3 -name=P3
mount -path=$D3 -name=P3
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkgrp -name=devs
mkusr -user=daniel -pass=123 -grp=devs
mkdir -p -path=/home/user/docs
mkfile -path=/home/user/docs/a.txt -size=5
mkfile -path=/home/user/docs/b.txt -size=6
mkfile -path=/locked.txt -size=7
chown -path=/home/user -user=daniel -r
chown -path=/locked.txt -user=daniel
chmod -path=/home/user/docs/b.txt -ugo=444
chmod -path=/locked.txt -ugo=444
logout
login -user=daniel -pass=123 -id=441A
remove -path=/home/user
cat -file1=/home/user/docs/a.txt
cat -file1=/home/user/docs/b.txt
remove -path=/locked.txt
cat -file1=/locked.txt")
assert_contains "14. REMOVE reporta eliminacion parcial por permisos" "$OUT" 'no se pudo eliminar completamente /home/user'
assert_contains "15. hijo con escritura se elimina" "$OUT" 'CAT: /home/user/docs/a.txt: no existe la ruta solicitada'
assert_contains "16. hijo sin escritura se conserva" "$OUT" '012345'
assert_contains "17. archivo objetivo sin escritura no se elimina" "$OUT" 'no tiene permiso de escritura sobre /locked.txt'
assert_contains "18. archivo bloqueado sigue disponible" "$OUT" '0123456'

OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=daniel -pass=123 -id=441A
chmod -path=/home/user/docs/b.txt -ugo=664
remove -path=/home/user
cat -file1=/home/user/docs/b.txt")
assert_contains "19. al recuperar permiso se eliminan padres restantes" "$OUT" '\[OK\] REMOVE: eliminado /home/user'
assert_contains "20. arbol restante desaparece" "$OUT" 'CAT: /home/user/docs/b.txt: no existe la ruta solicitada'

# ---------------------------------------------------------------------------
# 4) RENAME de archivos y carpetas
# ---------------------------------------------------------------------------
D4="$TEST_DIR/rename.mia"
OUT=$(run_multi "mkdisk -size=14 -unit=M -path=$D4
fdisk -size=9 -unit=M -path=$D4 -name=P4
mount -path=$D4 -name=P4
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkdir -p -path=/home/old/sub
mkfile -path=/home/a.txt -size=15
mkfile -path=/home/old/sub/data.txt -size=12
rename -path=/home/a.txt -name=b.txt
cat -file1=/home/b.txt
cat -file1=/home/a.txt
rename -path=/home/old -name=nuevo
cat -file1=/home/nuevo/sub/data.txt
cat -file1=/home/old/sub/data.txt")
assert_contains "21. RENAME cambia nombre de archivo" "$OUT" '\[OK\] RENAME: /home/a.txt renombrado a b.txt'
assert_contains "22. contenido se conserva tras renombrar archivo" "$OUT" '012345678901234'
assert_contains "23. nombre anterior deja de existir" "$OUT" 'CAT: /home/a.txt: no existe la ruta solicitada'
assert_contains "24. RENAME cambia nombre de carpeta" "$OUT" '\[OK\] RENAME: /home/old renombrado a nuevo'
assert_contains "25. contenido interno se conserva al renombrar carpeta" "$OUT" '012345678901'
assert_contains "26. ruta vieja de carpeta deja de existir" "$OUT" 'CAT: /home/old/sub/data.txt: no existe la ruta solicitada'

OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
mkfile -path=/home/c.txt -size=3
mkfile -path=/home/d.txt -size=4
rename -path=/home/c.txt -name=d.txt
cat -file1=/home/c.txt")
assert_contains "27. RENAME rechaza nombre duplicado" "$OUT" 'ya existe un archivo o carpeta con el nombre d.txt'
assert_contains "28. duplicado no altera el origen" "$OUT" '012'

OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
rename -path=/home/no.txt -name=x.txt")
assert_contains "29. RENAME valida ruta inexistente" "$OUT" 'no existe el archivo o carpeta indicado'
OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
rename -path=/ -name=raiz")
assert_contains "30. RENAME protege la raiz" "$OUT" 'no se puede renombrar la carpeta raiz'
OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
rename -path=/home/b.txt -name=nombre_demasiado_largo")
assert_contains "31. RENAME valida maximo 12 caracteres" "$OUT" 'maximo 12 caracteres'
OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
rename -path=/home/b.txt -name=x/y")
assert_contains "32. RENAME rechaza slash en -name" "$OUT" 'no debe contener /'
OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
rename -path=/home/b.txt -name=.")
assert_contains "33. RENAME rechaza . y .." "$OUT" 'no puede ser . ni ..'
OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
rename -path=/home/b.txt")
assert_contains "34. RENAME exige -name" "$OUT" 'requiere -path y -name'
OUT=$(echo "rename -path=/a -name=b -foo=1" | "$BIN" 2>&1)
assert_contains "35. scanner rechaza parametro extra en RENAME" "$OUT" 'parametro no reconocido -foo'

# Nombres con espacios deben funcionar cuando se encierran entre comillas.
OUT=$(run_multi "mount -path=$D4 -name=P4
login -user=root -pass=123 -id=441A
mkfile -path=/home/q.txt -size=9
rename -path=/home/q.txt -name=\"mis datos\"
cat -file1=\"/home/mis datos\"")
assert_contains "36. RENAME acepta nombre con espacios entre comillas" "$OUT" 'renombrado a mis datos'
assert_contains "37. archivo con nuevo nombre espaciado conserva contenido" "$OUT" '012345678'

# ---------------------------------------------------------------------------
# 5) RENAME respeta permiso de escritura del objeto
# ---------------------------------------------------------------------------
D5="$TEST_DIR/rename_perms.mia"
OUT=$(run_multi "mkdisk -size=10 -unit=M -path=$D5
fdisk -size=6 -unit=M -path=$D5 -name=P5
mount -path=$D5 -name=P5
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkgrp -name=devs
mkusr -user=daniel -pass=123 -grp=devs
mkfile -path=/locked.txt -size=11
chown -path=/locked.txt -user=daniel
chmod -path=/locked.txt -ugo=444
logout
login -user=daniel -pass=123 -id=441A
rename -path=/locked.txt -name=open.txt
cat -file1=/locked.txt")
assert_contains "38. RENAME niega objeto sin permiso de escritura" "$OUT" 'no tiene permiso de escritura sobre /locked.txt'
assert_contains "39. RENAME fallido conserva nombre y contenido" "$OUT" '01234567890'

# ---------------------------------------------------------------------------
# 6) EXT3: journaling de REMOVE y RENAME
# ---------------------------------------------------------------------------
D6="$TEST_DIR/ext3_journal.mia"
OUT=$(run_multi "mkdisk -size=12 -unit=M -path=$D6
fdisk -size=7 -unit=M -path=$D6 -name=P6
mount -path=$D6 -name=P6
mkfs -id=441A -fs=3fs
login -user=root -pass=123 -id=441A
mkdir -p -path=/docs
mkfile -path=/docs/a.txt -size=8
rename -path=/docs/a.txt -name=b.txt
remove -path=/docs/b.txt
journaling -id=441A")
assert_contains "40. EXT3 journal registra RENAME" "$OUT" 'rename \| /docs/a.txt \| b.txt'
assert_contains "41. EXT3 journal registra REMOVE" "$OUT" 'remove \| /docs/b.txt \| complete'

# ---------------------------------------------------------------------------
# 7) REMOVE libera tambien apuntadores indirectos de un archivo grande
# ---------------------------------------------------------------------------
D7="$TEST_DIR/remove_big.mia"
run_multi "mkdisk -size=12 -unit=M -path=$D7
fdisk -size=8 -unit=M -path=$D7 -name=P7
mount -path=$D7 -name=P7
mkfs -id=441A" >/dev/null
BASE=$($INSPECT inspect "$D7" P7)
BASE_I=$(kv "$BASE" free_inodes)
BASE_B=$(kv "$BASE" free_blocks)
run_multi "mount -path=$D7 -name=P7
login -user=root -pass=123 -id=441A
mkfile -path=/grande.txt -size=2500" >/dev/null
USED=$($INSPECT inspect "$D7" P7)
USED_B=$(kv "$USED" free_blocks)
if [ "$USED_B" -lt "$BASE_B" ]; then pass "42. archivo grande consume bloques directos/indirectos"; else fail "42. archivo grande consume bloques directos/indirectos"; fi
run_multi "mount -path=$D7 -name=P7
login -user=root -pass=123 -id=441A
remove -path=/grande.txt" >/dev/null
FINAL=$($INSPECT inspect "$D7" P7)
assert_eq "43. REMOVE restaura inodo de archivo grande" "$(kv "$FINAL" free_inodes)" "$BASE_I"
assert_eq "44. REMOVE restaura todos los bloques de archivo grande" "$(kv "$FINAL" free_blocks)" "$BASE_B"

# Las estructuras binarias base no deben cambiar.
SZ=$($INSPECT sizes)
assert_eq "45. Superblock conserva 80 bytes" "$(kv "$SZ" SB)" "80"
assert_eq "46. Inode conserva 104 bytes" "$(kv "$SZ" INODE)" "104"
assert_eq "47. bloque conserva 64 bytes" "$(kv "$SZ" BLOCK)" "64"

echo "============================================"
echo " Resultados: $PASS pasaron, $FAIL fallaron"
if [ "$FAIL" -eq 0 ]; then
  echo " Todas las pruebas de Proyecto 2 Sprint 3 pasaron"
  exit 0
fi
echo " Hay pruebas fallidas en Proyecto 2 Sprint 3"
exit 1
