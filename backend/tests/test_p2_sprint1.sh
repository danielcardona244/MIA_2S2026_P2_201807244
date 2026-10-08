#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
INSPECT="backend/build/test_structs"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_p2_sprint1_$$"
mkdir -p "$TEST_DIR"
trap 'rm -rf "$TEST_DIR"' EXIT

pass(){ PASS=$((PASS+1)); echo "  PASS: $1"; }
fail(){ FAIL=$((FAIL+1)); echo "  FAIL: $1${2:+ -> $2}"; }
assert_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then pass "$d"; else fail "$d" "$o"; fi; }
assert_not_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then fail "$d" "$o"; else pass "$d"; fi; }
assert_eq(){ local d="$1" a="$2" e="$3"; if [ "$a" = "$e" ]; then pass "$d"; else fail "$d" "esperado=$e obtenido=$a"; fi; }
kv(){ echo "$1" | grep "^$2=" | head -1 | cut -d= -f2-; }
run_multi(){ printf "%s\n" "$1" | "$BIN" 2>&1; }
hex_at(){ od -An -tx1 -N "$3" -j "$2" "$1" 2>/dev/null | tr -d ' \n'; }

if [ ! -x "$BIN" ] || [ ! -x "$INSPECT" ]; then
  echo "ERROR: compile primero con: cmake -S backend -B backend/build && cmake --build backend/build"
  exit 1
fi

echo "============================================"
echo " Proyecto 2 - Sprint 1: FDISK + UNMOUNT"
echo "============================================"

# ---------------------------------------------------------------------------
# 1) FDISK -delete=fast / full sobre particiones primarias
# ---------------------------------------------------------------------------
D1="$TEST_DIR/delete_primary.mia"
OUT=$(run_multi "mkdisk -size=8 -unit=M -path=$D1
fdisk -size=1 -unit=M -path=$D1 -name=PFast")
assert_contains "1. preparacion particion para delete fast" "$OUT" '\[OK\] FDISK: particion PFast creada'
M1=$($INSPECT mbr "$D1")
PSTART=$(kv "$M1" p0_start)
PSIZE=$(kv "$M1" p0_s)
printf 'ABCD' | dd of="$D1" bs=1 seek=$((PSTART+256)) conv=notrunc status=none
BEFORE_FAST=$(hex_at "$D1" $((PSTART+256)) 4)
OUT=$(run_multi "fdisk -delete=fast -name=PFast -path=$D1")
assert_contains "2. delete fast muestra confirmacion" "$OUT" '\[CONFIRM\] FDISK'
assert_contains "3. delete fast elimina particion" "$OUT" '\[OK\] FDISK: particion PFast eliminada \(FAST\)'
M1=$($INSPECT mbr "$D1")
assert_eq "4. delete fast deja slot MBR vacio" "$(kv "$M1" p0_s)" "0"
assert_eq "5. delete fast reinicia start del slot" "$(kv "$M1" p0_start)" "-1"
AFTER_FAST=$(hex_at "$D1" $((PSTART+256)) 4)
assert_eq "6. delete fast conserva datos fisicos" "$AFTER_FAST" "$BEFORE_FAST"

OUT=$(run_multi "fdisk -size=1 -unit=M -path=$D1 -name=PFull")
assert_contains "7. se puede reutilizar espacio tras delete fast" "$OUT" '\[OK\] FDISK: particion PFull creada'
M1=$($INSPECT mbr "$D1")
FSTART=$(kv "$M1" p0_start)
printf 'WXYZ' | dd of="$D1" bs=1 seek=$((FSTART+512)) conv=notrunc status=none
DISK_SIZE_BEFORE=$(stat -c%s "$D1")
OUT=$(run_multi "fdisk -delete=full -name=PFull -path=$D1")
assert_contains "8. delete full elimina particion" "$OUT" '\[OK\] FDISK: particion PFull eliminada \(FULL\)'
assert_eq "9. delete full rellena datos con cero" "$(hex_at "$D1" $((FSTART+512)) 4)" "00000000"
assert_eq "10. delete full no cambia tamano del .mia" "$(stat -c%s "$D1")" "$DISK_SIZE_BEFORE"

OUT=$(run_multi "fdisk -delete=slow -name=NoExiste -path=$D1")
assert_contains "11. delete valida fast/full" "$OUT" '\[ERROR\] FDISK: -delete debe ser fast o full'
OUT=$(run_multi "fdisk -delete=fast -name=NoExiste -path=$D1")
assert_contains "12. delete exige que la particion exista" "$OUT" '\[ERROR\] FDISK: no existe una particion'

# ---------------------------------------------------------------------------
# 2) Eliminacion de extendida y logicas
# ---------------------------------------------------------------------------
D2="$TEST_DIR/delete_extended.mia"
OUT=$(run_multi "mkdisk -size=12 -unit=M -path=$D2
fdisk -size=8 -unit=M -type=E -path=$D2 -name=EXT
fdisk -size=1 -unit=M -type=L -path=$D2 -name=L1
fdisk -size=1 -unit=M -type=L -path=$D2 -name=L2
fdisk -delete=fast -path=$D2 -name=EXT")
assert_contains "13. eliminar extendida funciona" "$OUT" '\[OK\] FDISK: particion EXT eliminada \(FAST\)'
M2=$($INSPECT mbr "$D2")
assert_eq "14. eliminar extendida vacia entrada MBR" "$(kv "$M2" p0_s)" "0"
CHAIN=$($INSPECT ebr_chain "$D2" 2>&1 || true)
assert_contains "15. logicas dejan de ser accesibles al borrar extendida" "$CHAIN" 'ERROR=no_hay_extendida'

D3="$TEST_DIR/delete_logical.mia"
OUT=$(run_multi "mkdisk -size=12 -unit=M -path=$D3
fdisk -size=8 -unit=M -type=E -path=$D3 -name=EXT
fdisk -size=1 -unit=M -type=L -path=$D3 -name=L1
fdisk -size=1 -unit=M -type=L -path=$D3 -name=L2
fdisk -size=1 -unit=M -type=L -path=$D3 -name=L3
fdisk -delete=fast -path=$D3 -name=L2")
assert_contains "16. delete de logica funciona" "$OUT" '\[OK\] FDISK: particion logica L2 eliminada \(FAST\)'
CHAIN=$($INSPECT ebr_chain "$D3")
assert_not_contains "17. logica eliminada sale de cadena EBR" "$CHAIN" 'name=L2$'
assert_eq "18. cadena conserva las otras dos logicas" "$(kv "$CHAIN" ebr_count)" "2"
OUT=$(run_multi "fdisk -size=512 -unit=K -type=L -path=$D3 -name=L2")
assert_contains "19. espacio de logica eliminada puede reutilizarse" "$OUT" '\[OK\] FDISK: particion logica L2 creada'
CHAIN=$($INSPECT ebr_chain "$D3")
assert_eq "20. cadena EBR queda valida despues de reutilizar" "$(kv "$CHAIN" ebr_count)" "3"

# ---------------------------------------------------------------------------
# 3) FDISK -add positivo/negativo y espacio contiguo
# ---------------------------------------------------------------------------
D4="$TEST_DIR/add_primary.mia"
OUT=$(run_multi "mkdisk -size=10 -unit=M -path=$D4
fdisk -size=2 -unit=M -path=$D4 -name=A
fdisk -size=2 -unit=M -path=$D4 -name=B")
assert_contains "21. preparacion resize primarias" "$OUT" '\[OK\] FDISK: particion B creada'
M4=$($INSPECT mbr "$D4")
A0=$(kv "$M4" p0_s)
OUT=$(run_multi "fdisk -add=-512 -unit=K -path=$D4 -name=A")
assert_contains "22. add negativo reduce primaria" "$OUT" '\[OK\] FDISK: particion A redimensionada'
M4=$($INSPECT mbr "$D4")
A1=$(kv "$M4" p0_s)
assert_eq "23. reduccion primaria es 512 KiB" "$A1" "$((A0-512*1024))"
OUT=$(run_multi "fdisk -add=256 -unit=K -path=$D4 -name=A")
assert_contains "24. add positivo usa hueco inmediato" "$OUT" '\[OK\] FDISK: particion A redimensionada'
M4=$($INSPECT mbr "$D4")
A2=$(kv "$M4" p0_s)
assert_eq "25. crecimiento primaria es 256 KiB" "$A2" "$((A1+256*1024))"
OUT=$(run_multi "fdisk -add=512 -unit=K -path=$D4 -name=A")
assert_contains "26. add positivo falla sin espacio inmediato suficiente" "$OUT" '\[ERROR\] FDISK: no hay espacio libre suficiente inmediatamente despues'
OUT=$(run_multi "fdisk -add=-999 -unit=M -path=$D4 -name=A")
assert_contains "27. add negativo no permite tamano no positivo" "$OUT" '\[ERROR\] FDISK:.*tamano positivo'
OUT=$(run_multi "fdisk -add=1 -unit=K -path=$D4 -name=Fantasma")
assert_contains "28. add exige particion existente" "$OUT" '\[ERROR\] FDISK: no existe una particion'
OUT=$(run_multi "fdisk -add=abc -unit=K -path=$D4 -name=A")
assert_contains "29. add valida numero" "$OUT" '\[ERROR\] FDISK: add no es un numero valido'

# Regla del enunciado: si aparecen size/add/delete juntos, manda el primero.
M4=$($INSPECT mbr "$D4")
A_BEFORE=$(kv "$M4" p0_s)
OUT=$(run_multi "fdisk -add=-1 -size=NO_NUMERO -unit=K -path=$D4 -name=A")
assert_contains "30. add primero ignora size posterior" "$OUT" '\[OK\] FDISK: particion A redimensionada'
M4=$($INSPECT mbr "$D4")
assert_eq "31. precedencia add aplica exactamente 1 KiB" "$(kv "$M4" p0_s)" "$((A_BEFORE-1024))"
OUT=$(run_multi "fdisk -size=256 -add=999 -unit=K -path=$D4 -name=SizePrimero")
assert_contains "32. size primero crea particion e ignora add" "$OUT" '\[OK\] FDISK: particion SizePrimero creada \(P, 262144 bytes\)'

# ---------------------------------------------------------------------------
# 4) Resize de logicas y proteccion de extendida
# ---------------------------------------------------------------------------
D5="$TEST_DIR/add_logical.mia"
OUT=$(run_multi "mkdisk -size=12 -unit=M -path=$D5
fdisk -size=7 -unit=M -type=E -path=$D5 -name=EXT
fdisk -size=1 -unit=M -type=L -path=$D5 -name=L1
fdisk -size=1 -unit=M -type=L -path=$D5 -name=L2")
assert_contains "33. preparacion resize logicas" "$OUT" '\[OK\] FDISK: particion logica L2 creada'
CHAIN=$($INSPECT ebr_chain "$D5")
L1_0=$(kv "$CHAIN" ebr0_s)
OUT=$(run_multi "fdisk -add=-256 -unit=K -path=$D5 -name=L1
fdisk -add=128 -unit=K -path=$D5 -name=L1")
assert_contains "34. logica permite reducir y volver a crecer" "$OUT" '\[OK\] FDISK: particion logica L1 redimensionada'
CHAIN=$($INSPECT ebr_chain "$D5")
assert_eq "35. tamano logica refleja -128 KiB netos" "$(kv "$CHAIN" ebr0_s)" "$((L1_0-128*1024))"
OUT=$(run_multi "fdisk -add=256 -unit=K -path=$D5 -name=L1")
assert_contains "36. logica no invade la siguiente" "$OUT" '\[ERROR\] FDISK: no hay espacio libre suficiente inmediatamente despues de la particion logica'
OUT=$(run_multi "fdisk -add=-6 -unit=M -path=$D5 -name=EXT")
assert_contains "37. extendida no puede reducirse cortando logicas" "$OUT" '\[ERROR\] FDISK: no se puede reducir la extendida porque cortaria particiones logicas'

# ---------------------------------------------------------------------------
# 5) UNMOUNT
# ---------------------------------------------------------------------------
D6="$TEST_DIR/unmount.mia"
OUT=$(run_multi "mkdisk -size=10 -unit=M -path=$D6
fdisk -size=2 -unit=M -path=$D6 -name=P1
fdisk -size=2 -unit=M -path=$D6 -name=P2
mount -path=$D6 -name=P1
mount -path=$D6 -name=P2
unmount -id=441A
mounted")
assert_contains "38. unmount desmonta id existente" "$OUT" '\[OK\] UNMOUNT: particion P1 desmontada \(id=441A\)'
M6=$($INSPECT mbr "$D6")
assert_eq "39. unmount cambia status a 0" "$(kv "$M6" p0_status)" "0"
assert_eq "40. unmount reinicia correlativo a 0" "$(kv "$M6" p0_correlative)" "0"
assert_eq "41. unmount limpia part_id" "$(kv "$M6" p0_id)" ""
assert_eq "42. segunda particion sigue montada" "$(kv "$M6" p1_status)" "1"

D7="$TEST_DIR/unmount_reuse.mia"
OUT=$(run_multi "mkdisk -size=10 -unit=M -path=$D7
fdisk -size=2 -unit=M -path=$D7 -name=P1
fdisk -size=2 -unit=M -path=$D7 -name=P2
mount -path=$D7 -name=P1
mount -path=$D7 -name=P2
unmount -id=441A
mount -path=$D7 -name=P1
mounted")
FINAL_LIST=$(echo "$OUT" | awk '/--- Particiones montadas ---/{flag=1;next} flag && /----------------------------/{exit} flag{print}')
assert_contains "43. correlativo liberado se reutiliza sin duplicar" "$FINAL_LIST" 'id=441A.*particion=P1'
assert_contains "44. montaje 442A permanece" "$FINAL_LIST" 'id=442A.*particion=P2'
LINES=$(echo "$FINAL_LIST" | grep -c 'id=' || true)
assert_eq "45. mounted lista exactamente dos particiones" "$LINES" "2"

OUT=$(run_multi "unmount -id=999Z")
assert_contains "46. unmount valida id inexistente" "$OUT" '\[ERROR\] UNMOUNT: no existe una particion montada'
OUT=$(run_multi "unmount -id=441A -extra=x")
assert_contains "47. scanner rechaza parametros extra en unmount" "$OUT" '\[ERROR\] UNMOUNT: parametro no reconocido -extra'
OUT=$(run_multi "unmount")
assert_contains "48. unmount exige id" "$OUT" '\[ERROR\] UNMOUNT: falta parametro -id'

# Estructuras binarias del Proyecto 1 no deben cambiar en Sprint 1.
SIZES=$($INSPECT sizes)
assert_eq "49. sizeof MBR se conserva" "$(kv "$SIZES" MBR)" "168"
assert_eq "50. sizeof Partition se conserva" "$(kv "$SIZES" Partition)" "36"
assert_eq "51. sizeof EBR se conserva" "$(kv "$SIZES" EBR)" "32"

echo "============================================"
TOTAL=$((PASS+FAIL))
echo " Resultados: $PASS/$TOTAL pasaron"
if [ "$FAIL" -gt 0 ]; then
  echo " $FAIL pruebas FALLARON"
  exit 1
fi
echo " Todas las pruebas de Proyecto 2 Sprint 1 pasaron"
