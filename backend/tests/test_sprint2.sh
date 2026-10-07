#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
TEST_BIN="backend/build/test_sprint2"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_sprint2_$$"
mkdir -p "$TEST_DIR"
trap 'rm -rf "$TEST_DIR"' EXIT

pass(){ PASS=$((PASS+1)); echo "  PASS: $1"; }
fail(){ FAIL=$((FAIL+1)); echo "  FAIL: $1${2:+ -> $2}"; }
assert_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then pass "$d"; else fail "$d" "$o"; fi; }
assert_eq(){ local d="$1" a="$2" e="$3"; if [ "$a" = "$e" ]; then pass "$d"; else fail "$d" "esperado=$e obtenido=$a"; fi; }
kv(){ echo "$1" | grep "^$2=" | head -1 | cut -d= -f2-; }
run_multi(){ printf "%s\n" "$1" | "$BIN" 2>&1; }

if [ ! -x "$BIN" ] || [ ! -x "$TEST_BIN" ]; then
  echo "ERROR: primero ejecute: cmake -S backend -B backend/build && cmake --build backend/build"
  exit 1
fi

echo "============================================"
echo " Sprint 2 - EXT2 y usuarios"
echo "============================================"

DISK="$TEST_DIR/s2.mia"
CMDS="mkdisk -size=8 -unit=M -path=$DISK
fdisk -size=3 -unit=M -path=$DISK -name=PartS2
mount -path=$DISK -name=PartS2
mkfs -id=441A"
OUT=$(run_multi "$CMDS")
assert_contains "1. MKFS exitoso" "$OUT" "\\[OK\\] MKFS"

INS=$($TEST_BIN inspect "$DISK" PartS2)
SZ=$($TEST_BIN sizes)
SB_SIZE=$(kv "$SZ" SB)
INODE_SIZE=$(kv "$SZ" INODE)
BLOCK_SIZE=$(kv "$SZ" BLOCK)
PART_START=$(kv "$INS" part_start)
PART_S=$(kv "$INS" part_s)
N=$(( (PART_S - SB_SIZE) / (1 + 3 + INODE_SIZE + 3 * BLOCK_SIZE) ))

assert_eq "2. filesystem type EXT2" "$(kv "$INS" fs_type)" "2"
assert_eq "3. magic 0xEF53" "$(kv "$INS" magic)" "61267"
assert_eq "4. numero de inodos calculado" "$(kv "$INS" inodes_count)" "$N"
assert_eq "5. bloques = 3*n" "$(kv "$INS" blocks_count)" "$((3*N))"
assert_eq "6. inodos libres = n-2" "$(kv "$INS" free_inodes)" "$((N-2))"
assert_eq "7. bloques libres = 3n-2" "$(kv "$INS" free_blocks)" "$((3*N-2))"
assert_eq "8. s_inode_s correcto" "$(kv "$INS" inode_s)" "$INODE_SIZE"
assert_eq "9. s_block_s = 64" "$(kv "$INS" block_s)" "64"

BMI=$((PART_START + SB_SIZE))
BMB=$((BMI + N))
IST=$((BMB + 3*N))
BST=$((IST + N*INODE_SIZE))
assert_eq "10. bm inode offset" "$(kv "$INS" bm_inode_start)" "$BMI"
assert_eq "11. bm block offset" "$(kv "$INS" bm_block_start)" "$BMB"
assert_eq "12. inode table offset" "$(kv "$INS" inode_start)" "$IST"
assert_eq "13. block area offset" "$(kv "$INS" block_start)" "$BST"
assert_eq "14. primer inodo libre es direccion del inodo 2" "$(kv "$INS" first_ino)" "$((IST + 2*INODE_SIZE))"
assert_eq "15. primer bloque libre es direccion del bloque 2" "$(kv "$INS" first_blo)" "$((BST + 2*64))"
assert_contains "16. bitmap inodos inicia 110" "$(kv "$INS" bm_inode_prefix)" '^110'
assert_contains "17. bitmap bloques inicia 110" "$(kv "$INS" bm_block_prefix)" '^110'

assert_eq "18. root uid" "$(kv "$INS" root_uid)" "1"
assert_eq "19. root gid" "$(kv "$INS" root_gid)" "1"
assert_eq "20. root es carpeta" "$(kv "$INS" root_type)" "0"
assert_eq "21. root permisos 664" "$(kv "$INS" root_perm)" "664"
assert_eq "22. root apunta bloque 0" "$(kv "$INS" root_block0)" "0"
assert_eq "23. users es archivo" "$(kv "$INS" users_type)" "1"
assert_eq "24. users permisos 664" "$(kv "$INS" users_perm)" "664"
assert_eq "25. users apunta bloque 1" "$(kv "$INS" users_block0)" "1"
assert_eq "26. entrada ." "$(kv "$INS" root_entry0_name)" "."
assert_eq "27. entrada .." "$(kv "$INS" root_entry1_name)" ".."
assert_eq "28. entrada users.txt" "$(kv "$INS" root_entry2_name)" "users.txt"
assert_eq "29. users.txt apunta inodo 1" "$(kv "$INS" root_entry2_inode)" "1"

USERS=$($TEST_BIN users "$DISK" PartS2)
assert_contains "30. users.txt tiene grupo root" "$USERS" '^1,G,root$'
assert_contains "31. users.txt tiene usuario root" "$USERS" '^1,U,root,root,123$'

# MKFS errores
OUT=$(echo "mkfs -id=999Z" | "$BIN" 2>&1)
assert_contains "32. mkfs id no montado" "$OUT" '\[ERROR\] MKFS'
OUT=$(run_multi "mount -path=$DISK -name=PartS2
mkfs -id=441A -type=fast")
assert_contains "33. mkfs rechaza type distinto de full" "$OUT" '\[ERROR\] MKFS.*type'

# El archivo .mia no debe crecer con MKFS
SIZE_BEFORE=$(stat -c%s "$DISK")
run_multi "mount -path=$DISK -name=PartS2
mkfs -id=441A" >/dev/null
SIZE_AFTER=$(stat -c%s "$DISK")
assert_eq "34. MKFS no cambia tamano del disco" "$SIZE_AFTER" "$SIZE_BEFORE"

# LOGIN / LOGOUT
OUT=$(run_multi "mount -path=$DISK -name=PartS2
login -user=root -pass=123 -id=441A
login -user=root -pass=123 -id=441A
logout
logout")
assert_contains "35. login root correcto" "$OUT" '\[OK\] LOGIN'
assert_contains "36. no permite doble login" "$OUT" 'ya existe una sesion activa'
assert_contains "37. logout correcto" "$OUT" '\[OK\] LOGOUT'
assert_contains "38. logout sin sesion da error" "$OUT" 'no hay una sesion activa'
OUT=$(run_multi "mount -path=$DISK -name=PartS2
login -user=root -pass=XXX -id=441A")
assert_contains "39. password incorrecto" "$OUT" 'autenticacion fallida'

# Comandos de usuarios sin sesion
OUT=$(echo "mkgrp -name=devs" | "$BIN" 2>&1)
assert_contains "40. MKGRP requiere sesion" "$OUT" 'necesita iniciar sesion'

# Flujo de administracion de usuarios/grupos
OUT=$(run_multi "mount -path=$DISK -name=PartS2
login -user=root -pass=123 -id=441A
mkgrp -name=devs
mkgrp -name=ops
mkgrp -name=devs
mkusr -user=Daniel -pass=Clave123 -grp=devs
mkusr -user=Daniel -pass=Otra -grp=ops
chgrp -user=Daniel -grp=ops
logout
login -user=Daniel -pass=Clave123 -id=441A
mkgrp -name=nope
logout
login -user=root -pass=123 -id=441A
rmusr -user=Daniel
rmgrp -name=devs")
assert_contains "41. crear grupo devs" "$OUT" '\[OK\] MKGRP: grupo devs'
assert_contains "42. crear grupo ops" "$OUT" '\[OK\] MKGRP: grupo ops'
assert_contains "43. grupo duplicado falla" "$OUT" 'grupo ya existe'
assert_contains "44. crear usuario" "$OUT" '\[OK\] MKUSR: usuario Daniel'
assert_contains "45. usuario duplicado falla" "$OUT" 'usuario ya existe'
assert_contains "46. CHGRP funciona" "$OUT" '\[OK\] CHGRP'
assert_contains "47. login distingue usuario/password y funciona" "$OUT" 'sesion iniciada como Daniel'
assert_contains "48. no-root no puede MKGRP" "$OUT" 'solo el usuario root'
assert_contains "49. RMUSR funciona" "$OUT" '\[OK\] RMUSR'
assert_contains "50. RMGRP funciona" "$OUT" '\[OK\] RMGRP'

USERS2=$($TEST_BIN users "$DISK" PartS2)
assert_contains "51. grupo devs queda eliminado con id 0" "$USERS2" '^0,G,devs$'
assert_contains "52. grupo ops permanece" "$USERS2" '^[0-9]+,G,ops$'
assert_contains "53. usuario Daniel queda eliminado" "$USERS2" '^0,U,ops,Daniel,Clave123$'

# Case sensitive: usuario eliminado ya no debe permitir login
OUT=$(run_multi "mount -path=$DISK -name=PartS2
login -user=daniel -pass=Clave123 -id=441A")
assert_contains "54. LOGIN distingue mayusculas/minusculas" "$OUT" 'usuario no existe'

# Limites de 10 caracteres
OUT=$(run_multi "mount -path=$DISK -name=PartS2
login -user=root -pass=123 -id=441A
mkgrp -name=ABCDEFGHIJK
mkusr -user=ABCDEFGHIJK -pass=123 -grp=ops")
assert_contains "55. grupo maximo 10 caracteres" "$OUT" 'maximo 10'
assert_contains "56. usuario maximo 10 caracteres" "$OUT" 'maximo 10'

# Crecimiento de users.txt a varios bloques directos
LONG_CMDS="mount -path=$DISK -name=PartS2
login -user=root -pass=123 -id=441A"
for i in 1 2 3 4 5 6 7 8; do LONG_CMDS="$LONG_CMDS
mkusr -user=u$i -pass=p$i -grp=ops"; done
OUT=$(run_multi "$LONG_CMDS")
assert_contains "57. users.txt puede crecer a varios bloques" "$OUT" '\[OK\] MKUSR: usuario u8'
INS2=$($TEST_BIN inspect "$DISK" PartS2)
USIZE=$(kv "$INS2" users_size)
if [ "$USIZE" -gt 64 ]; then pass "58. users.txt supera un bloque de 64 bytes"; else fail "58. users.txt supera un bloque de 64 bytes" "size=$USIZE"; fi

# Forzar el uso del apuntador simple (mas de 12 bloques de 64 bytes).
INDIRECT_CMDS="mount -path=$DISK -name=PartS2
login -user=root -pass=123 -id=441A"
for i in $(seq 1 30); do
  U=$(printf "u%09d" "$i")
  P=$(printf "p%09d" "$i")
  INDIRECT_CMDS="$INDIRECT_CMDS
mkusr -user=$U -pass=$P -grp=ops"
done
OUT=$(run_multi "$INDIRECT_CMDS")
assert_contains "59. escritura de users.txt supera 12 bloques directos" "$OUT" '\[OK\] MKUSR: usuario u000000030'
INS_IND=$($TEST_BIN inspect "$DISK" PartS2)
USIZE_IND=$(kv "$INS_IND" users_size)
UB12=$(kv "$INS_IND" users_block12)
if [ "$USIZE_IND" -gt 768 ] && [ "$UB12" -ge 0 ]; then
  pass "60. users.txt usa apuntador simple al exceder 768 bytes"
else
  fail "60. users.txt usa apuntador simple al exceder 768 bytes" "size=$USIZE_IND i_block12=$UB12"
fi
USERS_IND=$($TEST_BIN users "$DISK" PartS2)
assert_contains "61. lectura mediante apuntador simple conserva ultimo usuario" "$USERS_IND" '^.*u000000030.*$'

# Continuar creciendo para forzar el apuntador doble (> 12 + 16 bloques de datos).
DOUBLE_CMDS="mount -path=$DISK -name=PartS2
login -user=root -pass=123 -id=441A"
for i in $(seq 31 70); do
  U=$(printf "u%09d" "$i")
  P=$(printf "p%09d" "$i")
  DOUBLE_CMDS="$DOUBLE_CMDS
mkusr -user=$U -pass=$P -grp=ops"
done
OUT=$(run_multi "$DOUBLE_CMDS")
assert_contains "62. escritura de users.txt supera capacidad directa+simple" "$OUT" '\[OK\] MKUSR: usuario u000000070'
INS_DBL=$($TEST_BIN inspect "$DISK" PartS2)
USIZE_DBL=$(kv "$INS_DBL" users_size)
UB13=$(kv "$INS_DBL" users_block13)
if [ "$USIZE_DBL" -gt 1792 ] && [ "$UB13" -ge 0 ]; then
  pass "63. users.txt usa apuntador doble al exceder 28 bloques de datos"
else
  fail "63. users.txt usa apuntador doble al exceder 28 bloques de datos" "size=$USIZE_DBL i_block13=$UB13"
fi
USERS_DBL=$($TEST_BIN users "$DISK" PartS2)
assert_contains "64. lectura mediante apuntador doble conserva ultimo usuario" "$USERS_DBL" '^.*u000000070.*$'

# Re-mount de EXT2 debe incrementar contador de montajes
MNT_BEFORE=$(kv "$INS_DBL" mnt_count)
run_multi "mount -path=$DISK -name=PartS2" >/dev/null
INS3=$($TEST_BIN inspect "$DISK" PartS2)
MNT_AFTER=$(kv "$INS3" mnt_count)
if [ "$MNT_AFTER" -gt "$MNT_BEFORE" ]; then pass "65. remount actualiza s_mnt_count"; else fail "65. remount actualiza s_mnt_count" "$MNT_BEFORE->$MNT_AFTER"; fi

echo "============================================"
TOTAL=$((PASS+FAIL))
echo " Resultados: $PASS/$TOTAL pasaron"
if [ "$FAIL" -gt 0 ]; then
  echo " $FAIL pruebas FALLARON"
  exit 1
fi
echo " Todas las pruebas pasaron"
