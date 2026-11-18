pub mod catalog;
pub mod compositor;
pub mod config;
pub mod source3d;

use catalog::{
    build_characters, build_moods, build_recipe, discover_styles, normalize_mood, CharacterEntry,
    MoodEntry, StyleEntry, StyleKind,
};
use compositor::{RenderedFrame, RgbaImage};
use config::{default_config_path, normalize_character_id, strip_emojis, AppConfig};
use source3d::{render_l4d2_doki, Source3dStore};
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::path::{Path, PathBuf};
use std::sync::Arc;

#[repr(C)]
pub struct NatsukiFrame {
    pub data: *const u8,
    pub width: i32,
    pub height: i32,
    pub stride: i32,
    pub handle: *mut NatsukiFrameHandle,
}

pub struct NatsukiFrameHandle {
    _frame: Arc<RenderedFrame>,
}

pub struct NatsukiEngine {
    sprites_root: PathBuf,
    config_path: PathBuf,
    config: AppConfig,
    characters: Vec<CharacterEntry>,
    styles: Vec<StyleEntry>,
    moods: Vec<MoodEntry>,
    png_cache: HashMap<PathBuf, Arc<RgbaImage>>,
    frame_cache: HashMap<String, Arc<RenderedFrame>>,
    source3d: Source3dStore,
}

impl NatsukiEngine {
    pub fn new(sprites_root: PathBuf, config_path: PathBuf) -> Self {
        let mut config = AppConfig::load_or_default(&config_path);
        let characters = build_characters();
        let styles = discover_styles(&sprites_root);
        let moods = build_moods();

        // When upgrading from an older config or first boot with L4D2 3D models installed,
        // promote each Doki's L4D2 3D style as default if character_styles is still empty.
        let active_char = normalize_character_id(&config.character_id);
        let upgrading_to_3d = config.character_styles.is_empty();
        if upgrading_to_3d {
            for cid in ["natsuki", "monika", "sayori", "yuri"] {
                if let Some(s3d) = styles
                    .iter()
                    .find(|s| s.character_id == cid && matches!(s.kind, StyleKind::L4d2ThreeD))
                {
                    config.character_styles.insert(cid.to_string(), s3d.id.to_string());
                    if cid == active_char {
                        config.style_id = s3d.id.to_string();
                    }
                }
            }
        }

        // Ensure active style matches active character if available
        let matches_char = styles
            .iter()
            .any(|s| s.character_id == active_char && s.id == config.style_id);
        if !matches_char {
            if let Some(first_for_char) = styles.iter().find(|s| s.character_id == active_char) {
                config.style_id = first_for_char.id.to_string();
            } else if let Some(first_any) = styles.first() {
                config.style_id = first_any.id.to_string();
            }
        }

        Self {
            sprites_root,
            config_path,
            config,
            characters,
            styles,
            moods,
            png_cache: HashMap::new(),
            frame_cache: HashMap::new(),
            source3d: Source3dStore::new(),
        }
    }

    fn styles_for<'a>(&'a self, character_id: &str) -> Vec<&'a StyleEntry> {
        let cid = normalize_character_id(character_id);
        self.styles.iter().filter(|s| s.character_id == cid).collect()
    }

    fn load_layer_cached(&mut self, path: &Path) -> Option<Arc<RgbaImage>> {
        if let Some(hit) = self.png_cache.get(path) {
            return Some(Arc::clone(hit));
        }
        let img = RgbaImage::load_png(path).ok()?;
        if self.png_cache.len() > 380 {
            self.png_cache.clear();
        }
        let arc = Arc::new(img);
        self.png_cache.insert(path.to_path_buf(), Arc::clone(&arc));
        Some(arc)
    }

    pub fn render_frame(
        &mut self,
        character_id: Option<&str>,
        style_id: &str,
        mood: &str,
        blink: bool,
        variant: u32,
        target_height: i32,
    ) -> Option<Arc<RenderedFrame>> {
        self.render_frame_ex(character_id, style_id, mood, blink, variant, target_height, 0.0, 0.0)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn render_frame_ex(
        &mut self,
        character_id: Option<&str>,
        style_id: &str,
        mood: &str,
        blink: bool,
        variant: u32,
        target_height: i32,
        yaw_deg: f32,
        pitch_deg: f32,
    ) -> Option<Arc<RenderedFrame>> {
        if self.styles.is_empty() {
            return None;
        }
        let target_char = character_id
            .filter(|s| !s.is_empty())
            .map(normalize_character_id);

        let style = if let Some(cid) = target_char {
            self.styles
                .iter()
                .find(|s| s.character_id == cid && s.id == style_id)
                .or_else(|| self.styles.iter().find(|s| s.character_id == cid))
                .unwrap_or(&self.styles[0])
                .clone()
        } else {
            self.styles
                .iter()
                .find(|s| s.id == style_id)
                .unwrap_or(&self.styles[0])
                .clone()
        };

        let raw_mood_lower = mood.trim().to_ascii_lowercase();
        let norm_mood = if matches!(style.kind, StyleKind::L4d2ThreeD) && raw_mood_lower == "fly" {
            "fly"
        } else {
            normalize_mood(mood)
        };
        let th = target_height.clamp(48, 640);
        let qyaw = (yaw_deg * 2.0).round() * 0.5;
        let qpitch = (pitch_deg * 2.0).round() * 0.5;
        let key = if matches!(style.kind, StyleKind::L4d2ThreeD) {
            format!(
                "3d:{}:{}:{}:{}:{}:{:.1}:{:.1}",
                style.character_id,
                norm_mood,
                blink as u8,
                variant % 6,
                th,
                qyaw,
                qpitch
            )
        } else {
            format!(
                "{}:{}:{}:{}:{}:{}",
                style.character_id,
                style.id,
                norm_mood,
                blink as u8,
                variant % 6,
                th
            )
        };

        if let Some(cached) = self.frame_cache.get(&key) {
            return Some(Arc::clone(cached));
        }

        let frame = if matches!(style.kind, StyleKind::L4d2ThreeD) {
            let model = self.source3d.get_model(style.character_id)?;
            let rendered = render_l4d2_doki(&model, style.character_id, norm_mood, blink, variant, th, qyaw, qpitch).ok()?;
            Arc::new(rendered)
        } else {
            let recipe = build_recipe(&self.sprites_root, &style, norm_mood, blink, variant);
            let mut canvas = RgbaImage::new(recipe.canvas_w, recipe.canvas_h);
            let mut loaded_any = false;

            for layer_path in &recipe.layers {
                if !layer_path.exists() {
                    continue;
                }
                if let Some(layer) = self.load_layer_cached(layer_path) {
                    canvas.composite_over(&layer);
                    loaded_any = true;
                }
            }
            if !loaded_any {
                return None;
            }
            Arc::new(canvas.crop_resize_to_cairo_argb32(recipe.crop, th))
        };

        if self.frame_cache.len() > 220 {
            self.frame_cache.clear();
        }
        self.frame_cache.insert(key, Arc::clone(&frame));
        Some(frame)
    }
}

fn cstr_to_string(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    unsafe { CStr::from_ptr(ptr) }
        .to_str()
        .ok()
        .map(|s| s.to_string())
}

fn resolve_sprites_root(arg: Option<String>) -> PathBuf {
    if let Some(s) = arg.filter(|s| !s.is_empty()) {
        return PathBuf::from(s);
    }
    if let Ok(env) = std::env::var("DDLC_SHIMEJI_SPRITES") {
        if !env.is_empty() {
            return PathBuf::from(env);
        }
    }
    if let Ok(home_dir) = std::env::var("NATSUKI_SHIMEJI_HOME") {
        let p = PathBuf::from(home_dir).join("chikinenas");
        if p.is_dir() {
            return p;
        }
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(parent) = exe.parent() {
            for rel in ["chikinenas", "../chikinenas", "../../chikinenas", "../../../chikinenas"] {
                let p = parent.join(rel);
                if p.is_dir() {
                    return p;
                }
            }
        }
    }
    for rel in ["chikinenas", "../chikinenas"] {
        let p = PathBuf::from(rel);
        if p.is_dir() {
            return p;
        }
    }
    if let Ok(home) = std::env::var("HOME") {
        let p = PathBuf::from(home).join("Desktop/chikinenas");
        if p.is_dir() {
            return p;
        }
    }
    let chik = PathBuf::from("/home/foidslayer/Desktop/chikinenas");
    if chik.is_dir() {
        return chik;
    }
    PathBuf::from("chikinenas")
}

#[no_mangle]
pub extern "C" fn natsuki_engine_new(
    sprites_root: *const c_char,
    config_path: *const c_char,
) -> *mut NatsukiEngine {
    let root = resolve_sprites_root(cstr_to_string(sprites_root));
    let cfg = cstr_to_string(config_path)
        .filter(|s| !s.is_empty())
        .map(PathBuf::from)
        .unwrap_or_else(default_config_path);
    Box::into_raw(Box::new(NatsukiEngine::new(root, cfg)))
}

#[no_mangle]
pub extern "C" fn natsuki_engine_free(engine: *mut NatsukiEngine) {
    if !engine.is_null() {
        unsafe { drop(Box::from_raw(engine)) };
    }
}

#[no_mangle]
pub extern "C" fn natsuki_engine_character_count(engine: *const NatsukiEngine) -> i32 {
    if engine.is_null() {
        return 0;
    }
    unsafe { &*engine }.characters.len() as i32
}

#[no_mangle]
pub extern "C" fn natsuki_engine_character_id(engine: *const NatsukiEngine, index: i32) -> *const c_char {
    if engine.is_null() || index < 0 {
        return std::ptr::null();
    }
    unsafe { &*engine }
        .characters
        .get(index as usize)
        .map(|c| c.id_c.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn natsuki_engine_character_label(engine: *const NatsukiEngine, index: i32) -> *const c_char {
    if engine.is_null() || index < 0 {
        return std::ptr::null();
    }
    unsafe { &*engine }
        .characters
        .get(index as usize)
        .map(|c| c.label_c.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn natsuki_engine_style_count(engine: *const NatsukiEngine) -> i32 {
    if engine.is_null() {
        return 0;
    }
    let eng = unsafe { &*engine };
    eng.styles_for(&eng.config.character_id).len() as i32
}

#[no_mangle]
pub extern "C" fn natsuki_engine_style_id(engine: *const NatsukiEngine, index: i32) -> *const c_char {
    if engine.is_null() || index < 0 {
        return std::ptr::null();
    }
    let eng = unsafe { &*engine };
    eng.styles_for(&eng.config.character_id)
        .get(index as usize)
        .map(|s| s.id_c.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn natsuki_engine_style_label(engine: *const NatsukiEngine, index: i32) -> *const c_char {
    if engine.is_null() || index < 0 {
        return std::ptr::null();
    }
    let eng = unsafe { &*engine };
    eng.styles_for(&eng.config.character_id)
        .get(index as usize)
        .map(|s| s.label_c.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn natsuki_engine_style_count_for(
    engine: *const NatsukiEngine,
    character_id: *const c_char,
) -> i32 {
    if engine.is_null() {
        return 0;
    }
    let eng = unsafe { &*engine };
    let cid = cstr_to_string(character_id).unwrap_or_else(|| eng.config.character_id.clone());
    eng.styles_for(&cid).len() as i32
}

#[no_mangle]
pub extern "C" fn natsuki_engine_style_id_for(
    engine: *const NatsukiEngine,
    character_id: *const c_char,
    index: i32,
) -> *const c_char {
    if engine.is_null() || index < 0 {
        return std::ptr::null();
    }
    let eng = unsafe { &*engine };
    let cid = cstr_to_string(character_id).unwrap_or_else(|| eng.config.character_id.clone());
    eng.styles_for(&cid)
        .get(index as usize)
        .map(|s| s.id_c.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn natsuki_engine_style_label_for(
    engine: *const NatsukiEngine,
    character_id: *const c_char,
    index: i32,
) -> *const c_char {
    if engine.is_null() || index < 0 {
        return std::ptr::null();
    }
    let eng = unsafe { &*engine };
    let cid = cstr_to_string(character_id).unwrap_or_else(|| eng.config.character_id.clone());
    eng.styles_for(&cid)
        .get(index as usize)
        .map(|s| s.label_c.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn natsuki_engine_mood_count(engine: *const NatsukiEngine) -> i32 {
    if engine.is_null() {
        return 0;
    }
    let eng = unsafe { &*engine };
    eng.moods.len() as i32
}

#[no_mangle]
pub extern "C" fn natsuki_engine_mood_id(engine: *const NatsukiEngine, index: i32) -> *const c_char {
    if engine.is_null() || index < 0 {
        return std::ptr::null();
    }
    let eng = unsafe { &*engine };
    eng.moods
        .get(index as usize)
        .map(|m| m.id.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn natsuki_engine_mood_label(engine: *const NatsukiEngine, index: i32) -> *const c_char {
    if engine.is_null() || index < 0 {
        return std::ptr::null();
    }
    let eng = unsafe { &*engine };
    eng.moods
        .get(index as usize)
        .map(|m| m.label.as_ptr())
        .unwrap_or(std::ptr::null())
}

#[repr(C)]
pub struct NatsukiPoseParams {
    pub walk_phase: f32,
    pub walk_speed: f32,
    pub hair_lag_x: f32,
    pub hair_lag_y: f32,
    pub hair_bounce_z: f32,
    pub skirt_lag_x: f32,
    pub skirt_lag_y: f32,
    pub skirt_flare: f32,
    pub spin_vel: f32,
    pub sound_level: f32,
    pub action_id: i32,
    pub action_t: f32,
    pub weapon_id: *const c_char,
}

fn render_into_out(
    engine: *mut NatsukiEngine,
    character_id: Option<String>,
    style_id: *const c_char,
    mood_id: *const c_char,
    blink: u8,
    variant: u32,
    target_height: i32,
    yaw_deg: f32,
    pitch_deg: f32,
    pose_opt: Option<&source3d::PoseParams3D>,
    out_frame: *mut NatsukiFrame,
) -> i32 {
    if engine.is_null() || out_frame.is_null() {
        return -1;
    }
    let eng = unsafe { &mut *engine };
    let sid = cstr_to_string(style_id).unwrap_or_else(|| eng.config.style_id.clone());
    let mid = cstr_to_string(mood_id).unwrap_or_else(|| "neutral".to_string());
    let th = if target_height > 0 {
        target_height
    } else {
        eng.config.sprite_height_px
    };

    let cid_owned = character_id
        .as_deref()
        .map(normalize_character_id)
        .unwrap_or_else(|| normalize_character_id(&eng.config.character_id))
        .to_string();

    let style_is_3d = eng
        .styles
        .iter()
        .find(|s| s.id == sid && s.character_id == cid_owned)
        .or_else(|| eng.styles.iter().find(|s| s.id == sid))
        .map(|s| matches!(s.kind, StyleKind::L4d2ThreeD))
        .unwrap_or(false);

    let frame_opt = if let Some(pose) = pose_opt {
        if style_is_3d {
            let model = eng.source3d.get_model(&cid_owned);
            model.and_then(|m| {
                source3d::render_l4d2_doki_with_pose(
                    &m, &cid_owned, &mid, blink != 0, variant, th, yaw_deg, pitch_deg, pose,
                )
                .ok()
                .map(Arc::new)
            })
        } else {
            eng.render_frame_ex(
                Some(&cid_owned),
                &sid,
                &mid,
                blink != 0,
                variant,
                th,
                yaw_deg,
                pitch_deg,
            )
        }
    } else {
        eng.render_frame_ex(
            Some(&cid_owned),
            &sid,
            &mid,
            blink != 0,
            variant,
            th,
            yaw_deg,
            pitch_deg,
        )
    };

    let Some(frame_arc) = frame_opt else {
        return -2;
    };

    let data_ptr = frame_arc.data.as_ptr();
    let width = frame_arc.width;
    let height = frame_arc.height;
    let stride = frame_arc.stride;
    let handle = Box::into_raw(Box::new(NatsukiFrameHandle { _frame: frame_arc }));

    unsafe {
        (*out_frame).data = data_ptr;
        (*out_frame).width = width;
        (*out_frame).height = height;
        (*out_frame).stride = stride;
        (*out_frame).handle = handle;
    }
    0
}

#[no_mangle]
pub extern "C" fn natsuki_engine_render_frame(
    engine: *mut NatsukiEngine,
    style_id: *const c_char,
    mood_id: *const c_char,
    blink: u8,
    variant: u32,
    target_height: i32,
    out_frame: *mut NatsukiFrame,
) -> i32 {
    render_into_out(engine, None, style_id, mood_id, blink, variant, target_height, 0.0, 0.0, None, out_frame)
}

#[no_mangle]
pub extern "C" fn natsuki_engine_render_character_frame(
    engine: *mut NatsukiEngine,
    character_id: *const c_char,
    style_id: *const c_char,
    mood_id: *const c_char,
    blink: u8,
    variant: u32,
    target_height: i32,
    out_frame: *mut NatsukiFrame,
) -> i32 {
    render_into_out(
        engine,
        cstr_to_string(character_id),
        style_id,
        mood_id,
        blink,
        variant,
        target_height,
        0.0,
        0.0,
        None,
        out_frame,
    )
}

#[no_mangle]
pub extern "C" fn natsuki_engine_render_character_frame_ex(
    engine: *mut NatsukiEngine,
    character_id: *const c_char,
    style_id: *const c_char,
    mood_id: *const c_char,
    blink: u8,
    variant: u32,
    target_height: i32,
    yaw_deg: f32,
    pitch_deg: f32,
    out_frame: *mut NatsukiFrame,
) -> i32 {
    render_into_out(
        engine,
        cstr_to_string(character_id),
        style_id,
        mood_id,
        blink,
        variant,
        target_height,
        yaw_deg,
        pitch_deg,
        None,
        out_frame,
    )
}

#[no_mangle]
pub extern "C" fn natsuki_engine_render_character_frame_v2(
    engine: *mut NatsukiEngine,
    character_id: *const c_char,
    style_id: *const c_char,
    mood_id: *const c_char,
    blink: u8,
    variant: u32,
    target_height: i32,
    yaw_deg: f32,
    pitch_deg: f32,
    pose: *const NatsukiPoseParams,
    out_frame: *mut NatsukiFrame,
) -> i32 {
    let pose_rust = if !pose.is_null() {
        let p = unsafe { &*pose };
        source3d::PoseParams3D {
            walk_phase: p.walk_phase,
            walk_speed: p.walk_speed,
            hair_lag_x: p.hair_lag_x,
            hair_lag_y: p.hair_lag_y,
            hair_bounce_z: p.hair_bounce_z,
            skirt_lag_x: p.skirt_lag_x,
            skirt_lag_y: p.skirt_lag_y,
            skirt_flare: p.skirt_flare,
            spin_vel: p.spin_vel,
            sound_level: p.sound_level,
            action_id: p.action_id,
            action_t: p.action_t,
            weapon_id: cstr_to_string(p.weapon_id).unwrap_or_else(|| "none".to_string()),
            keep_center_anchor: true,
        }
    } else {
        source3d::PoseParams3D::default()
    };
    render_into_out(
        engine,
        cstr_to_string(character_id),
        style_id,
        mood_id,
        blink,
        variant,
        target_height,
        yaw_deg,
        pitch_deg,
        Some(&pose_rust),
        out_frame,
    )
}

const WEAPON_IDS_C: &[&[u8]] = &[
    b"auto\0",
    b"none\0",
    b"shovel\0",
    b"shotgun\0",
    b"pistol\0",
    b"knife\0",
    b"bat\0",
];

const WEAPON_LABELS_C: &[&[u8]] = &[
    b"Arma aleatoria / Caracteristica\0",
    b"Sin arma (Manos libres)\0",
    b"Pala (Shovel)\0",
    b"Escopeta (Shotgun)\0",
    b"Pistola (Deagle)\0",
    b"Cuchillo (Knife)\0",
    b"Bate Kawaii (Bat)\0",
];

#[no_mangle]
pub extern "C" fn natsuki_engine_weapon_count() -> i32 {
    WEAPON_IDS_C.len() as i32
}

#[no_mangle]
pub extern "C" fn natsuki_engine_weapon_id(index: i32) -> *const c_char {
    WEAPON_IDS_C
        .get(index as usize)
        .map(|s| s.as_ptr() as *const c_char)
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn natsuki_engine_weapon_label(index: i32) -> *const c_char {
    WEAPON_LABELS_C
        .get(index as usize)
        .map(|s| s.as_ptr() as *const c_char)
        .unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn natsuki_engine_style_is_3d(engine: *const NatsukiEngine, style_id: *const c_char) -> i32 {
    if engine.is_null() {
        return 0;
    }
    let Some(sid) = cstr_to_string(style_id) else {
        return 0;
    };
    let eng = unsafe { &*engine };
    eng.styles
        .iter()
        .find(|s| s.id == sid)
        .map(|s| if matches!(s.kind, StyleKind::L4d2ThreeD) { 1 } else { 0 })
        .unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn natsuki_frame_free(handle: *mut NatsukiFrameHandle) {
    if !handle.is_null() {
        unsafe { drop(Box::from_raw(handle)) };
    }
}

#[no_mangle]
pub extern "C" fn natsuki_config_get_json(engine: *const NatsukiEngine) -> *mut c_char {
    if engine.is_null() {
        return std::ptr::null_mut();
    }
    let eng = unsafe { &*engine };
    let Ok(json) = serde_json::to_string(&eng.config) else {
        return std::ptr::null_mut();
    };
    CString::new(json)
        .map(|c| c.into_raw())
        .unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub extern "C" fn natsuki_config_update_json(
    engine: *mut NatsukiEngine,
    json_str: *const c_char,
) -> i32 {
    if engine.is_null() || json_str.is_null() {
        return -1;
    }
    let eng = unsafe { &mut *engine };
    let Some(raw) = cstr_to_string(json_str) else {
        return -2;
    };
    let Ok(mut next_cfg) = serde_json::from_str::<AppConfig>(&raw) else {
        return -3;
    };
    next_cfg.sanitize();
    if let Err(e) = next_cfg.save_atomic(&eng.config_path) {
        eprintln!("[natsuki_core] save config failed: {e}");
        return -4;
    }
    eng.config = next_cfg;
    0
}

#[no_mangle]
pub extern "C" fn natsuki_strip_emojis(text: *const c_char) -> *mut c_char {
    let Some(raw) = cstr_to_string(text) else {
        return std::ptr::null_mut();
    };
    CString::new(strip_emojis(&raw))
        .map(|c| c.into_raw())
        .unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub extern "C" fn natsuki_string_free(ptr: *mut c_char) {
    if !ptr.is_null() {
        unsafe { drop(CString::from_raw(ptr)) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_strip_emojis_keeps_spanish_removes_pictographs() {
        let out = strip_emojis("Hmph! Baka ❤️🔥😊 ¡Cupcake rico!");
        assert_eq!(out, "Hmph! Baka ¡Cupcake rico!");
    }

    #[test]
    fn test_config_roundtrip() {
        let tmp = std::env::temp_dir().join("natsuki_shimeji_unit_cfg.json");
        let _ = std::fs::remove_file(&tmp);
        let mut cfg = AppConfig::load_or_default(&tmp);
        assert_eq!(cfg.language, "en");
        assert_eq!(cfg.character_id, "natsuki");
        assert_eq!(cfg.side, "left");
        cfg.gemini_api_key = " test_key ".into();
        cfg.character_id = "monika".into();
        cfg.side = "right".into();
        cfg.user_name = "Batman 🔥".into();
        cfg.sprite_height_px = 999;
        cfg.screen_watch_sec = 2;
        cfg.sanitize();
        assert_eq!(cfg.gemini_api_key, "test_key");
        assert_eq!(cfg.sprite_height_px, 520);
        assert_eq!(cfg.user_name, "Batman");
        assert_eq!(cfg.screen_watch_sec, 5);
        assert!(cfg.screen_watch_enabled);
        cfg.save_atomic(&tmp).unwrap();
        let reloaded = AppConfig::load_or_default(&tmp);
        assert_eq!(reloaded.gemini_api_key, "test_key");
        assert_eq!(reloaded.character_id, "monika");
        assert_eq!(reloaded.side, "right");
        assert_eq!(reloaded.user_name, "Batman");
        assert_eq!(reloaded.screen_watch_sec, 5);
        let _ = std::fs::remove_file(&tmp);
    }

    #[test]
    fn test_discover_and_render_all_dokis() {
        let root = resolve_sprites_root(None);
        if !root.exists() {
            return;
        }
        let tmp = std::env::temp_dir().join("natsuki_shimeji_render_cfg.json");
        let mut eng = NatsukiEngine::new(root, tmp.clone());
        assert_eq!(eng.characters.len(), 4);
        for cid in ["natsuki", "monika", "sayori", "yuri"] {
            let styles: Vec<_> = eng.styles_for(cid).into_iter().cloned().collect();
            assert!(!styles.is_empty(), "expected styles for {cid}");
            for s in styles {
                let frame = eng
                    .render_frame(Some(cid), s.id, "happy", false, 0, 240)
                    .unwrap_or_else(|| panic!("failed rendering {cid}:{}", s.id));
                assert!(frame.width >= 48 && frame.height == 240);
                assert!(frame.data.iter().skip(3).step_by(4).any(|&a| a > 0));
            }
        }
        let _ = std::fs::remove_file(&tmp);
    }

    #[test]
    fn test_export_l4d2_3d_preview_contact_sheet() {
        let root = resolve_sprites_root(None);
        let tmp = std::env::temp_dir().join("natsuki_shimeji_3d_cfg.json");
        let _ = std::fs::remove_file(&tmp);
        let mut eng = NatsukiEngine::new(root, tmp.clone());
        let dokis = [
            ("natsuki", "natsuki_l4d2_3d", -14.0f32),
            ("monika", "monika_l4d2_3d", 0.0f32),
            ("sayori", "sayori_l4d2_3d", 14.0f32),
            ("yuri", "yuri_l4d2_3d", -18.0f32),
        ];
        let mut frames = Vec::new();
        for (cid, sid, yaw) in dokis {
            if !source3d::has_character_vpk(cid) {
                return;
            }
            let frame = eng
                .render_frame_ex(Some(cid), sid, "happy", false, 0, 320, yaw, 0.0)
                .unwrap_or_else(|| panic!("failed 3d render for {cid}"));
            frames.push(frame);
        }
        let total_w: usize = frames.iter().map(|f| f.width as usize).sum();
        let h = 320usize;
        let mut out_rgba = vec![0u8; total_w * h * 4];
        let mut x_off = 0usize;
        for f in &frames {
            let fw = f.width as usize;
            for y in 0..h {
                for x in 0..fw {
                    let off = (y * fw + x) * 4;
                    let b = f.data[off];
                    let g = f.data[off + 1];
                    let r = f.data[off + 2];
                    let a = f.data[off + 3];
                    // unpremultiply on light checkerboard so transparent edges show clearly
                    let bg = if ((x / 12 + y / 12) % 2) == 0 { 235u16 } else { 205u16 };
                    let inv = 255u16 - a as u16;
                    let rr = (r as u16 + (bg * inv) / 255).min(255) as u8;
                    let gg = (g as u16 + (bg * inv) / 255).min(255) as u8;
                    let bb = (b as u16 + (bg * inv) / 255).min(255) as u8;
                    let doff = (y * total_w + (x_off + x)) * 4;
                    out_rgba[doff] = rr;
                    out_rgba[doff + 1] = gg;
                    out_rgba[doff + 2] = bb;
                    out_rgba[doff + 3] = 255;
                }
            }
            x_off += fw;
        }
        let out_path = PathBuf::from("/home/foidslayer/.gemini/antigravity/brain/60a250bd-79dc-411f-ade5-fffc381bc719/scratch/l4d2_3d_preview.png");
        let _ = std::fs::create_dir_all(out_path.parent().unwrap());
        let file = std::fs::File::create(&out_path).unwrap();
        let mut enc = png::Encoder::new(std::io::BufWriter::new(file), total_w as u32, h as u32);
        enc.set_color(png::ColorType::Rgba);
        enc.set_depth(png::BitDepth::Eight);
        let mut writer = enc.write_header().unwrap();
        writer.write_image_data(&out_rgba).unwrap();
        let _ = std::fs::remove_file(&tmp);
    }

    #[test]
    fn test_3d_walking_weapons_physics_and_antics() {
        let root = resolve_sprites_root(None);
        let tmp = std::env::temp_dir().join("natsuki_shimeji_3d_v2_cfg.json");
        let _ = std::fs::remove_file(&tmp);
        let mut eng = NatsukiEngine::new(root, tmp.clone());
        if !source3d::has_character_vpk("natsuki") {
            return;
        }
        let cases: &[(&str, f32, source3d::PoseParams3D)] = &[
            ("natsuki", -52.0, source3d::PoseParams3D {
                walk_phase: 1.4,
                walk_speed: 1.0,
                hair_lag_x: 0.9,
                hair_bounce_z: 0.4,
                skirt_lag_x: 0.7,
                skirt_flare: 0.45,
                weapon_id: "shovel".to_string(),
                keep_center_anchor: true,
                ..Default::default()
            }),
            ("monika", -32.0, source3d::PoseParams3D {
                action_id: 3,
                action_t: 0.50,
                hair_lag_y: 0.6,
                skirt_flare: 0.3,
                weapon_id: "shotgun".to_string(),
                keep_center_anchor: true,
                ..Default::default()
            }),
            ("sayori", 35.0, source3d::PoseParams3D {
                action_id: 3,
                action_t: 0.52,
                hair_lag_x: -0.7,
                weapon_id: "pistol".to_string(),
                keep_center_anchor: true,
                ..Default::default()
            }),
            ("yuri", -20.0, source3d::PoseParams3D {
                action_id: 5,
                action_t: 0.50,
                hair_lag_x: 0.8,
                skirt_flare: 0.5,
                weapon_id: "knife".to_string(),
                keep_center_anchor: true,
                ..Default::default()
            }),
            ("natsuki", -40.0, source3d::PoseParams3D {
                action_id: 2,
                action_t: 0.5,
                spin_vel: 2.4,
                skirt_flare: 0.9,
                hair_bounce_z: 0.7,
                weapon_id: "bat".to_string(),
                keep_center_anchor: true,
                ..Default::default()
            }),
            ("sayori", -28.0, source3d::PoseParams3D {
                action_id: 6,
                action_t: 0.45,
                skirt_flare: 0.5,
                keep_center_anchor: true,
                ..Default::default()
            }),
            ("monika", -45.0, source3d::PoseParams3D {
                action_id: 1,
                action_t: 0.5,
                hair_lag_y: -0.9,
                skirt_flare: 0.7,
                keep_center_anchor: true,
                ..Default::default()
            }),
        ];

        let mut frames = Vec::new();
        for (cid, yaw, pose) in cases {
            let model = eng.source3d.get_model(cid).expect("model");
            let frame = source3d::render_l4d2_doki_with_pose(
                &model, cid, "happy", false, 0, 260, *yaw, 0.0, pose,
            )
            .expect("v2 render");
            assert!(frame.width >= 32 && frame.height == 260);
            frames.push(frame);
        }

        let total_w: usize = frames.iter().map(|f| f.width as usize).sum();
        let h = 260usize;
        let mut out_rgba = vec![0u8; total_w * h * 4];
        let mut x_off = 0usize;
        for f in &frames {
            let fw = f.width as usize;
            for y in 0..h {
                for x in 0..fw {
                    let off = (y * fw + x) * 4;
                    let (b, g, r, a) = (f.data[off], f.data[off + 1], f.data[off + 2], f.data[off + 3]);
                    let bg = if ((x / 12 + y / 12) % 2) == 0 { 235u16 } else { 205u16 };
                    let inv = 255u16 - a as u16;
                    let doff = (y * total_w + (x_off + x)) * 4;
                    out_rgba[doff] = (r as u16 + (bg * inv) / 255).min(255) as u8;
                    out_rgba[doff + 1] = (g as u16 + (bg * inv) / 255).min(255) as u8;
                    out_rgba[doff + 2] = (b as u16 + (bg * inv) / 255).min(255) as u8;
                    out_rgba[doff + 3] = 255;
                }
            }
            x_off += fw;
        }
        let out_path = PathBuf::from("/home/foidslayer/.gemini/antigravity/brain/60a250bd-79dc-411f-ade5-fffc381bc719/scratch/l4d2_weapons_actions_preview.png");
        let _ = std::fs::create_dir_all(out_path.parent().unwrap());
        let file = std::fs::File::create(&out_path).unwrap();
        let mut enc = png::Encoder::new(std::io::BufWriter::new(file), total_w as u32, h as u32);
        enc.set_color(png::ColorType::Rgba);
        enc.set_depth(png::BitDepth::Eight);
        let mut writer = enc.write_header().unwrap();
        writer.write_image_data(&out_rgba).unwrap();
        let _ = std::fs::remove_file(&tmp);
    }
}
