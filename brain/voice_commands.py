#!/usr/bin/env python3
"""
Local offline Spanish voice-command recognition (Vosk) & desktop action runner
for DDLC Doki Shimeji on Wayland / Hyprland.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import threading
import time
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple


DEFAULT_MODEL_REL = Path("models") / "vosk-model-small-es-0.42"
WAKE_WORDS = ("natsuki", "monika", "sayori", "yuri", "doki", "shimeji", "oye", "compu", "computadora")
OPEN_VERBS = ("abrir", "abre", "abiri", "iniciar", "inicia", "lanza", "lanzar", "ejecutar", "ejecuta", "pon", "poner")


def normalize_voice_text(text: str) -> str:
    """Lowercase ASCII-folded single-spaced normalization for Spanish voice transcripts."""
    raw = unicodedata.normalize("NFD", (text or "").strip().lower())
    no_accents = "".join(ch for ch in raw if unicodedata.category(ch) != "Mn")
    clean = re.sub(r"[^a-z0-9\s]", " ", no_accents)
    return re.sub(r"\s+", " ", clean).strip()


def _strip_optional_wake_word(norm: str) -> Tuple[str, bool]:
    words = norm.split()
    had_wake = False
    while words and words[0] in WAKE_WORDS:
        words.pop(0)
        had_wake = True
    while words and words[0] in ("por", "favor", "plis", "de", "la", "el"):
        if words[0] in ("por", "favor", "plis"):
            words.pop(0)
        else:
            break
    return " ".join(words), had_wake


@dataclass(frozen=True)
class VoiceMatch:
    action_id: str
    heard: str
    confidence: float
    cooldown_sec: float = 1.2


# Order matters: more specific rules (e.g. "explorador de archivos") precede generic ones ("archivos").
ACTION_RULES: List[Dict[str, Any]] = [
    {
        "id": "open_antigravity",
        "cooldown": 2.0,
        "require_open_verb": True,
        "phrases": [
            "antigravity", "anti gravity", "anti gravedad", "antigravedad",
            "anti gratis", "ante gravity", "antigravity ide",
        ],
        "reply_es": "Abriendo Antigravity!",
        "reply_en": "Opening Antigravity!",
        "mood": "excited",
    },
    {
        "id": "open_librewolf",
        "cooldown": 2.0,
        "require_open_verb": True,
        "phrases": [
            "librewolf", "libre wolf", "libre gol", "libre golf",
            "libre lobo", "libro wolf", "navegador libre", "navegador",
        ],
        "reply_es": "Abriendo LibreWolf!",
        "reply_en": "Opening LibreWolf!",
        "mood": "happy",
    },
    {
        "id": "open_steam",
        "cooldown": 2.5,
        "require_open_verb": True,
        "phrases": ["steam", "estin", "estim", "juegos"],
        "reply_es": "Abriendo Steam! A viciar un rato?",
        "reply_en": "Opening Steam! Time for games?",
        "mood": "excited",
    },
    {
        "id": "open_foot",
        "cooldown": 1.5,
        "require_open_verb": True,
        "phrases": ["foot", "fut", "food", "futbol", "terminal foot"],
        "reply_es": "Abriendo terminal Foot!",
        "reply_en": "Opening Foot terminal!",
        "mood": "smug",
    },
    {
        "id": "open_kitty",
        "cooldown": 1.5,
        "require_open_verb": True,
        "phrases": ["kitty", "kiti", "quiti", "quity", "terminal kitty", "terminal", "consola"],
        "reply_es": "Abriendo Kitty!",
        "reply_en": "Opening Kitty!",
        "mood": "happy",
    },
    {
        "id": "open_files",
        "cooldown": 1.8,
        "require_open_verb": True,
        "phrases": [
            "el explorador de archivos", "explorador de archivos", "explorador",
            "gestor de archivos", "mis archivos", "archivos", "carpetas", "thunar",
        ],
        "reply_es": "Abriendo el explorador de archivos!",
        "reply_en": "Opening file explorer!",
        "mood": "happy",
    },
    {
        "id": "volume_up",
        "cooldown": 0.45,
        "phrases": [
            "subir el volumen", "subir volumen", "sube el volumen", "sube volumen",
            "subele al volumen", "subele volumen", "sube audio", "subir audio",
            "aumentar volumen", "aumentar el volumen", "mas volumen", "volumen arriba",
            "sube sonido", "subir sonido", "subir el sonido",
        ],
        "reply_es": "Subiendo el volumen!",
        "reply_en": "Turning volume up!",
        "mood": "happy",
    },
    {
        "id": "volume_down",
        "cooldown": 0.45,
        "phrases": [
            "bajar el volumen", "bajar volumen", "baja el volumen", "baja volumen",
            "bajale al volumen", "bajale volumen", "baja audio", "bajar audio",
            "disminuir volumen", "disminuir el volumen", "menos volumen", "volumen abajo",
            "baja sonido", "bajar sonido", "bajar el sonido",
        ],
        "reply_es": "Bajando el volumen!",
        "reply_en": "Turning volume down!",
        "mood": "neutral",
    },
    {
        "id": "brightness_up",
        "cooldown": 0.45,
        "phrases": [
            "subir el brillo", "subir brillo", "sube el brillo", "sube brillo",
            "subele al brillo", "subele brillo", "mas brillo", "aumentar brillo",
            "aumentar el brillo", "brillo arriba", "subir luz", "mas luz",
        ],
        "reply_es": "Subiendo el brillo!",
        "reply_en": "Increasing brightness!",
        "mood": "happy",
    },
    {
        "id": "brightness_down",
        "cooldown": 0.45,
        "phrases": [
            "bajar el brillo", "bajar brillo", "baja el brillo", "baja brillo",
            "bajale al brillo", "bajale brillo", "menos brillo", "disminuir brillo",
            "disminuir el brillo", "brillo abajo", "bajar luz", "menos luz",
        ],
        "reply_es": "Bajando el brillo!",
        "reply_en": "Lowering brightness!",
        "mood": "neutral",
    },
    {
        "id": "stop_record_video",
        "cooldown": 2.0,
        "phrases": [
            "detener grabacion", "deten grabacion", "parar grabacion", "para grabacion",
            "detener la grabacion", "parar la grabacion", "dejar de grabar",
            "terminar grabacion", "terminar de grabar", "detener grabar", "parar grabar",
        ],
        "reply_es": "Deteniendo grabacion de video!",
        "reply_en": "Stopping video recording!",
        "mood": "happy",
    },
    {
        "id": "record_video",
        "cooldown": 2.0,
        "phrases": [
            "grabar video", "grabar un video", "graba video", "grabar pantalla",
            "graba pantalla", "graba la pantalla", "grabar la pantalla",
            "iniciar grabacion", "empezar a grabar", "grabando pantalla",
        ],
        "reply_es": "Listo! Selecciona el area para grabar video.",
        "reply_en": "Ready! Select region to record video.",
        "mood": "excited",
    },
    {
        "id": "screenshot",
        "cooldown": 1.8,
        "phrases": [
            "tomar screenshot", "tomar screen shot", "toma screenshot", "toma screen shot",
            "tomar captura de pantalla", "tomar captura", "toma captura", "toma captura de pantalla",
            "sacar screenshot", "sacar screen shot", "sacar captura",
            "captura de pantalla", "hacer screenshot", "hacer captura",
            "foto de pantalla", "screenshot", "screen shot", "capturar pantalla",
        ],
        "reply_es": "Listo! Selecciona el area para la captura.",
        "reply_en": "Ready! Select region for screenshot.",
        "mood": "happy",
    },
    {
        "id": "media_seek_fwd_10",
        "cooldown": 0.8,
        "phrases": [
            "adelantar diez segundos", "adelanta diez segundos", "adelante diez segundos",
            "adelantar 10 segundos", "adelanta 10 segundos", "adelante 10 segundos",
            "avanzar diez segundos", "avanza diez segundos", "avanzar 10 segundos", "avanza 10 segundos",
            "saltar diez segundos", "salta diez segundos", "saltar 10 segundos", "salta 10 segundos",
            "diez segundos adelante", "10 segundos adelante",
        ],
        "reply_es": "Adelantando 10 segundos!",
        "reply_en": "Skipping forward 10 seconds!",
        "mood": "happy",
    },
    {
        "id": "media_seek_fwd_5",
        "cooldown": 0.8,
        "phrases": [
            "adelantar cinco segundos", "adelanta cinco segundos", "adelante cinco segundos",
            "adelantar 5 segundos", "adelanta 5 segundos", "adelante 5 segundos",
            "avanzar cinco segundos", "avanza cinco segundos", "avanzar 5 segundos", "avanza 5 segundos",
            "saltar cinco segundos", "salta cinco segundos", "saltar 5 segundos", "salta 5 segundos",
            "cinco segundos adelante", "5 segundos adelante",
            "adelantar", "adelanta", "adelantale",
        ],
        "reply_es": "Adelantando 5 segundos!",
        "reply_en": "Skipping forward 5 seconds!",
        "mood": "happy",
    },
    {
        "id": "media_seek_back_10",
        "cooldown": 0.8,
        "phrases": [
            "retroceder diez segundos", "retrocede diez segundos",
            "retroceder 10 segundos", "retrocede 10 segundos",
            "atrasar diez segundos", "atrasa diez segundos",
            "atrasar 10 segundos", "atrasa 10 segundos",
            "regresar diez segundos", "regresa diez segundos",
            "regresar 10 segundos", "regresa 10 segundos",
            "diez segundos atras", "10 segundos atras", "atras diez segundos", "atras 10 segundos",
        ],
        "reply_es": "Retrocediendo 10 segundos!",
        "reply_en": "Rewinding 10 seconds!",
        "mood": "happy",
    },
    {
        "id": "media_seek_back_5",
        "cooldown": 0.8,
        "phrases": [
            "retroceder cinco segundos", "retrocede cinco segundos",
            "retroceder 5 segundos", "retrocede 5 segundos",
            "atrasar cinco segundos", "atrasa cinco segundos",
            "atrasar 5 segundos", "atrasa 5 segundos",
            "regresar cinco segundos", "regresa cinco segundos",
            "regresar 5 segundos", "regresa 5 segundos",
            "cinco segundos atras", "5 segundos atras", "atras cinco segundos", "atras 5 segundos",
            "retroceder", "retrocede", "atrasar", "atrasa",
        ],
        "reply_es": "Retrocediendo 5 segundos!",
        "reply_en": "Rewinding 5 seconds!",
        "mood": "happy",
    },
    {
        "id": "media_next",
        "cooldown": 1.0,
        "phrases": [
            "video siguiente", "siguiente video", "siguiente cancion", "proxima cancion",
            "proximo video", "pasa el video", "cambia el video", "cambia de video",
            "adelante video", "siguiente pista", "siguiente",
        ],
        "reply_es": "Video siguiente!",
        "reply_en": "Next track/video!",
        "mood": "happy",
    },
    {
        "id": "media_prev",
        "cooldown": 1.0,
        "phrases": [
            "video anterior", "anterior video", "cancion anterior", "anterior cancion",
            "regresa el video", "regresa video", "video de atras", "pista anterior",
            "anterior",
        ],
        "reply_es": "Video anterior!",
        "reply_en": "Previous track/video!",
        "mood": "happy",
    },
    {
        "id": "media_pause",
        "cooldown": 1.0,
        "phrases": [
            "pausa el video", "pausar video", "pausar el video", "pon pausa",
            "poner pausa", "pon en pausa", "pausar musica", "pausar", "pausa",
        ],
        "reply_es": "En pausa!",
        "reply_en": "Paused!",
        "mood": "neutral",
    },
    {
        "id": "media_resume",
        "cooldown": 1.0,
        "phrases": [
            "reanudar video", "reanudar el video", "reanuda el video", "reanuda video",
            "continuar video", "continua el video", "reproducir video", "pon play",
            "dale play", "quitar pausa", "quita la pausa", "reanudar", "reanuda",
            "continuar", "reproducir",
        ],
        "reply_es": "Reanudando reproduccion!",
        "reply_en": "Resuming playback!",
        "mood": "happy",
    },
]

RULE_BY_ID: Dict[str, Dict[str, Any]] = {r["id"]: r for r in ACTION_RULES}


def _avg_word_confidence(words_meta: Optional[List[Dict[str, Any]]]) -> float:
    if not words_meta:
        return 1.0
    confs = [float(w.get("conf", 1.0)) for w in words_meta if isinstance(w, dict) and w.get("word") != "[unk]"]
    if not confs:
        return 0.0
    return sum(confs) / len(confs)


def parse_voice_command(
    raw_text: str,
    words_meta: Optional[List[Dict[str, Any]]] = None,
    require_wake_word: bool = False,
) -> Optional[VoiceMatch]:
    """
    Matches spoken or typed Spanish phrases against supported desktop commands.
    Returns None for normal conversation / [unk] noise to avoid false triggers.
    """
    norm = normalize_voice_text(raw_text)
    if not norm or norm == "unk":
        return None

    # Drop explicit "[unk]" tokens left over if recognizer mixes unknown words around a command.
    tokens_no_unk = [t for t in norm.split() if t != "unk"]
    if not tokens_no_unk:
        return None
    norm = " ".join(tokens_no_unk)

    body, had_wake = _strip_optional_wake_word(norm)
    if require_wake_word and not had_wake:
        return None
    if not body:
        return None

    avg_conf = _avg_word_confidence(words_meta)
    words = body.split()
    has_open_verb = any(words[0] == v or (len(words) > 1 and words[1] == v) for v in OPEN_VERBS)

    # Reject single short trigger words unless spoken with strong confidence.
    if len(words) == 1 and words_meta is not None and avg_conf < 0.62:
        return None
    if words_meta is not None and avg_conf < 0.44:
        return None

    # Strip leading open verb + optional article when matching app names.
    target_after_verb = body
    if words and words[0] in OPEN_VERBS:
        rest = words[1:]
        if rest and rest[0] in ("el", "la", "un", "una"):
            rest = rest[1:]
        target_after_verb = " ".join(rest)

    for rule in ACTION_RULES:
        needs_verb = bool(rule.get("require_open_verb", False))
        if needs_verb and not has_open_verb:
            continue
        candidate = target_after_verb if needs_verb else body
        for phrase in rule["phrases"]:
            if candidate == phrase or (
                not needs_verb
                and len(phrase.split()) >= 2
                and f" {phrase} " in f" {body} "
            ) or (
                needs_verb
                and candidate.startswith(phrase)
                and len(candidate) <= len(phrase) + 12
            ):
                return VoiceMatch(
                    action_id=rule["id"],
                    heard=body,
                    confidence=avg_conf,
                    cooldown_sec=float(rule.get("cooldown", 1.2)),
                )
    return None


def _spawn_detached(cmd: List[str]) -> bool:
    if not cmd:
        return False
    exe = cmd[0]
    if not os.path.isabs(exe) and not shutil.which(exe):
        return False
    if os.path.isabs(exe) and not os.access(exe, os.X_OK):
        return False
    subprocess.Popen(
        cmd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    return True


def _run_short(cmd: List[str], timeout: float = 2.0) -> Tuple[bool, str]:
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, check=False)
        return proc.returncode == 0, (proc.stdout or proc.stderr or "").strip()
    except Exception as exc:
        return False, str(exc)


def _adjust_brightness(delta_pct: int) -> bool:
    # 1. Prefer native desktop IPC (shows OSD cleanly on end4-pC Quickshell)
    if shutil.which("qs"):
        fn = "increment" if delta_pct > 0 else "decrement"
        ok, _ = _run_short(["qs", "-c", "end4-pC", "ipc", "call", "brightness", fn])
        if ok:
            return True

    # 2. Try brightnessctl if installed
    if shutil.which("brightnessctl"):
        step = f"{abs(delta_pct)}%+" if delta_pct > 0 else f"{abs(delta_pct)}%-"
        ok, _ = _run_short(["brightnessctl", "set", step])
        if ok:
            return True

    # 3. Direct systemd-logind D-Bus backlight control (works rootless on Wayland)
    bl_root = Path("/sys/class/backlight")
    if bl_root.exists() and shutil.which("gdbus"):
        for dev in sorted(bl_root.iterdir()):
            try:
                cur = int((dev / "brightness").read_text().strip())
                mx = int((dev / "max_brightness").read_text().strip())
            except Exception:
                continue
            step = max(1, round(mx * abs(delta_pct) / 100.0))
            nxt = max(1, min(mx, cur + step if delta_pct > 0 else cur - step))
            ok, _ = _run_short([
                "gdbus", "call", "--system",
                "--dest", "org.freedesktop.login1",
                "--object-path", "/org/freedesktop/login1/session/auto",
                "--method", "org.freedesktop.login1.Session.SetBrightness",
                "backlight", dev.name, str(nxt),
            ])
            if ok:
                return True
    return False


def _adjust_volume(delta_pct: int) -> bool:
    if shutil.which("wpctl"):
        _run_short(["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "0"])
        arg = f"{abs(delta_pct)}%+" if delta_pct > 0 else f"{abs(delta_pct)}%-"
        cmd = ["wpctl", "set-volume"]
        if delta_pct > 0:
            cmd.extend(["-l", "1.0"])
        cmd.extend(["@DEFAULT_AUDIO_SINK@", arg])
        ok, _ = _run_short(cmd)
        if ok:
            if shutil.which("qs"):
                _run_short(["qs", "-c", "end4-pC", "ipc", "call", "osdVolume", "trigger"], timeout=1.0)
            return True
    if shutil.which("pactl"):
        sign = f"+{abs(delta_pct)}%" if delta_pct > 0 else f"-{abs(delta_pct)}%"
        ok, _ = _run_short(["pactl", "set-sink-volume", "@DEFAULT_SINK@", sign])
        return ok
    return False


_FIREFOX_CLASSES = ("librewolf", "firefox", "floorp", "zen", "waterfox")
_CHROMIUM_CLASSES = ("chromium", "google-chrome", "brave", "vivaldi", "opera")
_BROWSER_CLASSES = _FIREFOX_CLASSES + _CHROMIUM_CLASSES + ("epiphany",)


def _is_browser_class(cls_name: str) -> bool:
    c = (cls_name or "").lower()
    return any(b in c for b in _BROWSER_CLASSES)


def _hypr_active_window() -> Dict[str, Any]:
    if not shutil.which("hyprctl"):
        return {}
    ok, out = _run_short(["hyprctl", "activewindow", "-j"], timeout=1.5)
    if not ok or not out:
        return {}
    try:
        data = json.loads(out)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def _hypr_find_browser_window() -> Dict[str, Any]:
    """Returns active window if it is a browser, otherwise the best open browser window."""
    aw = _hypr_active_window()
    if _is_browser_class(str(aw.get("class", ""))):
        return aw
    if not shutil.which("hyprctl"):
        return {}
    ok, out = _run_short(["hyprctl", "clients", "-j"], timeout=1.5)
    if not ok or not out:
        return {}
    try:
        clients = json.loads(out)
    except Exception:
        return {}
    if not isinstance(clients, list):
        return {}
    # Prefer LibreWolf, then any browser currently on a visible/non-hidden workspace
    browsers = [c for c in clients if isinstance(c, dict) and _is_browser_class(str(c.get("class", "")))]
    if not browsers:
        return {}
    browsers.sort(key=lambda c: (0 if "librewolf" in str(c.get("class", "")).lower() else 1, c.get("focusHistoryID", 99)))
    return browsers[0]


def _browser_send_shortcut(addr: str, key: str, mods: str = "") -> bool:
    """Sends a keystroke directly to a Hyprland window (supports Hyprland Lua and classic syntax)."""
    if not shutil.which("hyprctl") or not addr:
        return False
    lua_arg = f'hl.dsp.send_shortcut({{ mods = "{mods}", key = "{key}", window = "address:{addr}" }})'
    ok, _ = _run_short(["hyprctl", "dispatch", lua_arg], timeout=1.5)
    if ok:
        return True
    classic_arg = f"{mods}, {key}, address:{addr}"
    ok, _ = _run_short(["hyprctl", "dispatch", "sendshortcut", classic_arg], timeout=1.5)
    return ok


def _mpris_players() -> List[str]:
    if not shutil.which("playerctl"):
        return []
    ok, out = _run_short(["playerctl", "-l"], timeout=1.2)
    if not ok or not out:
        return []
    return [line.strip() for line in out.splitlines() if line.strip()]


def _pick_browser_mpris_player(win_class: str, players: List[str]) -> Optional[str]:
    c = (win_class or "").lower()
    if any(b in c for b in _FIREFOX_CLASSES):
        for p in players:
            pl = p.lower()
            if "firefox" in pl or "librewolf" in pl or "zen" in pl or "floorp" in pl:
                return p
    if any(b in c for b in _CHROMIUM_CLASSES):
        for p in players:
            pl = p.lower()
            if any(b in pl for b in ("chromium", "chrome", "brave", "vivaldi", "opera")):
                return p
    return players[0] if players else None


def _mpris_player_info(player_name: Optional[str]) -> Dict[str, Any]:
    info: Dict[str, Any] = {
        "status": "",
        "url": "",
        "can_go_next": False,
        "can_go_prev": False,
        "can_play": False,
        "can_pause": False,
        "can_seek": False,
    }
    if not player_name or not shutil.which("gdbus"):
        return info
    ok, out = _run_short([
        "gdbus", "call", "--session",
        "--dest", f"org.mpris.MediaPlayer2.{player_name}",
        "--object-path", "/org/mpris/MediaPlayer2",
        "--method", "org.freedesktop.DBus.Properties.GetAll",
        "org.mpris.MediaPlayer2.Player",
    ], timeout=1.2)
    if not ok or not out:
        return info
    m_st = re.search(r"'PlaybackStatus':\s*<'([^']+)'>", out)
    if m_st:
        info["status"] = m_st.group(1)
    m_url = re.search(r"'xesam:url':\s*<'([^']*)'>", out)
    if m_url:
        info["url"] = m_url.group(1)
    info["can_go_next"] = "'CanGoNext': <true>" in out
    info["can_go_prev"] = "'CanGoPrevious': <true>" in out
    info["can_play"] = "'CanPlay': <true>" in out
    info["can_pause"] = "'CanPause': <true>" in out
    info["can_seek"] = "'CanSeek': <true>" in out
    return info


def choose_browser_nav_shortcut(op: str, title: str, url: str) -> Tuple[str, str]:
    """
    Chooses the appropriate keyboard shortcut (mods, key) for web video players when
    MPRIS does not expose next/previous track controls (e.g. TikTok, YouTube Shorts, Reels, YouTube).
    """
    ctx = f"{title or ''} {url or ''}".lower()
    is_short_feed = any(k in ctx for k in ("tiktok", "/shorts", "youtube shorts", "reels", "instagram"))
    is_youtube_long = ("youtube" in ctx or "youtu.be" in ctx) and not is_short_feed
    if op == "next":
        return ("SHIFT", "n") if is_youtube_long else ("", "Down")
    if op == "previous":
        return ("SHIFT", "p") if is_youtube_long else ("", "Up")
    if op in ("pause", "play"):
        return ("", "k") if is_youtube_long else ("", "space")
    return ("", "space")


def choose_browser_seek_shortcut(delta_sec: int, title: str, url: str) -> Tuple[str, str, int]:
    """
    Chooses (mods, key, repeat_count) to seek forward/backward by 5s or 10s in any web video player:
    - YouTube uses 'l'/'j' for +/-10s and Right/Left for +/-5s.
    - Generic HTML5 players (Twitch, Anime, Vimeo, etc.) use Right/Left (5s per press, 2x for 10s).
    """
    ctx = f"{title or ''} {url or ''}".lower()
    is_short_feed = any(k in ctx for k in ("tiktok", "/shorts", "youtube shorts", "reels", "instagram"))
    is_youtube_long = ("youtube" in ctx or "youtu.be" in ctx) and not is_short_feed
    if abs(delta_sec) >= 10:
        if is_youtube_long:
            return ("", "l" if delta_sec > 0 else "j", 1)
        return ("", "Right" if delta_sec > 0 else "Left", 2)
    return ("", "Right" if delta_sec > 0 else "Left", 1)


def _resolve_media_target() -> Dict[str, Any]:
    aw = _hypr_active_window()
    aw_is_browser = _is_browser_class(str(aw.get("class", "")))
    browser_win = aw if aw_is_browser else _hypr_find_browser_window()
    players = _mpris_players()

    target_player = _pick_browser_mpris_player(str(browser_win.get("class", "")), players) if browser_win else (players[0] if players else None)
    # Prefer a non-browser native player (e.g. Spotify/mpv/VLC) only when active window is NOT a browser
    if not aw_is_browser and players:
        native = [p for p in players if not any(b in p.lower() for b in ("firefox", "librewolf", "chromium", "chrome", "brave", "vivaldi", "opera"))]
        if native:
            target_player = native[0]

    info = _mpris_player_info(target_player)
    addr = str(browser_win.get("address", ""))
    win_title = str(browser_win.get("title", ""))
    mpris_matches_win = True
    if aw_is_browser and win_title and target_player:
        # Check whether MPRIS metadata matches the active browser tab (avoid acting on a background tab)
        ok_mt, m_title = _run_short(["playerctl", "-p", target_player, "metadata", "xesam:title"], timeout=1.0)
        mt = (m_title or "").strip().lower()
        if ok_mt and mt:
            head = mt[:20]
            if head not in win_title.lower() and mt not in win_title.lower():
                mpris_matches_win = False

    return {
        "aw_is_browser": aw_is_browser,
        "browser_win": browser_win,
        "target_player": target_player,
        "info": info,
        "addr": addr,
        "win_title": win_title,
        "url": str(info.get("url", "")),
        "mpris_matches_win": mpris_matches_win,
    }


def _playerctl_get_position(player_name: str) -> Optional[float]:
    ok, out = _run_short(["playerctl", "-p", player_name, "position"], timeout=1.0)
    if not ok or not out:
        return None
    try:
        return float(out.strip())
    except ValueError:
        return None


def _player_seek(delta_sec: int) -> bool:
    ctx = _resolve_media_target()
    aw_is_browser = ctx["aw_is_browser"]
    browser_win = ctx["browser_win"]
    target_player = ctx["target_player"]
    info = ctx["info"]
    addr = ctx["addr"]
    win_title = ctx["win_title"]
    url = ctx["url"]
    mpris_matches_win = ctx["mpris_matches_win"]

    step_arg = f"{abs(delta_sec)}{'+' if delta_sec >= 0 else '-'}"
    # Use MPRIS seek for native players, or matching browser sessions whose position actually advances
    if target_player and (not aw_is_browser or (mpris_matches_win and info["can_seek"])):
        pos_before = _playerctl_get_position(target_player)
        ok, _ = _run_short(["playerctl", "-p", target_player, "position", step_arg], timeout=1.2)
        if ok:
            if pos_before is None and not aw_is_browser:
                return True
            time.sleep(0.06)
            pos_after = _playerctl_get_position(target_player)
            if pos_before is not None and pos_after is not None and abs(pos_after - pos_before) >= 1.5:
                return True

    # Direct shortcut to active/target browser window (YouTube 'l'/'j' for 10s, Right/Left 1x or 2x for any HTML5 video)
    if (aw_is_browser or browser_win) and addr:
        mods, key, repeat = choose_browser_seek_shortcut(delta_sec, win_title, url if mpris_matches_win else "")
        any_ok = False
        for i in range(max(1, repeat)):
            if _browser_send_shortcut(addr, key, mods=mods):
                any_ok = True
            if i + 1 < repeat:
                time.sleep(0.04)
        if any_ok:
            return True

    if shutil.which("playerctl"):
        ok, _ = _run_short(["playerctl", "position", step_arg], timeout=1.2)
        if ok:
            return True
    return False


def _player_control(op: str) -> bool:
    # op in ("next", "previous", "pause", "play")
    ctx = _resolve_media_target()
    aw_is_browser = ctx["aw_is_browser"]
    browser_win = ctx["browser_win"]
    target_player = ctx["target_player"]
    info = ctx["info"]
    status = info["status"]
    addr = ctx["addr"]
    win_title = ctx["win_title"]
    url = ctx["url"]
    mpris_matches_win = ctx["mpris_matches_win"]

    # 1. Pause / Play on active browser or MPRIS media
    if op in ("pause", "play"):
        want_playing = (op == "play")
        if mpris_matches_win and target_player:
            already_ok = (status == "Playing" and want_playing) or (status == "Paused" and not want_playing)
            if already_ok:
                return True
            if (op == "pause" and info["can_pause"]) or (op == "play" and info["can_play"]):
                ok, _ = _run_short(["playerctl", "-p", target_player, op], timeout=1.2)
                if ok:
                    time.sleep(0.08)
                    new_status = _mpris_player_info(target_player).get("status", "")
                    if (want_playing and new_status == "Playing") or ((not want_playing) and new_status == "Paused"):
                        return True
        # Fallback: direct shortcut to active/target browser window (handles active tab or HTML5 without MPRIS)
        if addr:
            mods, key = choose_browser_nav_shortcut(op, win_title, url if mpris_matches_win else "")
            if _browser_send_shortcut(addr, key, mods=mods):
                return True
        if shutil.which("playerctl"):
            ok, _ = _run_short(["playerctl", op], timeout=1.2)
            if ok:
                return True
        if shutil.which("qs"):
            qs_op = "pauseAll" if op == "pause" else "playPause"
            ok, _ = _run_short(["qs", "-c", "end4-pC", "ipc", "call", "mpris", qs_op], timeout=1.2)
            return ok
        return False

    # 2. Next / Previous video
    if op in ("next", "previous"):
        can_mpris_nav = info["can_go_next"] if op == "next" else info["can_go_prev"]
        if mpris_matches_win and target_player and can_mpris_nav:
            ok, _ = _run_short(["playerctl", "-p", target_player, op], timeout=1.2)
            if ok:
                return True
        # Active window is a browser (or browser is the media target whose MPRIS doesn't support next/prev, e.g. TikTok/Shorts/YouTube)
        if (aw_is_browser or browser_win) and addr:
            mods, key = choose_browser_nav_shortcut(op, win_title, url if mpris_matches_win else "")
            if _browser_send_shortcut(addr, key, mods=mods):
                return True
        if shutil.which("playerctl"):
            ok, _ = _run_short(["playerctl", op], timeout=1.2)
            if ok:
                return True
        if shutil.which("qs"):
            qs_op = "next" if op == "next" else "previous"
            ok, _ = _run_short(["qs", "-c", "end4-pC", "ipc", "call", "mpris", qs_op], timeout=1.2)
            return ok
        return False

    return False


def _is_recording_active() -> bool:
    ok, _ = _run_short(["pgrep", "-x", "wf-recorder"], timeout=1.0)
    return ok


def execute_voice_action(action_id: str, language: str = "es", dry_run: bool = False) -> Dict[str, Any]:
    """Executes the mapped desktop action and returns a shimeji speech payload."""
    rule = RULE_BY_ID.get(action_id)
    if not rule:
        return {"ok": False, "mood": "thinking", "text": f"Unknown voice action: {action_id}"}

    is_es = (language or "es").lower() == "es"
    ok = True

    if not dry_run:
        if action_id == "open_antigravity":
            ok = (
                _spawn_detached(["/usr/bin/antigravity-ide"])
                or _spawn_detached(["antigravity-ide"])
                or _spawn_detached(["antigravity"])
                or _spawn_detached(["codium"])
            )
        elif action_id == "open_librewolf":
            ok = _spawn_detached(["librewolf"]) or _spawn_detached(["firefox"])
        elif action_id == "open_steam":
            ok = _spawn_detached(["steam"])
        elif action_id == "open_foot":
            ok = _spawn_detached(["foot"])
        elif action_id == "open_kitty":
            ok = _spawn_detached(["kitty"])
        elif action_id == "open_files":
            ok = _spawn_detached(["thunar"]) or _spawn_detached(["dolphin"]) or _spawn_detached(["xdg-open", str(Path.home())])
        elif action_id == "volume_up":
            ok = _adjust_volume(+10)
        elif action_id == "volume_down":
            ok = _adjust_volume(-10)
        elif action_id == "brightness_up":
            ok = _adjust_brightness(+5)
        elif action_id == "brightness_down":
            ok = _adjust_brightness(-5)
        elif action_id == "stop_record_video":
            if not _is_recording_active():
                return {
                    "ok": True,
                    "voice_command": action_id,
                    "executed": False,
                    "mood": "thinking",
                    "text": "No habia ninguna grabacion activa!" if is_es else "No active recording right now!",
                }
            script = Path.home() / ".local" / "bin" / "toggle-record.sh"
            ok = _spawn_detached([str(script)]) if script.exists() else _run_short(["killall", "-INT", "wf-recorder"])[0]
        elif action_id == "record_video":
            was_rec = _is_recording_active()
            script = Path.home() / ".local" / "bin" / "toggle-record.sh"
            if script.exists():
                ok = _spawn_detached([str(script)])
            elif shutil.which("qs"):
                ok = _spawn_detached(["qs", "-c", "end4-pC", "ipc", "call", "region", "recordWithSound"])
            else:
                ok = False
            if ok and was_rec:
                return {
                    "ok": True,
                    "voice_command": "stop_record_video",
                    "executed": True,
                    "mood": "happy",
                    "text": "Grabacion detenida y guardada en Videos!" if is_es else "Recording stopped and saved to Videos!",
                }
        elif action_id == "screenshot":
            script = Path.home() / ".local" / "bin" / "takeshot.sh"
            if script.exists():
                ok = _spawn_detached([str(script)])
            elif shutil.which("qs"):
                ok = _spawn_detached(["qs", "-c", "end4-pC", "ipc", "call", "region", "screenshot"])
            else:
                ok = False
        elif action_id == "media_seek_fwd_10":
            ok = _player_seek(10)
        elif action_id == "media_seek_fwd_5":
            ok = _player_seek(5)
        elif action_id == "media_seek_back_10":
            ok = _player_seek(-10)
        elif action_id == "media_seek_back_5":
            ok = _player_seek(-5)
        elif action_id == "media_next":
            ok = _player_control("next")
        elif action_id == "media_prev":
            ok = _player_control("previous")
        elif action_id == "media_pause":
            ok = _player_control("pause")
        elif action_id == "media_resume":
            ok = _player_control("play")

    if not ok:
        return {
            "ok": True,
            "voice_command": action_id,
            "executed": False,
            "mood": "tsun",
            "text": (
                f"Escuche el comando ({action_id}), pero no pude ejecutarlo en este equipo!"
                if is_es
                else f"Heard ({action_id}), but could not run it on this system!"
            ),
        }

    return {
        "ok": True,
        "voice_command": action_id,
        "executed": True,
        "mood": rule["mood"],
        "text": rule["reply_es"] if is_es else rule["reply_en"],
    }


def build_vosk_grammar(model: Any) -> List[str]:
    """
    Builds a grammar list using only words present in the Vosk Spanish lexicon
    (plus '[unk]') so KaldiRecognizer yields zero OOV warnings and minimal false positives.
    """
    phrases: List[str] = []
    seen = set()

    def add_phrase(p: str) -> None:
        toks = [w for w in p.split() if model.vosk_model_find_word(w) >= 0]
        if not toks or len(toks) != len(p.split()):
            return
        joined = " ".join(toks)
        if joined not in seen:
            seen.add(joined)
            phrases.append(joined)

    # Core direct phrases + open verb combinations
    direct_verbs = ("abrir", "abre", "iniciar", "lanza", "pon")
    wake_prefixes = ("oye", "computadora")
    for rule in ACTION_RULES:
        if rule.get("require_open_verb"):
            for v in direct_verbs:
                for p in rule["phrases"]:
                    add_phrase(f"{v} {p}")
                    for wk in wake_prefixes:
                        add_phrase(f"{wk} {v} {p}")
                    if not p.startswith("el "):
                        add_phrase(f"{v} el {p}")
                        for wk in wake_prefixes:
                            add_phrase(f"{wk} {v} el {p}")
        else:
            for p in rule["phrases"]:
                add_phrase(p)
                for wk in wake_prefixes:
                    add_phrase(f"{wk} {p}")

    # Optional wake prefix for in-vocab wake words ("oye", "computadora")
    for w in wake_prefixes:
        if model.vosk_model_find_word(w) >= 0:
            phrases.append(w)

    phrases.append("[unk]")
    return phrases


class VoiceCommandListener:
    """Background daemon capturing 16kHz mono PCM from PipeWire/PulseAudio and running Vosk STT."""

    def __init__(
        self,
        on_event: Callable[[Dict[str, Any]], None],
        get_language: Callable[[], str],
        model_dir: Optional[Path] = None,
    ) -> None:
        self._on_event = on_event
        self._get_language = get_language
        self._model_dir = model_dir or (Path(__file__).resolve().parent / DEFAULT_MODEL_REL)
        self._enabled = False
        self._require_wake_word = False
        self._stop_flag = threading.Event()
        self._state_lock = threading.Lock()
        self._thread: Optional[threading.Thread] = None
        self._proc: Optional[subprocess.Popen[bytes]] = None
        self._model: Any = None
        self._last_fired: Dict[str, float] = {}
        self.status = "stopped"

    def configure(self, enabled: bool, require_wake_word: bool = False) -> None:
        with self._state_lock:
            self._require_wake_word = bool(require_wake_word)
            want = bool(enabled)
            if want == self._enabled:
                return
            self._enabled = want

        if want:
            self.start()
        else:
            self.stop()

    def start(self) -> None:
        with self._state_lock:
            if self._thread and self._thread.is_alive():
                return
            self._stop_flag.clear()
            self._thread = threading.Thread(target=self._run_loop, name="doki-voice", daemon=True)
            self._thread.start()

    def stop(self) -> None:
        self._stop_flag.set()
        proc = self._proc
        if proc is not None:
            try:
                proc.terminate()
            except Exception:
                pass
        th = self._thread
        if th and th.is_alive() and threading.current_thread() is not th:
            th.join(timeout=1.5)
        self._thread = None
        self.status = "disabled"

    @staticmethod
    def _preferred_mic_source() -> str:
        # Prefer the built-in ALSA mic so voice capture never forces Bluetooth headsets into SCO/HFP mode.
        if shutil.which("pactl"):
            try:
                out = subprocess.check_output(
                    ["pactl", "list", "short", "sources"],
                    stderr=subprocess.DEVNULL,
                    timeout=2.0,
                ).decode("utf-8", errors="replace")
                for line in out.splitlines():
                    cols = line.split("\t")
                    if len(cols) >= 2:
                        src = cols[1].strip()
                        if src.startswith("alsa_input.") and ".monitor" not in src:
                            return src
            except Exception:
                pass
        return "@DEFAULT_SOURCE@"

    def _open_audio_stream(self) -> Optional[subprocess.Popen[bytes]]:
        src = self._preferred_mic_source()
        cmds = []
        if shutil.which("parec"):
            cmds.append(["parec", "--format=s16le", "--rate=16000", "--channels=1", f"--device={src}", "--latency-msec=80"])
        if shutil.which("parecord"):
            cmds.append(["parecord", "--raw", "--format=s16le", "--rate=16000", "--channels=1", f"--device={src}"])
        if shutil.which("pw-record"):
            cmds.append(["pw-record", "--format=s16", "--rate=16000", "--channels=1", "-"])
        for cmd in cmds:
            try:
                p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                return p
            except Exception:
                continue
        return None

    def _run_loop(self) -> None:
        try:
            import vosk  # type: ignore
            vosk.SetLogLevel(-1)
        except Exception as exc:
            self.status = f"no_vosk:{exc}"
            return

        if not self._model_dir.exists():
            self.status = "no_model"
            return

        try:
            if self._model is None:
                self._model = vosk.Model(str(self._model_dir))
            grammar = build_vosk_grammar(self._model)
            rec = vosk.KaldiRecognizer(self._model, 16000, json.dumps(grammar, ensure_ascii=False))
            rec.SetWords(True)
        except Exception as exc:
            self.status = f"model_err:{exc}"
            return

        while not self._stop_flag.is_set():
            proc = self._open_audio_stream()
            if proc is None or proc.stdout is None:
                self.status = "no_mic"
                self._stop_flag.wait(2.5)
                continue

            self._proc = proc
            self.status = "listening"
            try:
                while not self._stop_flag.is_set():
                    chunk = proc.stdout.read(3200)  # ~100 ms of 16kHz s16le mono
                    if not chunk:
                        break
                    if rec.AcceptWaveform(chunk):
                        res = json.loads(rec.Result() or "{}")
                        self._handle_transcript(res.get("text", ""), res.get("result"))
            except Exception:
                pass
            finally:
                try:
                    proc.terminate()
                    proc.wait(timeout=1.0)
                except Exception:
                    try:
                        proc.kill()
                    except Exception:
                        pass
                self._proc = None

            if not self._stop_flag.is_set():
                self._stop_flag.wait(1.0)

    def _handle_transcript(self, text: str, words_meta: Optional[List[Dict[str, Any]]]) -> None:
        if not text:
            return
        with self._state_lock:
            req_wake = self._require_wake_word
        match = parse_voice_command(text, words_meta=words_meta, require_wake_word=req_wake)
        if not match:
            return

        now = time.monotonic()
        prev = self._last_fired.get(match.action_id, 0.0)
        if (now - prev) < match.cooldown_sec:
            return
        self._last_fired[match.action_id] = now

        payload = execute_voice_action(match.action_id, language=self._get_language(), dry_run=False)
        payload["heard"] = match.heard
        payload["confidence"] = round(match.confidence, 3)
        payload["source"] = "voice"
        self._on_event(payload)
