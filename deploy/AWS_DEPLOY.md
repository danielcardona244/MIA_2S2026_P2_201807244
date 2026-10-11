# ExtreamFS - Despliegue AWS (Sprint 7)

Arquitectura requerida por Proyecto 2:

```text
Navegador
   |
   +--> S3 Static Website: React/Vite
   |          |
   |          +---- HTTP REST ----> EC2:8080
   |                                C++ ExtreamFS
   |                                archivos .mia
   |                                Graphviz
```

## 1. EC2 - backend C++

Crear una instancia Linux/Ubuntu con IP publica. En el Security Group:

- SSH TCP 22: limitar a su propia IP cuando sea posible.
- Custom TCP 8080: permitir el acceso que necesite la demostracion. Para que un navegador
  en Internet pueda llegar directamente al backend se requiere una fuente que lo permita.

Clonar el repositorio y ejecutar:

```bash
git clone <URL_REPOSITORIO_PRIVADO>
cd MIA_2S2026_P2_201807244
bash deploy/ec2/install_backend.sh
```

El instalador compila el backend y crea el servicio `extreamfs.service` ejecutando:

```bash
backend/build/extreamfs --server 8080 0.0.0.0
```

Comandos utiles:

```bash
sudo systemctl status extreamfs
sudo systemctl restart extreamfs
sudo journalctl -u extreamfs -f
curl http://127.0.0.1:8080/api/health
```

Desde la computadora local probar:

```bash
curl http://EC2_PUBLIC_IP:8080/api/health
```

Debe responder JSON con `"ok":true`.

Para que los discos sobrevivan al reinicio de la instancia, crear los `.mia` en una ruta
persistente del proyecto, por ejemplo:

```text
/home/ubuntu/MIA_2S2026_P2_201807244/data/Disco1.mia
```

Los montajes y la sesion activa permanecen en RAM y deben restablecerse si el proceso
backend se reinicia.

## 2. S3 - frontend React

Antes de subir el frontend, construirlo indicando la URL publica del backend:

```bash
bash deploy/s3/build_frontend.sh http://EC2_PUBLIC_IP:8080
```

Esto ejecuta `npm ci` y `npm run build` con `VITE_API_URL` y genera `frontend/dist/`.

Crear un bucket S3 y habilitar **Static website hosting** usando:

- Index document: `index.html`
- Error document: `index.html`

Para un sitio S3 publico directo se necesita permitir lectura de los objetos. El archivo
`deploy/s3/bucket-policy.json.template` contiene la politica base; sustituir
`__BUCKET_NAME__` por el nombre real del bucket.

Luego puede sincronizar manualmente:

```bash
aws s3 sync frontend/dist/ s3://NOMBRE_BUCKET/ --delete
```

o compilar y sincronizar en un paso:

```bash
bash deploy/s3/build_frontend.sh http://EC2_PUBLIC_IP:8080 NOMBRE_BUCKET
```

## 3. HTTP/HTTPS

El website endpoint directo de S3 usa HTTP. Para el esquema minimo del proyecto, use el
endpoint de S3 por HTTP y configure `VITE_API_URL` con `http://EC2_PUBLIC_IP:8080`.

Si posteriormente coloca CloudFront/HTTPS delante del frontend, el navegador bloqueara un
backend HTTP como contenido mixto. En ese escenario tambien debera publicar el backend con
HTTPS (por ejemplo mediante un proxy/reverse proxy con certificado). Eso no es necesario
para la arquitectura minima S3 website + EC2 solicitada.

## 4. Prueba de aceptacion en nube

Desde la URL publica de S3:

1. Comprobar que la pagina carga.
2. Ejecutar `mkdisk`, `fdisk`, `mount` y `mkfs -fs=3fs`.
3. Iniciar sesion desde la GUI.
4. Crear carpetas/archivos y comprobar que aparecen en el explorador.
5. Probar `rename`, `copy`, `move`, `find` y `remove`.
6. Abrir Journaling EXT3.
7. Ejecutar LOSS y comprobar bitmaps antes/despues.
8. Cerrar sesion desde la GUI.

Capturar evidencias de:

- instancia EC2 ejecutando el servicio;
- `/api/health` publico;
- bucket S3 y Static website hosting;
- URL del frontend en S3;
- frontend comunicandose con EC2;
- explorador y journaling funcionando desde la nube.
