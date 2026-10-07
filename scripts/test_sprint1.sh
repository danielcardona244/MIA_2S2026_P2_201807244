#!/bin/bash
# ============================================================
# Pruebas Sprint 1 - ExtreamFS
# ============================================================
# Ejecutar desde la raiz del proyecto:
#   bash scripts/test_sprint1.sh
# ============================================================

BIN="backend/build/extreamfs"
TEST_BIN="backend/build/test_structs"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_test_$$"

cleanup() {
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

mkdir -p "$TEST_DIR"

# ejecuta un comando y limpia el prompt/header de la salida
run_cmd() {
    echo "$1" | $BIN 2>&1 | sed 's/^>> //g' | grep -v "^ExtreamFS" | grep -v "^Ingrese comandos" | grep -v "^Saliendo" | grep -v "^[[:space:]]*$"
}

run_multi() {
    printf "%s\n" "$1" | $BIN 2>&1 | sed 's/^>> //g' | grep -v "^ExtreamFS" | grep -v "^Ingrese comandos" | grep -v "^Saliendo" | grep -v "^[[:space:]]*$"
}

assert_contains() {
    local desc="$1"
    local output="$2"
    local expected="$3"

    if echo "$output" | grep -qE "$expected"; then
        PASS=$((PASS + 1))
        echo "  PASS: $desc"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $desc"
        echo "    esperado regex: $expected"
        echo "    salida: $(echo "$output" | head -3)"
    fi
}

assert_file_size() {
    local desc="$1"
    local filepath="$2"
    local expected_size="$3"

    if [ ! -f "$filepath" ]; then
        FAIL=$((FAIL + 1))
        echo "  FAIL: $desc (archivo no existe: $filepath)"
        return
    fi
    local actual_size
    actual_size=$(stat -c%s "$filepath" 2>/dev/null)
    if [ "$actual_size" = "$expected_size" ]; then
        PASS=$((PASS + 1))
        echo "  PASS: $desc"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $desc (esperado=$expected_size, actual=$actual_size)"
    fi
}

assert_file_exists() {
    local desc="$1"
    local filepath="$2"
    if [ -f "$filepath" ]; then
        PASS=$((PASS + 1))
        echo "  PASS: $desc"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $desc (archivo no existe: $filepath)"
    fi
}

assert_file_not_exists() {
    local desc="$1"
    local filepath="$2"
    if [ ! -f "$filepath" ]; then
        PASS=$((PASS + 1))
        echo "  PASS: $desc"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $desc (archivo aun existe: $filepath)"
    fi
}

assert_equals() {
    local desc="$1"
    local actual="$2"
    local expected="$3"

    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
        echo "  PASS: $desc"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $desc"
        echo "    esperado: $expected"
        echo "    obtenido: $actual"
    fi
}

echo "============================================"
echo " Sprint 1 - Suite de pruebas"
echo "============================================"
echo ""

# ==================== MKDISK ====================
echo "--- MKDISK ---"

# 1. ruta absoluta
OUT=$(run_cmd "mkdisk -size=5 -unit=M -path=$TEST_DIR/disco1.mia")
assert_contains "1. mkdisk ruta absoluta" "$OUT" "\\[OK\\] MKDISK"
assert_file_size "1b. tamano exacto 5MB" "$TEST_DIR/disco1.mia" "5242880"

# 2. ruta relativa
PREV_DIR=$(pwd)
cd "$TEST_DIR"
OUT=$(echo "mkdisk -size=1 -unit=M -path=relativo.mia" | "$PREV_DIR/$BIN" 2>&1 | sed 's/^>> //g' | grep -v "^ExtreamFS" | grep -v "^Ingrese" | grep -v "^[[:space:]]*$")
cd "$PREV_DIR"
assert_contains "2. mkdisk ruta relativa" "$OUT" "\\[OK\\] MKDISK"
assert_file_exists "2b. archivo relativo creado" "$TEST_DIR/relativo.mia"

# 3. ruta con espacios
OUT=$(run_cmd "mkdisk -size=1 -unit=M -path=\"$TEST_DIR/mis discos/d1.mia\"")
assert_contains "3. mkdisk ruta con espacios" "$OUT" "\\[OK\\] MKDISK"
assert_file_exists "3b. archivo con espacios creado" "$TEST_DIR/mis discos/d1.mia"

# 4. extension incorrecta
OUT=$(run_cmd "mkdisk -size=1 -unit=M -path=$TEST_DIR/disco.dsk")
assert_contains "4. mkdisk extension incorrecta" "$OUT" "\\[ERROR\\]"

# 5. size 0
OUT=$(run_cmd "mkdisk -size=0 -path=$TEST_DIR/zero.mia")
assert_contains "5. mkdisk size 0" "$OUT" "\\[ERROR\\]"

# 5b. size negativo
OUT=$(run_cmd "mkdisk -size=-5 -path=$TEST_DIR/neg.mia")
assert_contains "5b. mkdisk size negativo" "$OUT" "\\[ERROR\\]"

# 6. size no numerico
OUT=$(run_cmd "mkdisk -size=abc -path=$TEST_DIR/bad.mia")
assert_contains "6a. mkdisk size texto" "$OUT" "\\[ERROR\\]"
OUT=$(run_cmd "mkdisk -size=10abc -path=$TEST_DIR/bad2.mia")
assert_contains "6b. mkdisk size parcial 10abc" "$OUT" "\\[ERROR\\]"

# 7. parametro desconocido
OUT=$(run_cmd "mkdisk -size=1 -path=$TEST_DIR/t7.mia -foo=bar")
assert_contains "7. mkdisk param desconocido" "$OUT" "\\[ERROR\\].*no reconocido"

# 8. unit invalida
OUT=$(run_cmd "mkdisk -size=1 -unit=G -path=$TEST_DIR/t8.mia")
assert_contains "8. mkdisk unit invalida" "$OUT" "\\[ERROR\\]"

# 9. fit invalido
OUT=$(run_cmd "mkdisk -size=1 -fit=XX -path=$TEST_DIR/t9.mia")
assert_contains "9. mkdisk fit invalido" "$OUT" "\\[ERROR\\]"

# 10. tamano exacto en K
OUT=$(run_cmd "mkdisk -size=500 -unit=K -path=$TEST_DIR/disco_k.mia")
assert_file_size "10. tamano exacto 500K" "$TEST_DIR/disco_k.mia" "512000"

# 10b. parametro sin guion
OUT=$(run_cmd "mkdisk size=10 -path=$TEST_DIR/t10b.mia")
assert_contains "10b. param sin guion" "$OUT" "\\[ERROR\\]"

# 10c. comillas sin cerrar
OUT=$(run_cmd "mkdisk -size=10 -path=\"$TEST_DIR/sin_cerrar.mia")
assert_contains "10c. comillas sin cerrar" "$OUT" "\\[ERROR\\].*comillas"

# 10d. param sin valor
OUT=$(run_cmd "mkdisk -size= -path=$TEST_DIR/t10d.mia")
assert_contains "10d. param sin valor -size=" "$OUT" "\\[ERROR\\]"

# 10e. key vacio -=10
OUT=$(run_cmd "mkdisk -=10 -path=$TEST_DIR/t10e.mia")
assert_contains "10e. param mal formado -=10" "$OUT" "\\[ERROR\\]"

echo ""

# ==================== RMDISK ====================
echo "--- RMDISK ---"

run_cmd "mkdisk -size=1 -unit=M -path=$TEST_DIR/borrable.mia" > /dev/null
OUT=$(run_cmd "rmdisk -path=$TEST_DIR/borrable.mia")
assert_contains "11. rmdisk correcto" "$OUT" "\\[OK\\] RMDISK"
assert_file_not_exists "11b. archivo eliminado" "$TEST_DIR/borrable.mia"

OUT=$(run_cmd "rmdisk -path=$TEST_DIR/noexiste.mia")
assert_contains "11c. rmdisk no existe" "$OUT" "\\[ERROR\\]"

OUT=$(run_cmd "rmdisk -path=$TEST_DIR/borrable.mia -size=10")
assert_contains "11d. rmdisk param desconocido" "$OUT" "\\[ERROR\\].*no reconocido"

OUT=$(run_cmd "rmdisk -path=$TEST_DIR/algo.dsk")
assert_contains "11e. rmdisk extension invalida" "$OUT" "\\[ERROR\\].*extension"

echo ""

# ==================== FDISK ====================
echo "--- FDISK ---"

# preparar disco de 5MB
run_cmd "mkdisk -size=5 -unit=M -path=$TEST_DIR/fdisk_test.mia" > /dev/null

# 12. primaria correcta
OUT=$(run_cmd "fdisk -size=500 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=Part1")
assert_contains "12. fdisk primaria" "$OUT" "\\[OK\\] FDISK"

# 13. extendida correcta
OUT=$(run_cmd "fdisk -type=E -size=1 -unit=M -path=$TEST_DIR/fdisk_test.mia -name=Ext1")
assert_contains "13. fdisk extendida" "$OUT" "\\[OK\\] FDISK"

# 14. primera logica
OUT=$(run_cmd "fdisk -type=L -size=200 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=Log1")
assert_contains "14. primera logica" "$OUT" "\\[OK\\] FDISK"

# 15. segunda logica enlazada
OUT=$(run_cmd "fdisk -type=L -size=200 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=Log2")
assert_contains "15. segunda logica enlazada" "$OUT" "\\[OK\\] FDISK"

# 16. prueba de borde: logica con espacio exacto sizeof(EBR)+data
# sizeof(EBR)=32 en este compilador (sin pragma pack)
# creamos extendida de 202 bytes y logica de 170 bytes: 32+170=202 justo
run_cmd "mkdisk -size=1 -unit=M -path=$TEST_DIR/borde.mia" > /dev/null
OUT=$(run_multi "fdisk -type=E -size=202 -unit=B -path=$TEST_DIR/borde.mia -name=ExtBorde
fdisk -type=L -size=170 -unit=B -path=$TEST_DIR/borde.mia -name=LogBorde")
assert_contains "16. logica espacio exacto" "$OUT" "\\[OK\\] FDISK.*LogBorde"

# 17. segunda extendida rechazada
OUT=$(run_cmd "fdisk -type=E -size=100 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=Ext2")
assert_contains "17. segunda extendida" "$OUT" "\\[ERROR\\].*extendida"

# 18. mas de 4 P/E
run_cmd "fdisk -size=500 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=Part2" > /dev/null
run_cmd "fdisk -size=500 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=Part3" > /dev/null
OUT=$(run_cmd "fdisk -size=100 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=Part5")
assert_contains "18. mas de 4 particiones" "$OUT" "\\[ERROR\\].*4 particion"

# 19. logica sin extendida
run_cmd "mkdisk -size=1 -unit=M -path=$TEST_DIR/noext.mia" > /dev/null
OUT=$(run_cmd "fdisk -type=L -size=100 -unit=K -path=$TEST_DIR/noext.mia -name=NoLog")
assert_contains "19. logica sin extendida" "$OUT" "\\[ERROR\\].*extendida"

# 20. nombre duplicado
OUT=$(run_cmd "fdisk -size=100 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=Part1")
assert_contains "20. nombre duplicado" "$OUT" "\\[ERROR\\].*nombre"

# 21. nombre demasiado largo (>16)
OUT=$(run_cmd "fdisk -size=100 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=EsteNombreEsMuyLargoo")
assert_contains "21. nombre demasiado largo" "$OUT" "\\[ERROR\\].*largo"

# 22. param desconocido
OUT=$(run_cmd "fdisk -size=100 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=X -delete=fast")
assert_contains "22. fdisk param desconocido" "$OUT" "\\[ERROR\\].*no reconocido"

# 23. sin espacio
run_cmd "mkdisk -size=1 -unit=K -path=$TEST_DIR/tiny.mia" > /dev/null
OUT=$(run_cmd "fdisk -size=900 -unit=B -path=$TEST_DIR/tiny.mia -name=Huge")
assert_contains "23. fdisk sin espacio" "$OUT" "\\[ERROR\\].*espacio"

# 30. tamano no cambia tras fdisk
FSIZE_BEFORE=$(stat -c%s "$TEST_DIR/fdisk_test.mia" 2>/dev/null)
run_cmd "fdisk -type=L -size=50 -unit=K -path=$TEST_DIR/fdisk_test.mia -name=Log3" > /dev/null
FSIZE_AFTER=$(stat -c%s "$TEST_DIR/fdisk_test.mia" 2>/dev/null)
if [ "$FSIZE_BEFORE" = "$FSIZE_AFTER" ]; then
    PASS=$((PASS + 1))
    echo "  PASS: 30. tamano .mia no cambia tras fdisk"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: 30. tamano cambio ($FSIZE_BEFORE -> $FSIZE_AFTER)"
fi

echo ""

# ==================== MOUNT / MOUNTED ====================
echo "--- MOUNT / MOUNTED ---"

# preparar discos y particiones (fdisk no necesita estado en RAM)
run_cmd "mkdisk -size=5 -unit=M -path=$TEST_DIR/mount_test.mia" > /dev/null
run_cmd "fdisk -size=500 -unit=K -path=$TEST_DIR/mount_test.mia -name=PartM1" > /dev/null
run_cmd "fdisk -size=500 -unit=K -path=$TEST_DIR/mount_test.mia -name=PartM2" > /dev/null
run_cmd "fdisk -type=E -size=500 -unit=K -path=$TEST_DIR/mount_test.mia -name=ExtM" > /dev/null

run_cmd "mkdisk -size=2 -unit=M -path=$TEST_DIR/mount_test2.mia" > /dev/null
run_cmd "fdisk -size=500 -unit=K -path=$TEST_DIR/mount_test2.mia -name=PartN1" > /dev/null

# 24, 25, 26, 27: todas las operaciones de mount en una sola ejecucion
OUT=$(run_multi "mount -path=$TEST_DIR/mount_test.mia -name=PartM1
mount -path=$TEST_DIR/mount_test.mia -name=PartM2
mount -path=$TEST_DIR/mount_test2.mia -name=PartN1
mount -path=$TEST_DIR/mount_test.mia -name=PartM1
mounted")
assert_contains "24. mount primera => 441A" "$OUT" "id=441A"
assert_contains "25. mount segunda => 442A" "$OUT" "id=442A"
assert_contains "26. mount otro disco => 441B" "$OUT" "id=441B"
assert_contains "27. montaje duplicado" "$OUT" "\\[ERROR\\].*montada"

# 28. montar extendida (proceso separado, no necesita estado previo)
OUT=$(run_cmd "mount -path=$TEST_DIR/mount_test.mia -name=ExtM")
assert_contains "28. montar extendida" "$OUT" "\\[ERROR\\]"

# 29a. param desconocido en mount
OUT=$(run_cmd "mount -path=$TEST_DIR/mount_test.mia -name=PartM1 -foo=1")
assert_contains "29a. mount param desconocido" "$OUT" "\\[ERROR\\].*no reconocido"

# 29b. mounted con parametros
OUT=$(run_cmd "mounted -foo=1")
assert_contains "29b. mounted con params" "$OUT" "\\[ERROR\\].*no reconocido"

# 29c. mounted sin montajes (proceso nuevo)
OUT=$(run_cmd "mounted")
assert_contains "29c. mounted sin particiones" "$OUT" "no hay particiones"

# 30b. tamano no cambia tras mount
MSIZE_BEFORE=$(stat -c%s "$TEST_DIR/mount_test.mia" 2>/dev/null)
run_cmd "mount -path=$TEST_DIR/mount_test.mia -name=PartM1" > /dev/null
MSIZE_AFTER=$(stat -c%s "$TEST_DIR/mount_test.mia" 2>/dev/null)
if [ "$MSIZE_BEFORE" = "$MSIZE_AFTER" ]; then
    PASS=$((PASS + 1))
    echo "  PASS: 30b. tamano .mia no cambia tras mount"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: 30b. tamano cambio ($MSIZE_BEFORE -> $MSIZE_AFTER)"
fi

echo ""

# ==================== PRUEBAS ADICIONALES ====================
echo "--- ADICIONALES ---"

# 31. parametros separados por tabulaciones
OUT=$(printf "mkdisk\t-size=1\t-unit=K\t-path=$TEST_DIR/tabs_test.mia" | $BIN 2>&1 | sed 's/^>> //g' | grep -v "^ExtreamFS" | grep -v "^Ingrese" | grep -v "^[[:space:]]*$")
assert_contains "31. params con tabs" "$OUT" "\\[OK\\] MKDISK"
assert_file_exists "31b. archivo con tabs creado" "$TEST_DIR/tabs_test.mia"

# 32. mbr_dsk_signature distintos entre discos (leidos con structs.h via test_structs)
run_cmd "mkdisk -size=1 -unit=K -path=$TEST_DIR/sig1.mia" > /dev/null
run_cmd "mkdisk -size=1 -unit=K -path=$TEST_DIR/sig2.mia" > /dev/null
run_cmd "mkdisk -size=1 -unit=K -path=$TEST_DIR/sig3.mia" > /dev/null
SIG1=$($TEST_BIN mbr "$TEST_DIR/sig1.mia" | grep "^mbr_dsk_signature=" | cut -d= -f2)
SIG2=$($TEST_BIN mbr "$TEST_DIR/sig2.mia" | grep "^mbr_dsk_signature=" | cut -d= -f2)
SIG3=$($TEST_BIN mbr "$TEST_DIR/sig3.mia" | grep "^mbr_dsk_signature=" | cut -d= -f2)
if [ -n "$SIG1" ] && [ -n "$SIG2" ] && [ -n "$SIG3" ] && [ "$SIG1" != "$SIG2" ] && [ "$SIG1" != "$SIG3" ] && [ "$SIG2" != "$SIG3" ]; then
    PASS=$((PASS + 1))
    echo "  PASS: 32. signatures distintos ($SIG1, $SIG2, $SIG3)"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: 32. signatures repetidos ($SIG1, $SIG2, $SIG3)"
fi

echo ""

# ==================== PRUEBAS ESTRUCTURALES BINARIAS ====================
echo "--- PRUEBAS ESTRUCTURALES BINARIAS ---"

# 1. MBR DESPUES DE MKDISK
run_cmd "mkdisk -size=2 -unit=M -path=$TEST_DIR/struct_d1.mia" > /dev/null
MBR_OUT=$($TEST_BIN mbr "$TEST_DIR/struct_d1.mia")

MBR_SZ=$(echo "$MBR_OUT" | grep "^mbr_tamano=" | cut -d= -f2)
assert_equals "33. mbr_tamano coincide con 2MB (2097152)" "$MBR_SZ" "2097152"

MBR_FIT_DEF=$(echo "$MBR_OUT" | grep "^dsk_fit=" | cut -d= -f2)
assert_equals "34. dsk_fit default es F" "$MBR_FIT_DEF" "F"

run_cmd "mkdisk -size=1 -unit=M -fit=BF -path=$TEST_DIR/struct_d_bf.mia" > /dev/null
MBR_FIT_BF=$($TEST_BIN mbr "$TEST_DIR/struct_d_bf.mia" | grep "^dsk_fit=" | cut -d= -f2)
assert_equals "35. dsk_fit con -fit=BF queda B" "$MBR_FIT_BF" "B"

run_cmd "mkdisk -size=1 -unit=M -fit=WF -path=$TEST_DIR/struct_d_wf.mia" > /dev/null
MBR_FIT_WF=$($TEST_BIN mbr "$TEST_DIR/struct_d_wf.mia" | grep "^dsk_fit=" | cut -d= -f2)
assert_equals "36. dsk_fit con -fit=WF queda W" "$MBR_FIT_WF" "W"

ALL_EMPTY=true
for i in 0 1 2 3; do
    P_STAT=$(echo "$MBR_OUT" | grep "^p${i}_status=" | cut -d= -f2)
    P_START=$(echo "$MBR_OUT" | grep "^p${i}_start=" | cut -d= -f2)
    P_S=$(echo "$MBR_OUT" | grep "^p${i}_s=" | cut -d= -f2)
    P_CORR=$(echo "$MBR_OUT" | grep "^p${i}_correlative=" | cut -d= -f2)
    if [ "$P_STAT" != "0" ] || [ "$P_START" != "-1" ] || [ "$P_S" != "0" ] || [ "$P_CORR" != "-1" ]; then
        ALL_EMPTY=false
    fi
done
if [ "$ALL_EMPTY" = true ]; then
    PASS=$((PASS + 1))
    echo "  PASS: 37. 4 particiones inicializadas vacias"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: 37. particiones no estan vacias en nuevo disco"
fi

# 2. FDISK PRIMARIA
run_cmd "fdisk -size=256 -unit=K -path=$TEST_DIR/struct_d1.mia -name=PriEstruct -fit=BF" > /dev/null
MBR_P1=$($TEST_BIN mbr "$TEST_DIR/struct_d1.mia")

P0_TYPE=$(echo "$MBR_P1" | grep "^p0_type=" | cut -d= -f2)
assert_equals "38. primaria part_type == P" "$P0_TYPE" "P"

P0_FIT=$(echo "$MBR_P1" | grep "^p0_fit=" | cut -d= -f2)
assert_equals "39. primaria part_fit == B" "$P0_FIT" "B"

P0_S=$(echo "$MBR_P1" | grep "^p0_s=" | cut -d= -f2)
assert_equals "40. primaria part_s == 262144 (256K)" "$P0_S" "262144"

P0_START=$(echo "$MBR_P1" | grep "^p0_start=" | cut -d= -f2)
MBR_SIZE=$($TEST_BIN sizes | grep "^MBR=" | cut -d= -f2)
if [ "$P0_START" -ge "$MBR_SIZE" ]; then
    PASS=$((PASS + 1))
    echo "  PASS: 41. primaria part_start ($P0_START) >= sizeof(MBR) ($MBR_SIZE)"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: 41. primaria part_start ($P0_START) < sizeof(MBR) ($MBR_SIZE)"
fi

P0_NAME=$(echo "$MBR_P1" | grep "^p0_name=" | cut -d= -f2)
assert_equals "42. primaria part_name correcto" "$P0_NAME" "PriEstruct"

P0_CORR=$(echo "$MBR_P1" | grep "^p0_correlative=" | cut -d= -f2)
assert_equals "43. primaria part_correlative == -1 antes de mount" "$P0_CORR" "-1"

# 3. PARTICION EXTENDIDA
run_cmd "fdisk -type=E -size=512 -unit=K -path=$TEST_DIR/struct_d1.mia -name=ExtEstruct -fit=FF" > /dev/null
MBR_E=$($TEST_BIN mbr "$TEST_DIR/struct_d1.mia")
P1_TYPE=$(echo "$MBR_E" | grep "^p1_type=" | cut -d= -f2)
assert_equals "44. extendida part_type == E" "$P1_TYPE" "E"

EBR_INIT=$($TEST_BIN ebr_chain "$TEST_DIR/struct_d1.mia")
P1_START=$(echo "$MBR_E" | grep "^p1_start=" | cut -d= -f2)
EBR0_POS=$(echo "$EBR_INIT" | grep "^ebr0_pos=" | cut -d= -f2)
assert_equals "45. EBR inicial exactamente en ext.part_start" "$EBR0_POS" "$P1_START"

EBR0_S=$(echo "$EBR_INIT" | grep "^ebr0_s=" | cut -d= -f2)
EBR0_NEXT=$(echo "$EBR_INIT" | grep "^ebr0_next=" | cut -d= -f2)
if [ "$EBR0_S" = "0" ] && [ "$EBR0_NEXT" = "-1" ]; then
    PASS=$((PASS + 1))
    echo "  PASS: 46. EBR inicial vacio (part_s=0, part_next=-1)"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: 46. EBR inicial no vacio (s=$EBR0_S, next=$EBR0_NEXT)"
fi

# 4. CADENA DE EBRs
run_cmd "fdisk -type=L -size=64 -unit=K -path=$TEST_DIR/struct_d1.mia -name=LogCad1" > /dev/null
run_cmd "fdisk -type=L -size=64 -unit=K -path=$TEST_DIR/struct_d1.mia -name=LogCad2" > /dev/null
run_cmd "fdisk -type=L -size=64 -unit=K -path=$TEST_DIR/struct_d1.mia -name=LogCad3" > /dev/null

EBR_CHAIN=$($TEST_BIN ebr_chain "$TEST_DIR/struct_d1.mia")

EBR0_NEXT=$(echo "$EBR_CHAIN" | grep "^ebr0_next=" | cut -d= -f2)
EBR1_START=$(echo "$EBR_CHAIN" | grep "^ebr1_start=" | cut -d= -f2)
assert_equals "47. EBR1.part_next == EBR2.part_start" "$EBR0_NEXT" "$EBR1_START"

EBR1_NEXT=$(echo "$EBR_CHAIN" | grep "^ebr1_next=" | cut -d= -f2)
EBR2_START=$(echo "$EBR_CHAIN" | grep "^ebr2_start=" | cut -d= -f2)
assert_equals "48. EBR2.part_next == EBR3.part_start" "$EBR1_NEXT" "$EBR2_START"

EBR2_NEXT=$(echo "$EBR_CHAIN" | grep "^ebr2_next=" | cut -d= -f2)
assert_equals "49. EBR3.part_next == -1" "$EBR2_NEXT" "-1"

EBR0_START=$(echo "$EBR_CHAIN" | grep "^ebr0_start=" | cut -d= -f2)
if [ "$EBR1_START" -gt "$EBR0_START" ] && [ "$EBR2_START" -gt "$EBR1_START" ]; then
    PASS=$((PASS + 1))
    echo "  PASS: 50. posiciones crecientes (EBR3 > EBR2 > EBR1)"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: 50. orden de starts incorrecto: $EBR0_START, $EBR1_START, $EBR2_START"
fi

EXT_START=$(echo "$EBR_CHAIN" | grep "^ext_start=" | cut -d= -f2)
EXT_SIZE=$(echo "$EBR_CHAIN" | grep "^ext_size=" | cut -d= -f2)
EXT_LIMIT=$((EXT_START + EXT_SIZE))
SZ_EBR=$($TEST_BIN sizes | grep "^EBR=" | cut -d= -f2)

EBR0_S=$(echo "$EBR_CHAIN" | grep "^ebr0_s=" | cut -d= -f2)
EBR1_S=$(echo "$EBR_CHAIN" | grep "^ebr1_s=" | cut -d= -f2)
EBR2_S=$(echo "$EBR_CHAIN" | grep "^ebr2_s=" | cut -d= -f2)

LIMITS_OK=true
END0=$((EBR0_START + SZ_EBR + EBR0_S))
END1=$((EBR1_START + SZ_EBR + EBR1_S))
END2=$((EBR2_START + SZ_EBR + EBR2_S))

if [ "$END0" -gt "$EXT_LIMIT" ] || [ "$END1" -gt "$EXT_LIMIT" ] || [ "$END2" -gt "$EXT_LIMIT" ]; then
    LIMITS_OK=false
fi

if [ "$LIMITS_OK" = true ]; then
    PASS=$((PASS + 1))
    echo "  PASS: 51. limites (start+sizeof(EBR)+s <= ext_limit) cumplidos para Log1, Log2 y Log3"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: 51. alguna logica excede limite de extendida (END0=$END0, END1=$END1, END2=$END2, LIMIT=$EXT_LIMIT)"
fi

# 5. FIT DEL DISCO (FF / BF / WF) MEDIANTE FDISK REAL
# Probar First Fit (FF): disco con dsk_fit = F
$TEST_BIN create_gapped "$TEST_DIR/gap_ff.mia" F > /dev/null
run_cmd "fdisk -size=1500 -unit=B -path=$TEST_DIR/gap_ff.mia -name=PartFF" > /dev/null
MBR_FF=$($TEST_BIN mbr "$TEST_DIR/gap_ff.mia")
FF_START=$(echo "$MBR_FF" | grep "^p2_start=" | cut -d= -f2)
assert_equals "52. FDISK real First Fit ubica en primer hueco (168)" "$FF_START" "168"

# Probar Best Fit (BF): disco con dsk_fit = B
$TEST_BIN create_gapped "$TEST_DIR/gap_bf.mia" B > /dev/null
run_cmd "fdisk -size=1500 -unit=B -path=$TEST_DIR/gap_bf.mia -name=PartBF" > /dev/null
MBR_BF=$($TEST_BIN mbr "$TEST_DIR/gap_bf.mia")
BF_START=$(echo "$MBR_BF" | grep "^p2_start=" | cut -d= -f2)
assert_equals "53. FDISK real Best Fit ubica en menor hueco (3668)" "$BF_START" "3668"

# Probar Worst Fit (WF): disco con dsk_fit = W
$TEST_BIN create_gapped "$TEST_DIR/gap_wf.mia" W > /dev/null
run_cmd "fdisk -size=1500 -unit=B -path=$TEST_DIR/gap_wf.mia -name=PartWF" > /dev/null
MBR_WF=$($TEST_BIN mbr "$TEST_DIR/gap_wf.mia")
WF_START=$(echo "$MBR_WF" | grep "^p2_start=" | cut -d= -f2)
assert_equals "54. FDISK real Worst Fit ubica en mayor hueco (5668)" "$WF_START" "5668"

# Probar FDISK sin espacio suficiente (tamano 6000 > max hueco 5000)
OUT_NF=$(run_cmd "fdisk -size=6000 -unit=B -path=$TEST_DIR/gap_ff.mia -name=NoFitPart")
assert_contains "55. FDISK real rechaza particion cuando ningun hueco cabe" "$OUT_NF" "\\[ERROR\\].*espacio"

# 6. MOUNT BINARIO
run_cmd "mount -path=$TEST_DIR/struct_d1.mia -name=PriEstruct" > /dev/null
MBR_M1=$($TEST_BIN mbr "$TEST_DIR/struct_d1.mia")

M1_STAT=$(echo "$MBR_M1" | grep "^p0_status=" | cut -d= -f2)
assert_equals "56. mount p0_status == 1" "$M1_STAT" "1"

M1_CORR=$(echo "$MBR_M1" | grep "^p0_correlative=" | cut -d= -f2)
assert_equals "57. mount p0_correlative == 1" "$M1_CORR" "1"

M1_ID=$(echo "$MBR_M1" | grep "^p0_id=" | cut -d= -f2)
assert_equals "58. mount p0_id == 441A (4 bytes exactos)" "$M1_ID" "441A"

run_cmd "fdisk -size=128 -unit=K -path=$TEST_DIR/struct_d1.mia -name=Pri2" > /dev/null
run_multi "mount -path=$TEST_DIR/struct_d1.mia -name=PriEstruct
mount -path=$TEST_DIR/struct_d1.mia -name=Pri2" > /dev/null
MBR_M2=$($TEST_BIN mbr "$TEST_DIR/struct_d1.mia")

M2_CORR=$(echo "$MBR_M2" | grep "^p2_correlative=" | cut -d= -f2)
assert_equals "59. mount segunda particion correlative == 2" "$M2_CORR" "2"

M2_ID=$(echo "$MBR_M2" | grep "^p2_id=" | cut -d= -f2)
assert_equals "60. mount segunda particion id == 442A" "$M2_ID" "442A"

# 7. CASE INSENSITIVE / ORDEN
OUT_CASE=$(run_cmd "MkDiSk -PaTh=$TEST_DIR/case_disk.mia -UnIt=K -SiZe=1000 -FiT=BF")
assert_contains "61. MkDiSk con mayusculas/minusculas mezcladas" "$OUT_CASE" "\\[OK\\] MKDISK"
MBR_CASE=$($TEST_BIN mbr "$TEST_DIR/case_disk.mia")
CASE_FIT=$(echo "$MBR_CASE" | grep "^dsk_fit=" | cut -d= -f2)
assert_equals "61b. fit almacenado correcto con case mixto" "$CASE_FIT" "B"

OUT_ORD=$(run_cmd "fdisk -name=PartOrd -path=$TEST_DIR/case_disk.mia -type=P -fit=WF -unit=K -size=100")
assert_contains "62. fdisk con parametros en orden arbitrario" "$OUT_ORD" "\\[OK\\] FDISK"
MBR_ORD=$($TEST_BIN mbr "$TEST_DIR/case_disk.mia")
ORD_NAME=$(echo "$MBR_ORD" | grep "^p0_name=" | cut -d= -f2)
assert_equals "62b. particion creada correctamente con orden arbitrario" "$ORD_NAME" "PartOrd"

# 8. TEST DE STRUCT SIZES
SIZES_OUT=$($TEST_BIN sizes)
SZ_FB=$(echo "$SIZES_OUT" | grep "^FolderBlock=" | cut -d= -f2)
SZ_FLB=$(echo "$SIZES_OUT" | grep "^FileBlock=" | cut -d= -f2)
SZ_PB=$(echo "$SIZES_OUT" | grep "^PointerBlock=" | cut -d= -f2)

assert_equals "63. sizeof(FolderBlock) == 64" "$SZ_FB" "64"
assert_equals "64. sizeof(FileBlock) == 64" "$SZ_FLB" "64"
assert_equals "65. sizeof(PointerBlock) == 64" "$SZ_PB" "64"

echo ""

# ==================== RESUMEN ====================
echo "============================================"
TOTAL=$((PASS + FAIL))
echo " Resultados: $PASS/$TOTAL pasaron"
if [ $FAIL -gt 0 ]; then
    echo " $FAIL pruebas FALLARON"
    exit 1
else
    echo " Todas las pruebas pasaron"
    exit 0
fi

