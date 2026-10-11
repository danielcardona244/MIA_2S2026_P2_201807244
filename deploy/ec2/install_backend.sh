#!/usr/bin/env bash
set -euo pipefail

# Ejecutar desde el repositorio en la instancia EC2:
#   bash deploy/ec2/install_backend.sh
# Opcional:
#   EXTREAMFS_PORT=8080 EXTREAMFS_HOST=0.0.0.0 bash deploy/ec2/install_backend.sh

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PORT="${EXTREAMFS_PORT:-8080}"
HOST="${EXTREAMFS_HOST:-0.0.0.0}"
SERVICE_NAME="extreamfs"
RUN_USER="${SUDO_USER:-$USER}"

if [[ "$RUN_USER" == "root" ]]; then
  echo "[ERROR] Ejecute el script como el usuario normal de la instancia (por ejemplo ubuntu), no como root." >&2
  exit 1
fi

if ! [[ "$PORT" =~ ^[0-9]+$ ]] || (( PORT < 1 || PORT > 65535 )); then
  echo "[ERROR] EXTREAMFS_PORT invalido: $PORT" >&2
  exit 1
fi

if [[ "$HOST" != "0.0.0.0" && "$HOST" != "127.0.0.1" ]]; then
  echo "[AVISO] Se usara la direccion IPv4 indicada: $HOST"
fi

echo "[1/5] Instalando dependencias del backend..."
sudo apt-get update
sudo apt-get install -y build-essential cmake graphviz curl

echo "[2/5] Compilando ExtreamFS..."
rm -rf "$ROOT/backend/build"
cmake -S "$ROOT/backend" -B "$ROOT/backend/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/backend/build" --parallel 1

mkdir -p "$ROOT/data" "$ROOT/reports"

echo "[3/5] Instalando servicio systemd..."
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
sudo tee "$SERVICE_FILE" >/dev/null <<SERVICE
[Unit]
Description=ExtreamFS C++ REST API
After=network.target

[Service]
Type=simple
User=$RUN_USER
WorkingDirectory=$ROOT
ExecStart=$ROOT/backend/build/extreamfs --server $PORT $HOST
Restart=on-failure
RestartSec=2
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
SERVICE

sudo systemctl daemon-reload
sudo systemctl enable --now "$SERVICE_NAME"

echo "[4/5] Verificando servicio..."
sleep 1
sudo systemctl --no-pager --full status "$SERVICE_NAME" || true

echo "[5/5] Probando health local..."
curl --fail --silent --show-error "http://127.0.0.1:${PORT}/api/health"
echo

echo "[OK] Backend instalado."
echo "     Bind: $HOST:$PORT"
echo "     Logs: sudo journalctl -u $SERVICE_NAME -f"
echo "     Recuerde permitir TCP/$PORT en el Security Group de EC2."
