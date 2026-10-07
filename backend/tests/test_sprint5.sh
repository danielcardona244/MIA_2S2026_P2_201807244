#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_sprint5_$$"
REPORT_DIR="$TEST_DIR/reportes con espacios"
mkdir -p "$TEST_DIR"
trap 'rm -rf "$TEST_DIR"' EXIT

pass(){ PASS=$((PASS+1)); echo "  PASS: $1"; }
fail(){ FAIL=$((FAIL+1)); echo "  FAIL: $1${2:+ -> $2}"; }
assert_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then pass "$d"; else fail "$d" "$o"; fi; }
assert_file(){ local d="$1" f="$2"; if [ -s "$f" ]; then pass "$d"; else fail "$d" "archivo inexistente o vacio: $f"; fi; }
assert_grep_file(){ local d="$1" f="$2" r="$3"; if [ -s "$f" ] && grep -qE "$r" "$f"; then pass "$d"; else fail "$d" "no se encontro $r en $f"; fi; }
run_multi(){ printf "%s\n" "$1" | "$BIN" 2>&1; }

if [ ! -x "$BIN" ]; then
  echo "ERROR: primero ejecute: cmake -S backend -B backend/build && cmake --build backend/build"
  exit 1
fi

if ! command -v dot >/dev/null 2>&1; then
  echo "ERROR: Sprint 5 requiere Graphviz. Instale graphviz para disponer del comando dot."
  exit 1
fi

echo "============================================"
echo " Sprint 5 - Reportes Graphviz"
echo "============================================"

# Validaciones sintacticas basicas.
OUT=$(run_multi "rep -name=mbr -path=$TEST_DIR/x.jpg")
assert_contains "1. REP exige id" "$OUT" "falta parametro -id"
OUT=$(run_multi "rep -id=999A -path=$TEST_DIR/x.jpg -name=mbr")
assert_contains "2. REP valida id montado" "$OUT" "id no corresponde"
OUT=$(run_multi "rep -id=999A -path=$TEST_DIR/x.jpg -name=invalido")
assert_contains "3. REP valida nombre" "$OUT" "nombre de reporte no valido"
OUT=$(run_multi "rep -id=999A -path=$TEST_DIR/x.jpg -name=mbr -otro=1")
assert_contains "4. REP rechaza parametro desconocido" "$OUT" "parametro no reconocido -otro"

DISK="$TEST_DIR/s5.mia"
# MBR y DISK deben poder producirse con solo montar la particion, aun antes de MKFS.
PRE="mkdisk -size=20 -unit=M -path=$DISK
fdisk -size=8 -unit=M -path=$DISK -name=PartS5 -type=P
fdisk -size=8 -unit=M -path=$DISK -name=Extendida -type=E
fdisk -size=2 -unit=M -path=$DISK -name=Log1 -type=L
fdisk -size=2 -unit=M -path=$DISK -name=Log2 -type=L
mount -path=$DISK -name=PartS5
rep -id=441A -path=\"$REPORT_DIR/nivel 1/mbr.jpg\" -name=mbr
rep -id=441A -path=\"$REPORT_DIR/disk.pdf\" -name=disk
rep -id=441A -path=\"$REPORT_DIR/mbr.dot\" -name=mbr
rep -id=441A -path=\"$REPORT_DIR/disk.dot\" -name=disk
rep -id=441A -path=\"$REPORT_DIR/inode_pre.jpg\" -name=inode"
OUT=$(run_multi "$PRE")
assert_contains "5. MOUNT preparado" "$OUT" "\[OK\] MOUNT"
assert_contains "6. MBR se genera antes de MKFS" "$OUT" "reporte mbr generado"
assert_contains "7. DISK se genera antes de MKFS" "$OUT" "reporte disk generado"
assert_contains "8. INODE exige EXT2 valido" "$OUT" "EXT2 valido"
assert_file "9. MBR crea carpetas padre" "$REPORT_DIR/nivel 1/mbr.jpg"
assert_file "10. DISK genera PDF" "$REPORT_DIR/disk.pdf"
assert_grep_file "11. MBR incluye EBR" "$REPORT_DIR/mbr.dot" "EBR 1"
assert_grep_file "12. MBR incluye primera logica" "$REPORT_DIR/mbr.dot" "Log1"
assert_grep_file "13. MBR incluye segunda logica" "$REPORT_DIR/mbr.dot" "Log2"
assert_grep_file "14. DISK representa 100 por ciento" "$REPORT_DIR/disk.dot" "Total representado: 100.00%"
if head -c 4 "$REPORT_DIR/disk.pdf" | grep -q '%PDF'; then pass "15. DISK es un PDF real"; else fail "15. DISK es un PDF real"; fi

# Crear EXT2 y suficiente contenido para ejercitar bloques carpeta, archivo y apuntadores.
CMDS="mkdisk -size=20 -unit=M -path=$TEST_DIR/fs.mia
fdisk -size=10 -unit=M -path=$TEST_DIR/fs.mia -name=Data
mount -path=$TEST_DIR/fs.mia -name=Data
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkdir -p -path=/home/docs
mkfile -path=/home/docs/a.txt -size=900
mkfile -path=/home/docs/b.txt -size=15
mkgrp -name=devs
mkusr -user=daniel -pass=123 -grp=devs
chown -path=/home/docs/b.txt -user=daniel
rep -id=441A -path=\"$REPORT_DIR/inode.jpg\" -name=inode
rep -id=441A -path=\"$REPORT_DIR/inode.dot\" -name=inode
rep -id=441A -path=\"$REPORT_DIR/block.jpg\" -name=block
rep -id=441A -path=\"$REPORT_DIR/block.dot\" -name=block
rep -id=441A -path=\"$REPORT_DIR/bm_inode.txt\" -name=bm_inode
rep -id=441A -path=\"$REPORT_DIR/bm_block.txt\" -name=bm_block
rep -id=441A -path=\"$REPORT_DIR/bm_bloc_alias.txt\" -name=bm_bloc
rep -id=441A -path=\"$REPORT_DIR/tree.jpg\" -name=tree
rep -id=441A -path=\"$REPORT_DIR/tree.dot\" -name=tree
rep -id=441A -path=\"$REPORT_DIR/sb.jpg\" -name=sb
rep -id=441A -path=\"$REPORT_DIR/sb.dot\" -name=sb
rep -id=441A -path=\"$REPORT_DIR/file.txt\" -path_file_ls=/home/docs/a.txt -name=file
rep -id=441A -path=\"$REPORT_DIR/file.jpg\" -path_file_ls=/home/docs/b.txt -name=file
rep -id=441A -path=\"$REPORT_DIR/ls.jpg\" -path_file_ls=/home/docs -name=ls
rep -id=441A -path=\"$REPORT_DIR/ls.dot\" -path_file_ls=/home/docs -name=ls"
OUT=$(run_multi "$CMDS")
assert_contains "16. Preparacion MKFS" "$OUT" "\[OK\] MKFS"
assert_contains "17. archivo de 900 bytes creado" "$OUT" "a.txt creado \(900 bytes\)"
assert_contains "18. REP INODE exitoso" "$OUT" "reporte inode generado"
assert_contains "19. REP BLOCK exitoso" "$OUT" "reporte block generado"
assert_contains "20. REP TREE exitoso" "$OUT" "reporte tree generado"
assert_contains "21. REP SB exitoso" "$OUT" "reporte sb generado"
assert_contains "22. REP FILE exitoso" "$OUT" "reporte file generado"
assert_contains "23. REP LS exitoso" "$OUT" "reporte ls generado"

assert_file "24. INODE genera imagen" "$REPORT_DIR/inode.jpg"
assert_grep_file "25. INODE muestra inodo raiz" "$REPORT_DIR/inode.dot" "INODO 0"
assert_grep_file "26. INODE muestra permisos" "$REPORT_DIR/inode.dot" "i_perm"
assert_file "27. BLOCK genera imagen" "$REPORT_DIR/block.jpg"
assert_grep_file "28. BLOCK incluye carpeta" "$REPORT_DIR/block.dot" "CARPETA"
assert_grep_file "29. BLOCK incluye archivo" "$REPORT_DIR/block.dot" "ARCHIVO"
assert_grep_file "30. BLOCK incluye apuntadores" "$REPORT_DIR/block.dot" "APUNTADORES"

assert_file "31. bitmap de inodos generado" "$REPORT_DIR/bm_inode.txt"
assert_file "32. bitmap de bloques generado" "$REPORT_DIR/bm_block.txt"
if awk 'NF>20{bad=1} END{exit bad}' "$REPORT_DIR/bm_inode.txt" && [ "$(awk 'NR==1{print NF}' "$REPORT_DIR/bm_inode.txt")" = "20" ]; then
  pass "33. bitmap de inodos usa 20 registros por linea"
else
  fail "33. bitmap de inodos usa 20 registros por linea"
fi
if awk 'NF>20{bad=1} END{exit bad}' "$REPORT_DIR/bm_block.txt" && [ "$(awk 'NR==1{print NF}' "$REPORT_DIR/bm_block.txt")" = "20" ]; then
  pass "34. bitmap de bloques usa 20 registros por linea"
else
  fail "34. bitmap de bloques usa 20 registros por linea"
fi
assert_grep_file "35. bitmap inodos refleja estructuras usadas" "$REPORT_DIR/bm_inode.txt" "^1 1 1"
assert_file "36. alias bm_bloc del enunciado funciona" "$REPORT_DIR/bm_bloc_alias.txt"

assert_file "37. TREE genera imagen" "$REPORT_DIR/tree.jpg"
assert_grep_file "38. TREE enlaza inodo y bloque" "$REPORT_DIR/tree.dot" "inode0 -> block0"
assert_grep_file "39. TREE enlaza carpeta con users.txt" "$REPORT_DIR/tree.dot" "users.txt"
assert_file "40. SB genera imagen" "$REPORT_DIR/sb.jpg"
assert_grep_file "41. SB muestra magic EXT2" "$REPORT_DIR/sb.dot" "0xEF53"
assert_grep_file "42. SB muestra bitmap de bloques" "$REPORT_DIR/sb.dot" "s_bm_block_start"

assert_grep_file "43. FILE incluye nombre" "$REPORT_DIR/file.txt" "Archivo: /home/docs/a.txt"
assert_grep_file "44. FILE incluye contenido completo" "$REPORT_DIR/file.txt" "01234567890123456789"
assert_file "45. FILE tambien puede generarse con Graphviz" "$REPORT_DIR/file.jpg"
assert_file "46. LS genera imagen" "$REPORT_DIR/ls.jpg"
assert_grep_file "47. LS incluye a.txt" "$REPORT_DIR/ls.dot" "a.txt"
assert_grep_file "48. LS incluye b.txt" "$REPORT_DIR/ls.dot" "b.txt"
assert_grep_file "49. LS resuelve owner root" "$REPORT_DIR/ls.dot" ">root<"
assert_grep_file "50. LS resuelve owner daniel" "$REPORT_DIR/ls.dot" ">daniel<"
assert_grep_file "51. LS muestra permisos simbolicos" "$REPORT_DIR/ls.dot" "rw-rw-r--"

OUT=$(run_multi "mkdisk -size=8 -unit=M -path=$TEST_DIR/e.mia
fdisk -size=4 -unit=M -path=$TEST_DIR/e.mia -name=P
mount -path=$TEST_DIR/e.mia -name=P
mkfs -id=441A
rep -id=441A -path=$TEST_DIR/a.txt -name=file")
assert_contains "52. FILE exige path_file_ls" "$OUT" "path_file_ls es obligatorio"
OUT=$(run_multi "mkdisk -size=8 -unit=M -path=$TEST_DIR/e2.mia
fdisk -size=4 -unit=M -path=$TEST_DIR/e2.mia -name=P
mount -path=$TEST_DIR/e2.mia -name=P
mkfs -id=441A
rep -id=441A -path=$TEST_DIR/a.jpg -name=ls")
assert_contains "53. LS exige path_file_ls" "$OUT" "path_file_ls es obligatorio"
OUT=$(run_multi "mkdisk -size=8 -unit=M -path=$TEST_DIR/e3.mia
fdisk -size=4 -unit=M -path=$TEST_DIR/e3.mia -name=P
mount -path=$TEST_DIR/e3.mia -name=P
mkfs -id=441A
rep -id=441A -path=$TEST_DIR/a.txt -path_file_ls=/no/existe -name=file")
assert_contains "54. FILE valida ruta interna" "$OUT" "no existe path_file_ls"
OUT=$(run_multi "mkdisk -size=8 -unit=M -path=$TEST_DIR/e4.mia
fdisk -size=4 -unit=M -path=$TEST_DIR/e4.mia -name=P
mount -path=$TEST_DIR/e4.mia -name=P
mkfs -id=441A
rep -id=441A -path=$TEST_DIR/sb.xyz -name=sb")
assert_contains "55. Graphviz valida extension de salida" "$OUT" "extension no soportada"

echo "============================================"
echo " Resultados: $PASS pasaron, $FAIL fallaron"
if [ "$FAIL" -eq 0 ]; then
  echo " Todas las pruebas obligatorias pasaron"
  exit 0
else
  echo " Hay pruebas fallidas"
  exit 1
fi
