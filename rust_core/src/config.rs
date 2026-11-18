use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

/// Canonicalices character IDs across the 4 DDLC Dokis.
pub fn normalize_character_id(raw: &str) -> &'static str {
    match raw.trim().to_ascii_lowercase().as_str() {
        "monika" => "monika",
        "sayori" => "sayori",
        "yuri" => "yuri",
        _ => "natsuki",
    }
}

fn default_true() -> bool {
    true
}

fn default_false() -> bool {
    false
}

fn default_ui_theme() -> String {
    "auto".to_string()
}

pub fn normalize_ui_theme(raw: &str) -> &'static str {
    match raw.trim().to_ascii_lowercase().as_str() {
        "mono_dark" | "monochrome_dark" | "dark" => "mono_dark",
        "mono_light" | "monochrome_light" | "light" => "mono_light",
        _ => "auto",
    }
}

fn default_auto_switch_sec() -> u32 {
    120
}

fn default_screen_watch_sec() -> u32 {
    30
}

fn default_weapon_id() -> String {
    "auto".to_string()
}

pub fn normalize_weapon_id(raw: &str) -> &'static str {
    match raw.trim().to_ascii_lowercase().as_str() {
        "none" => "none",
        "shovel" => "shovel",
        "shotgun" => "shotgun",
        "pistol" => "pistol",
        "knife" => "knife",
        "bat" => "bat",
        _ => "auto",
    }
}

/// Persistent configuration stored at ~/.config/natsuki-shimeji/config.json.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct AppConfig {
    pub character_id: String,
    pub side: String,
    pub user_name: String,
    pub gemini_api_key: String,
    pub gemini_model: String,
    pub language: String,
    pub dialogue_interval_sec: u32,
    pub auto_dialogue_enabled: bool,
    pub style_id: String,
    pub character_styles: HashMap<String, String>,
    pub sprite_height_px: i32,
    pub sprite_opacity_pct: u32,
    pub bubble_duration_sec: u32,
    pub always_on_top: bool,
    pub sound_reactive: bool,
    pub audio_source: String,
    pub audio_sensitivity: f64,
    #[serde(default = "default_true")]
    pub companion_random: bool,
    #[serde(default = "default_auto_switch_sec")]
    pub auto_switch_sec: u32,
    #[serde(default = "default_true")]
    pub roam_enabled: bool,
    #[serde(default = "default_true")]
    pub auto_antics: bool,
    #[serde(default = "default_weapon_id")]
    pub weapon_id: String,
    #[serde(default = "default_true")]
    pub screen_watch_enabled: bool,
    #[serde(default = "default_screen_watch_sec")]
    pub screen_watch_sec: u32,
    #[serde(default = "default_true")]
    pub voice_commands_enabled: bool,
    #[serde(default = "default_false")]
    pub voice_wake_word: bool,
    #[serde(default = "default_ui_theme")]
    pub ui_theme: String,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            character_id: "natsuki".to_string(),
            side: "left".to_string(),
            user_name: String::new(),
            gemini_api_key: String::new(),
            gemini_model: "gemini-3.8-flash".to_string(),
            language: "en".to_string(),
            dialogue_interval_sec: 45,
            auto_dialogue_enabled: true,
            style_id: "salvation_uniform".to_string(),
            character_styles: HashMap::new(),
            sprite_height_px: 300,
            sprite_opacity_pct: 100,
            bubble_duration_sec: 14,
            always_on_top: true,
            sound_reactive: false,
            audio_source: "monitor".to_string(),
            audio_sensitivity: 1.35,
            companion_random: true,
            auto_switch_sec: 120,
            roam_enabled: true,
            auto_antics: true,
            weapon_id: "auto".to_string(),
            screen_watch_enabled: true,
            screen_watch_sec: 30,
            voice_commands_enabled: true,
            voice_wake_word: false,
            ui_theme: "auto".to_string(),
        }
    }
}

impl AppConfig {
    /// Clamps numeric fields and sanitizes strings (no emojis allowed).
    pub fn sanitize(&mut self) {
        self.character_id = normalize_character_id(&self.character_id).to_string();
        self.side = if self.side.trim().eq_ignore_ascii_case("right") {
            "right".to_string()
        } else {
            "left".to_string()
        };
        let clean_name = strip_emojis(self.user_name.trim());
        self.user_name = clean_name.chars().take(36).collect();
        self.gemini_api_key = self.gemini_api_key.trim().to_string();
        self.gemini_model = strip_emojis(self.gemini_model.trim());
        if self.gemini_model.is_empty() {
            self.gemini_model = "gemini-3.8-flash".to_string();
        }
        self.language = if self.language.eq_ignore_ascii_case("es") {
            "es".to_string()
        } else {
            "en".to_string()
        };
        self.dialogue_interval_sec = self.dialogue_interval_sec.clamp(10, 3600);
        self.sprite_height_px = self.sprite_height_px.clamp(140, 520);
        self.sprite_opacity_pct = self.sprite_opacity_pct.clamp(15, 100);
        self.bubble_duration_sec = self.bubble_duration_sec.clamp(4, 60);
        self.audio_source = if self.audio_source.trim().eq_ignore_ascii_case("mic") {
            "mic".to_string()
        } else {
            "monitor".to_string()
        };
        if !self.audio_sensitivity.is_finite() {
            self.audio_sensitivity = 1.35;
        }
        self.audio_sensitivity = self.audio_sensitivity.clamp(0.4, 3.5);
        self.auto_switch_sec = self.auto_switch_sec.clamp(0, 1800);
        self.screen_watch_sec = self.screen_watch_sec.clamp(5, 600);
        self.weapon_id = normalize_weapon_id(&self.weapon_id).to_string();
        self.style_id = strip_emojis(self.style_id.trim());
        if self.style_id.is_empty() {
            self.style_id = "salvation_uniform".to_string();
        }
        self.ui_theme = normalize_ui_theme(&self.ui_theme).to_string();
    }

    pub fn load_or_default(path: &Path) -> Self {
        match fs::read_to_string(path) {
            Ok(raw) => {
                let mut cfg: AppConfig = serde_json::from_str(&raw).unwrap_or_default();
                cfg.sanitize();
                cfg
            }
            Err(_) => {
                let mut cfg = AppConfig::default();
                cfg.sanitize();
                let _ = cfg.save_atomic(path);
                cfg
            }
        }
    }

    pub fn save_atomic(&self, path: &Path) -> Result<(), String> {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).map_err(|e| format!("create_dir_all failed: {e}"))?;
        }
        let json = serde_json::to_string_pretty(self)
            .map_err(|e| format!("serialize config failed: {e}"))?;
        let tmp = PathBuf::from(format!("{}.tmp", path.display()));
        fs::write(&tmp, json.as_bytes()).map_err(|e| format!("write temp failed: {e}"))?;
        fs::rename(&tmp, path).map_err(|e| format!("rename config failed: {e}"))?;
        Ok(())
    }
}

/// Resolves default config path (~/.config/natsuki-shimeji/config.json).
pub fn default_config_path() -> PathBuf {
    if let Ok(xdg) = std::env::var("XDG_CONFIG_HOME") {
        if !xdg.is_empty() {
            return PathBuf::from(xdg).join("natsuki-shimeji/config.json");
        }
    }
    if let Ok(home) = std::env::var("HOME") {
        return PathBuf::from(home).join(".config/natsuki-shimeji/config.json");
    }
    PathBuf::from("/tmp/natsuki-shimeji-config.json")
}

/// Checks whether a Unicode scalar value belongs to emoji / pictograph / dingbat blocks.
pub fn is_emoji_char(ch: char) -> bool {
    let cp = ch as u32;
    matches!(
        cp,
        0x200D
        | 0x20E3
        | 0xFE0E..=0xFE0F
        | 0x2194..=0x21AA
        | 0x231A..=0x231B
        | 0x23E9..=0x23FA
        | 0x24C2
        | 0x25AA..=0x25FE
        | 0x2600..=0x27BF
        | 0x2934..=0x2935
        | 0x2B05..=0x2B55
        | 0x3030
        | 0x303D
        | 0x3297..=0x3299
        | 0x1F000..=0x1FAFF
        | 0xE0020..=0xE007F
    )
}

/// Removes all emoji/pictograph characters while normalizing accidental multiple spaces.
pub fn strip_emojis(input: &str) -> String {
    let mut out = String::with_capacity(input.len());
    let mut prev_space = false;
    for ch in input.chars() {
        if is_emoji_char(ch) {
            continue;
        }
        if ch == ' ' {
            if !prev_space {
                out.push(' ');
            }
            prev_space = true;
        } else {
            out.push(ch);
            prev_space = false;
        }
    }
    out.trim().to_string()
}
