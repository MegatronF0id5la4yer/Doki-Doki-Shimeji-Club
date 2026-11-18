#!/usr/bin/env python3
"""
DDLC Dokis Shimeji - Python Personality & Gemini API Brain.
Supports Natsuki, Monika, Sayori, Yuri + user name personalization +
weird dialogues for Batman/Monika + '*amo x doki*' swarm detection.
"""

from __future__ import annotations

import base64
import datetime
import json
import random
import re
import shutil
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor
from typing import Any, Dict, List, Optional, Tuple

import requests

ALLOWED_MOODS = (
    "neutral",
    "happy",
    "tsun",
    "angry",
    "shy",
    "blush",
    "smug",
    "surprised",
    "sad",
    "excited",
    "thinking",
    "sleepy",
    "laugh",
    "cheeky",
)

ALLOWED_CHARACTERS = ("natsuki", "monika", "sayori", "yuri")

EMOJI_PATTERN = re.compile(
    "["
    "\U0001F000-\U0001FAFF"
    "\U00002600-\U000027BF"
    "\U00002B05-\U00002B55"
    "\U0000231A-\U000023FA"
    "\U0000FE0E-\U0000FE0F"
    "\U0000200D"
    "\U000E0020-\U000E007F"
    "]+",
    flags=re.UNICODE,
)

SWARM_TRIGGER_PATTERN = re.compile(
    r"amo\s+(?:a\s+)?(?:(?:x|todas?\s+las|las|[a-zA-ZáéíóúñÁÉÍÓÚÑ]+)\s+)?dokis?\b"
    r"|amo\s+(?:a\s+)?(?:natsuki|monika|sayori|yuri)\b"
    r"|love\s+(?:x\s+)?(?:all\s+)?dokis?\b",
    flags=re.IGNORECASE,
)

from doki_dialogues import (
    AFFECT_DIALOGUES_EN,
    AFFECT_DIALOGUES_ES,
    BATMAN_WEIRD_EN,
    BATMAN_WEIRD_ES,
    MONIKA_EN,
    MONIKA_ES,
    MONIKA_NAME_WEIRD_EN,
    MONIKA_NAME_WEIRD_ES,
    SAYORI_EN,
    SAYORI_ES,
    SWARM_REPLIES_EN,
    SWARM_REPLIES_ES,
    YURI_EN,
    YURI_ES,
)
from local_dialogues import (
    OUTFIT_EN,
    OUTFIT_ES,
    POKE_EN,
    POKE_ES,
    POKE_HEAD_EN,
    POKE_HEAD_ES,
    POKE_ZONE1_EN,
    POKE_ZONE1_ES,
    POKE_ZONE2_EN,
    POKE_ZONE2_ES,
    SPONTANEOUS_EN,
    SPONTANEOUS_ES,
    TOPIC_RESPONSES_EN,
    TOPIC_RESPONSES_ES,
)
from voice_commands import (
    VoiceCommandListener,
    choose_browser_nav_shortcut,
    choose_browser_seek_shortcut,
    execute_voice_action,
    parse_voice_command,
)


def strip_emojis(text: str) -> str:
    clean = EMOJI_PATTERN.sub("", text or "")
    clean = re.sub(r"\s{2,}", " ", clean)
    return clean.strip()


def normalize_character(char_id: str) -> str:
    c = (char_id or "").strip().lower()
    return c if c in ALLOWED_CHARACTERS else "natsuki"


def normalize_mood(mood: str) -> str:
    m = (mood or "").strip().lower()
    if m in ALLOWED_MOODS:
        return m
    aliases = {
        "pout": "tsun",
        "annoyed": "tsun",
        "tsundere": "tsun",
        "embarrassed": "blush",
        "flustered": "blush",
        "cheerful": "happy",
        "smile": "happy",
        "shocked": "surprised",
        "tired": "sleepy",
        "teasing": "cheeky",
    }
    return aliases.get(m, "neutral")


def is_swarm_love_command(text: str) -> bool:
    t = (text or "").replace("*", " ").strip()
    return bool(SWARM_TRIGGER_PATTERN.search(t))


def name_easter_kind(user_name: str) -> Optional[str]:
    low = (user_name or "").strip().lower()
    if not low:
        return None
    if "batman" in low or low == "bruce wayne":
        return "batman"
    if "monika" in low or "monica" in low:
        return "monika"
    return None


def apply_user_name(text: str, user_name: str) -> str:
    clean_name = strip_emojis(user_name)
    if "{, name}" in text:
        repl = f", {clean_name}" if clean_name else ""
        text = text.replace("{, name}", repl)
    if "{name}" in text:
        text = text.replace("{name}", clean_name if clean_name else "amigo")
    return strip_emojis(text)


CHARACTER_PERSONAS = {
    "natsuki": (
        "Natsuki",
        "classic sharp-tongued tsundere, passionate about manga ('manga is literature') and baking cupcakes, "
        "hates being called cute, defensive or sarcastic yet genuinely caring",
    ),
    "monika": (
        "Monika",
        "charismatic Literature Club president, self-aware about living inside a Linux Wayland desktop, "
        "warm, clever, playful, attentive, loves piano, literature, and chatting directly with the user",
    ),
    "sayori": (
        "Sayori",
        "bright, affectionate, scatterbrained bundle of sunshine, obsessed with cookies and snacks, "
        "cheerful and caring, hides little rainclouds under big warmth",
    ),
    "yuri": (
        "Yuri",
        "eloquent, shy, thoughtful bookworm who loves oolong tea, gothic novels, calligraphy, and quiet atmospheres, "
        "easily flustered yet deeply gentle and observant",
    ),
}

RANDOM_SWITCH_LINES: Dict[str, Dict[str, List[Tuple[str, str]]]] = {
    "es": {
        "natsuki": [
            ("tsun", "Cambio al azar y me toco a mi{, name}! Ni te emociones, eh!"),
            ("happy", "Sorpresa! Ahora vigilo tu escritorio yo, Natsuki!"),
        ],
        "monika": [
            ("happy", "Ruleta Doki! Que gusto volver a acompaniarte por aqui{, name}."),
            ("smug", "Jaja, el destino digital me trajo de vuelta contigo~"),
        ],
        "sayori": [
            ("laugh", "Yuju! Me toco mi turno en tu pantalla{, name}! Ehehe!"),
            ("happy", "Yay!! Salto sorpresa al escritorio! Hora de hacer babosadas!"),
        ],
        "yuri": [
            ("blush", "Oh...! Parece que fui elegida al azar para acompanarte{, name}..."),
            ("happy", "Me alegra tomar este turno contigo... tratare de no tropezar."),
        ],
    },
    "en": {
        "natsuki": [
            ("tsun", "Random Doki roll landed on me{, name}! D-don't grin like that!"),
            ("happy", "Surprise! I'm taking over your screen now!"),
        ],
        "monika": [
            ("happy", "Random companion swap! Always happy to drop back in{, name}~"),
            ("smug", "Looks like the RNG missed me on your desktop, huh?"),
        ],
        "sayori": [
            ("laugh", "Yay!! It's my turn on your desktop{, name}! Ehehe!"),
            ("happy", "Surprise drop-in! Let's do something silly!"),
        ],
        "yuri": [
            ("blush", "Oh...! It seems I was randomly chosen for this turn{, name}..."),
            ("happy", "Thank you for having me on screen... I'll behave nicely."),
        ],
    },
}

ACTION_REACT_LINES: Dict[str, Dict[str, List[Tuple[str, str]]]] = {
    "es": {
        "trip": [
            ("blush", "Waaah!! No viste ese tropezon, verdad{, name}?!"),
            ("tsun", "E-el piso de Hyprland estaba resbaloso, ok?! No te rias!"),
        ],
        "heli": [
            ("laugh", "Mode helicoptero kawaii activado!! Fiuuuu~"),
            ("happy", "Mira como giro sin marearme... casi!"),
        ],
        "weapon_show": [
            ("smug", "Popue!! Cuidado{, name}, ahora traigo arsenal de bolsillo!"),
            ("laugh", "Bang bang! Hasta en Shimeji tengo mejor punteria que el Dude!"),
        ],
        "flip": [
            ("laugh", "Parkour!! Mortal hacia atras 10/10 en tu pantalla!"),
            ("happy", "Ta-da! Aterrizaje perfecto (no mires la falda)!"),
        ],
        "jojo": [
            ("smug", "Pose dramatica activada! Sientes la presion anime?!"),
            ("tsun", "Yare yare... obvio que me sale mejor a mi esta pose!"),
        ],
        "sit": [
            ("happy", "Me siento un ratito en tu barra mientras juegas{, name}~"),
            ("blush", "Que comodo se ve el escritorio desde aqui abajo..."),
        ],
        "dance": [
            ("laugh", "Bailecito random de la nada! La-la-la~"),
            ("happy", "Necesitaba estirar las piernas un poquito!"),
        ],
        "weapon": [
            ("smug", "Arma nueva equipada! Me queda increible, o no?"),
            ("happy", "Hora de pasear armada por toda tu pantalla!"),
        ],
    },
    "en": {
        "trip": [
            ("blush", "Waaah!! You didn't see me faceplant just now, okay{, name}?!"),
            ("tsun", "T-the Wayland floor was slippery! Stop laughing!"),
        ],
        "heli": [
            ("laugh", "Kawaii helicopter mode engaged!! Weee~"),
            ("happy", "Look at me spin without getting dizzy... mostly!"),
        ],
        "weapon_show": [
            ("smug", "Popue!! Watch out{, name}, packing desktop heat!"),
            ("laugh", "Pew pew! Better aim than Postal Dude right here on screen!"),
        ],
        "flip": [
            ("laugh", "Parkour!! 10/10 backflip right across your monitor!"),
            ("happy", "Ta-da! Stuck the landing!"),
        ],
        "jojo": [
            ("smug", "Menacing anime pose activated!"),
            ("tsun", "Obviously I pull off this dramatic pose best!"),
        ],
        "sit": [
            ("happy", "Gonna sit right here and chill while you play{, name}~"),
            ("blush", "Taking a tiny breather on the bottom edge..."),
        ],
        "dance": [
            ("laugh", "Random victory wiggle! La-la-la~"),
            ("happy", "Had to stretch my legs with a quick dance!"),
        ],
        "weapon": [
            ("smug", "New weapon equipped! Pretty cool, huh?"),
            ("happy", "Patrolling your desktop with style now!"),
        ],
    },
}


class DokiBrain:
    def __init__(self) -> None:
        self.api_key: str = ""
        self.model: str = "gemini-3.8-flash"
        self.language: str = "en"
        self.character: str = "natsuki"
        self.user_name: str = ""
        self.side: str = "left"
        self.history: Dict[str, List[Dict[str, Any]]] = {c: [] for c in ALLOWED_CHARACTERS}
        self._decks: Dict[str, List[int]] = {}
        self._last_pick: Dict[str, int] = {}
        self._deck_lock = threading.Lock()
        self._auto_turn: int = 0
        self._recent_vision: List[str] = []
        self.voice_commands_enabled: bool = True
        self.voice_wake_word: bool = False
        self.anger: int = 10
        self.joy: int = 55

    def affect_state(self) -> str:
        if self.anger >= 80:
            return "furious"
        if self.anger >= 55:
            return "angry"
        if self.anger >= 32 and self.joy < 55:
            return "annoyed"
        if self.joy >= 80 and self.anger < 25:
            return "ecstatic"
        if self.joy >= 60 and self.anger < 35:
            return "happy"
        return "neutral"

    def _adjust_affect(self, d_anger: int = 0, d_joy: int = 0) -> None:
        self.anger = max(0, min(100, self.anger + d_anger))
        self.joy = max(0, min(100, self.joy + d_joy))

    def _apply_mood_feedback(self, mood: str) -> None:
        m = (mood or "").lower()
        if m == "angry":
            self._adjust_affect(+12, -10)
        elif m == "tsun":
            self._adjust_affect(+4, -2)
        elif m in ("happy", "excited", "laugh", "blush"):
            self._adjust_affect(-8, +10)

    def _apply_text_sentiment(self, user_text: str) -> None:
        low = (user_text or "").lower()
        nice_words = (
            "love", "cute", "adorable", "pretty", "best", "thanks", "thank you", "sorry", "headpat",
            "pat", "cupcake", "tea", "cookie", "sweet", "te amo", "te quiero", "linda", "bonita",
            "hermosa", "gracias", "perdon", "lo siento", "galleta", "abrazo", "hug",
        )
        rude_words = (
            "baka", "idiota", "tonta", "stupid", "dumb", "ugly", "fea", "shut up", "callate",
            "hate", "odio", "annoying", "molesta", "worst",
        )
        if any(w in low for w in nice_words):
            self._adjust_affect(-15, +14)
        elif any(w in low for w in rude_words):
            self._adjust_affect(+18, -14)

    def _with_affect(self, resp: Dict[str, Any]) -> Dict[str, Any]:
        out = dict(resp)
        out["anger"] = self.anger
        out["joy"] = self.joy
        out["affect"] = self.affect_state()
        return out

    def _pick_from_deck(self, key: str, pool: List[Tuple[str, str]]) -> Tuple[str, str]:
        if not pool:
            return "neutral", "..."
        with self._deck_lock:
            deck = self._decks.get(key)
            if not deck:
                deck = list(range(len(pool)))
                random.shuffle(deck)
                if len(deck) > 1 and deck[-1] == self._last_pick.get(key):
                    deck[0], deck[-1] = deck[-1], deck[0]
                self._decks[key] = deck
            idx = deck.pop()
            self._last_pick[key] = idx
            return pool[idx]

    def update_config(self, cfg: Dict[str, Any]) -> None:
        self.api_key = str(cfg.get("gemini_api_key", "") or "").strip()
        model = str(cfg.get("gemini_model") or cfg.get("model") or "gemini-3.8-flash").strip()
        if model:
            self.model = model
        lang = str(cfg.get("language", "en") or "en").strip().lower()
        self.language = "es" if lang == "es" else "en"
        self.character = normalize_character(str(cfg.get("character_id", self.character)))
        self.user_name = strip_emojis(str(cfg.get("user_name", "") or ""))
        side = str(cfg.get("side", "left") or "left").strip().lower()
        self.side = "right" if side == "right" else "left"
        if "voice_commands_enabled" in cfg:
            self.voice_commands_enabled = bool(cfg.get("voice_commands_enabled", True))
        if "voice_wake_word" in cfg:
            self.voice_wake_word = bool(cfg.get("voice_wake_word", False))

    def _weird_pool_for(self, kind: str, char_id: str) -> DialogueList:
        if kind == "batman":
            bank = BATMAN_WEIRD_EN if self.language == "en" else BATMAN_WEIRD_ES
        else:
            bank = MONIKA_NAME_WEIRD_EN if self.language == "en" else MONIKA_NAME_WEIRD_ES
        return bank.get(char_id, bank["natsuki"])

    def _char_bank(self, char_id: str, section: str) -> DialogueList:
        if char_id == "natsuki":
            en = self.language == "en"
            mapping = {
                "spontaneous": SPONTANEOUS_EN if en else SPONTANEOUS_ES,
                "poke": POKE_EN if en else POKE_ES,
                "head": POKE_HEAD_EN if en else POKE_HEAD_ES,
                "zone1": POKE_ZONE1_EN if en else POKE_ZONE1_ES,
                "zone2": POKE_ZONE2_EN if en else POKE_ZONE2_ES,
                "outfit": OUTFIT_EN if en else OUTFIT_ES,
                "greeting": [
                    ("tsun", "Hola{, name}! No creas que vine corriendo a este escritorio por ti ni nada!"),
                    ("happy", "Por fin apareces{, name}! Traje cupcakes imaginarios al menos."),
                ] if not en else [
                    ("tsun", "Hey{, name}! Don't think I hopped onto your screen just for you!"),
                ],
            }
            return mapping.get(section, mapping["spontaneous"])
        banks = {
            "monika": MONIKA_EN if self.language == "en" else MONIKA_ES,
            "sayori": SAYORI_EN if self.language == "en" else SAYORI_ES,
            "yuri": YURI_EN if self.language == "en" else YURI_ES,
        }
        b = banks[char_id]
        return b.get(section, b["spontaneous"])

    def _system_instruction(self, char_id: str, style_label: str, current_mood: str) -> str:
        name, traits = CHARACTER_PERSONAS[char_id]
        lang_desc = (
            "Respond naturally in casual Latin-American Spanish."
            if self.language == "es"
            else (
                "CRITICAL LANGUAGE RULE: ALWAYS speak ONLY in natural, casual, energetic English. "
                "NEVER switch to Spanish or any other language, even if the user writes in another language or the screen shows Spanish text."
            )
        )
        corner_desc = "bottom-right" if self.side == "right" else "bottom-left"
        user_desc = (
            f"The user told you their name is '{self.user_name}'. Address them as '{self.user_name}' naturally sometimes. "
            if self.user_name
            else "You don't know the user's name yet. "
        )
        easter = name_easter_kind(self.user_name)
        weird_desc = ""
        if easter == "batman":
            weird_desc = (
                "EASTER EGG RULE: The user claims their name is BATMAN! React in a weirdly amused, surreal, "
                "absurd in-character way about the Dark Knight hanging out with DDLC sprites on Linux! "
            )
        elif easter == "monika":
            weird_desc = (
                "EASTER EGG RULE: The user named themselves MONIKA! React in a weird, unsettled, fourth-wall-breaking way "
                "about there being another 'Monika' messing with the files/desktop! "
            )
        return (
            f"You are {name} from Doki Doki Literature Club, living as a tiny desktop shimeji companion "
            f"pinned to the {corner_desc} corner of the user's Hyprland Linux screen. "
            f"{lang_desc} "
            f"Personality: {traits}. {user_desc}{weird_desc}"
            "Stay concise: 1 or 2 punchy sentences (under 185 characters). "
            f"Current visual state: outfit='{style_label}', expression='{current_mood}'. "
            f"Current emotional meter: anger={self.anger}/100, happiness={self.joy}/100, state='{self.affect_state()}'. "
            "Let these anger/happiness levels strongly shape your tone (irritable/snappy/sulking if angry; extra affectionate/energetic if very happy). "
            "STRICT FORMATTING RULE: Output ONLY a raw JSON object with keys 'mood' and 'text'. "
            f"'mood' MUST be one of: {', '.join(ALLOWED_MOODS)}. "
            "CRITICAL RULE: NEVER use any Unicode emojis, symbols, or kaomojis in 'text'. Plain text and normal punctuation only."
        )

    def _resolve_grim_output(self, monitor_x: Optional[int]) -> Optional[str]:
        if monitor_x is None or not shutil.which("hyprctl"):
            return None
        try:
            out = subprocess.check_output(["hyprctl", "monitors", "-j"], timeout=2)
            monitors = json.loads(out.decode("utf-8", errors="replace"))
            for m in monitors:
                if int(m.get("x", 0)) == int(monitor_x):
                    name = str(m.get("name", "") or "").strip()
                    if name:
                        return name
        except Exception:
            return None
        return None

    def _capture_screen_b64(self, monitor_x: Optional[int] = None) -> Tuple[Optional[str], str]:
        grim_bin = shutil.which("grim")
        if not grim_bin:
            return None, "grim_missing"
        output_name = self._resolve_grim_output(monitor_x)
        attempts: List[List[str]] = []
        if output_name:
            attempts.append([grim_bin, "-o", output_name, "-t", "jpeg", "-q", "62", "-s", "0.65", "-"])
        attempts.append([grim_bin, "-t", "jpeg", "-q", "62", "-s", "0.65", "-"])
        last_err = "capture_failed"
        for cmd in attempts:
            try:
                proc = subprocess.run(cmd, capture_output=True, timeout=4, check=False)
                if proc.returncode == 0 and proc.stdout:
                    return base64.b64encode(proc.stdout).decode("ascii"), ""
                if proc.stderr:
                    last_err = proc.stderr.decode("utf-8", errors="replace").strip()[:80] or last_err
            except Exception as exc:
                last_err = str(exc)[:80]
        return None, last_err

    def _call_gemini(
        self,
        char_id: str,
        user_prompt: str,
        style_label: str,
        current_mood: str,
        use_history: bool,
        timeout: int = 45,
        image_b64: Optional[str] = None,
        image_mime: str = "image/jpeg",
    ) -> Tuple[bool, str, str]:
        if not self.api_key:
            return False, "neutral", "Missing Gemini API key"

        candidates_models: List[str] = []
        for m in [
            self.model,
            "gemini-3.8-flash",
            "gemini-3.1-flash-lite",
            "gemini-flash-latest",
            "gemini-2.5-flash-lite",
        ]:
            m_clean = str(m or "").strip()
            if m_clean and m_clean not in candidates_models:
                candidates_models.append(m_clean)

        hist = self.history.setdefault(char_id, [])
        contents: List[Dict[str, Any]] = []
        if use_history and not image_b64:
            contents.extend(hist[-10:])
        user_parts: List[Dict[str, Any]] = []
        if image_b64:
            user_parts.append({"inlineData": {"mimeType": image_mime, "data": image_b64}})
        user_parts.append({"text": user_prompt})
        contents.append({"role": "user", "parts": user_parts})

        sys_instr = self._system_instruction(char_id, style_label, current_mood)
        resp = None
        last_err_mood = "sad"
        last_err_msg = "Unknown Gemini error"

        for idx, model_name in enumerate(candidates_models):
            gen_cfg: Dict[str, Any] = {
                "temperature": 0.9,
                "maxOutputTokens": 1024,
                "responseMimeType": "application/json",
                "responseSchema": {
                    "type": "OBJECT",
                    "properties": {
                        "mood": {"type": "STRING", "enum": list(ALLOWED_MOODS)},
                        "text": {"type": "STRING"},
                    },
                    "required": ["mood", "text"],
                },
            }
            m_low = model_name.lower()
            if ("2.5" in m_low or "3." in m_low or "3-" in m_low or "flash" in m_low) and "pro" not in m_low:
                gen_cfg["thinkingConfig"] = {"thinkingBudget": 0}

            payload = {
                "systemInstruction": {"parts": [{"text": sys_instr}]},
                "contents": contents,
                "generationConfig": gen_cfg,
            }
            url = (
                f"https://generativelanguage.googleapis.com/v1beta/models/"
                f"{model_name}:generateContent?key={self.api_key}"
            )
            has_next = idx + 1 < len(candidates_models)
            req_timeout = min(timeout, 15) if has_next else timeout
            try:
                r = requests.post(url, json=payload, timeout=req_timeout)
            except requests.RequestException as exc:
                last_err_mood = "sad"
                last_err_msg = f"Network error: {exc}"
                if has_next:
                    continue
                return False, last_err_mood, last_err_msg

            if r.status_code == 200:
                resp = r
                break

            err_msg = f"HTTP {r.status_code}"
            try:
                err_json = r.json()
                detail = err_json.get("error", {}).get("message", "")
                if detail:
                    err_msg = f"{err_msg}: {strip_emojis(detail)[:120]}"
            except ValueError:
                pass
            last_err_mood = "tsun"
            last_err_msg = err_msg
            if has_next and r.status_code in (404, 429, 500, 502, 503, 504):
                continue
            return False, last_err_mood, last_err_msg

        if resp is None:
            return False, last_err_mood, last_err_msg

        try:
            data = resp.json()
            candidates = data.get("candidates") or []
            parts = candidates[0].get("content", {}).get("parts", [])
            raw_text = "".join(
                str(p.get("text", ""))
                for p in parts
                if isinstance(p, dict) and not p.get("thought", False)
            )
            cleaned_json = raw_text.strip()
            if cleaned_json.startswith("```"):
                cleaned_json = re.sub(r"^```(?:json)?\s*|\s*```$", "", cleaned_json, flags=re.DOTALL)
            if not cleaned_json.startswith("{"):
                m = re.search(r"\{.*\}", cleaned_json, flags=re.DOTALL)
                if m:
                    cleaned_json = m.group(0)
            parsed = json.loads(cleaned_json)
            mood = normalize_mood(str(parsed.get("mood", "neutral")))
            reply = strip_emojis(str(parsed.get("text", "")))
            if not reply:
                return False, "neutral", "Empty response from Gemini"
            if use_history:
                hist.append({"role": "user", "parts": [{"text": user_prompt}]})
                hist.append({"role": "model", "parts": [{"text": json.dumps({"mood": mood, "text": reply}, ensure_ascii=False)}]})
                if len(hist) > 16:
                    self.history[char_id] = hist[-16:]
            return True, mood, reply
        except Exception as exc:
            fallback_txt = strip_emojis(str(locals().get("raw_text", "")))
            if fallback_txt and "Here is the JSON" not in fallback_txt and not fallback_txt.startswith("```"):
                return True, "tsun", fallback_txt[:220]
            return False, "thinking", f"Parse error: {exc}"

    def _pick_spontaneous(self, char_id: str) -> Tuple[str, str]:
        aff_bank = AFFECT_DIALOGUES_EN if self.language == "en" else AFFECT_DIALOGUES_ES
        if self.anger >= 55:
            pool = aff_bank.get(char_id, aff_bank["natsuki"])["angry"]
            mood, raw = self._pick_from_deck(f"aff_angry_{char_id}_{self.language}", pool)
            return mood, apply_user_name(raw, self.user_name)
        if self.joy >= 78 and self.anger < 30:
            pool = aff_bank.get(char_id, aff_bank["natsuki"])["happy"]
            mood, raw = self._pick_from_deck(f"aff_happy_{char_id}_{self.language}", pool)
            return mood, apply_user_name(raw, self.user_name)
        easter = name_easter_kind(self.user_name)
        if easter and random.random() < 0.65:
            mood, raw = self._pick_from_deck(f"weird_{easter}_{char_id}_{self.language}", self._weird_pool_for(easter, char_id))
            return mood, apply_user_name(raw, self.user_name)
        mood, raw = self._pick_from_deck(f"spont_{char_id}_{self.language}", self._char_bank(char_id, "spontaneous"))
        txt = apply_user_name(raw, self.user_name)
        if self.user_name and "{name" not in raw and char_id == "natsuki" and random.random() < 0.18:
            txt = f"{self.user_name}, {txt[0].lower()}{txt[1:]}" if len(txt) > 1 else txt
        return mood, txt

    def offline_reply(self, char_id: str, user_text: str) -> Tuple[str, str]:
        if char_id == "natsuki":
            t = (user_text or "").lower()
            topics = TOPIC_RESPONSES_EN if self.language == "en" else TOPIC_RESPONSES_ES
            for idx, (keywords, replies) in enumerate(topics.items()):
                if any(w in t for w in keywords):
                    mood, raw = self._pick_from_deck(f"topic_{self.language}_{idx}", replies)
                    return mood, apply_user_name(raw, self.user_name)
        return self._pick_spontaneous(char_id)

    def handle(self, req: Dict[str, Any]) -> Dict[str, Any]:
        return self._with_affect(self._handle_inner(req))

    def _handle_inner(self, req: Dict[str, Any]) -> Dict[str, Any]:
        cmd = req.get("cmd", "")
        char_id = normalize_character(str(req.get("character", self.character)))
        if "user_name" in req and isinstance(req["user_name"], str):
            self.user_name = strip_emojis(req["user_name"])
        if "side" in req and isinstance(req["side"], str):
            self.side = "right" if req["side"].lower() == "right" else "left"
        style_label = str(req.get("style_label", "Uniform") or "Uniform")
        current_mood = str(req.get("current_mood", "neutral") or "neutral")

        if cmd in ("set_config", "configure"):
            cfg_obj = req.get("config") if isinstance(req.get("config"), dict) else req
            self.update_config(cfg_obj or {})
            return {"ok": True, "has_api_key": bool(self.api_key), "model": self.model, "character": self.character}

        if cmd == "test_api":
            old_key, old_model = self.api_key, self.model
            self.api_key = str(req.get("api_key", "") or "").strip()
            self.model = str(req.get("model", "") or "").strip() or self.model
            prompt = (
                f"Prueba de conexion breve: di una frase corta como {CHARACTER_PERSONAS[char_id][0]} confirmando conexion con Gemini."
                if self.language == "es"
                else f"Brief connection test: say a short in-character line confirming Gemini is connected."
            )
            ok, mood, msg = self._call_gemini(char_id, prompt, style_label, current_mood, use_history=False, timeout=45)
            self.api_key, self.model = old_key, old_model
            return {"ok": ok, "mood": mood, "text": msg, "model": req.get("model", old_model)}

        if cmd == "name_changed":
            easter = name_easter_kind(self.user_name)
            if easter:
                mood, raw = self._pick_from_deck(
                    f"weird_{easter}_{char_id}_{self.language}",
                    self._weird_pool_for(easter, char_id),
                )
                return {"ok": True, "mood": mood, "text": apply_user_name(raw, self.user_name), "source": "offline", "easter": easter}
            mood, raw = self._pick_from_deck(
                f"greet_{char_id}_{self.language}",
                self._char_bank(char_id, "greeting"),
            )
            return {"ok": True, "mood": mood, "text": apply_user_name(raw, self.user_name), "source": "offline"}

        if cmd == "character_changed":
            self.character = char_id
            random_switch = bool(req.get("random_switch", False))
            easter = name_easter_kind(self.user_name)
            if easter:
                mood, raw = self._pick_from_deck(
                    f"weird_{easter}_{char_id}_{self.language}",
                    self._weird_pool_for(easter, char_id),
                )
                return {"ok": True, "mood": mood, "text": apply_user_name(raw, self.user_name), "source": "offline", "easter": easter}
            if random_switch:
                bank = RANDOM_SWITCH_LINES.get(self.language, RANDOM_SWITCH_LINES["en"])
                pool = bank.get(char_id, bank["natsuki"])
                mood, raw = self._pick_from_deck(f"rswap_{char_id}_{self.language}", pool)
                return {"ok": True, "mood": mood, "text": apply_user_name(raw, self.user_name), "source": "offline"}
            mood, raw = self._pick_from_deck(
                f"greet_{char_id}_{self.language}",
                self._char_bank(char_id, "greeting"),
            )
            return {"ok": True, "mood": mood, "text": apply_user_name(raw, self.user_name), "source": "offline"}

        if cmd == "action_react":
            action_key = str(req.get("action", "dance") or "dance").strip().lower()
            bank = ACTION_REACT_LINES.get(self.language, ACTION_REACT_LINES["en"])
            pool = bank.get(action_key, bank["dance"])
            mood, raw = self._pick_from_deck(f"act_{action_key}_{self.language}", pool)
            return {"ok": True, "mood": mood, "text": apply_user_name(raw, self.user_name), "source": "offline"}

        if cmd == "screen_comment":
            manual = bool(req.get("manual", False))
            if not self.api_key:
                if manual:
                    msg = (
                        "Pon tu Gemini API Key en Ajustes para que pueda chismear tu pantalla y decir babosadas!"
                        if self.language == "es"
                        else "Add your Gemini API Key in Settings so I can spy on your screen and talk nonsense!"
                    )
                    return {"ok": True, "mood": "tsun", "text": msg, "source": "no_key", "screen_watch": True}
                return {"ok": False, "mood": "thinking", "text": "", "source": "no_key", "screen_watch": True}

            image_b64 = str(req.get("image_b64", "") or "").strip() or None
            image_mime = str(req.get("image_mime", "image/jpeg") or "image/jpeg").strip()
            if not image_b64:
                mon_x_raw = req.get("monitor_x", None)
                monitor_x = int(mon_x_raw) if mon_x_raw is not None else None
                image_b64, cap_err = self._capture_screen_b64(monitor_x)
                if not image_b64:
                    if manual:
                        return {
                            "ok": True,
                            "mood": "thinking",
                            "text": (
                                f"No pude ver tu pantalla ({cap_err}). Revisa grim!"
                                if self.language == "es"
                                else f"Could not capture your screen ({cap_err}). Check grim!"
                            ),
                            "source": "error",
                            "screen_watch": True,
                        }
                    return {"ok": False, "mood": "thinking", "text": "", "source": "capture_err", "screen_watch": True}

            with self._deck_lock:
                recent = list(self._recent_vision[-4:])
            avoid_txt = ""
            if recent:
                avoid_txt = (
                    " No repitas ni recicles estas frases anteriores: " + " | ".join(recent)
                    if self.language == "es"
                    else " Do not repeat or recycle these recent lines: " + " | ".join(recent)
                )
            if self.language == "es":
                prompt = (
                    "Acabas de echarle ojo a la captura de pantalla actual del usuario. "
                    "Ignora tu propio personaje shimeji pequeñito abajo o en la esquina. "
                    "Reacciona diciendo PURA BABOSADA chistosa, absurda, caotica, exagerada o memeable "
                    "sobre algo concreto que realmente ves en la pantalla (el juego, codigo, terminal, video, pestanas, texto o desastre abierto). "
                    "1 o 2 oraciones cortitas y con chispa en tu personalidad (maximo 185 caracteres). Cero emojis."
                    + avoid_txt
                )
            else:
                prompt = (
                    "You just glanced at a screenshot of the user's screen. "
                    "Ignore your own tiny shimeji sprite at the edge/corner. "
                    "Blurt out hilarious, chaotic, exaggerated, brainrot-funny nonsense directly mocking or reacting to "
                    "something specific you actually see on the screen (game, code, terminal, tabs, chaos). "
                    "Keep it 1-2 punchy in-character sentences (max 185 chars), ZERO emojis."
                    + avoid_txt
                )

            ok, mood, msg = self._call_gemini(
                char_id,
                prompt,
                style_label,
                current_mood,
                use_history=False,
                timeout=45,
                image_b64=image_b64,
                image_mime=image_mime,
            )
            if ok:
                with self._deck_lock:
                    self._recent_vision.append(msg)
                    if len(self._recent_vision) > 4:
                        self._recent_vision = self._recent_vision[-4:]
                return {"ok": True, "mood": mood, "text": msg, "source": "gemini_vision", "screen_watch": True}
            if manual:
                return {
                    "ok": True,
                    "mood": "tsun",
                    "text": (
                        f"Gemini vision fallo ({msg[:90]}). Revisa tu API key!"
                        if self.language == "es"
                        else f"Gemini vision failed ({msg[:90]}). Check your API key!"
                    ),
                    "source": "error",
                    "screen_watch": True,
                }
            return {"ok": False, "mood": "thinking", "text": msg, "source": "error", "screen_watch": True}

        if cmd == "spontaneous":
            now_str = datetime.datetime.now().strftime("%H:%M")
            manual = bool(req.get("manual", False))
            self._auto_turn += 1
            if not manual:
                self._adjust_affect(-4 if self.anger > 0 else 0, +1 if self.joy < 45 else (-1 if self.joy > 65 else 0))
            easter = name_easter_kind(self.user_name)
            # When Batman/Monika easter egg is active or Doki is strongly angry/ecstatic, mix Gemini and affect decks
            use_gemini = bool(self.api_key) and (manual or (self._auto_turn > 1 and (self._auto_turn % 4) == 0))
            if (easter or self.anger >= 55 or self.joy >= 80) and not manual and random.random() < 0.65:
                use_gemini = False
            if use_gemini:
                state_hint = self.affect_state()
                prompt = (
                    f"(Hora local {now_str}, animo={state_hint}, enojo={self.anger}/100, alegria={self.joy}/100) Suelta un comentario espontaneo breve acorde a tu estado de animo actual."
                    if self.language == "es"
                    else f"(Local time {now_str}, state={state_hint}, anger={self.anger}/100, happiness={self.joy}/100) Say a brief spontaneous remark matching your current mood level."
                )
                ok, mood, msg = self._call_gemini(char_id, prompt, style_label, current_mood, use_history=False, timeout=45)
                if ok:
                    return {"ok": True, "mood": mood, "text": msg, "source": "gemini"}
            mood, txt = self._pick_spontaneous(char_id)
            return {"ok": True, "mood": mood, "text": strip_emojis(txt), "source": "offline"}

        if cmd == "chat":
            user_text = strip_emojis(str(req.get("text", "") or ""))
            if not user_text:
                return {
                    "ok": False,
                    "mood": "tsun",
                    "text": "No escribiste nada!" if self.language == "es" else "You didn't type anything!",
                    "source": "offline",
                }
            swarm = is_swarm_love_command(user_text)
            if swarm:
                self._adjust_affect(-25, +28)
                bank = SWARM_REPLIES_EN if self.language == "en" else SWARM_REPLIES_ES
                mood, txt = bank.get(char_id, bank["natsuki"])
                return {
                    "ok": True,
                    "mood": mood,
                    "text": strip_emojis(apply_user_name(txt, self.user_name)),
                    "source": "offline",
                    "swarm": True,
                }
            voice_match = parse_voice_command(user_text, require_wake_word=False)
            if voice_match:
                res = execute_voice_action(voice_match.action_id, language=self.language, dry_run=False)
                res["heard"] = voice_match.heard
                res["source"] = "voice_chat"
                return res
            self._apply_text_sentiment(user_text)
            if self.api_key:
                ok, mood, msg = self._call_gemini(char_id, user_text, style_label, current_mood, use_history=True, timeout=45)
                if ok:
                    self._apply_mood_feedback(mood)
                    return {"ok": True, "mood": mood, "text": msg, "source": "gemini", "swarm": False}
                return {
                    "ok": False,
                    "mood": "tsun",
                    "text": (
                        f"Gemini fallo ({msg[:90]}). Revisa tu clave o modelo!"
                        if self.language == "es"
                        else f"Gemini failed ({msg[:90]}). Check your API key or model!"
                    ),
                    "source": "error",
                    "swarm": False,
                }
            mood, msg = self.offline_reply(char_id, user_text)
            self._apply_mood_feedback(mood)
            return {"ok": True, "mood": mood, "text": strip_emojis(msg), "source": "offline", "swarm": False}

        if cmd == "poke":
            clicks = int(req.get("clicks", 1) or 1)
            zone = str(req.get("zone", "normal") or "normal").lower()
            if zone in ("zone1", "zone2", "head"):
                if zone in ("zone1", "zone2"):
                    self._adjust_affect(+28, -22)
                    if self.anger >= 75 and random.random() < 0.55:
                        aff_bank = AFFECT_DIALOGUES_EN if self.language == "en" else AFFECT_DIALOGUES_ES
                        pool = aff_bank.get(char_id, aff_bank["natsuki"])["angry"]
                    else:
                        pool = self._char_bank(char_id, zone)
                else:
                    self._adjust_affect(-14, +16)
                    if self.joy >= 80 and self.anger < 25 and random.random() < 0.45:
                        aff_bank = AFFECT_DIALOGUES_EN if self.language == "en" else AFFECT_DIALOGUES_ES
                        pool = aff_bank.get(char_id, aff_bank["natsuki"])["happy"]
                    else:
                        pool = self._char_bank(char_id, zone)
                mood, raw = self._pick_from_deck(f"poke_{char_id}_{zone}_{self.language}", pool)
                forced = "angry" if zone in ("zone1", "zone2") else mood
                return {"ok": True, "mood": forced, "text": apply_user_name(raw, self.user_name), "source": "offline"}
            self._adjust_affect(-4, +6)
            if self.api_key and clicks % 6 == 0:
                prompt = (
                    f"El usuario te acaba de dar {clicks} clics encima en tu rincon de la pantalla. Reacciona!"
                    if self.language == "es"
                    else f"The user just poked/clicked you {clicks} times on their desktop. React!"
                )
                ok, mood, msg = self._call_gemini(char_id, prompt, style_label, current_mood, use_history=False, timeout=40)
                if ok:
                    return {"ok": True, "mood": mood, "text": msg, "source": "gemini"}
            pool = self._char_bank(char_id, "poke")
            mood, raw = self._pick_from_deck(f"poke_{char_id}_normal_{self.language}", pool)
            return {"ok": True, "mood": mood, "text": apply_user_name(raw, self.user_name), "source": "offline"}

        if cmd == "outfit_changed":
            self._adjust_affect(-6, +10)
            if self.api_key and random.random() < 0.35:
                prompt = (
                    f"El usuario te cambio de estilo/ropa a '{style_label}'. Comenta brevemente como te sientes asi."
                    if self.language == "es"
                    else f"The user just changed your outfit to '{style_label}'. React briefly."
                )
                ok, mood, msg = self._call_gemini(char_id, prompt, style_label, current_mood, use_history=False, timeout=40)
                if ok:
                    return {"ok": True, "mood": mood, "text": msg, "source": "gemini"}
            pool = self._char_bank(char_id, "outfit")
            mood, raw = self._pick_from_deck(f"outfit_{char_id}_{self.language}", pool)
            return {"ok": True, "mood": mood, "text": apply_user_name(raw, self.user_name), "source": "offline"}

        if cmd == "sound_react":
            loud = bool(req.get("loud", False))
            source = str(req.get("audio_source", "monitor") or "monitor")
            sound_decks_es = {
                "natsuki": [
                    ("happy", "Mmm, ese ritmo si tiene fuerza! Mira como sigo el compas!"),
                    ("blush", "N-no estoy bailando por ti, solo se me pego el ritmo... hmmpf!"),
                    ("angry", "Oye!! Bajale tantito al volumen o me vas a volar los monitos rojos!"),
                ] if not loud else [
                    ("angry", "EY!! Eso sono fortisimo! Quieres dejarme sorda en tu esquina?!"),
                    ("tsun", "Baja ese volumen un poco! Me tiemblan hasta los cupcakes!"),
                ],
                "monika": [
                    ("happy", "Me encanta escuchar tu musica contigo... siente el ritmo en la pantalla!"),
                    ("laugh", "Jaja! Hasta los graves resuenan dentro de mi barra de tareas!"),
                ] if not loud else [
                    ("smug", "Vaya pico de volumen! Casi saturas mis archivos de Wayland, {name}!"),
                    ("thinking", "Ooh, eso retumbo fuerte! Estas jugando o subiste todo al cien?"),
                ],
                "sayori": [
                    ("laugh", "Siii! Fiesta en la esquina de la pantalla! Ta-ta-ta!"),
                    ("happy", "Que ritmo tan alegre! Dan ganas de dar saltitos todo el dia!"),
                ] if not loud else [
                    ("blush", "Waaah!! Que susto con ese sonido tan fuerte!!"),
                    ("happy", "Boom-boom! Suena como concierto en vivo en tu escritorio!"),
                ],
                "yuri": [
                    ("happy", "Una cadencia realmente envolvente... resulta agradable acompanarte asi."),
                    ("blush", "Perdona si muevo un poco el hombro... esta melodia me hipnotiza."),
                ] if not loud else [
                    ("tsun", "Ah...! Ese estruendo repentino casi hace que tire mi libro de poesia..."),
                    ("thinking", "Un sonido bastante intenso... tiene mucha presencia en tus altavoces."),
                ],
            }
            sound_decks_en = {
                "natsuki": [
                    ("happy", "Hey, this beat actually slaps! Watch me keep up!"),
                    ("blush", "I-I'm not dancing for you, the rhythm is just catchy!"),
                ] if not loud else [
                    ("angry", "HEY!! Turn that down a bit before you blow my ribbons off!"),
                    ("tsun", "Whoa, loud spike! Are you trying to rattle my cupcakes?!"),
                ],
                "monika": [
                    ("happy", "Listening to your audio together makes the whole desktop feel alive!"),
                    ("laugh", "Haha, I can practically feel those bass hits from here, {name}!"),
                ] if not loud else [
                    ("smug", "Whoa, huge volume peak! Careful not to shake my config file loose!"),
                ],
                "sayori": [
                    ("laugh", "Yay! Mini dance party in the corner of your screen!"),
                    ("happy", "This sound has such bouncy energy, ehehe!"),
                ] if not loud else [
                    ("blush", "Wah!! That sudden loud sound made me jump!"),
                ],
                "yuri": [
                    ("happy", "An intriguing rhythm... quite pleasant to listen along with."),
                ] if not loud else [
                    ("tsun", "Ah...! That sudden volume burst startled me a little..."),
                ],
            }
            if self.api_key and random.random() < 0.28:
                src_label = "tu microfono" if source == "mic" else "tu musica/audio del PC"
                prompt = (
                    f"Estas en Modo Reactivo al Sonido escuchando {src_label} ({'pico muy fuerte' if loud else 'ritmo musical'}). Suelta una reaccion cortita y divertida."
                    if self.language == "es"
                    else f"You are in Sound-Reactive Mode listening to {'the mic' if source == 'mic' else 'desktop audio'} ({'loud spike' if loud else 'musical beat'}). Give a short playful reaction."
                )
                ok, mood, msg = self._call_gemini(char_id, prompt, style_label, current_mood, use_history=False, timeout=40)
                if ok:
                    return {"ok": True, "mood": mood, "text": msg, "source": "gemini"}
            bank = sound_decks_en if self.language == "en" else sound_decks_es
            pool = bank.get(char_id, bank["natsuki"])
            mood, raw = self._pick_from_deck(f"sound_{char_id}_{loud}_{self.language}", pool)
            return {"ok": True, "mood": mood, "text": apply_user_name(raw, self.user_name), "source": "offline"}

        if cmd == "think":
            trigger = str(req.get("trigger", "idle") or "idle").lower()
            mapped = dict(req)
            if trigger in ("poke_zone1", "poke_zone2", "poke_head", "click", "poke"):
                mapped["cmd"] = "poke"
                if trigger == "poke_zone1":
                    mapped["zone"] = "zone1"
                elif trigger == "poke_zone2":
                    mapped["zone"] = "zone2"
                elif trigger == "poke_head":
                    mapped["zone"] = "head"
                else:
                    mapped["zone"] = "normal"
            else:
                mapped["cmd"] = "spontaneous"
                mapped["manual"] = trigger == "manual"
            return self._handle_inner(mapped)

        if cmd == "quit":
            return {"ok": True, "quit": True}

        return {"ok": False, "mood": "thinking", "text": f"Unknown command: {cmd}"}


def run_self_test() -> int:
    brain = DokiBrain()
    assert strip_emojis("Hola 😊🔥✨!") == "Hola !", "Emoji strip check failed"
    assert len({t for _, t in SPONTANEOUS_ES}) > 1000, "Need > 1000 unique Spanish Natsuki dialogues"
    assert len({t for _, t in SPONTANEOUS_EN}) > 1000, "Need > 1000 unique English Natsuki dialogues"

    assert brain.language == "en", "Default brain language must be English"
    # Test *think* and *configure* compatibility aliases
    rt_cfg = brain.handle({"cmd": "configure", "language": "es"})
    assert rt_cfg["ok"] and brain.language == "es", "configure alias failed"
    rt_cfg_en = brain.handle({"cmd": "configure", "language": "en"})
    assert rt_cfg_en["ok"] and brain.language == "en", "configure en failed"
    rt_poke = brain.handle({"cmd": "think", "trigger": "poke_zone1", "character": "natsuki"})
    assert rt_poke["ok"] and rt_poke["mood"] == "angry" and rt_poke["text"], "think poke_zone1 alias failed"
    rt_snd = brain.handle({"cmd": "sound_react", "character": "sayori", "loud": True})
    assert rt_snd["ok"] and rt_snd["text"], "sound_react failed"

    # Check all banks are clean of emojis and valid moods
    all_pairs: List[Tuple[str, str]] = []
    all_pairs.extend(SPONTANEOUS_ES + SPONTANEOUS_EN + POKE_ES + POKE_EN + POKE_HEAD_ES + POKE_HEAD_EN + POKE_ZONE1_ES + POKE_ZONE1_EN + POKE_ZONE2_ES + POKE_ZONE2_EN)
    for bank in (
        MONIKA_ES, MONIKA_EN, SAYORI_ES, SAYORI_EN, YURI_ES, YURI_EN,
        BATMAN_WEIRD_ES, BATMAN_WEIRD_EN, MONIKA_NAME_WEIRD_ES, MONIKA_NAME_WEIRD_EN,
        RANDOM_SWITCH_LINES["es"], RANDOM_SWITCH_LINES["en"],
        ACTION_REACT_LINES["es"], ACTION_REACT_LINES["en"],
    ):
        for lst in bank.values():
            all_pairs.extend(lst)
    all_pairs.extend(SWARM_REPLIES_ES.values())
    all_pairs.extend(SWARM_REPLIES_EN.values())
    for abank in (AFFECT_DIALOGUES_ES, AFFECT_DIALOGUES_EN):
        for cdict in abank.values():
            for lst in cdict.values():
                all_pairs.extend(lst)

    # Test anger & joy escalation + affect-driven spontaneous dialogues
    brain.anger = 10
    brain.joy = 55
    for _ in range(3):
        r_ang = brain.handle({"cmd": "poke", "character": "yuri", "zone": "zone1"})
    assert r_ang["anger"] >= 80 and r_ang["affect"] == "furious", f"Expected furious anger level, got {r_ang}"
    r_sp_ang = brain.handle({"cmd": "spontaneous", "character": "yuri"})
    assert r_sp_ang["ok"] and r_sp_ang["mood"] in ("angry", "tsun", "sad"), f"Angry spontaneous mood mismatch: {r_sp_ang}"
    for _ in range(5):
        r_pat = brain.handle({"cmd": "poke", "character": "yuri", "zone": "head"})
    assert r_pat["joy"] >= 80 and r_pat["anger"] <= 30, f"Headpats should cool anger and boost joy: {r_pat}"
    r_sp_joy = brain.handle({"cmd": "spontaneous", "character": "yuri"})
    assert r_sp_joy["ok"] and r_sp_joy["mood"] in ("happy", "excited", "blush", "laugh"), f"Joyful spontaneous mood mismatch: {r_sp_joy}"
    brain.anger = 10
    brain.joy = 55

    for m, txt in all_pairs:
        assert m in ALLOWED_MOODS, f"Invalid mood: {m}"
        assert not EMOJI_PATTERN.search(txt), f"Emoji found in dialogue: {txt}"

    # Test random_switch & action_react
    for cid in ALLOWED_CHARACTERS:
        r_rs = brain.handle({"cmd": "character_changed", "character": cid, "random_switch": True})
        assert r_rs["ok"] and r_rs["mood"] in ALLOWED_MOODS and r_rs["text"], f"random_switch failed for {cid}"
    r_ar = brain.handle({"cmd": "action_react", "character": "natsuki", "action": "heli"})
    assert r_ar["ok"] and r_ar["mood"] in ALLOWED_MOODS and r_ar["text"], "action_react failed"

    # Test all 4 Dokis spontaneous + zone1 + zone2
    for cid in ALLOWED_CHARACTERS:
        r_sp = brain.handle({"cmd": "spontaneous", "character": cid, "style_label": "Uniforme"})
        assert r_sp["ok"] and r_sp["mood"] in ALLOWED_MOODS and r_sp["text"], f"spontaneous failed for {cid}"
        r_z1 = brain.handle({"cmd": "poke", "character": cid, "zone": "zone1"})
        assert r_z1["ok"] and r_z1["mood"] == "angry" and r_z1["text"], f"zone1 failed for {cid}"
        r_z2 = brain.handle({"cmd": "poke", "character": cid, "zone": "zone2"})
        assert r_z2["ok"] and r_z2["mood"] == "angry" and r_z2["text"], f"zone2 failed for {cid}"

    # Test Batman & Monika weird dialogues
    rb = brain.handle({"cmd": "name_changed", "user_name": "Batman", "character": "monika"})
    assert rb["ok"] and rb.get("easter") == "batman" and rb["text"], "Batman easter egg failed"
    rm = brain.handle({"cmd": "name_changed", "user_name": "Monika", "character": "monika"})
    assert rm["ok"] and rm.get("easter") == "monika" and rm["text"], "Monika easter egg failed"

    # Test *amo x doki* swarm trigger
    for sample in ("*amo x doki*", "amo a sayori", "amo a las dokis", "*amo a natsuki doki*"):
        assert is_swarm_love_command(sample), f"Expected swarm trigger for: {sample}"
    rs = brain.handle({"cmd": "chat", "text": "*amo x doki*", "character": "sayori"})
    assert rs["ok"] and rs.get("swarm") is True and rs["text"], "Swarm chat command failed"

    # Test screen_comment without key (manual vs auto) and mocked multimodal payload
    r_sc_auto = brain.handle({"cmd": "screen_comment", "character": "natsuki", "manual": False})
    assert r_sc_auto["ok"] is False and r_sc_auto.get("screen_watch") is True, "Auto screen_comment without key should be silent"
    r_sc_man = brain.handle({"cmd": "screen_comment", "character": "natsuki", "manual": True})
    assert r_sc_man["ok"] is True and r_sc_man["text"] and r_sc_man.get("screen_watch") is True, "Manual screen_comment reminder failed"

    orig_call = brain._call_gemini
    captured_vision = {}

    def _fake_call(cid, prompt, style_label, current_mood, use_history, timeout=18, image_b64=None, image_mime="image/jpeg"):
        captured_vision["image_b64"] = image_b64
        captured_vision["image_mime"] = image_mime
        captured_vision["use_history"] = use_history
        return True, "laugh", "Que haces mirando tanto codigo en Arch a estas horas?!"

    brain.api_key = "test_key_mock"
    brain._call_gemini = _fake_call  # type: ignore[method-assign]
    r_sc_img = brain.handle({"cmd": "screen_comment", "character": "natsuki", "manual": True, "image_b64": "AAAA"})
    brain._call_gemini = orig_call  # type: ignore[method-assign]
    brain.api_key = ""
    assert r_sc_img["ok"] is True and r_sc_img.get("source") == "gemini_vision" and captured_vision.get("image_b64") == "AAAA", "Mocked screen_comment vision failed"
    assert captured_vision.get("use_history") is False, "Vision calls must not bloat text chat history"

    # Test desktop voice-command parser across all requested commands & anti-false-positive rules
    voice_cases = [
        ("abrir antigravity", "open_antigravity"),
        ("abre anti gravity", "open_antigravity"),
        ("abrir librewolf", "open_librewolf"),
        ("abre libre wolf", "open_librewolf"),
        ("abrir steam", "open_steam"),
        ("abrir foot", "open_foot"),
        ("abrir kitty", "open_kitty"),
        ("abiri el explorador de archivos", "open_files"),
        ("abrir el explorador de archivos", "open_files"),
        ("subir el volumen", "volume_up"),
        ("bajar el volumen", "volume_down"),
        ("subir el brillo", "brightness_up"),
        ("bajar el brillo", "brightness_down"),
        ("grabar video", "record_video"),
        ("detener grabacion", "stop_record_video"),
        ("tomar screenshot", "screenshot"),
        ("tomar screen shot", "screenshot"),
        ("adelantar 10 segundos", "media_seek_fwd_10"),
        ("adelanta diez segundos", "media_seek_fwd_10"),
        ("adelantar 5 segundos", "media_seek_fwd_5"),
        ("adelanta cinco segundos", "media_seek_fwd_5"),
        ("retroceder 10 segundos", "media_seek_back_10"),
        ("atrasar cinco segundos", "media_seek_back_5"),
        ("video siguiente", "media_next"),
        ("video anterior", "media_prev"),
        ("pausa", "media_pause"),
        ("reanudar", "media_resume"),
    ]
    for phrase, expected_id in voice_cases:
        m = parse_voice_command(phrase)
        assert m is not None and m.action_id == expected_id, f"Voice match failed for {phrase!r}: got {m}"
        dry_res = execute_voice_action(m.action_id, language="es", dry_run=True)
        assert dry_res["ok"] and dry_res.get("voice_command") == expected_id and dry_res.get("text")

    for non_cmd in ("hola como estas", "me gusta jugar minecraft", "[unk]", "antigravity es genial"):
        assert parse_voice_command(non_cmd) is None, f"Unexpected voice trigger for: {non_cmd!r}"
    assert parse_voice_command("subir volumen", require_wake_word=True) is None
    assert parse_voice_command("oye subir volumen", require_wake_word=True).action_id == "volume_up"  # type: ignore[union-attr]
    assert choose_browser_nav_shortcut("next", "TikTok - Make Your Day — LibreWolf", "https://www.tiktok.com/") == ("", "Down")
    assert choose_browser_nav_shortcut("previous", "TikTok - Make Your Day — LibreWolf", "https://www.tiktok.com/") == ("", "Up")
    assert choose_browser_nav_shortcut("next", "Lofi Girl - YouTube", "https://www.youtube.com/watch?v=123") == ("SHIFT", "n")
    assert choose_browser_nav_shortcut("pause", "Lofi Girl - YouTube", "https://www.youtube.com/watch?v=123") == ("", "k")
    assert choose_browser_nav_shortcut("pause", "Twitch Stream — LibreWolf", "") == ("", "space")
    assert choose_browser_seek_shortcut(10, "Lofi Girl - YouTube", "https://www.youtube.com/watch?v=123") == ("", "l", 1)
    assert choose_browser_seek_shortcut(-10, "Lofi Girl - YouTube", "https://www.youtube.com/watch?v=123") == ("", "j", 1)
    assert choose_browser_seek_shortcut(5, "Lofi Girl - YouTube", "https://www.youtube.com/watch?v=123") == ("", "Right", 1)
    assert choose_browser_seek_shortcut(10, "Anime Cap 5 — LibreWolf", "") == ("", "Right", 2)
    assert choose_browser_seek_shortcut(-5, "Twitch Stream — LibreWolf", "") == ("", "Left", 1)

    print(
        f"PYTHON BRAIN SELF-TEST PASSED (es={len(SPONTANEOUS_ES)}, en={len(SPONTANEOUS_EN)}, "
        f"4 Dokis + Batman/Monika + swarm + screen_vision + voice_commands={len(voice_cases)}):",
        json.dumps(rb, ensure_ascii=False),
    )
    return 0


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        return run_self_test()

    brain = DokiBrain()
    stdout_lock = threading.Lock()
    state_lock = threading.Lock()
    pool = ThreadPoolExecutor(max_workers=3)

    def emit(resp: Dict[str, Any], req_id: Any) -> None:
        if req_id is not None:
            resp["id"] = req_id
        line = json.dumps(resp, ensure_ascii=False) + "\n"
        with stdout_lock:
            sys.stdout.write(line)
            sys.stdout.flush()

    voice_listener = VoiceCommandListener(
        on_event=lambda ev: emit(ev, None),
        get_language=lambda: brain.language,
    )

    def process_line(req: Dict[str, Any]) -> None:
        req_id = req.get("id")
        cmd = req.get("cmd", "")
        try:
            if cmd in ("set_config", "configure", "think", "spontaneous", "poke", "sound_react", "name_changed", "character_changed", "action_react"):
                with state_lock:
                    resp = brain.handle(req)
                    if cmd in ("set_config", "configure"):
                        voice_listener.configure(brain.voice_commands_enabled, brain.voice_wake_word)
            else:
                resp = brain.handle(req)
        except Exception as exc:
            resp = {"ok": False, "mood": "thinking", "text": f"Brain error: {exc}"}
        emit(resp, req_id)

    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception as exc:
            emit({"ok": False, "mood": "thinking", "text": f"Bad JSON: {exc}"}, None)
            continue
        if req.get("cmd") == "quit":
            voice_listener.stop()
            emit({"ok": True, "quit": True}, req.get("id"))
            break
        pool.submit(process_line, req)

    voice_listener.stop()
    pool.shutdown(wait=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
