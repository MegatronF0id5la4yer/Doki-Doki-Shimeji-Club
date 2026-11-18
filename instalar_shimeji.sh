#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Instalador / Preparador de Doki Shimeji Chad ==="

# 1. Crear y copiar los PNGs 2D (chikinenas) en ~/Desktop/chikinenas
DESKTOP_CHIK="$HOME/Desktop/chikinenas"
mkdir -p "$HOME/Desktop"
if [[ -d "$ROOT/chikinenas" ]]; then
  if [[ "$ROOT/chikinenas" != "$DESKTOP_CHIK" ]]; then
    echo "-> Copiando sprites PNG 2D a: $DESKTOP_CHIK ..."
    mkdir -p "$DESKTOP_CHIK"
    cp -rf "$ROOT/chikinenas/." "$DESKTOP_CHIK/"
  fi
else
  echo "ADVERTENCIA: No se encontro $ROOT/chikinenas"
fi

# 2. Crear el path de Workshop de L4D2 y copiar los 4 modelos .vpk 3D
WORKSHOP_TARGET="$HOME/.local/share/Steam/steamapps/common/Left 4 Dead 2/left4dead2/addons/workshop"
if [[ -d "$ROOT/l4d2_workshop_vpks" ]]; then
  echo "-> Creando ruta de modelos 3D (.vpk) en: $WORKSHOP_TARGET ..."
  mkdir -p "$WORKSHOP_TARGET"
  cp -f "$ROOT/l4d2_workshop_vpks/"*.vpk "$WORKSHOP_TARGET/"
fi

# 3. Recompilar si estan disponibles cargo y g++ (opcional; ya incluye binario compilado)
if command -v cargo >/dev/null 2>&1 && command -v g++ >/dev/null 2>&1 && command -v make >/dev/null 2>&1; then
  echo "-> Compilando binario nativo para tu sistema..."
  (cd "$ROOT" && make all) || echo "-> Usando el binario precompilado incluido."
fi

chmod +x "$ROOT/run.sh" "$ROOT/natsuki-shimeji" 2>/dev/null || true
echo "=== Listo! Rutas creadas y skins 2D/3D instaladas ==="
echo "Puedes iniciar el shimeji ejecutando: $ROOT/run.sh"

