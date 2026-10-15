# ExtreamFS - Proyecto 2 MIA 2S2026

**C++ DISK ONLINE** - simulador educativo de discos y sistemas de archivos EXT2/EXT3 sobre archivos binarios `.mia`.

## Estado final

- Backend: C++17 desplegado en AWS EC2 Ubuntu 24.04 LTS.
- Frontend: React + Vite desplegado como Static Website en AWS S3.
- Comunicación: HTTP REST + JSON + CORS.
- Persistencia: MBR, EBR, Superblock, Journal, bitmaps, inodos y bloques.
- GUI: Terminal, carga `.smia`, Login/Logout, Explorador de solo lectura y Journaling.
- EXT3: journaling persistente y simulación `LOSS`.
- Servicio: `extreamfs.service` administrado por systemd.

## Arquitectura

```text
Navegador
   |
   v
S3 Static Website - React/Vite
   |
   | HTTP REST / JSON
   v
EC2 Ubuntu :8080 - C++ ExtreamFS
   |
   +--> archivos .mia
   +--> Graphviz / reportes
```

### Despliegue utilizado

- S3: `http://extreamfs-p2-201807244.s3-website.us-east-2.amazonaws.com`
- EC2 API: `http://18.191.144.83:8080`
- Región: `us-east-2`

> La IP pública de EC2 puede cambiar después de un Stop/Start. Si cambia, reconstruir el frontend con el nuevo `VITE_API_URL`.

## Estructura

```text
backend/        C++17, API REST, discos, EXT2/EXT3, usuarios, permisos, reportes
data/           ubicación recomendada para discos .mia en EC2
deploy/ec2/     instalación del backend y servicio systemd
deploy/s3/      build del frontend y política pública de S3
docs/           manual técnico, manual de usuario y guías finales
frontend/       React + Vite
scripts/        scripts de prueba/demostración
```

## Compilar localmente

```bash
sudo apt update
sudo apt install build-essential cmake graphviz nodejs npm
cmake -S backend -B backend/build
cmake --build backend/build
cd frontend && npm install && cd ..
```

## Ejecutar localmente

```bash
backend/build/extreamfs --server 8080 127.0.0.1
```

En otra terminal:

```bash
cd frontend
VITE_API_URL=http://127.0.0.1:8080 npm run dev
```

## Despliegue EC2

```bash
bash deploy/ec2/install_backend.sh
sudo systemctl status extreamfs --no-pager
curl http://127.0.0.1:8080/api/health
```

El servicio ejecuta:

```text
backend/build/extreamfs --server 8080 0.0.0.0
```

En `t3.micro` se utiliza 2 GiB de swap y la compilación se limita a `--parallel 1`.

## Despliegue S3

```bash
cd frontend
VITE_API_URL=http://EC2_PUBLIC_IP:8080 npm run build
```

Subir el **contenido** de `frontend/dist/` al bucket:

```text
index.html
assets/
```

Configurar `index.html` como documento de índice y error del Static Website.

## Endpoints REST

| Método | Ruta | Función |
|---|---|---|
| GET | `/api/health` | Estado del backend |
| GET | `/api/session` | Sesión activa |
| POST | `/api/login` | Login GUI |
| POST | `/api/logout` | Logout GUI |
| GET | `/api/disks` | Discos conocidos |
| POST | `/api/partitions` | Particiones de un disco |
| POST | `/api/fs/list` | Listado de carpeta |
| POST | `/api/fs/file` | Contenido de archivo |
| POST | `/api/execute` | Ejecución de comandos/scripts |
| POST | `/api/journaling` | Journal EXT3 |

## Comandos

`MKDISK`, `RMDISK`, `FDISK`, `MOUNT`, `MOUNTED`, `UNMOUNT`, `MKFS`, `JOURNALING`, `LOSS`, `LOGIN`, `LOGOUT`, `MKGRP`, `RMGRP`, `MKUSR`, `RMUSR`, `CHGRP`, `MKDIR`, `MKFILE`, `CAT`, `CHMOD`, `CHOWN`, `REMOVE`, `RENAME`, `COPY`, `MOVE`, `FIND`, `REP`.

## Prueba mínima en nube

1. Abrir el website de S3.
2. Crear disco/partición, montar y formatear `3fs`.
3. Iniciar sesión `root / 123` desde la GUI.
4. Crear carpeta/archivo y visualizarlo en Explorador.
5. Mostrar Journaling.
6. Ejecutar `LOSS` sobre el disco de prueba y verificar bitmaps/journal.

## Documentación

- `docs/Manual_Tecnico_ExtreamFS_P2_Final.pdf`
- `docs/Manual_Usuario_ExtreamFS_P2_Final.pdf`
- `docs/GUIA_DEFENSA.md`
- `docs/CHECKLIST_ENTREGA.md`

## Nota

Los archivos `.mia`, `backend/build/`, `frontend/node_modules/`, `frontend/dist/` y archivos de entorno local están excluidos por `.gitignore`. La llave privada `.pem` nunca debe almacenarse en el repositorio.
