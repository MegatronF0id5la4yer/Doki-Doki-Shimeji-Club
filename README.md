# DDLC Shimeji (Natsuki, Monika, Sayori y Yuri — Wayland)

Acompañante de escritorio para Wayland (esquina inferior izquierda o derecha) implementado en **C++17** (`gtk-layer-shell` + GTK3 + Cairo + librsvg), **Rust** (`natsuki_core`: cargador/rasterizador 3D de modelos **L4D2 Steam Workshop (.vpk)** + compositor de capas **PNG 2D** RGBA->ARGB32 + configuración JSON) y **Python** (`natsuki_brain.py`: >1,200 diálogos en español y >1,000 en inglés sin emojis, Easter eggs e integración opcional con Gemini).

---

## Dependencias necesarias

### 1. Entorno gráfico (runtime)
- Sesión **Wayland** con un compositor compatible con el protocolo `wlr-layer-shell` (`zwlr_layer_shell_v1`), por ejemplo **Hyprland**, **Sway**, **Wayfire**, **River** o **niri** (requerido por `gtk-layer-shell`).

### 2. Herramientas de compilación
- Compilador **C++17**: `g++` (probado con GCC 16.2.1)
- **GNU Make**: `make` (probado con 4.4.1)
- **pkg-config** / `pkgconf` (probado con 3.0.10)
- Toolchain de **Rust**: `rustc` y `cargo` con Edition 2021 (probado con Rust 1.99.1)
  - Crates en `rust_core/Cargo.toml` (resueltos automáticamente por `cargo` mediante `rust_core/Cargo.lock`):
    - `png = "0.17"`
    - `serde = "1.0"` (feature `derive`)
    - `serde_json = "1.0"`

### 3. Bibliotecas y cabeceras C / C++ (`pkg-config` / headers)
- `gtk+-3.0` (probado con `gtk3` 3.24.52)
- `gtk-layer-shell-0` (probado con `gtk-layer-shell` 0.10.1)
- `librsvg-2.0` (probado con `librsvg` 2.63.4)
- `pangocairo` (probado con `pango` 1.58.2, junto con Cairo, GDK, GIO y GdkPixbuf)
- Cabecera C++ `nlohmann/json.hpp` (probado con `nlohmann-json` 3.12.0)

### 4. Runtime de Python
- `python3` (probado con Python 3.14.10)
- Módulo `requests` (`python-requests` 2.34.2, o `pip install requests`) — requerido por `brain/natsuki_brain.py` para consultar Gemini; los diálogos locales funcionan también sin clave API ni red.

---

## Instalación rápida de dependencias

### Arch Linux (paquetes exactos verificados)
```bash
sudo pacman -S --needed \
  gcc make pkgconf rust \
  gtk3 gtk-layer-shell librsvg pango nlohmann-json \
  python python-requests
```

### Otras distribuciones Linux
Instala los paquetes `-dev` / `-devel` equivalentes que provean:
- Compilador C++17 (`g++`), `make`, `pkg-config`, `rustc` y `cargo`
- Módulos `pkg-config`: `gtk+-3.0`, `gtk-layer-shell-0`, `librsvg-2.0`, `pangocairo`
- Cabecera `<nlohmann/json.hpp>`
- `python3` y el paquete Python `requests`

---

## Modelos 3D (Steam L4D2) y Skins PNG 2D

1. **Modelos 3D de Steam Workshop (Left 4 Dead 2)** *(por defecto si están instalados)*:
   - Se cargan directamente desde `$HOME/.local/share/Steam/steamapps/common/Left 4 Dead 2/left4dead2/addons/workshop/` (o `NATSUKI_L4D2_WORKSHOP_DIR`):
     - `2565563118.vpk` (Natsuki)
     - `2561380893.vpk` (Monika)
     - `2567184223.vpk` (Sayori)
     - `2564088900.vpk` (Yuri)
   - Rotación 3D libre arrastrando horizontalmente sobre la Doki o usando los botones del cajón **Dokis, Ropa y 3D**.

2. **Skins 2D en PNG (`chikinenas`)** *(seleccionables en cualquier momento en el mismo selector de Ropa/Modelo)*:
   Por defecto, `natsuki_engine_new` en `rust_core/src/lib.rs` busca las carpetas `Natsuki/`, `Monika/`, `Sayori/` y `Yuri/` en:

```text
/home/foidslayer/Desktop/chikinenas/
```

Subcarpetas 2D soportadas automáticamente por `rust_core/src/catalog.rs`:
- **Natsuki**: Salvation Remake (`uniform`, `casual`, `swimsuit`), Just Natsuki (sentada), Vanilla, Blue Skies ES (`winter`, `halloween`), Exit Music (`coat`, `scarf`, `jumper`), New Eyes.
- **Monika**: Monika After Story (sentada), Salvation Remake (`uniform`), Vanilla.
- **Sayori**: Salvation Remake (`uniform`, `casual`), Vanilla (`uniform`, `casual`), Blue Skies ES, New Eyes.
- **Yuri**: Vanilla (`uniform`, `casual`), Blue Skies ES (`winter`, `halloween`), Exit Music (`unbutton`, `funeral`), New Eyes.

Si instalas el proyecto en otro equipo:
1. Coloca los `.vpk` en tu carpeta `addons/workshop` de L4D2 (o define `NATSUKI_L4D2_WORKSHOP_DIR`), y/o las carpetas PNG en `/home/foidslayer/Desktop/chikinenas/` (o `DDLC_SHIMEJI_SPRITES`), **o bien**
2. Sin L4D2 instalado, usa automáticamente cualquier skin PNG disponible.

---

## Compilación, pruebas y ejecución

Desde la raíz del proyecto (`natsuki-shimeji/`):

```bash
# 1. Compilar la librería compartida Rust y el binario C++
make all

# 2. Ejecutar tests de Rust y autocomprobación del módulo Python
make test

# 3. Comprobar arranque del binario nativo e iconos/catálogo (sin mostrar UI permanente)
./natsuki-shimeji --self-test

# 4. Iniciar Natsuki Shimeji
./run.sh
```

Para limpiar artefactos compilados (`natsuki-shimeji` y `rust_core/target`):
```bash
make clean
```

---

## Uso y configuración

- **Clic izquierdo sobre la Doki**:
  - Reacción con animación de rebote; incluye dos zonas especiales sensibles que disparan diálogos enojados/tsundere.
  - En modelos **L4D2 3D**, arrastra horizontalmente sobre el personaje para rotar su ángulo 3D.
- **Clic derecho sobre la Doki**:
  - Muestra u oculta el menú inferior (dock) con botones SVG: siguiente diálogo, chat rápido, selector de Doki/ropa/3D (modelos L4D2 3D y skins PNG), cambio rápido `3D / 2D`, cambio de esquina **Izquierda / Derecha**, tu nombre, ajustes/Gemini API, pausa, **modo reactivo al sonido**, ocultar barra y cerrar (`Salir`).
- **Modo reactivo al sonido** *(opcional, usa `parec` de `pipewire-pulse`/`libpulse` o `pw-record`)*:
  - Actívalo con el ícono de música en el dock o desde **Ajustes** (fuente **Audio del PC** o **Micrófono** y control deslizante de sensibilidad). La Doki baila al compás de los graves, lanza notas vectoriales y muestra un mini ecualizador pastel en tiempo real.
- **Tu nombre y Easter eggs**:
  - Al iniciar sin nombre guardado (o desde el botón de usuario / Ajustes) te pregunta tu nombre. Poner **Batman** o **Monika** desbloquea diálogos raros especiales de cuarta pared.
- **Invocación `*amo x doki*`**:
  - Escribir `*amo x doki*` (o variantes como `amo a las dokis`, `amo a natsuki`) en el chat invoca una lluvia transparente a pantalla completa con las 4 Dokis volando e íconos vectoriales SVG.
- **Configuración persistente**:
  - Guardada automáticamente en `~/.config/natsuki-shimeji/config.json` (`character_id`, `style_id`, `character_styles`, `side` `"left"`|`"right"`, `user_name`, `gemini_api_key`, `model`, `language`, `talk_interval_s`, `sprite_height_px`, `speech_opacity`, `speech_bg`/`fg`/`border`, `muted`, `autonomous`, `always_on_top`, `sound_reactive`, `audio_source`, `audio_sensitivity`).
- **Estructura principal**:
  - `cpp_ui/{main.cpp,audio_reactive.hpp,brain_client.hpp,svg_icons.hpp}`: interfaz Wayland (`gtk-layer-shell`), monitor de audio en tiempo real, dock, cajones y overlay `*amo x doki*`.
  - `rust_core/src/{source3d.rs,catalog.rs,compositor.rs,config.rs,lib.rs}`: motor 3D Source Engine (`.vpk`/`.mdl`/`.vvd`/`.dx90.vtx`/`.vtf`) y compositor PNG 2D.
  - `brain/{natsuki_brain.py,doki_dialogues.py,local_dialogues.py,local_dialogues_en.py}`: diálogos locales multi-Doki (>2,200 frases en total) y cliente REST para Gemini.

