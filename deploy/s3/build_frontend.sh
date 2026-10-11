#!/usr/bin/env bash
set -euo pipefail

# Uso:
#   bash deploy/s3/build_frontend.sh http://EC2_PUBLIC_IP:8080
# Opcionalmente sincroniza con un bucket ya configurado:
#   bash deploy/s3/build_frontend.sh http://EC2_PUBLIC_IP:8080 nombre-bucket

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Uso: $0 <API_URL> [S3_BUCKET]" >&2
  exit 1
fi

API_URL="${1%/}"
BUCKET="${2:-}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [[ ! "$API_URL" =~ ^https?:// ]]; then
  echo "[ERROR] API_URL debe comenzar con http:// o https://" >&2
  exit 1
fi

cd "$ROOT/frontend"

echo "[1/3] Instalando dependencias reproducibles..."
npm ci

echo "[2/3] Compilando frontend con VITE_API_URL=$API_URL"
VITE_API_URL="$API_URL" npm run build

echo "[OK] Build creado en $ROOT/frontend/dist"

if [[ -n "$BUCKET" ]]; then
  if ! command -v aws >/dev/null 2>&1; then
    echo "[ERROR] Se indico un bucket pero AWS CLI no esta instalado." >&2
    exit 1
  fi
  echo "[3/3] Sincronizando dist/ con s3://$BUCKET ..."
  aws s3 sync "$ROOT/frontend/dist/" "s3://$BUCKET/" --delete
  echo "[OK] Archivos sincronizados. Active Static website hosting desde S3 si aun no lo hizo."
else
  echo "[3/3] Sin sincronizacion S3 (no se proporciono bucket)."
fi
