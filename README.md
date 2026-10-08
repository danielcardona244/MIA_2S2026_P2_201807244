# ExtreamFS - Proyecto 2 MIA 2S2026

Simulador educativo de un sistema de archivos **EXT2** sobre archivos binarios `.mia`, con backend en **C++17**, frontend en **React + Vite**, API REST local y reportes generados con **Graphviz**.

## Arquitectura

- **Frontend:** React/Vite. Entrada de comandos, salida, carga de scripts `.smia`.
- **API REST:** `GET /api/health` y `POST /api/execute`.
- **Backend C++:** parser de comandos, discos/particiones, montajes, EXT2, usuarios, archivos/permisos y reportes.
- **Persistencia:** archivos `.mia` de tamaño fijo con MBR, EBR, superbloque, bitmaps, inodos y bloques.
- **Reportes Graphviz:** MBR, DISK, INODE, BLOCK, bm_inode, bm_block, TREE, SB, FILE y LS.

## Dependencias

```bash
sudo apt update
sudo apt install build-essential cmake graphviz nodejs npm
```

## Compilar backend

```bash
cmake -S backend -B backend/build
cmake --build backend/build
```

## Preparar frontend

```bash
cd frontend
npm install
cd ..
```

## Ejecutar

Terminal 1:

```bash
backend/build/extreamfs --server 8080
```

Terminal 2:

```bash
cd frontend
npm run dev
```

Abrir `http://127.0.0.1:5173`.

## Flujo mínimo

```text
mkdisk -size=15 -unit=M -path=/tmp/demo.mia
fdisk -size=10 -unit=M -path=/tmp/demo.mia -name=Part1
mount -path=/tmp/demo.mia -name=Part1
mkfs -id=441A
login -user=root -pass=123 -id=441A
mkdir -p -path=/home/docs
mkfile -path=/home/docs/a.txt -size=25
cat -file1=/home/docs/a.txt
rep -id=441A -path=/tmp/tree.jpg -name=tree
```

## Pruebas

```bash
bash backend/tests/test_p2_sprint1.sh
bash backend/tests/test_sprint6.sh
bash backend/tests/test_sprint5.sh
bash backend/tests/test_sprint4.sh
bash backend/tests/test_sprint3.sh
bash backend/tests/test_sprint2.sh
```


## Proyecto 2 - Sprint 1

Ya se incorporaron las primeras extensiones solicitadas para Proyecto 2:

- `FDISK -delete=fast|full` para primarias, extendidas y lógicas.
- `FDISK -add=<valor>` positivo o negativo, respetando espacio contiguo.
- `UNMOUNT -id=<id>`, reiniciando el correlativo a `0`.
- Confirmación de `FDISK -delete` en consola y confirmación previa desde el frontend.
- Conservación de tamaños binarios de `MBR`, `Partition` y `EBR`.

## Documentación

La carpeta `docs/` contiene:

- `Manual_Tecnico_ExtreamFS.pdf`
- `Manual_Usuario_ExtreamFS.pdf`
- `Diagrama_Arquitectura_ExtreamFS.pdf`
- `Diagrama_Flujo_ExtreamFS.pdf`
- `Informe_Impacto_ExtreamFS.pdf`
- `Plan_Mantenimiento_ExtreamFS.pdf`

También se incluyen versiones `.docx` editables de los documentos textuales.

## Nota de implementación

Los montajes y la sesión se conservan en RAM mientras el proceso backend permanece activo. El ID de montaje usa el prefijo `44` correspondiente a los dos últimos dígitos del carné `201807244`.
