#!/usr/bin/env bash
set -u

BIN="backend/build/extreamfs"
PASS=0
FAIL=0
TEST_DIR="/tmp/mia_sprint4_$$"
mkdir -p "$TEST_DIR"
trap 'rm -rf "$TEST_DIR"' EXIT

pass(){ PASS=$((PASS+1)); echo "  PASS: $1"; }
fail(){ FAIL=$((FAIL+1)); echo "  FAIL: $1${2:+ -> $2}"; }
assert_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then pass "$d"; else fail "$d" "$o"; fi; }
assert_not_contains(){ local d="$1" o="$2" r="$3"; if echo "$o" | grep -qE "$r"; then fail "$d" "$o"; else pass "$d"; fi; }
run_multi(){ printf "%s\n" "$1" | "$BIN" 2>&1; }

if [ ! -x "$BIN" ]; then
  echo "ERROR: primero ejecute: cmake -S backend -B backend/build && cmake --build backend/build"
  exit 1
fi

echo "============================================"
echo " Sprint 4 - Archivos, carpetas y permisos"
echo "============================================"

DISK="$TEST_DIR/s4.mia"
HOST="$TEST_DIR/host.txt"
SECRET="$TEST_DIR/secret.txt"
printf 'ARCHIVO_HOST_S4\nsegunda_linea' > "$HOST"
printf 'OWNERSHIP_OK_S4' > "$SECRET"

# Sin sesion los nuevos comandos deben rechazarse.
OUT=$(run_multi "mkdir -p -path=/sin/sesion")
assert_contains "1. MKDIR exige sesion" "$OUT" "\\[ERROR\\] MKDIR: necesita iniciar sesion"
OUT=$(run_multi "mkfile -r -path=/sin/sesion/a.txt")
assert_contains "2. MKFILE exige sesion" "$OUT" "\\[ERROR\\] MKFILE: necesita iniciar sesion"
OUT=$(run_multi "cat -file1=/a.txt")
assert_contains "3. CAT exige sesion" "$OUT" "\\[ERROR\\] CAT: necesita iniciar sesion"

CMDS="mkdisk -size=12 -unit=M -path=$DISK
fdisk -size=8 -unit=M -path=$DISK -name=PartS4
mount -path=$DISK -name=PartS4
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkdir -path=/no/existe/hijo
mkdir -p -path=/home/user/docs
mkfile -size=15 -path=/home/user/docs/a.txt
cat -file1=/home/user/docs/a.txt
mkfile -size=999 -cont=$HOST -path=/home/user/docs/b.txt
cat -file1=/home/user/docs/a.txt -file2=/home/user/docs/b.txt
mkfile -r -size=20 -path=/otra/ruta/c.txt
cat -file1=/otra/ruta/c.txt
mkfile -size=900 -path=/home/user/docs/large.txt
cat -file1=/home/user/docs/large.txt
mkfile -size=5 -path=/home/user/docs/a.txt
cat -file1=/home/user/docs/a.txt
mkdir -p -path=/muchos
mkdir -path=/muchos/d01
mkdir -path=/muchos/d02
mkdir -path=/muchos/d03
mkdir -path=/muchos/d04
mkdir -path=/muchos/d05
mkdir -path=/muchos/d06
mkdir -path=/muchos/d07
mkdir -path=/muchos/d08
mkdir -path=/muchos/d09
mkdir -path=/muchos/d10
mkfile -size=3 -path=/muchos/d10/z.txt
cat -file1=/muchos/d10/z.txt
mkgrp -name=usuarios
mkusr -user=alice -pass=abc -grp=usuarios
mkfile -cont=$SECRET -path=/home/user/docs/priv.txt
chmod -path=/home/user/docs/priv.txt -ugo=600
logout
login -user=alice -pass=abc -id=441A
cat -file1=/home/user/docs/priv.txt
mkfile -size=2 -path=/home/user/docs/alice.txt
logout
login -user=root -pass=123 -id=441A
chown -path=/home/user/docs/priv.txt -user=alice
logout
login -user=alice -pass=abc -id=441A
cat -file1=/home/user/docs/priv.txt
exit"

OUT=$(run_multi "$CMDS")

assert_contains "4. preparacion MKFS" "$OUT" "\\[OK\\] MKFS"
assert_contains "5. LOGIN root" "$OUT" "\\[OK\\] LOGIN: sesion iniciada como root"
assert_contains "6. MKDIR sin -p detecta padres faltantes" "$OUT" "\\[ERROR\\] MKDIR: no existen todas las carpetas padre"
assert_contains "7. MKDIR -p crea ruta completa" "$OUT" "\\[OK\\] MKDIR: carpeta creada /home/user/docs"
assert_contains "8. MKFILE -size crea archivo" "$OUT" "\\[OK\\] MKFILE: archivo /home/user/docs/a.txt creado \\(15 bytes\\)"
assert_contains "9. CAT lee contenido numerico" "$OUT" "012345678901234"
assert_contains "10. -cont tiene prioridad sobre -size" "$OUT" "\\[OK\\] MKFILE: archivo /home/user/docs/b.txt creado \\(29 bytes\\)"
assert_contains "11. CAT lee archivo host" "$OUT" "ARCHIVO_HOST_S4"
assert_contains "12. CAT multiple conserva segundo archivo" "$OUT" "segunda_linea"
assert_contains "13. MKFILE -r crea carpetas padre" "$OUT" "\\[OK\\] MKFILE: archivo /otra/ruta/c.txt creado \\(20 bytes\\)"
assert_contains "14. archivo grande de 20 bytes se lee" "$OUT" "01234567890123456789"
assert_contains "15. MKFILE soporta archivo con apuntador indirecto" "$OUT" "\[OK\] MKFILE: archivo /home/user/docs/large.txt creado \(900 bytes\)"
assert_contains "16. CAT lee archivo de 900 bytes" "$OUT" "012345678901234567890123456789"
assert_contains "17. MKFILE sobreescribe archivo existente" "$OUT" "\\[INFO\\] MKFILE: el archivo ya existia; se sobreescribio su contenido"
assert_contains "18. contenido sobreescrito es correcto" "$OUT" "01234"
assert_contains "19. carpeta amplia acepta mas de dos hijos" "$OUT" "\\[OK\\] MKDIR: carpeta creada /muchos/d10"
assert_contains "20. archivo dentro del decimo hijo funciona" "$OUT" "\\[OK\\] MKFILE: archivo /muchos/d10/z.txt creado \\(3 bytes\\)"
assert_contains "21. CAT en carpeta ampliada funciona" "$OUT" "(^|[^0-9])012([^0-9]|$)"
assert_contains "22. CHMOD cambia permisos" "$OUT" "\\[OK\\] CHMOD: permisos 600 aplicados"
assert_contains "23. CAT niega lectura sin permiso" "$OUT" "\\[ERROR\\] CAT: no tiene permiso de lectura sobre /home/user/docs/priv.txt"
assert_contains "24. permisos 664 impiden escritura a otros en carpeta padre" "$OUT" "\\[ERROR\\] MKFILE: no tiene permiso de escritura en la carpeta padre"
assert_contains "25. CHOWN cambia propietario" "$OUT" "\\[OK\\] CHOWN: propietario de /home/user/docs/priv.txt cambiado a alice"
# Tras CHOWN, alice es propietaria y 600 le concede lectura.
assert_contains "26. nuevo propietario puede leer con 600" "$OUT" "OWNERSHIP_OK_S4"

# Validaciones de flags y parametros dinamicos de CAT.
SETUP="mkdisk -size=5 -unit=M -path=$TEST_DIR/flags.mia
fdisk -size=3 -unit=M -path=$TEST_DIR/flags.mia -name=Flags
mount -path=$TEST_DIR/flags.mia -name=Flags
mkfs -id=441A
login -user=root -pass=123 -id=441A"
OUT2=$(run_multi "$SETUP
mkdir -p=1 -path=/x
mkfile -r=1 -path=/x/a.txt
cat -foo=/x/a.txt
chmod -path=/ -ugo=999")
assert_contains "27. -p no recibe valor" "$OUT2" "(parametro sin valor|parametro -p no recibe valor)"
assert_contains "28. -r no recibe valor" "$OUT2" "parametro -r no recibe valor"
assert_contains "29. CAT rechaza parametro distinto de fileN" "$OUT2" "\\[ERROR\\] CAT: parametro no reconocido -foo"
assert_contains "30. CHMOD valida octal" "$OUT2" "\\[ERROR\\] CHMOD: -ugo debe contener exactamente tres digitos octales"

echo "============================================"
echo " Resultados: $PASS pasaron, $FAIL fallaron"
if [ "$FAIL" -eq 0 ]; then
  echo " Todas las pruebas obligatorias pasaron"
  exit 0
else
  echo " Hay pruebas fallidas"
  exit 1
fi
