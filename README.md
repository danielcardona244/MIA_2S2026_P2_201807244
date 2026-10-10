# ExtreamFS - Proyecto 2 MIA 2S2026

Simulador educativo de sistemas de archivos **EXT2/EXT3** sobre archivos binarios `.mia`, con backend en **C++17**, frontend en **React + Vite**, API REST local y reportes generados con **Graphviz**.

## Arquitectura

- **Frontend:** React/Vite. Entrada de comandos, salida, carga de scripts `.smia`.
- **API REST:** `GET /api/health` y `POST /api/execute`.
- **Backend C++:** parser de comandos, discos/particiones, montajes, EXT2/EXT3, journaling, usuarios, archivos/permisos y reportes.
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
bash backend/tests/test_p2_sprint2.sh
bash backend/tests/test_p2_sprint3.sh
bash backend/tests/test_p2_sprint4.sh
bash backend/tests/test_p2_sprint5.sh
bash backend/tests/test_p2_sprint6.sh
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

## Proyecto 2 - Sprint 2

Se agregó soporte para sistemas de archivos EXT2/EXT3 mediante `mkfs -fs=2fs|3fs`.
EXT3 reserva el área de journaling antes de los bitmaps y registra transacciones usando
las estructuras `Journal` e `Information` definidas en el enunciado. El comando
`journaling -id=<id>` muestra las transacciones almacenadas sin generar un reporte Graphviz.
Los comandos de mutación existentes (MKDIR, MKFILE, MKGRP, RMGRP, MKUSR, RMUSR,
CHGRP, CHMOD y CHOWN) escriben journal cuando la partición utiliza EXT3.

## Proyecto 2 - Sprint 3

Se incorporaron los comandos solicitados para administrar eliminaciones y nombres:

- `REMOVE -path=<ruta>` elimina archivos y carpetas recursivamente liberando inodos,
  bloques de datos y bloques apuntadores. Respeta el permiso de escritura; si dentro de
  una carpeta hay elementos no eliminables, conserva esos elementos y sus carpetas padre,
  manteniendo las eliminaciones de los hijos que sí tenían permiso, como muestra el ejemplo
  del enunciado.
- `RENAME -path=<ruta> -name=<nuevo_nombre>` cambia únicamente la entrada de nombre en
  la carpeta padre, conserva el inodo y el contenido, valida duplicados y permisos de escritura.
- En EXT3, ambas operaciones se registran en el journaling.
- Se protegen la carpeta raíz, nombres inválidos y rutas inexistentes.

## Proyecto 2 - Sprint 4

Se incorporaron los comandos de copia, movimiento y búsqueda solicitados:

- `COPY -path=<ruta> -destino=<carpeta>` copia archivos o carpetas recursivamente.
  Requiere lectura en el origen y escritura en el destino; los hijos sin permiso de
  lectura se omiten sin impedir la copia de los demás elementos.
- `MOVE -path=<ruta> -destino=<carpeta>` mueve archivos o carpetas dentro de la
  partición cambiando referencias de directorio, sin duplicar inodos ni bloques.
  Valida escritura sobre el objeto origen y la carpeta destino, duplicados y ciclos.
- `FIND -path=<carpeta> -name=<patron>` busca recursivamente y admite `?` para un
  carácter y `*` para uno o más caracteres, respetando permisos de lectura.
- En EXT3, `COPY`, `MOVE` y `FIND` quedan registrados en el journaling.

## Proyecto 2 - Sprint 5

Se agregó la simulación de pérdida de EXT3 y la visualización dedicada del journaling:

- `LOSS -id=<id>` funciona únicamente sobre particiones EXT3 montadas. Antes de limpiar
  muestra una vista previa de los bitmaps de inodos y bloques y, después de la operación,
  confirma que ambos quedaron en cero.
- LOSS limpia con `\0` el bitmap de inodos, bitmap de bloques, área de inodos y área de
  bloques, preservando el superbloque y el área de journaling.
- La operación `LOSS` se registra en el journal antes de simular la pérdida, de modo que la
  evidencia de la transacción sobrevive al fallo.
- Se agregó `POST /api/journaling`, que devuelve las transacciones de una partición EXT3
  montada como JSON con operación, ruta, contenido, fecha y hora.
- El frontend React incorpora un apartado **Journaling EXT3** que consulta ese endpoint y
  muestra las transacciones en una tabla.
- El frontend solicita confirmación antes de ejecutar un comando `LOSS` por tratarse de una
  operación destructiva.
- Los reportes `bm_inode` y `bm_block` continúan disponibles después de LOSS para evidenciar
  los bitmaps en cero.
## Proyecto 2 - Sprint 6

Se incorporó el flujo gráfico de autenticación y el visualizador de solo lectura solicitado:

- Inicio y cierre de sesión desde React mediante `POST /api/login` y `POST /api/logout`.
- `GET /api/session` permite conservar y consultar la sesión activa entre solicitudes.
- `GET /api/disks` muestra los discos conocidos por la instancia con capacidad, fit, fecha,
  cantidad de particiones y montajes activos.
- `POST /api/partitions` lista particiones primarias, extendidas y lógicas mostrando tamaño,
  fit, estado de montaje e ID cuando corresponde.
- `POST /api/fs/list` navega carpetas desde `/` en modo solo lectura y devuelve nombre, tipo,
  tamaño, permisos, UID/GID, fecha de modificación y disponibilidad de lectura.
- `POST /api/fs/file` permite visualizar el contenido de archivos de texto respetando permisos.
- El frontend incorpora vistas **Terminal**, **Iniciar sesión**, **Explorador** y
  **Journaling EXT3**, además de breadcrumbs para navegar carpetas y un visor de contenido.
- La sesión iniciada desde la GUI es la misma que utilizan los comandos ejecutados en la
  terminal.
- Se agregó soporte para `VITE_API_URL`, preparando el frontend para apuntar al backend de
  EC2 cuando se realice el despliegue en AWS.

