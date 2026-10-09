#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
INSPECT="backend/build/test_sprint2"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_p2_sprint2_$$"
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
echo " Proyecto 2 - Sprint 2: EXT3 + Journaling"
echo "============================================"

SZ=$($INSPECT sizes)
SB_SIZE=$(kv "$SZ" SB)
INODE_SIZE=$(kv "$SZ" INODE)
BLOCK_SIZE=$(kv "$SZ" BLOCK)
JOURNAL_SIZE=$(kv "$SZ" JOURNAL)
assert_eq "1. Superblock conserva 80 bytes" "$SB_SIZE" "80"
assert_eq "2. Inode conserva 104 bytes" "$INODE_SIZE" "104"
assert_eq "3. bloques conservan 64 bytes" "$BLOCK_SIZE" "64"
if [ -n "$JOURNAL_SIZE" ] && [ "$JOURNAL_SIZE" -gt 0 ] 2>/dev/null; then pass "4. estructura Journal existe"; else fail "4. estructura Journal existe" "$JOURNAL_SIZE"; fi

# ---------------------------------------------------------------------------
# 1) MKFS por defecto y -fs=2fs deben conservar EXT2
# ---------------------------------------------------------------------------
D2="$TEST_DIR/ext2.mia"
OUT=$(run_multi "mkdisk -size=8 -unit=M -path=$D2
fdisk -size=3 -unit=M -path=$D2 -name=P2
mount -path=$D2 -name=P2
mkfs -id=441A")
assert_contains "5. MKFS por defecto sigue siendo EXT2" "$OUT" 'formateada como EXT2'
I2=$($INSPECT inspect "$D2" P2)
assert_eq "6. superbloque por defecto guarda fs_type=2" "$(kv "$I2" fs_type)" "2"

D2B="$TEST_DIR/ext2_explicit.mia"
OUT=$(run_multi "mkdisk -size=8 -unit=M -path=$D2B
fdisk -size=3 -unit=M -path=$D2B -name=P2B
mount -path=$D2B -name=P2B
mkfs -id=441A -fs=2fs")
assert_contains "7. MKFS acepta -fs=2fs" "$OUT" 'formateada como EXT2'
I2B=$($INSPECT inspect "$D2B" P2B)
assert_eq "8. -fs=2fs guarda tipo 2" "$(kv "$I2B" fs_type)" "2"

OUT=$(run_multi "mkdisk -size=5 -unit=M -path=$TEST_DIR/badfs.mia
fdisk -size=2 -unit=M -path=$TEST_DIR/badfs.mia -name=P
mount -path=$TEST_DIR/badfs.mia -name=P
mkfs -id=441A -fs=4fs")
assert_contains "9. MKFS rechaza fs desconocido" "$OUT" '\[ERROR\] MKFS: -fs solo admite 2fs o 3fs'

# ---------------------------------------------------------------------------
# 2) Layout EXT3: SB -> journal -> bm_inode -> bm_block -> inodos -> bloques
# ---------------------------------------------------------------------------
D3="$TEST_DIR/ext3.mia"
OUT=$(run_multi "mkdisk -size=12 -unit=M -path=$D3
fdisk -size=6 -unit=M -path=$D3 -name=P3
mount -path=$D3 -name=P3
mkfs -id=441A -fs=3fs")
assert_contains "10. MKFS crea EXT3" "$OUT" '\[OK\] MKFS:.*EXT3'
I3=$($INSPECT inspect "$D3" P3)
assert_eq "11. superbloque EXT3 guarda fs_type=3" "$(kv "$I3" fs_type)" "3"
assert_eq "12. magic EXT3 sigue siendo EF53" "$(kv "$I3" magic)" "61267"

PART_START=$(kv "$I3" part_start)
PART_S=$(kv "$I3" part_s)
N=$(( (PART_S - SB_SIZE) / (JOURNAL_SIZE + 1 + 3 + INODE_SIZE + 3 * BLOCK_SIZE) ))
assert_eq "13. numero de inodos usa formula EXT3" "$(kv "$I3" inodes_count)" "$N"
assert_eq "14. bloques EXT3 = 3*n" "$(kv "$I3" blocks_count)" "$((3*N))"
BMI=$((PART_START + SB_SIZE + N*JOURNAL_SIZE))
BMB=$((BMI + N))
IST=$((BMB + 3*N))
BST=$((IST + N*INODE_SIZE))
assert_eq "15. bm_inode inicia despues del journal" "$(kv "$I3" bm_inode_start)" "$BMI"
assert_eq "16. bm_block EXT3 correcto" "$(kv "$I3" bm_block_start)" "$BMB"
assert_eq "17. tabla de inodos EXT3 correcta" "$(kv "$I3" inode_start)" "$IST"
assert_eq "18. area de bloques EXT3 correcta" "$(kv "$I3" block_start)" "$BST"
assert_contains "19. bitmap de inodos EXT3 inicia con root/users" "$(kv "$I3" bm_inode_prefix)" '^110'
assert_contains "20. bitmap de bloques EXT3 inicia con root/users" "$(kv "$I3" bm_block_prefix)" '^110'

# ---------------------------------------------------------------------------
# 3) Journaling inicial y operaciones existentes sobre EXT3
# ---------------------------------------------------------------------------
OUT=$(run_multi "mount -path=$D3 -name=P3
journaling -id=441A")
assert_contains "21. JOURNALING funciona sobre EXT3" "$OUT" '\[OK\] JOURNALING:.*EXT3'
assert_contains "22. MKFS queda registrado en journal" "$OUT" '1 \| mkfs \| / \| 3fs'
assert_contains "23. JOURNALING muestra fecha y hora" "$OUT" '[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}'

OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=root -pass=123 -id=441A
mkdir -p -path=/home/docs
mkfile -path=/home/docs/a.txt -size=25
mkgrp -name=devs
mkusr -user=daniel -pass=secreto -grp=devs
chmod -path=/home/docs/a.txt -ugo=600
chown -path=/home/docs/a.txt -user=daniel
journaling -id=441A")
assert_contains "24. LOGIN funciona sobre EXT3" "$OUT" '\[OK\] LOGIN'
assert_contains "25. MKDIR funciona sobre EXT3" "$OUT" '\[OK\] MKDIR'
assert_contains "26. MKFILE funciona sobre EXT3" "$OUT" '\[OK\] MKFILE'
assert_contains "27. journal registra MKDIR" "$OUT" 'mkdir \| /home/docs'
assert_contains "28. journal registra MKFILE" "$OUT" 'mkfile \| /home/docs/a.txt'
assert_contains "29. journal registra MKGRP" "$OUT" 'mkgrp \| /users.txt \| devs'
assert_contains "30. journal registra MKUSR sin password" "$OUT" 'mkusr \| /users.txt \| user=daniel,grp=devs'
assert_not_contains "31. journal no expone password de MKUSR" "$OUT" 'secreto'
assert_contains "32. journal registra CHMOD" "$OUT" 'chmod \| /home/docs/a.txt \| 600'
assert_contains "33. journal registra CHOWN" "$OUT" 'chown \| /home/docs/a.txt \| daniel'
assert_contains "34. contador reporta varias transacciones" "$OUT" '\[INFO\] JOURNALING: [7-9][0-9]* transacciones registradas|\[INFO\] JOURNALING: [7-9] transacciones registradas'

# CAT debe poder leer archivos en EXT3
OUT=$(run_multi "mount -path=$D3 -name=P3
login -user=daniel -pass=secreto -id=441A
cat -file1=/home/docs/a.txt")
assert_contains "35. CAT funciona sobre EXT3" "$OUT" '0123456789012345678901234'

# El journal debe persistir tras desmontar/montar.
OUT=$(run_multi "mount -path=$D3 -name=P3
unmount -id=441A
mount -path=$D3 -name=P3
journaling -id=441A")
assert_contains "36. journal persiste tras UNMOUNT/MOUNT" "$OUT" 'mkfile \| /home/docs/a.txt'

# ---------------------------------------------------------------------------
# 4) Validaciones de JOURNALING y compatibilidad de reportes
# ---------------------------------------------------------------------------
OUT=$(run_multi "mount -path=$D2 -name=P2
journaling -id=441A")
assert_contains "37. JOURNALING rechaza EXT2" "$OUT" '\[ERROR\] JOURNALING: la particion no utiliza EXT3'
OUT=$(echo "journaling -id=999Z" | "$BIN" 2>&1)
assert_contains "38. JOURNALING valida id montado" "$OUT" '\[ERROR\] JOURNALING'
OUT=$(echo "journaling" | "$BIN" 2>&1)
assert_contains "39. JOURNALING exige id" "$OUT" 'falta parametro -id'
OUT=$(echo "journaling -id=441A -foo=bar" | "$BIN" 2>&1)
assert_contains "40. scanner rechaza parametro extra en JOURNALING" "$OUT" 'parametro no reconocido -foo'

REPORT="$TEST_DIR/sb_ext3.png"
OUT=$(run_multi "mount -path=$D3 -name=P3
rep -name=sb -path=$REPORT -id=441A")
assert_contains "41. REP SB acepta EXT3" "$OUT" '\[OK\] REP'
if [ -s "$REPORT" ]; then pass "42. reporte SB EXT3 genera archivo"; else fail "42. reporte SB EXT3 genera archivo"; fi

# Re-formatear EXT3 debe limpiar journal anterior y crear solo la nueva entrada MKFS.
OUT=$(run_multi "mount -path=$D3 -name=P3
mkfs -id=441A -fs=3fs
journaling -id=441A")
assert_contains "43. reformateo EXT3 exitoso" "$OUT" 'formateada como EXT3'
assert_contains "44. reformateo reinicia journal" "$OUT" '\[INFO\] JOURNALING: 1 transacciones registradas'
assert_not_contains "45. journal anterior se elimina con formato full" "$OUT" 'mkfile \| /home/docs/a.txt'

# El archivo de disco no debe cambiar de tamano por EXT3.
SIZE_BEFORE=$(stat -c%s "$D3")
run_multi "mount -path=$D3 -name=P3
mkfs -id=441A -fs=3fs" >/dev/null
SIZE_AFTER=$(stat -c%s "$D3")
assert_eq "46. MKFS EXT3 no cambia tamano fisico del disco" "$SIZE_AFTER" "$SIZE_BEFORE"

echo "============================================"
echo " Resultados: $PASS pasaron, $FAIL fallaron"
if [ "$FAIL" -eq 0 ]; then
  echo " Todas las pruebas de Proyecto 2 Sprint 2 pasaron"
  exit 0
fi
echo " Hay pruebas fallidas en Proyecto 2 Sprint 2"
exit 1
