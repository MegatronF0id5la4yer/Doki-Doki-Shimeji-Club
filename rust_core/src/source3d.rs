use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use crate::compositor::RenderedFrame;

#[derive(Clone, Copy, Debug)]
struct Mat34([[f32; 4]; 3]);

impl Mat34 {
    fn identity() -> Self {
        Self([
            [1.0, 0.0, 0.0, 0.0],
            [0.0, 1.0, 0.0, 0.0],
            [0.0, 0.0, 1.0, 0.0],
        ])
    }

    fn mul(&self, rhs: &Mat34) -> Mat34 {
        let mut out = [[0.0f32; 4]; 3];
        for r in 0..3 {
            for c in 0..3 {
                out[r][c] = self.0[r][0] * rhs.0[0][c]
                    + self.0[r][1] * rhs.0[1][c]
                    + self.0[r][2] * rhs.0[2][c];
            }
            out[r][3] = self.0[r][0] * rhs.0[0][3]
                + self.0[r][1] * rhs.0[1][3]
                + self.0[r][2] * rhs.0[2][3]
                + self.0[r][3];
        }
        Mat34(out)
    }

    fn from_quat_pos(q: [f32; 4], p: [f32; 3]) -> Mat34 {
        let (qx, qy, qz, qw) = (q[0], q[1], q[2], q[3]);
        let (xx, yy, zz) = (qx * qx, qy * qy, qz * qz);
        let (xy, xz, yz) = (qx * qy, qx * qz, qy * qz);
        let (wx, wy, wz) = (qw * qx, qw * qy, qw * qz);
        Mat34([
            [1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz), 2.0 * (xz + wy), p[0]],
            [2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx), p[1]],
            [2.0 * (xz - wy), 2.0 * (yz + wx), 1.0 - 2.0 * (xx + yy), p[2]],
        ])
    }

    fn from_axis_angle_around(axis: [f32; 3], angle_rad: f32, pivot: [f32; 3]) -> Mat34 {
        let len = (axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]).sqrt();
        if len < 1e-6 || angle_rad.abs() < 1e-6 {
            return Self::identity();
        }
        let (ux, uy, uz) = (axis[0] / len, axis[1] / len, axis[2] / len);
        let (s, c) = angle_rad.sin_cos();
        let t = 1.0 - c;
        let r = [
            [c + ux * ux * t, ux * uy * t - uz * s, ux * uz * t + uy * s],
            [uy * ux * t + uz * s, c + uy * uy * t, uy * uz * t - ux * s],
            [uz * ux * t - uy * s, uz * uy * t + ux * s, c + uz * uz * t],
        ];
        // T(+pivot) * R * T(-pivot)
        let tx = pivot[0] - (r[0][0] * pivot[0] + r[0][1] * pivot[1] + r[0][2] * pivot[2]);
        let ty = pivot[1] - (r[1][0] * pivot[0] + r[1][1] * pivot[1] + r[1][2] * pivot[2]);
        let tz = pivot[2] - (r[2][0] * pivot[0] + r[2][1] * pivot[1] + r[2][2] * pivot[2]);
        Mat34([
            [r[0][0], r[0][1], r[0][2], tx],
            [r[1][0], r[1][1], r[1][2], ty],
            [r[2][0], r[2][1], r[2][2], tz],
        ])
    }

    #[inline]
    fn transform_point(&self, p: [f32; 3]) -> [f32; 3] {
        [
            self.0[0][0] * p[0] + self.0[0][1] * p[1] + self.0[0][2] * p[2] + self.0[0][3],
            self.0[1][0] * p[0] + self.0[1][1] * p[1] + self.0[1][2] * p[2] + self.0[1][3],
            self.0[2][0] * p[0] + self.0[2][1] * p[1] + self.0[2][2] * p[2] + self.0[2][3],
        ]
    }

    #[inline]
    fn transform_vec(&self, v: [f32; 3]) -> [f32; 3] {
        [
            self.0[0][0] * v[0] + self.0[0][1] * v[1] + self.0[0][2] * v[2],
            self.0[1][0] * v[0] + self.0[1][1] * v[1] + self.0[1][2] * v[2],
            self.0[2][0] * v[0] + self.0[2][1] * v[1] + self.0[2][2] * v[2],
        ]
    }
}

#[inline]
fn r_u16(b: &[u8], off: usize) -> u16 {
    u16::from_le_bytes([b[off], b[off + 1]])
}
#[inline]
fn r_u32(b: &[u8], off: usize) -> u32 {
    u32::from_le_bytes([b[off], b[off + 1], b[off + 2], b[off + 3]])
}
#[inline]
fn r_i32(b: &[u8], off: usize) -> i32 {
    i32::from_le_bytes([b[off], b[off + 1], b[off + 2], b[off + 3]])
}
#[inline]
fn r_f32(b: &[u8], off: usize) -> f32 {
    f32::from_le_bytes([b[off], b[off + 1], b[off + 2], b[off + 3]])
}
fn read_cstr(b: &[u8], off: usize) -> (String, usize) {
    let mut end = off;
    while end < b.len() && b[end] != 0 {
        end += 1;
    }
    let s = String::from_utf8_lossy(&b[off..end]).into_owned();
    (s, (end + 1).min(b.len()))
}

struct Bone {
    parent: i32,
    fk_bind: Mat34,
}

#[derive(Clone, Copy)]
struct SkinnedVertex {
    pos: [f32; 3],
    norm: [f32; 3],
    uv: [f32; 2],
    weights: [f32; 3],
    bones: [u8; 3],
    num_bones: u8,
}

#[derive(Clone)]
struct Texture2D {
    width: usize,
    height: usize,
    rgba: Vec<[u8; 4]>,
}

impl Texture2D {
    #[inline]
    fn sample_bilinear(&self, u: f32, v: f32) -> [f32; 4] {
        let w = self.width as f32;
        let h = self.height as f32;
        let uu = u.rem_euclid(1.0) * w - 0.5;
        let vv = v.rem_euclid(1.0) * h - 0.5;
        let x0 = uu.floor() as isize;
        let y0 = vv.floor() as isize;
        let fx = uu - (x0 as f32);
        let fy = vv - (y0 as f32);
        let iw = self.width as isize;
        let ih = self.height as isize;
        let ix0 = x0.rem_euclid(iw) as usize;
        let ix1 = (x0 + 1).rem_euclid(iw) as usize;
        let iy0 = y0.rem_euclid(ih) as usize;
        let iy1 = (y0 + 1).rem_euclid(ih) as usize;

        let p00 = self.rgba[iy0 * self.width + ix0];
        let p10 = self.rgba[iy0 * self.width + ix1];
        let p01 = self.rgba[iy1 * self.width + ix0];
        let p11 = self.rgba[iy1 * self.width + ix1];

        let w00 = (1.0 - fx) * (1.0 - fy);
        let w10 = fx * (1.0 - fy);
        let w01 = (1.0 - fx) * fy;
        let w11 = fx * fy;

        [
            p00[0] as f32 * w00 + p10[0] as f32 * w10 + p01[0] as f32 * w01 + p11[0] as f32 * w11,
            p00[1] as f32 * w00 + p10[1] as f32 * w10 + p01[1] as f32 * w01 + p11[1] as f32 * w11,
            p00[2] as f32 * w00 + p10[2] as f32 * w10 + p01[2] as f32 * w01 + p11[2] as f32 * w11,
            p00[3] as f32 * w00 + p10[3] as f32 * w10 + p01[3] as f32 * w01 + p11[3] as f32 * w11,
        ]
    }
}

struct MeshDraw {
    tex_index: usize,
    is_eye_or_face: bool,
    tris: Vec<[u32; 3]>,
}

#[derive(Clone, Copy)]
struct SkirtBoneInfo {
    idx: usize,
    depth: u8,
    radial_x: f32,
    radial_y: f32,
    tangent_x: f32,
    tangent_y: f32,
    phase: f32,
}

#[derive(Clone, Copy)]
struct HairBoneInfo {
    idx: usize,
    depth: u8,
    out_x: f32,
    out_y: f32,
    is_bangs: bool,
    is_front_lock: bool,
    phase: f32,
}

#[derive(Clone, Debug)]
pub struct PoseParams3D {
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
    pub weapon_id: String,
    pub keep_center_anchor: bool,
}

impl Default for PoseParams3D {
    fn default() -> Self {
        Self {
            walk_phase: 0.0,
            walk_speed: 0.0,
            hair_lag_x: 0.0,
            hair_lag_y: 0.0,
            hair_bounce_z: 0.0,
            skirt_lag_x: 0.0,
            skirt_lag_y: 0.0,
            skirt_flare: 0.0,
            spin_vel: 0.0,
            sound_level: 0.0,
            action_id: 0,
            action_t: 0.0,
            weapon_id: "none".to_string(),
            keep_center_anchor: false,
        }
    }
}

pub const WEAPON_CATALOG: &[(&str, &str)] = &[
    ("auto", "Arma aleatoria / Caracteristica"),
    ("none", "Sin arma (Manos libres)"),
    ("shovel", "Pala (Shovel)"),
    ("shotgun", "Escopeta (Shotgun)"),
    ("pistol", "Pistola (Deagle)"),
    ("knife", "Cuchillo (Knife)"),
    ("bat", "Bate Kawaii (Bat)"),
];

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum WeaponKind {
    None,
    Shovel,
    Shotgun,
    Pistol,
    Knife,
    Bat,
}

fn resolve_weapon_kind(weapon_id: &str, character_id: &str, variant: u32) -> WeaponKind {
    match weapon_id.trim().to_ascii_lowercase().as_str() {
        "none" | "" => WeaponKind::None,
        "shovel" => WeaponKind::Shovel,
        "shotgun" => WeaponKind::Shotgun,
        "pistol" => WeaponKind::Pistol,
        "knife" => WeaponKind::Knife,
        "bat" => WeaponKind::Bat,
        _ => match (character_id, variant % 3) {
            ("natsuki", 0) => WeaponKind::Bat,
            ("natsuki", 1) => WeaponKind::Shovel,
            ("natsuki", _) => WeaponKind::Pistol,
            ("monika", 0) => WeaponKind::Pistol,
            ("monika", 1) => WeaponKind::Shotgun,
            ("monika", _) => WeaponKind::Knife,
            ("sayori", 0) => WeaponKind::Shovel,
            ("sayori", 1) => WeaponKind::Bat,
            ("sayori", _) => WeaponKind::Shotgun,
            ("yuri", 0) => WeaponKind::Knife,
            ("yuri", 1) => WeaponKind::Shotgun,
            _ => WeaponKind::Pistol,
        },
    }
}

pub struct LoadedDokiModel {
    bones: Vec<Bone>,
    vertices: Vec<SkinnedVertex>,
    textures: Vec<Texture2D>,
    meshes: Vec<MeshDraw>,
    basewarp: Vec<[f32; 3]>,
    min_z: f32,
    max_z: f32,
    // Key ValveBiped bone indices cached for fast posing
    b_pelvis: Option<usize>,
    b_spine2: Option<usize>,
    b_neck: Option<usize>,
    b_head: Option<usize>,
    b_l_clav: Option<usize>,
    b_l_upper: Option<usize>,
    b_l_fore: Option<usize>,
    b_l_hand: Option<usize>,
    b_r_clav: Option<usize>,
    b_r_upper: Option<usize>,
    b_r_fore: Option<usize>,
    b_r_hand: Option<usize>,
    b_l_thigh: Option<usize>,
    b_l_calf: Option<usize>,
    b_l_foot: Option<usize>,
    b_r_thigh: Option<usize>,
    b_r_calf: Option<usize>,
    b_r_foot: Option<usize>,
    b_weapon: Option<usize>,
    weapon_grip_mat: Mat34,
    skirt_bones: Vec<SkirtBoneInfo>,
    hair_bones: Vec<HairBoneInfo>,
    skirt_vert_t: Vec<f32>,
    skirt_pelvis_center: [f32; 3],
}

fn workshop_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();
    if let Ok(env_dir) = std::env::var("NATSUKI_L4D2_WORKSHOP_DIR") {
        if !env_dir.is_empty() {
            dirs.push(PathBuf::from(env_dir));
        }
    }
    if let Ok(home_dir) = std::env::var("NATSUKI_SHIMEJI_HOME") {
        dirs.push(PathBuf::from(home_dir).join("l4d2_workshop_vpks"));
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(parent) = exe.parent() {
            for rel in [
                "l4d2_workshop_vpks",
                "../l4d2_workshop_vpks",
                "../../l4d2_workshop_vpks",
                "../../../l4d2_workshop_vpks",
            ] {
                dirs.push(parent.join(rel));
            }
        }
    }
    dirs.push(PathBuf::from("l4d2_workshop_vpks"));
    dirs.push(PathBuf::from("../l4d2_workshop_vpks"));
    if let Ok(home) = std::env::var("HOME") {
        let h = PathBuf::from(home);
        dirs.push(h.join(".local/share/Steam/steamapps/common/Left 4 Dead 2/left4dead2/addons/workshop"));
        dirs.push(h.join(".steam/steam/steamapps/common/Left 4 Dead 2/left4dead2/addons/workshop"));
        dirs.push(h.join(".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Left 4 Dead 2/left4dead2/addons/workshop"));
    }
    dirs
}

pub fn find_vpk_path(character_id: &str) -> Option<PathBuf> {
    let vpk_file = match character_id {
        "natsuki" => "2565563118.vpk",
        "monika" => "2561380893.vpk",
        "sayori" => "2567184223.vpk",
        "yuri" => "2564088900.vpk",
        _ => return None,
    };
    for dir in workshop_dirs() {
        let cand = dir.join(vpk_file);
        if cand.is_file() {
            return Some(cand);
        }
    }
    None
}

pub fn has_character_vpk(character_id: &str) -> bool {
    find_vpk_path(character_id).is_some()
}

fn parse_vpk(path: &Path) -> Result<HashMap<String, Vec<u8>>, String> {
    let data = fs::read(path).map_err(|e| format!("read vpk {}: {e}", path.display()))?;
    if data.len() < 12 || r_u32(&data, 0) != 0x55aa1234 {
        return Err(format!("invalid vpk signature in {}", path.display()));
    }
    let ver = r_u32(&data, 4);
    let tree_size = r_u32(&data, 8) as usize;
    let tree_start = if ver == 2 { 28 } else { 12 };
    let tree_end = tree_start + tree_size;
    if tree_end > data.len() {
        return Err("truncated vpk directory tree".to_string());
    }

    let mut pos = tree_start;
    let mut files = HashMap::new();
    while pos < tree_end {
        let (ext, next) = read_cstr(&data, pos);
        pos = next;
        if ext.is_empty() {
            break;
        }
        loop {
            let (dir, next) = read_cstr(&data, pos);
            pos = next;
            if dir.is_empty() {
                break;
            }
            loop {
                let (fname, next) = read_cstr(&data, pos);
                pos = next;
                if fname.is_empty() {
                    break;
                }
                if pos + 18 > tree_end {
                    return Err("truncated vpk entry".to_string());
                }
                let preload_bytes = r_u16(&data, pos + 4) as usize;
                let arch_idx = r_u16(&data, pos + 6);
                let entry_off = r_u32(&data, pos + 8) as usize;
                let entry_len = r_u32(&data, pos + 12) as usize;
                pos += 18;

                let mut payload = Vec::with_capacity(preload_bytes + entry_len);
                if preload_bytes > 0 && pos + preload_bytes <= tree_end {
                    payload.extend_from_slice(&data[pos..pos + preload_bytes]);
                }
                pos += preload_bytes;
                if entry_len > 0 && arch_idx == 0x7fff {
                    let abs = tree_end + entry_off;
                    if abs + entry_len <= data.len() {
                        payload.extend_from_slice(&data[abs..abs + entry_len]);
                    }
                }
                let mut key = String::new();
                if dir != " " {
                    key.push_str(&dir);
                    key.push('/');
                }
                key.push_str(&fname);
                if ext != " " {
                    key.push('.');
                    key.push_str(&ext);
                }
                files.insert(key.to_ascii_lowercase(), payload);
            }
        }
    }
    Ok(files)
}

#[inline]
fn decode_rgb565(c: u16) -> [u8; 3] {
    let r5 = ((c >> 11) & 0x1f) as u16;
    let g6 = ((c >> 5) & 0x3f) as u16;
    let b5 = (c & 0x1f) as u16;
    [
        ((r5 << 3) | (r5 >> 2)) as u8,
        ((g6 << 2) | (g6 >> 4)) as u8,
        ((b5 << 3) | (b5 >> 2)) as u8,
    ]
}

#[inline]
fn lerp_rgb(a: [u8; 3], b: [u8; 3], num: u16, den: u16) -> [u8; 3] {
    [
        ((a[0] as u16 * (den - num) + b[0] as u16 * num + den / 2) / den) as u8,
        ((a[1] as u16 * (den - num) + b[1] as u16 * num + den / 2) / den) as u8,
        ((a[2] as u16 * (den - num) + b[2] as u16 * num + den / 2) / den) as u8,
    ]
}

fn decode_vtf(raw: &[u8]) -> Result<Texture2D, String> {
    if raw.len() < 64 || &raw[0..4] != b"VTF\0" {
        return Err("invalid VTF header".to_string());
    }
    let width = r_u16(raw, 16) as usize;
    let height = r_u16(raw, 18) as usize;
    let fmt = r_i32(raw, 52);
    if width == 0 || height == 0 {
        return Err("empty VTF dimensions".to_string());
    }

    let mut rgba = vec![[255u8, 255, 255, 255]; width * height];
    let mut min_alpha = 255u8;

    match fmt {
        15 => {
            // DXT5
            let bx = width.div_ceil(4);
            let by = height.div_ceil(4);
            let mip_bytes = bx * by * 16;
            if raw.len() < mip_bytes {
                return Err("truncated DXT5 mip0".to_string());
            }
            let src = &raw[raw.len() - mip_bytes..];
            for y_blk in 0..by {
                for x_blk in 0..bx {
                    let blk = &src[(y_blk * bx + x_blk) * 16..][..16];
                    let a0 = blk[0] as u16;
                    let a1 = blk[1] as u16;
                    let mut alphas = [0u8; 8];
                    alphas[0] = a0 as u8;
                    alphas[1] = a1 as u8;
                    if a0 > a1 {
                        for i in 1..7 {
                            alphas[1 + i] = ((a0 * (7 - i as u16) + a1 * i as u16 + 3) / 7) as u8;
                        }
                    } else {
                        for i in 1..5 {
                            alphas[1 + i] = ((a0 * (5 - i as u16) + a1 * i as u16 + 2) / 5) as u8;
                        }
                        alphas[6] = 0;
                        alphas[7] = 255;
                    }
                    let abits: u64 = (blk[2] as u64)
                        | ((blk[3] as u64) << 8)
                        | ((blk[4] as u64) << 16)
                        | ((blk[5] as u64) << 24)
                        | ((blk[6] as u64) << 32)
                        | ((blk[7] as u64) << 40);

                    let c0 = u16::from_le_bytes([blk[8], blk[9]]);
                    let c1 = u16::from_le_bytes([blk[10], blk[11]]);
                    let p0 = decode_rgb565(c0);
                    let p1 = decode_rgb565(c1);
                    let colors = [p0, p1, lerp_rgb(p0, p1, 1, 3), lerp_rgb(p0, p1, 2, 3)];
                    let cbits = r_u32(blk, 12);

                    for py in 0..4 {
                        let y = y_blk * 4 + py;
                        if y >= height {
                            continue;
                        }
                        for px in 0..4 {
                            let x = x_blk * 4 + px;
                            if x >= width {
                                continue;
                            }
                            let pi = py * 4 + px;
                            let ai = ((abits >> (pi * 3)) & 0x7) as usize;
                            let ci = ((cbits >> (pi * 2)) & 0x3) as usize;
                            let a = alphas[ai];
                            min_alpha = min_alpha.min(a);
                            let rgb = colors[ci];
                            rgba[y * width + x] = [rgb[0], rgb[1], rgb[2], a];
                        }
                    }
                }
            }
        }
        13 => {
            // DXT1
            let bx = width.div_ceil(4);
            let by = height.div_ceil(4);
            let mip_bytes = bx * by * 8;
            if raw.len() < mip_bytes {
                return Err("truncated DXT1 mip0".to_string());
            }
            let src = &raw[raw.len() - mip_bytes..];
            for y_blk in 0..by {
                for x_blk in 0..bx {
                    let blk = &src[(y_blk * bx + x_blk) * 8..][..8];
                    let c0 = u16::from_le_bytes([blk[0], blk[1]]);
                    let c1 = u16::from_le_bytes([blk[2], blk[3]]);
                    let p0 = decode_rgb565(c0);
                    let p1 = decode_rgb565(c1);
                    let colors = if c0 > c1 {
                        [
                            [p0[0], p0[1], p0[2], 255],
                            [p1[0], p1[1], p1[2], 255],
                            {
                                let c = lerp_rgb(p0, p1, 1, 3);
                                [c[0], c[1], c[2], 255]
                            },
                            {
                                let c = lerp_rgb(p0, p1, 2, 3);
                                [c[0], c[1], c[2], 255]
                            },
                        ]
                    } else {
                        [
                            [p0[0], p0[1], p0[2], 255],
                            [p1[0], p1[1], p1[2], 255],
                            {
                                let c = lerp_rgb(p0, p1, 1, 2);
                                [c[0], c[1], c[2], 255]
                            },
                            [0, 0, 0, 0],
                        ]
                    };
                    let cbits = r_u32(blk, 4);
                    for py in 0..4 {
                        let y = y_blk * 4 + py;
                        if y >= height {
                            continue;
                        }
                        for px in 0..4 {
                            let x = x_blk * 4 + px;
                            if x >= width {
                                continue;
                            }
                            let ci = ((cbits >> ((py * 4 + px) * 2)) & 0x3) as usize;
                            let pxv = colors[ci];
                            min_alpha = min_alpha.min(pxv[3]);
                            rgba[y * width + x] = pxv;
                        }
                    }
                }
            }
        }
        1 => {
            // ABGR8888
            let mip_bytes = width * height * 4;
            if raw.len() < mip_bytes {
                return Err("truncated ABGR8888 mip0".to_string());
            }
            let src = &raw[raw.len() - mip_bytes..];
            for i in 0..width * height {
                let s = &src[i * 4..i * 4 + 4];
                min_alpha = min_alpha.min(s[0]);
                rgba[i] = [s[3], s[2], s[1], s[0]];
            }
        }
        0 => {
            // RGBA8888
            let mip_bytes = width * height * 4;
            if raw.len() < mip_bytes {
                return Err("truncated RGBA8888 mip0".to_string());
            }
            let src = &raw[raw.len() - mip_bytes..];
            for i in 0..width * height {
                let s = &src[i * 4..i * 4 + 4];
                min_alpha = min_alpha.min(s[3]);
                rgba[i] = [s[0], s[1], s[2], s[3]];
            }
        }
        12 => {
            // BGRA8888
            let mip_bytes = width * height * 4;
            if raw.len() < mip_bytes {
                return Err("truncated BGRA8888 mip0".to_string());
            }
            let src = &raw[raw.len() - mip_bytes..];
            for i in 0..width * height {
                let s = &src[i * 4..i * 4 + 4];
                min_alpha = min_alpha.min(s[3]);
                rgba[i] = [s[2], s[1], s[0], s[3]];
            }
        }
        2 => {
            // RGB888
            let mip_bytes = width * height * 3;
            if raw.len() < mip_bytes {
                return Err("truncated RGB888 mip0".to_string());
            }
            let src = &raw[raw.len() - mip_bytes..];
            for i in 0..width * height {
                let s = &src[i * 3..i * 3 + 3];
                rgba[i] = [s[0], s[1], s[2], 255];
            }
        }
        3 => {
            // BGR888
            let mip_bytes = width * height * 3;
            if raw.len() < mip_bytes {
                return Err("truncated BGR888 mip0".to_string());
            }
            let src = &raw[raw.len() - mip_bytes..];
            for i in 0..width * height {
                let s = &src[i * 3..i * 3 + 3];
                rgba[i] = [s[2], s[1], s[0], 255];
            }
        }
        other => {
            return Err(format!("unsupported VTF image format {other}"));
        }
    }

    Ok(Texture2D {
        width,
        height,
        rgba,
    })
}

fn extract_vmt_basetexture(vmt_bytes: &[u8]) -> Option<String> {
    let txt = String::from_utf8_lossy(vmt_bytes);
    for line in txt.lines() {
        let lower = line.to_ascii_lowercase();
        if lower.contains("$basetexture") {
            let parts: Vec<&str> = line.split('"').collect();
            if parts.len() >= 4 {
                return Some(parts[3].replace('\\', "/").to_ascii_lowercase());
            }
        }
    }
    None
}

pub fn load_source_model(character_id: &str) -> Result<LoadedDokiModel, String> {
    let vpk_path = find_vpk_path(character_id)
        .ok_or_else(|| format!("L4D2 VPK not found for {character_id}"))?;
    let files = parse_vpk(&vpk_path)?;

    let mdl_key = files
        .keys()
        .find(|k| k.starts_with("models/survivors/") && k.ends_with(".mdl"))
        .cloned()
        .ok_or_else(|| format!("no survivor MDL in {}", vpk_path.display()))?;
    let stem = mdl_key.strip_suffix(".mdl").unwrap();
    let vvd_key = format!("{stem}.vvd");
    let vtx_key = format!("{stem}.dx90.vtx");

    let mdl = files.get(&mdl_key).ok_or("missing mdl")?;
    let vvd = files.get(&vvd_key).ok_or("missing vvd")?;
    let vtx = files.get(&vtx_key).ok_or("missing dx90.vtx")?;

    // Parse MDL bones, textures, and bodyparts
    let nbones = r_i32(mdl, 156) as usize;
    let bone_off = r_i32(mdl, 160) as usize;
    let ntex = r_i32(mdl, 204) as usize;
    let tex_off = r_i32(mdl, 208) as usize;
    let ncd = r_i32(mdl, 212) as usize;
    let cd_off = r_i32(mdl, 216) as usize;
    let nbp = r_i32(mdl, 232) as usize;
    let bp_off = r_i32(mdl, 236) as usize;

    let mut bones: Vec<Bone> = Vec::with_capacity(nbones);
    let mut bone_names_lower: Vec<String> = Vec::with_capacity(nbones);
    let mut b_pelvis = None;
    let mut b_spine2 = None;
    let mut b_neck = None;
    let mut b_head = None;
    let mut b_l_clav = None;
    let mut b_l_upper = None;
    let mut b_l_fore = None;
    let mut b_l_hand = None;
    let mut b_r_clav = None;
    let mut b_r_upper = None;
    let mut b_r_fore = None;
    let mut b_r_hand = None;
    let mut b_l_thigh = None;
    let mut b_l_calf = None;
    let mut b_l_foot = None;
    let mut b_r_thigh = None;
    let mut b_r_calf = None;
    let mut b_r_foot = None;
    let mut b_weapon = None;
    let mut b_r_finger1 = None;
    let mut b_r_finger4 = None;

    for bi in 0..nbones {
        let bo = bone_off + bi * 216;
        let szname = r_i32(mdl, bo) as isize;
        let name_off = (bo as isize + szname) as usize;
        let (bname, _) = read_cstr(mdl, name_off);
        let name_lower = bname.to_ascii_lowercase();
        let parent = r_i32(mdl, bo + 4);
        let pos = [r_f32(mdl, bo + 32), r_f32(mdl, bo + 36), r_f32(mdl, bo + 40)];
        let quat = [
            r_f32(mdl, bo + 44),
            r_f32(mdl, bo + 48),
            r_f32(mdl, bo + 52),
            r_f32(mdl, bo + 56),
        ];
        let local_bind = Mat34::from_quat_pos(quat, pos);
        let fk_bind = if parent >= 0 && (parent as usize) < bones.len() {
            bones[parent as usize].fk_bind.mul(&local_bind)
        } else {
            local_bind
        };

        if name_lower.ends_with("bip01_pelvis") {
            b_pelvis = Some(bi);
        } else if name_lower.ends_with("bip01_spine2") {
            b_spine2 = Some(bi);
        } else if name_lower.ends_with("bip01_neck1") {
            b_neck = Some(bi);
        } else if name_lower.ends_with("bip01_head1") {
            b_head = Some(bi);
        } else if name_lower.ends_with("bip01_l_clavicle") {
            b_l_clav = Some(bi);
        } else if name_lower.ends_with("bip01_l_upperarm") {
            b_l_upper = Some(bi);
        } else if name_lower.ends_with("bip01_l_forearm") {
            b_l_fore = Some(bi);
        } else if name_lower.ends_with("bip01_l_hand") {
            b_l_hand = Some(bi);
        } else if name_lower.ends_with("bip01_r_clavicle") {
            b_r_clav = Some(bi);
        } else if name_lower.ends_with("bip01_r_upperarm") {
            b_r_upper = Some(bi);
        } else if name_lower.ends_with("bip01_r_forearm") {
            b_r_fore = Some(bi);
        } else if name_lower.ends_with("bip01_r_hand") {
            b_r_hand = Some(bi);
        } else if name_lower.ends_with("bip01_l_thigh") {
            b_l_thigh = Some(bi);
        } else if name_lower.ends_with("bip01_l_calf") {
            b_l_calf = Some(bi);
        } else if name_lower.ends_with("bip01_l_foot") {
            b_l_foot = Some(bi);
        } else if name_lower.ends_with("bip01_r_thigh") {
            b_r_thigh = Some(bi);
        } else if name_lower.ends_with("bip01_r_calf") {
            b_r_calf = Some(bi);
        } else if name_lower.ends_with("bip01_r_foot") {
            b_r_foot = Some(bi);
        } else if name_lower == "valvebiped.weapon_bone" {
            b_weapon = Some(bi);
        } else if name_lower.ends_with("bip01_r_finger1") {
            b_r_finger1 = Some(bi);
        } else if name_lower.ends_with("bip01_r_finger4") {
            b_r_finger4 = Some(bi);
        }

        bone_names_lower.push(name_lower);
        bones.push(Bone {
            parent,
            fk_bind,
        });
    }

    let ancestry_depth = |idx: usize, ancestor: usize| -> Option<u8> {
        let mut cur = bones[idx].parent;
        let mut steps = 1u8;
        while cur >= 0 {
            if cur as usize == ancestor {
                return Some(steps);
            }
            cur = bones[cur as usize].parent;
            steps = steps.saturating_add(1);
            if steps > 16 {
                break;
            }
        }
        None
    };

    let mut skirt_bones = Vec::new();
    if let Some(p_idx) = b_pelvis {
        let px = bones[p_idx].fk_bind.0[0][3];
        let py = bones[p_idx].fk_bind.0[1][3];
        for (bi, b) in bones.iter().enumerate() {
            let bn = &bone_names_lower[bi];
            if !(bn.contains("qz") || bn.starts_with("fa")) {
                continue;
            }
            if let Some(depth) = ancestry_depth(bi, p_idx) {
                let bx = b.fk_bind.0[0][3];
                let by = b.fk_bind.0[1][3];
                let dx = bx - px;
                let mut dy = by - py;
                let len = (dx * dx + dy * dy).sqrt();
                let (ux, uy) = if len > 0.25 {
                    (dx / len, dy / len)
                } else {
                    dy = -1.0;
                    (0.0, dy)
                };
                skirt_bones.push(SkirtBoneInfo {
                    idx: bi,
                    depth,
                    radial_x: ux,
                    radial_y: uy,
                    tangent_x: uy,
                    tangent_y: -ux,
                    phase: uy.atan2(ux),
                });
            }
        }
    }

    let mut hair_bones = Vec::new();
    if let Some(h_idx) = b_head {
        let hx = bones[h_idx].fk_bind.0[0][3];
        let hy = bones[h_idx].fk_bind.0[1][3];
        let hz = bones[h_idx].fk_bind.0[2][3];
        for (bi, b) in bones.iter().enumerate() {
            let bn = &bone_names_lower[bi];
            if bn.contains("eye_") || bn.contains("forward") || bn.contains("weapon") || bn.contains("attach") {
                continue;
            }
            if let Some(depth) = ancestry_depth(bi, h_idx) {
                let bx = b.fk_bind.0[0][3];
                let by = b.fk_bind.0[1][3];
                let bz = b.fk_bind.0[2][3];
                let rx = bx - hx;
                let ry = by - hy;
                let rz = bz - hz;
                let hlen = (rx * rx + ry * ry).sqrt();
                let (ox, oy) = if hlen > 0.25 { (rx / hlen, ry / hlen) } else { (0.0, 1.0) };
                let is_bangs = ry < -0.55 && rz > 1.5;
                let is_front_lock = !is_bangs && ry < -1.5 && rz <= 1.5;
                hair_bones.push(HairBoneInfo {
                    idx: bi,
                    depth,
                    out_x: ox,
                    out_y: oy,
                    is_bangs,
                    is_front_lock,
                    phase: ry.atan2(rx),
                });
            }
        }
    }

    let weapon_grip_mat = build_weapon_grip_mat(
        &bones,
        b_r_fore,
        b_r_hand,
        b_weapon,
        b_r_finger1,
        b_r_finger4,
    );

    // Load textures using cdmaterials + VMT redirection
    let mut cd_dirs = Vec::new();
    for i in 0..ncd {
        let co = r_i32(mdl, cd_off + i * 4) as usize;
        let (cd, _) = read_cstr(mdl, co);
        let cleaned = cd
            .replace('\\', "/")
            .trim_matches('/')
            .to_ascii_lowercase();
        if !cleaned.is_empty() {
            cd_dirs.push(cleaned);
        }
    }

    let mut tex_names = Vec::with_capacity(ntex);
    let mut textures = Vec::with_capacity(ntex);
    for i in 0..ntex {
        let off = tex_off + i * 64;
        let noff = r_i32(mdl, off) as usize;
        let (tname, _) = read_cstr(mdl, off + noff);
        let tlower = tname.to_ascii_lowercase();
        tex_names.push(tlower.clone());

        let mut loaded_tex = None;
        for cd in &cd_dirs {
            let vmt_key = format!("materials/{cd}/{tlower}.vmt");
            if let Some(vmt_bytes) = files.get(&vmt_key) {
                if let Some(btex) = extract_vmt_basetexture(vmt_bytes) {
                    let vtf_key = format!("materials/{btex}.vtf");
                    if let Some(vtf_bytes) = files.get(&vtf_key) {
                        if let Ok(tex) = decode_vtf(vtf_bytes) {
                            loaded_tex = Some(tex);
                            break;
                        }
                    }
                }
            }
            let direct_vtf = format!("materials/{cd}/{tlower}.vtf");
            if let Some(vtf_bytes) = files.get(&direct_vtf) {
                if let Ok(tex) = decode_vtf(vtf_bytes) {
                    loaded_tex = Some(tex);
                    break;
                }
            }
        }
        textures.push(loaded_tex.unwrap_or_else(|| Texture2D {
            width: 1,
            height: 1,
            rgba: vec![[235, 220, 230, 255]],
        }));
    }

    // Load optional Source lightwarp ramp for authentic anime toon shading
    let mut basewarp = Vec::new();
    for cd in &cd_dirs {
        let bw_key = format!("materials/{cd}/basewarp.vtf");
        if let Some(bw_bytes) = files.get(&bw_key) {
            if let Ok(tex) = decode_vtf(bw_bytes) {
                if tex.width > 0 {
                    basewarp = (0..tex.width)
                        .map(|x| {
                            let px = tex.rgba[x];
                            [
                                px[0] as f32 / 255.0,
                                px[1] as f32 / 255.0,
                                px[2] as f32 / 255.0,
                            ]
                        })
                        .collect();
                }
                break;
            }
        }
    }

    // Parse VVD vertices (applying fixup table if present)
    let lod0_verts = r_i32(vvd, 16) as usize;
    let num_fixups = r_i32(vvd, 48) as usize;
    let fixup_off = r_i32(vvd, 52) as usize;
    let vdata_off = r_i32(vvd, 56) as usize;

    let read_vvd_vert = |src_idx: usize| -> SkinnedVertex {
        let vo = vdata_off + src_idx * 48;
        SkinnedVertex {
            weights: [r_f32(vvd, vo), r_f32(vvd, vo + 4), r_f32(vvd, vo + 8)],
            bones: [vvd[vo + 12], vvd[vo + 13], vvd[vo + 14]],
            num_bones: vvd[vo + 15],
            pos: [r_f32(vvd, vo + 16), r_f32(vvd, vo + 20), r_f32(vvd, vo + 24)],
            norm: [r_f32(vvd, vo + 28), r_f32(vvd, vo + 32), r_f32(vvd, vo + 36)],
            uv: [r_f32(vvd, vo + 40), r_f32(vvd, vo + 44)],
        }
    };

    let mut vertices = Vec::with_capacity(lod0_verts);
    if num_fixups == 0 {
        for i in 0..lod0_verts {
            vertices.push(read_vvd_vert(i));
        }
    } else {
        for fi in 0..num_fixups {
            let fo = fixup_off + fi * 12;
            let lod = r_i32(vvd, fo);
            let src_v = r_i32(vvd, fo + 4) as usize;
            let count = r_i32(vvd, fo + 8) as usize;
            if lod >= 0 {
                for k in 0..count {
                    vertices.push(read_vvd_vert(src_v + k));
                }
            }
        }
    }

    let mut min_z = f32::MAX;
    let mut max_z = f32::MIN;
    for v in &vertices {
        min_z = min_z.min(v.pos[2]);
        max_z = max_z.max(v.pos[2]);
    }

    // Pair MDL meshes + DX90.VTX triangles
    let vtx_nbp = r_i32(vtx, 28) as usize;
    let vtx_bp_off = r_i32(vtx, 32) as usize;
    let mut meshes = Vec::new();
    let mut is_skirt_vert = vec![false; vertices.len()];

    for bi in 0..nbp.min(vtx_nbp) {
        let mdl_bo = bp_off + bi * 16;
        let mdl_nmodels = r_i32(mdl, mdl_bo + 4) as usize;
        let mdl_model_off = r_i32(mdl, mdl_bo + 12) as usize;

        let vtx_bo = vtx_bp_off + bi * 8;
        let vtx_nmodels = r_i32(vtx, vtx_bo) as usize;
        let vtx_model_off = r_i32(vtx, vtx_bo + 4) as usize;

        if mdl_nmodels == 0 || vtx_nmodels == 0 {
            continue;
        }
        // Always model[0]
        let mdl_mo = mdl_bo + mdl_model_off;
        let mdl_nmesh = r_i32(mdl, mdl_mo + 72) as usize;
        let mdl_mesh_off = mdl_mo + r_i32(mdl, mdl_mo + 76) as usize;
        let mdl_vidx_verts = (r_i32(mdl, mdl_mo + 84) as usize) / 48;

        let vtx_mo = vtx_bo + vtx_model_off;
        let vtx_nlods = r_i32(vtx, vtx_mo) as usize;
        let vtx_lod_off = vtx_mo + r_i32(vtx, vtx_mo + 4) as usize;
        if vtx_nlods == 0 {
            continue;
        }
        let vtx_nmesh = r_i32(vtx, vtx_lod_off) as usize;
        let vtx_mesh_off = vtx_lod_off + r_i32(vtx, vtx_lod_off + 4) as usize;

        for mi in 0..mdl_nmesh.min(vtx_nmesh) {
            let mdl_mesho = mdl_mesh_off + mi * 116;
            let mat_idx = (r_i32(mdl, mdl_mesho) as usize).min(textures.len().saturating_sub(1));
            let mesh_voff = r_i32(mdl, mdl_mesho + 12) as usize;
            let base_v = mdl_vidx_verts + mesh_voff;
            let tname = &tex_names[mat_idx];
            let is_eye_or_face = tname.contains("eye") || tname.contains("face") || tname.contains("body2");
            let is_skirt_mesh = tname.contains("skirt") || tname.contains("falda");

            let vtx_ms_o = vtx_mesh_off + mi * 9;
            let nsg = r_i32(vtx, vtx_ms_o) as usize;
            let sgo = r_i32(vtx, vtx_ms_o + 4) as usize;

            let mut tris = Vec::new();
            for sg in 0..nsg {
                let sg_o = vtx_ms_o + sgo + sg * 25;
                let vo = r_i32(vtx, sg_o + 4) as usize;
                let io = r_i32(vtx, sg_o + 12) as usize;
                let nst = r_i32(vtx, sg_o + 16) as usize;
                let sto = r_i32(vtx, sg_o + 20) as usize;

                let resolve_vert = |local_vi: u16| -> u32 {
                    let v_o = sg_o + vo + (local_vi as usize) * 9;
                    let orig_id = r_u16(vtx, v_o + 4) as usize;
                    let abs_id = base_v + orig_id;
                    (abs_id.min(vertices.len().saturating_sub(1))) as u32
                };

                for si in 0..nst {
                    let st_o = sg_o + sto + si * 27;
                    let s_nidx = r_i32(vtx, st_o) as usize;
                    let s_idxoff = r_i32(vtx, st_o + 4) as usize;
                    let s_flags = vtx[st_o + 18];
                    let idx_base = sg_o + io + s_idxoff * 2;
                    if (s_flags & 0x2) != 0 {
                        // Triangle strip fallback
                        for k in 0..s_nidx.saturating_sub(2) {
                            let a = r_u16(vtx, idx_base + k * 2);
                            let b = r_u16(vtx, idx_base + (k + 1) * 2);
                            let c = r_u16(vtx, idx_base + (k + 2) * 2);
                            if a == b || b == c || a == c {
                                continue;
                            }
                            let (va, vb, vc) = (resolve_vert(a), resolve_vert(b), resolve_vert(c));
                            if (k & 1) == 0 {
                                tris.push([va, vb, vc]);
                            } else {
                                tris.push([vb, va, vc]);
                            }
                        }
                    } else {
                        // Triangle list
                        let mut k = 0;
                        while k + 2 < s_nidx {
                            let a = resolve_vert(r_u16(vtx, idx_base + k * 2));
                            let b = resolve_vert(r_u16(vtx, idx_base + (k + 1) * 2));
                            let c = resolve_vert(r_u16(vtx, idx_base + (k + 2) * 2));
                            tris.push([a, b, c]);
                            k += 3;
                        }
                    }
                }
            }

            if !tris.is_empty() {
                if is_skirt_mesh {
                    for tri in &tris {
                        for &vi in tri {
                            if (vi as usize) < is_skirt_vert.len() {
                                is_skirt_vert[vi as usize] = true;
                            }
                        }
                    }
                }
                meshes.push(MeshDraw {
                    tex_index: mat_idx,
                    is_eye_or_face,
                    tris,
                });
            }
        }
    }

    let mut skirt_z_min = f32::MAX;
    let mut skirt_z_max = f32::MIN;
    for (vi, &flag) in is_skirt_vert.iter().enumerate() {
        if flag {
            let z = vertices[vi].pos[2];
            skirt_z_min = skirt_z_min.min(z);
            skirt_z_max = skirt_z_max.max(z);
        }
    }
    let skirt_height = if skirt_z_max > skirt_z_min + 0.5 {
        skirt_z_max - skirt_z_min
    } else {
        1.0
    };
    let skirt_pelvis_center = if let Some(p_idx) = b_pelvis {
        [
            bones[p_idx].fk_bind.0[0][3],
            bones[p_idx].fk_bind.0[1][3],
            if skirt_z_max > skirt_z_min {
                skirt_z_max
            } else {
                bones[p_idx].fk_bind.0[2][3]
            },
        ]
    } else {
        [0.0, 0.0, skirt_z_max]
    };
    let skirt_vert_t: Vec<f32> = vertices
        .iter()
        .enumerate()
        .map(|(vi, v)| {
            if is_skirt_vert[vi] && skirt_z_max > skirt_z_min + 0.5 {
                ((skirt_z_max - v.pos[2]) / skirt_height).clamp(0.0, 1.0)
            } else {
                -1.0
            }
        })
        .collect();

    if character_id == "yuri" || character_id == "monika" {
        let is_yuri = character_id == "yuri";
        let z_c = if is_yuri { 51.8_f32 } else { 52.2_f32 };
        let amp_y = if is_yuri { 1.45_f32 } else { 1.15_f32 };
        for (vi, v) in vertices.iter_mut().enumerate() {
            if is_skirt_vert[vi] {
                continue;
            }
            let sx = v.pos[0];
            let sy_raw = v.pos[1];
            let sz = v.pos[2];
            let y_front = -sy_raw;
            if y_front <= 0.6 || sz < 47.5 || sz > 56.2 || sx.abs() > 4.6 {
                continue;
            }
            let fy = smoothstep01(0.6, 2.3, y_front)
                * (1.0 - 0.20 * smoothstep01(4.8, 7.2, y_front));
            let r2_l = ((sx - 1.65) / 2.15).powi(2) + ((sz - z_c) / 2.85).powi(2);
            let r2_r = ((sx + 1.65) / 2.15).powi(2) + ((sz - z_c) / 2.85).powi(2);
            let lobe_l = (1.0 - r2_l).max(0.0).powi(2);
            let lobe_r = (1.0 - r2_r).max(0.0).powi(2);
            let f_pec = fy * (lobe_l + lobe_r).min(1.0);
            if f_pec > 1e-4 {
                v.pos[1] -= amp_y * f_pec;
                v.pos[2] += 0.18 * f_pec * ((sz - z_c) / 2.85);
                v.pos[0] += 0.16
                    * f_pec
                    * (if sx >= 0.0 { 1.0 } else { -1.0 })
                    * smoothstep01(0.3, 1.8, sx.abs());
            }
        }
    }

    Ok(LoadedDokiModel {
        bones,
        vertices,
        textures,
        meshes,
        basewarp,
        min_z,
        max_z,
        b_pelvis,
        b_spine2,
        b_neck,
        b_head,
        b_l_clav,
        b_l_upper,
        b_l_fore,
        b_l_hand,
        b_r_clav,
        b_r_upper,
        b_r_fore,
        b_r_hand,
        b_l_thigh,
        b_l_calf,
        b_l_foot,
        b_r_thigh,
        b_r_calf,
        b_r_foot,
        b_weapon,
        weapon_grip_mat,
        skirt_bones,
        hair_bones,
        skirt_vert_t,
        skirt_pelvis_center,
    })
}

fn norm3(v: [f32; 3]) -> [f32; 3] {
    let l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]).sqrt();
    if l > 1e-5 {
        [v[0] / l, v[1] / l, v[2] / l]
    } else {
        [0.0, 0.0, 1.0]
    }
}

fn cross3(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn dot3(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn bone_world_pos(b: &Bone) -> [f32; 3] {
    [b.fk_bind.0[0][3], b.fk_bind.0[1][3], b.fk_bind.0[2][3]]
}

fn build_weapon_grip_mat(
    bones: &[Bone],
    b_r_fore: Option<usize>,
    b_r_hand: Option<usize>,
    b_weapon: Option<usize>,
    b_r_finger1: Option<usize>,
    b_r_finger4: Option<usize>,
) -> Mat34 {
    let origin = b_weapon
        .or(b_r_hand)
        .map(|i| bone_world_pos(&bones[i]))
        .unwrap_or([-18.0, -1.5, 42.3]);
    let up_raw = match (b_r_finger1, b_r_finger4) {
        (Some(f1), Some(f4)) => {
            let p1 = bone_world_pos(&bones[f1]);
            let p4 = bone_world_pos(&bones[f4]);
            [p1[0] - p4[0], p1[1] - p4[1], p1[2] - p4[2]]
        }
        _ => [0.0, -0.5, 1.0],
    };
    let up_u = norm3(up_raw);
    let fore_raw = match (b_r_fore, b_r_hand) {
        (Some(rf), Some(rh)) => {
            let pf = bone_world_pos(&bones[rf]);
            let ph = bone_world_pos(&bones[rh]);
            [ph[0] - pf[0], ph[1] - pf[1], ph[2] - pf[2]]
        }
        _ => [-0.75, -0.05, -0.65],
    };
    let proj = dot3(fore_raw, up_u);
    let fwd_raw = [
        fore_raw[0] - up_u[0] * proj,
        fore_raw[1] - up_u[1] * proj,
        fore_raw[2] - up_u[2] * proj,
    ];
    let fwd_u = norm3(fwd_raw);
    let right_u = norm3(cross3(fwd_u, up_u));
    // Re-orthogonalize fwd_u = cross(up_u, right_u)
    let fwd_orth = norm3(cross3(up_u, right_u));
    Mat34([
        [right_u[0], fwd_orth[0], up_u[0], origin[0]],
        [right_u[1], fwd_orth[1], up_u[1], origin[1]],
        [right_u[2], fwd_orth[2], up_u[2], origin[2]],
    ])
}

#[derive(Clone, Copy)]
struct SolidTri {
    pos: [[f32; 3]; 3],
    norm: [f32; 3],
    color: [u8; 4],
    unlit: bool,
}

fn push_quad(
    out: &mut Vec<SolidTri>,
    p0: [f32; 3],
    p1: [f32; 3],
    p2: [f32; 3],
    p3: [f32; 3],
    color: [u8; 4],
    unlit: bool,
) {
    let e1 = [p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]];
    let e2 = [p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]];
    let n = norm3(cross3(e1, e2));
    out.push(SolidTri { pos: [p0, p1, p2], norm: n, color, unlit });
    out.push(SolidTri { pos: [p0, p2, p3], norm: n, color, unlit });
}

fn push_box(
    out: &mut Vec<SolidTri>,
    x0: f32,
    x1: f32,
    y0: f32,
    y1: f32,
    z0: f32,
    z1: f32,
    color: [u8; 4],
) {
    let a = [x0, y0, z0];
    let b = [x1, y0, z0];
    let c = [x1, y1, z0];
    let d = [x0, y1, z0];
    let e = [x0, y0, z1];
    let f = [x1, y0, z1];
    let g = [x1, y1, z1];
    let h = [x0, y1, z1];
    push_quad(out, d, c, b, a, color, false); // -Z
    push_quad(out, e, f, g, h, color, false); // +Z
    push_quad(out, a, b, f, e, color, false); // -Y
    push_quad(out, c, d, h, g, color, false); // +Y
    push_quad(out, d, a, e, h, color, false); // -X
    push_quad(out, b, c, g, f, color, false); // +X
}

fn push_tapered_prism_z(
    out: &mut Vec<SolidTri>,
    rx0: f32,
    ry0: f32,
    z0: f32,
    rx1: f32,
    ry1: f32,
    z1: f32,
    color: [u8; 4],
) {
    let a = [-rx0, -ry0, z0];
    let b = [rx0, -ry0, z0];
    let c = [rx0, ry0, z0];
    let d = [-rx0, ry0, z0];
    let e = [-rx1, -ry1, z1];
    let f = [rx1, -ry1, z1];
    let g = [rx1, ry1, z1];
    let h = [-rx1, ry1, z1];
    push_quad(out, d, c, b, a, color, false);
    push_quad(out, e, f, g, h, color, false);
    push_quad(out, a, b, f, e, color, false);
    push_quad(out, c, d, h, g, color, false);
    push_quad(out, d, a, e, h, color, false);
    push_quad(out, b, c, g, f, color, false);
}

fn push_muzzle_flash(out: &mut Vec<SolidTri>, tip: [f32; 3], radius: f32) {
    let gold = [255, 224, 92, 255];
    let white = [255, 252, 225, 255];
    for i in 0..4 {
        let ang = (i as f32) * std::f32::consts::FRAC_PI_4;
        let (sa, ca) = ang.sin_cos();
        let p_in = [tip[0], tip[1], tip[2]];
        let p_a = [tip[0] + ca * radius, tip[1] + radius * 1.4, tip[2] + sa * radius];
        let p_b = [tip[0] - sa * radius * 0.45, tip[1] + radius * 0.55, tip[2] + ca * radius * 0.45];
        out.push(SolidTri {
            pos: [p_in, p_a, p_b],
            norm: [0.0, 1.0, 0.0],
            color: if i % 2 == 0 { gold } else { white },
            unlit: true,
        });
    }
}

fn build_weapon_mesh(kind: WeaponKind, show_flash: bool) -> Vec<SolidTri> {
    let mut tris = Vec::with_capacity(96);
    match kind {
        WeaponKind::None => {}
        WeaponKind::Shovel => {
            let wood = [148, 88, 46, 255];
            let grip = [48, 44, 48, 255];
            let steel = [188, 198, 212, 255];
            let edge = [228, 236, 245, 255];
            // Pommel D-grip at handle bottom
            push_box(&mut tris, -1.6, 1.6, -0.55, 0.55, -8.5, -7.1, grip);
            push_box(&mut tris, -1.5, -0.95, -0.45, 0.45, -7.2, -5.4, grip);
            push_box(&mut tris, 0.95, 1.5, -0.45, 0.45, -7.2, -5.4, grip);
            // Wooden shaft through hand
            push_tapered_prism_z(&mut tris, 0.58, 0.58, -5.6, 0.54, 0.54, 21.5, wood);
            // Steel socket collar
            push_tapered_prism_z(&mut tris, 0.78, 0.72, 20.5, 1.15, 0.65, 23.8, steel);
            // Wide spade body + beveled tip
            push_box(&mut tris, -3.5, 3.5, -0.35, 0.42, 23.6, 29.5, steel);
            push_tapered_prism_z(&mut tris, 3.5, 0.36, 29.5, 0.45, 0.14, 33.8, edge);
        }
        WeaponKind::Bat => {
            let wrap = [255, 108, 165, 255];
            let pink = [255, 178, 208, 255];
            let cream = [246, 224, 196, 255];
            // Knob + kawaii wrapped handle
            push_tapered_prism_z(&mut tris, 0.95, 0.95, -5.6, 0.55, 0.55, -4.4, pink);
            push_tapered_prism_z(&mut tris, 0.55, 0.55, -4.4, 0.66, 0.66, 4.0, wrap);
            // Mid shaft
            push_tapered_prism_z(&mut tris, 0.66, 0.66, 4.0, 1.15, 1.15, 15.0, cream);
            // Barrel + rounded crown
            push_tapered_prism_z(&mut tris, 1.15, 1.15, 15.0, 1.48, 1.48, 25.5, pink);
            push_tapered_prism_z(&mut tris, 1.48, 1.48, 25.5, 0.75, 0.75, 26.8, cream);
        }
        WeaponKind::Knife => {
            let hilt = [36, 32, 42, 255];
            let guard = [210, 180, 96, 255];
            let blade = [226, 234, 244, 255];
            let blood_tint = [244, 246, 252, 255];
            push_box(&mut tris, -0.46, 0.46, -0.78, 0.78, -4.2, 2.8, hilt);
            push_box(&mut tris, -0.75, 0.75, -1.65, 1.65, 2.8, 3.5, guard);
            // Tapered chef/tactical blade along +Z & slight forward +Y
            push_box(&mut tris, -0.22, 0.22, -0.95, 1.35, 3.5, 13.0, blade);
            push_tapered_prism_z(&mut tris, 0.22, 1.15, 13.0, 0.05, 0.18, 18.2, blood_tint);
        }
        WeaponKind::Pistol => {
            let grip = [42, 40, 48, 255];
            let frame = [70, 74, 86, 255];
            let slide = [212, 218, 228, 255];
            // Grip centered in palm
            push_box(&mut tris, -0.52, 0.52, -2.0, 1.35, -3.9, 0.9, grip);
            // Frame & barrel lower rail along +Y
            push_box(&mut tris, -0.58, 0.58, -2.4, 8.8, 0.9, 2.2, frame);
            // Trigger guard
            push_box(&mut tris, -0.35, 0.35, 1.3, 3.4, -0.6, 0.9, frame);
            // Silver Desert-Eagle slide
            push_box(&mut tris, -0.64, 0.64, -2.6, 8.4, 2.2, 3.9, slide);
            // Front sight
            push_box(&mut tris, -0.18, 0.18, 7.2, 8.1, 3.9, 4.45, grip);
            if show_flash {
                push_muzzle_flash(&mut tris, [0.0, 9.2, 3.0], 4.2);
            }
        }
        WeaponKind::Shotgun => {
            let wood = [152, 78, 42, 255];
            let steel = [58, 62, 72, 255];
            let metal_hi = [118, 124, 138, 255];
            // Wood buttstock extending behind wrist (-Y)
            push_box(&mut tris, -0.62, 0.62, -13.0, -3.5, -1.3, 1.7, wood);
            push_box(&mut tris, -0.58, 0.58, -3.5, 0.8, -1.8, 1.4, wood);
            // Dark steel receiver & lever loop
            push_box(&mut tris, -0.72, 0.72, -3.2, 7.0, 0.6, 3.5, steel);
            push_box(&mut tris, -0.32, 0.32, 0.8, 3.5, -1.4, 0.6, metal_hi);
            // Long barrel + underbarrel mag tube
            push_box(&mut tris, -0.44, 0.44, 6.8, 26.5, 2.2, 3.2, steel);
            push_box(&mut tris, -0.36, 0.36, 6.8, 22.8, 1.05, 1.95, metal_hi);
            // Forend wood pump grip
            push_box(&mut tris, -0.68, 0.68, 9.2, 16.5, 0.55, 2.15, wood);
            if show_flash {
                push_muzzle_flash(&mut tris, [0.0, 27.2, 2.7], 5.6);
            }
        }
    }
    tris
}

pub struct Source3dStore {
    models: HashMap<String, Option<Arc<LoadedDokiModel>>>,
}

impl Source3dStore {
    pub fn new() -> Self {
        Self {
            models: HashMap::new(),
        }
    }

    pub fn get_model(&mut self, character_id: &str) -> Option<Arc<LoadedDokiModel>> {
        if let Some(cached) = self.models.get(character_id) {
            return cached.clone();
        }
        let loaded = load_source_model(character_id).ok().map(Arc::new);
        self.models.insert(character_id.to_string(), loaded.clone());
        loaded
    }
}

struct PoseEval {
    bone_mats: Vec<Mat34>,
    l_thigh_fwd: f32,
    r_thigh_fwd: f32,
    l_thigh_out: f32,
    r_thigh_out: f32,
    skirt_extra_flare: f32,
    walk_speed: f32,
}

#[inline]
fn smoothstep01(edge0: f32, edge1: f32, x: f32) -> f32 {
    let t = ((x - edge0) / (edge1 - edge0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

#[inline]
fn compute_skirt_radial_deg(
    radial_x: f32,
    radial_y: f32,
    phase: f32,
    depth: u8,
    pose: &PoseParams3D,
    ws: f32,
    skirt_extra_flare: f32,
    l_thigh_fwd: f32,
    r_thigh_fwd: f32,
    l_thigh_out: f32,
    r_thigh_out: f32,
) -> f32 {
    let frontness = (-radial_y).clamp(0.0, 1.0);
    let backness = radial_y.clamp(0.0, 1.0);
    let left_bias = ((radial_x + 0.30) / 1.30).clamp(0.0, 1.0);
    let right_bias = ((-radial_x + 0.30) / 1.30).clamp(0.0, 1.0);
    let sidelen = radial_x.abs();

    let spin_abs = pose.spin_vel.abs().min(3.5);
    let sx_lag = pose.skirt_lag_x.clamp(-1.4, 1.4);
    let sy_lag = pose.skirt_lag_y.clamp(-1.4, 1.4);
    let lag_dot = sx_lag * radial_x + sy_lag * radial_y;
    let lag_outward = (lag_dot * 24.0).clamp(-3.0, 32.0);
    let base_flare = 3.5 + (pose.skirt_flare + skirt_extra_flare).clamp(0.0, 2.0) * 24.0;
    let spin_flare = spin_abs * 15.0;

    let fwd_l = l_thigh_fwd.max(0.0) * 0.78 * left_bias * (0.28 + 0.72 * frontness);
    let fwd_r = r_thigh_fwd.max(0.0) * 0.78 * right_bias * (0.28 + 0.72 * frontness);
    let back_l = (-l_thigh_fwd).max(0.0) * 0.72 * left_bias * (0.22 + 0.78 * backness);
    let back_r = (-r_thigh_fwd).max(0.0) * 0.72 * right_bias * (0.22 + 0.78 * backness);
    let side_l = l_thigh_out.max(0.0) * 0.65 * left_bias * sidelen;
    let side_r = r_thigh_out.max(0.0) * 0.65 * right_bias * sidelen;
    let leg_push = fwd_l
        .max(fwd_r)
        .max(back_l)
        .max(back_r)
        .max(side_l)
        .max(side_r);

    let ripple = (pose.walk_phase * 2.0 + phase * 3.0).sin()
        * (1.5 + 4.5 * ws + 5.0 * pose.sound_level.clamp(0.0, 1.0));
    let (chain_scale, max_deg) = match depth {
        1 => (0.84, 68.0),
        2 => (0.38, 28.0),
        _ => (0.24, 20.0),
    };
    ((lag_outward + base_flare + spin_flare + leg_push + ripple) * chain_scale)
        .clamp(-3.0, max_deg)
}

fn compute_pose_deltas(
    model: &LoadedDokiModel,
    character_id: &str,
    mood_id: &str,
    variant: u32,
    pose: &PoseParams3D,
    weapon_kind: WeaponKind,
) -> PoseEval {
    let nb = model.bones.len();
    let mut local_deltas = vec![Mat34::identity(); nb];
    let deg = std::f32::consts::PI / 180.0;

    // Bind-pose has +X pointing to left side, -X to right side, -Y facing front, +Z up.
    // Give each Doki a distinct, lively personality posture by default!
    let mut l_upper_drop: f32;
    let mut r_upper_drop: f32;
    let mut l_upper_fwd = -6.0f32;
    let mut r_upper_fwd = 6.0f32;
    let mut l_fore_in = -8.0f32;
    let mut r_fore_in = 8.0f32;
    let mut l_fore_fwd: f32;
    let mut r_fore_fwd: f32;
    let mut head_tilt: f32;
    let mut head_nod = 0.0f32;
    let mut head_turn: f32;
    let mut torso_lean = 0.0f32;
    let mut torso_twist = 0.0f32;

    let mut pelvis_pitch = 0.0f32;
    let mut pelvis_roll = 0.0f32;
    let mut pelvis_twist = 0.0f32;
    let mut pelvis_dz = 0.0f32;
    let mut pelvis_dy = 0.0f32;

    let mut l_thigh_fwd = 0.0f32; // + = leg forward (-Y)
    let mut r_thigh_fwd = 0.0f32;
    let mut l_thigh_out = 0.0f32;
    let mut r_thigh_out = 0.0f32;
    let mut l_knee_bend = 0.0f32; // + = shin backward (+Y)
    let mut r_knee_bend = 0.0f32;
    let mut l_foot_pitch = 0.0f32;
    let mut r_foot_pitch = 0.0f32;

    match character_id {
        "natsuki" => {
            // Sassy tsun stance: hand near hip/waist, playful head tilt
            l_upper_drop = 28.0;
            r_upper_drop = -35.0;
            l_fore_in = 24.0;
            l_fore_fwd = -32.0;
            r_fore_fwd = 16.0;
            head_tilt = -5.5;
            head_turn = 6.0;
        }
        "monika" => {
            // Poised club president stance: gentle welcoming hand lifted
            l_upper_drop = 39.0;
            r_upper_drop = -25.0;
            r_fore_in = -22.0;
            r_fore_fwd = 40.0;
            l_fore_fwd = -16.0;
            head_tilt = 4.5;
            head_turn = -4.0;
        }
        "sayori" => {
            // Cheerful bouncy stance: slightly lifted arm & sunny head tilt
            l_upper_drop = 22.0;
            r_upper_drop = -30.0;
            l_upper_fwd = -14.0;
            l_fore_in = 14.0;
            l_fore_fwd = -26.0;
            r_fore_fwd = 18.0;
            head_tilt = 7.0;
            head_nod = -3.0;
            head_turn = 0.0;
        }
        _ => {
            // Yuri: shy, graceful folded-inward hands & gentle dip
            l_upper_drop = 41.0;
            r_upper_drop = -41.0;
            l_upper_fwd = -14.0;
            r_upper_fwd = 14.0;
            l_fore_in = 18.0;
            r_fore_in = -18.0;
            l_fore_fwd = -28.0;
            r_fore_fwd = 28.0;
            head_tilt = -4.0;
            head_nod = 5.0;
            head_turn = -2.0;
        }
    }

    match mood_id {
        "fly" => {
            l_upper_drop = -24.0;
            r_upper_drop = 24.0;
            l_upper_fwd = -18.0;
            r_upper_fwd = 18.0;
            head_tilt = 8.0;
            head_nod = -10.0;
            torso_lean = 12.0;
            l_thigh_fwd = -18.0;
            l_knee_bend = 35.0;
            r_thigh_fwd = -8.0;
            r_knee_bend = 20.0;
        }
        "tsun" | "annoyed" | "angry" | "pout" => {
            l_upper_drop = 24.0;
            r_upper_drop = -24.0;
            l_fore_in = 28.0;
            r_fore_in = -28.0;
            l_fore_fwd = -36.0;
            r_fore_fwd = 36.0;
            head_tilt = -7.0;
            head_turn = 9.0;
        }
        "shy" | "blush" => {
            l_upper_drop = 42.0;
            r_upper_drop = -42.0;
            l_upper_fwd = -16.0;
            r_upper_fwd = 16.0;
            l_fore_in = 20.0;
            r_fore_in = -20.0;
            l_fore_fwd = -32.0;
            r_fore_fwd = 32.0;
            head_nod = 8.0;
            head_tilt += 3.0;
        }
        "sad" => {
            l_upper_drop = 43.0;
            r_upper_drop = -43.0;
            head_nod = 12.0;
            head_tilt = -4.0;
        }
        "happy" | "cute" | "smug" => {
            let phase = (variant % 3) as f32;
            l_upper_drop -= 4.0 + phase * 3.0;
            l_fore_fwd -= 10.0 + phase * 6.0;
            head_tilt += if variant % 2 == 0 { 3.5 } else { -3.5 };
            head_nod -= 2.5;
        }
        _ => {
            let var_alt = (variant % 4) as f32;
            head_turn += (var_alt - 1.5) * 2.2;
        }
    }

    // Natural right-hand weapon carry offset when idle/walking
    if weapon_kind != WeaponKind::None {
        match weapon_kind {
            WeaponKind::Pistol | WeaponKind::Shotgun => {
                r_upper_drop = r_upper_drop.min(-18.0) - 6.0;
                r_upper_fwd = 28.0;
                r_fore_fwd = 42.0;
                r_fore_in = -4.0;
            }
            WeaponKind::Shovel | WeaponKind::Bat | WeaponKind::Knife => {
                r_upper_drop = -22.0;
                r_upper_fwd = 20.0;
                r_fore_fwd = 34.0;
                r_fore_in = -8.0;
            }
            WeaponKind::None => {}
        }
    }

    // Procedural walking kinematics (stride, arm counter-swing, pelvis bob/twist)
    let ws = pose.walk_speed.clamp(0.0, 1.0);
    if ws > 0.01 {
        let s = pose.walk_phase.sin();
        let c = pose.walk_phase.cos();
        l_thigh_fwd += ws * 31.0 * s;
        r_thigh_fwd -= ws * 31.0 * s;
        l_knee_bend += ws * (7.0 + 31.0 * c.max(0.0));
        r_knee_bend += ws * (7.0 + 31.0 * (-c).max(0.0));
        l_foot_pitch += ws * (10.0 * s);
        r_foot_pitch -= ws * (10.0 * s);
        pelvis_dz += ws * (1.25 * (pose.walk_phase * 2.0).cos() - 0.6);
        pelvis_twist += ws * 5.0 * s;
        torso_twist -= ws * 6.5 * s;
        torso_lean += ws * 6.0;
        l_upper_fwd += ws * 22.0 * s;
        let arm_w_scale = if weapon_kind != WeaponKind::None { 10.0 } else { 22.0 };
        r_upper_fwd += ws * arm_w_scale * s;
        head_nod += ws * (3.5 * (pose.walk_phase * 2.0).sin());
    }

    // Sound-reactive subtle hip groove
    if pose.sound_level > 0.05 {
        let sl = pose.sound_level.clamp(0.0, 1.0);
        let beat_w = pose.walk_phase.sin();
        pelvis_dz -= sl * 1.4 * beat_w.abs();
        head_nod += sl * 7.0 * beat_w.abs();
        torso_lean += sl * 4.5 * beat_w.abs();
    }

    // Goofy Autonomous Antics ("babosadas", action_id 1..=7)
    let t = pose.action_t.clamp(0.0, 1.0);
    let mut r_wrist_pitch = 0.0f32;
    let mut skirt_extra_flare = 0.0f32;
    match pose.action_id {
        1 => {
            // 1: Trip / Faceplant & shake off!
            if t < 0.22 {
                let u = t / 0.22;
                let w = (t * 45.0).sin();
                pelvis_pitch = u * 48.0;
                pelvis_dz = -u * 5.0;
                l_upper_drop = -20.0 - w * 22.0;
                r_upper_drop = 20.0 + w * 22.0;
                l_thigh_fwd = 26.0 * u;
                r_thigh_fwd = -18.0 * u;
                head_nod = -18.0 * u;
            } else if t < 0.74 {
                let flail = ((t - 0.22) * 32.0).sin();
                pelvis_pitch = 83.0;
                pelvis_dz = -16.0;
                pelvis_dy = -4.0;
                l_upper_drop = -12.0;
                r_upper_drop = 12.0;
                l_upper_fwd = -38.0 + flail * 14.0;
                r_upper_fwd = 38.0 - flail * 14.0;
                l_knee_bend = 22.0 + flail * 16.0;
                r_knee_bend = 22.0 - flail * 16.0;
                head_nod = -38.0;
                head_tilt = flail * 10.0;
                skirt_extra_flare = 0.55;
            } else {
                let u = 1.0 - (t - 0.74) / 0.26;
                pelvis_pitch = u * 72.0;
                pelvis_dz = -u * 14.0;
                l_knee_bend = u * 35.0;
                r_knee_bend = u * 25.0;
                head_nod = -u * 20.0;
                skirt_extra_flare = u * 0.3;
            }
        }
        2 => {
            // 2: Helicopter T-Pose Spin!
            let ramp = (t * 4.0).min(1.0).min(((1.0 - t) * 4.0).min(1.0));
            l_upper_drop = -34.0 * ramp + (1.0 - ramp) * l_upper_drop;
            r_upper_drop = 34.0 * ramp + (1.0 - ramp) * r_upper_drop;
            l_upper_fwd = 0.0;
            r_upper_fwd = 0.0;
            l_fore_in = 0.0;
            r_fore_in = 0.0;
            l_fore_fwd = 0.0;
            r_fore_fwd = 0.0;
            l_thigh_out = 7.0 * ramp;
            r_thigh_out = 7.0 * ramp;
            torso_lean = -5.0 * ramp;
            head_tilt = 8.0 * (t * 18.0).sin() * ramp;
            skirt_extra_flare = 0.45 * ramp;
        }
        3 => {
            // 3: Weapon Show-Off / Fire Popue / Overhead Swing!
            match weapon_kind {
                WeaponKind::Pistol | WeaponKind::Shotgun => {
                    let recoil = if (0.44..=0.64).contains(&t) {
                        ((t - 0.44) / 0.20 * std::f32::consts::PI).sin()
                    } else {
                        0.0
                    };
                    let raise = (t * 4.5).min(1.0).min(((1.0 - t) * 4.5).min(1.0));
                    r_upper_drop = 26.0 * raise + (1.0 - raise) * r_upper_drop;
                    r_upper_fwd = (70.0 - recoil * 18.0) * raise;
                    r_fore_in = 8.0 * raise;
                    r_fore_fwd = (12.0 - recoil * 10.0) * raise;
                    r_wrist_pitch = -10.0 * recoil;
                    torso_twist = 14.0 * raise;
                    torso_lean = -8.0 * recoil;
                    l_thigh_fwd = 12.0 * raise;
                    r_thigh_fwd = -12.0 * raise;
                    head_tilt = -6.0 * raise;
                }
                _ => {
                    // Dramatic wide melee swing!
                    let swing = (t * std::f32::consts::PI * 2.0).sin();
                    let raise = (t * 4.0).min(1.0).min(((1.0 - t) * 4.0).min(1.0));
                    r_upper_drop = (-38.0 + swing * 24.0) * raise;
                    r_upper_fwd = (42.0 + swing * 48.0) * raise;
                    r_fore_fwd = (28.0 + swing * 32.0) * raise;
                    r_wrist_pitch = swing * 26.0;
                    torso_twist = swing * 18.0;
                    torso_lean = 10.0 * raise;
                    l_thigh_fwd = 15.0 * raise;
                    r_thigh_fwd = -15.0 * raise;
                    skirt_extra_flare = 0.4 * swing.abs();
                }
            }
        }
        4 => {
            // 4: Backflip 360!
            if t < 0.16 {
                let u = t / 0.16;
                pelvis_dz = -6.5 * u;
                torso_lean = 22.0 * u;
                l_thigh_fwd = 48.0 * u;
                r_thigh_fwd = 48.0 * u;
                l_knee_bend = 84.0 * u;
                r_knee_bend = 84.0 * u;
                l_upper_fwd = 42.0 * u;
                r_upper_fwd = -42.0 * u;
            } else if t < 0.84 {
                let u = (t - 0.16) / 0.68;
                pelvis_pitch = -360.0 * u;
                let tuck = (u * std::f32::consts::PI).sin();
                l_thigh_fwd = 65.0 * tuck;
                r_thigh_fwd = 65.0 * tuck;
                l_knee_bend = 105.0 * tuck;
                r_knee_bend = 105.0 * tuck;
                l_upper_drop = -10.0;
                r_upper_drop = 10.0;
                skirt_extra_flare = 0.95 * tuck;
            } else {
                let u = 1.0 - (t - 0.84) / 0.16;
                pelvis_dz = -5.5 * u;
                l_thigh_fwd = 38.0 * u;
                r_thigh_fwd = 38.0 * u;
                l_knee_bend = 70.0 * u;
                r_knee_bend = 70.0 * u;
                skirt_extra_flare = 0.7 * u;
            }
        }
        5 => {
            // 5: Dramatic Idol / JoJo Pose!
            let ramp = (t * 4.5).min(1.0).min(((1.0 - t) * 4.5).min(1.0));
            pelvis_roll = 8.0 * ramp;
            torso_lean = -14.0 * ramp;
            torso_twist = -18.0 * ramp;
            l_upper_drop = -42.0 * ramp + (1.0 - ramp) * l_upper_drop;
            l_upper_fwd = -36.0 * ramp;
            l_fore_fwd = -28.0 * ramp;
            r_upper_drop = -34.0 * ramp;
            r_fore_in = -28.0 * ramp;
            r_fore_fwd = 48.0 * ramp;
            l_thigh_fwd = 18.0 * ramp;
            r_thigh_fwd = -14.0 * ramp;
            l_thigh_out = 8.0 * ramp;
            head_tilt = 14.0 * ramp;
            head_turn = 18.0 * ramp;
            skirt_extra_flare = 0.35 * ramp;
        }
        6 => {
            // 6: Sit & kick feet playfully!
            let ramp = (t * 5.0).min(1.0).min(((1.0 - t) * 5.0).min(1.0));
            let kick = ((t * 16.0).sin()) * ramp;
            pelvis_dz = -13.0 * ramp;
            torso_lean = -8.0 * ramp;
            l_thigh_fwd = 82.0 * ramp;
            r_thigh_fwd = 82.0 * ramp;
            l_thigh_out = 5.0 * ramp;
            r_thigh_out = 5.0 * ramp;
            l_knee_bend = (76.0 + kick * 22.0) * ramp;
            r_knee_bend = (76.0 - kick * 22.0) * ramp;
            l_upper_drop = 22.0;
            r_upper_drop = -22.0;
            l_upper_fwd = 14.0 * ramp;
            r_upper_fwd = -14.0 * ramp;
            head_tilt = 7.0 * (t * 6.0).sin() * ramp;
            skirt_extra_flare = 0.55 * ramp;
        }
        7 => {
            // 7: Happy Caramelldansen-ish Idol Dance!
            let w = t * 24.0;
            let sw = w.sin();
            let cw = w.cos();
            let ramp = (t * 5.0).min(1.0).min(((1.0 - t) * 5.0).min(1.0));
            pelvis_roll = 10.0 * sw * ramp;
            pelvis_dz = -2.2 * cw.abs() * ramp;
            l_upper_drop = -38.0 * ramp;
            r_upper_drop = 38.0 * ramp;
            l_upper_fwd = -18.0 * ramp;
            r_upper_fwd = 18.0 * ramp;
            l_fore_fwd = (-48.0 + 18.0 * sw) * ramp;
            r_fore_fwd = (48.0 + 18.0 * sw) * ramp;
            l_thigh_fwd = 16.0 * sw.max(0.0) * ramp;
            r_thigh_fwd = 16.0 * (-sw).max(0.0) * ramp;
            l_knee_bend = 24.0 * cw.max(0.0) * ramp;
            r_knee_bend = 24.0 * (-cw).max(0.0) * ramp;
            head_tilt = 12.0 * sw * ramp;
            head_nod = 8.0 * cw * ramp;
            skirt_extra_flare = 0.45 * ramp;
        }
        _ => {}
    }

    let apply_world_rot = |local_deltas: &mut Vec<Mat34>, bone_opt: Option<usize>, axis: [f32; 3], deg_val: f32| {
        if deg_val.abs() < 0.01 {
            return;
        }
        if let Some(bi) = bone_opt {
            let pivot = [
                model.bones[bi].fk_bind.0[0][3],
                model.bones[bi].fk_bind.0[1][3],
                model.bones[bi].fk_bind.0[2][3],
            ];
            let rot = Mat34::from_axis_angle_around(axis, deg_val * deg, pivot);
            local_deltas[bi] = local_deltas[bi].mul(&rot);
        }
    };

    // Pelvis root translation + orientation
    if let Some(pi) = model.b_pelvis {
        if pelvis_dy.abs() > 0.01 || pelvis_dz.abs() > 0.01 {
            local_deltas[pi].0[1][3] += pelvis_dy;
            local_deltas[pi].0[2][3] += pelvis_dz;
        }
        apply_world_rot(&mut local_deltas, Some(pi), [1.0, 0.0, 0.0], pelvis_pitch);
        apply_world_rot(&mut local_deltas, Some(pi), [0.0, 1.0, 0.0], pelvis_roll);
        apply_world_rot(&mut local_deltas, Some(pi), [0.0, 0.0, 1.0], pelvis_twist);
    }

    // Legs (in world bind pose: -angle around +X swings leg forward -Y; +angle around +X bends knee backward)
    apply_world_rot(&mut local_deltas, model.b_l_thigh, [1.0, 0.0, 0.0], -l_thigh_fwd);
    apply_world_rot(&mut local_deltas, model.b_l_thigh, [0.0, 1.0, 0.0], -l_thigh_out);
    apply_world_rot(&mut local_deltas, model.b_l_calf, [1.0, 0.0, 0.0], l_knee_bend);
    apply_world_rot(&mut local_deltas, model.b_l_foot, [1.0, 0.0, 0.0], l_foot_pitch);

    apply_world_rot(&mut local_deltas, model.b_r_thigh, [1.0, 0.0, 0.0], -r_thigh_fwd);
    apply_world_rot(&mut local_deltas, model.b_r_thigh, [0.0, 1.0, 0.0], r_thigh_out);
    apply_world_rot(&mut local_deltas, model.b_r_calf, [1.0, 0.0, 0.0], r_knee_bend);
    apply_world_rot(&mut local_deltas, model.b_r_foot, [1.0, 0.0, 0.0], r_foot_pitch);

    // Torso, arms, head
    apply_world_rot(&mut local_deltas, model.b_spine2, [1.0, 0.0, 0.0], torso_lean);
    apply_world_rot(&mut local_deltas, model.b_spine2, [0.0, 0.0, 1.0], torso_twist);

    apply_world_rot(&mut local_deltas, model.b_l_upper, [0.0, 1.0, 0.0], l_upper_drop);
    apply_world_rot(&mut local_deltas, model.b_l_upper, [0.0, 0.0, 1.0], l_upper_fwd);
    apply_world_rot(&mut local_deltas, model.b_l_fore, [0.0, 1.0, 0.0], l_fore_in);
    apply_world_rot(&mut local_deltas, model.b_l_fore, [0.0, 0.0, 1.0], l_fore_fwd);

    apply_world_rot(&mut local_deltas, model.b_r_upper, [0.0, 1.0, 0.0], r_upper_drop);
    apply_world_rot(&mut local_deltas, model.b_r_upper, [0.0, 0.0, 1.0], r_upper_fwd);
    apply_world_rot(&mut local_deltas, model.b_r_fore, [0.0, 1.0, 0.0], r_fore_in);
    apply_world_rot(&mut local_deltas, model.b_r_fore, [0.0, 0.0, 1.0], r_fore_fwd);
    apply_world_rot(&mut local_deltas, model.b_r_hand, [1.0, 0.0, 0.0], r_wrist_pitch);

    apply_world_rot(&mut local_deltas, model.b_head, [0.0, 1.0, 0.0], head_tilt);
    apply_world_rot(&mut local_deltas, model.b_head, [1.0, 0.0, 0.0], head_nod);
    apply_world_rot(&mut local_deltas, model.b_head, [0.0, 0.0, 1.0], head_turn);

    // === Skirt Secondary Physics (Spring-Damper + Centrifugal + Leg Collision Flare) ===
    let spin_abs = pose.spin_vel.abs().min(3.5);
    let sx_lag = pose.skirt_lag_x.clamp(-1.4, 1.4);
    let sy_lag = pose.skirt_lag_y.clamp(-1.4, 1.4);
    for sb in &model.skirt_bones {
        let radial_deg = compute_skirt_radial_deg(
            sb.radial_x,
            sb.radial_y,
            sb.phase,
            sb.depth,
            pose,
            ws,
            skirt_extra_flare,
            l_thigh_fwd,
            r_thigh_fwd,
            l_thigh_out,
            r_thigh_out,
        );
        apply_world_rot(
            &mut local_deltas,
            Some(sb.idx),
            [sb.tangent_x, sb.tangent_y, 0.0],
            radial_deg,
        );

        let chain_scale = match sb.depth {
            1 => 0.72,
            2 => 0.48,
            _ => 0.34,
        };
        // Tangential cloth whip perpendicular to radial plane
        let tang_drag = (sx_lag * sb.tangent_x + sy_lag * sb.tangent_y) * 16.0
            + pose.spin_vel.clamp(-2.5, 2.5) * -14.0;
        let tang_deg = (tang_drag * chain_scale).clamp(-26.0, 26.0);
        apply_world_rot(
            &mut local_deltas,
            Some(sb.idx),
            [sb.radial_x, sb.radial_y, 0.0],
            tang_deg,
        );
    }

    // === Hair / Twintails / Ponytail Secondary Physics ===
    let hx_lag = pose.hair_lag_x.clamp(-1.6, 1.6);
    let hy_lag = pose.hair_lag_y.clamp(-1.6, 1.6);
    let hz_bounce = pose.hair_bounce_z.clamp(-1.4, 1.4);
    let hair_mag = (hx_lag * hx_lag + hy_lag * hy_lag).sqrt();
    for hb in &model.hair_bones {
        let d = hb.depth as f32;
        let seg_gain = if hb.is_bangs {
            0.22
        } else if hb.is_front_lock {
            (0.32 + 0.14 * (d - 1.0)).min(0.62)
        } else {
            (0.46 + 0.24 * (d - 1.0)).min(1.22)
        };

        // 1. Directional velocity/drag deflection
        if hair_mag > 0.015 {
            let drag_deg = (hair_mag * 28.0 * seg_gain).clamp(0.0, if hb.is_bangs { 9.0 } else { 42.0 });
            apply_world_rot(&mut local_deltas, Some(hb.idx), [hy_lag, -hx_lag, 0.0], drag_deg);
        }

        // 2. Vertical air-bounce + centrifugal spin lift + leg/breeze ripple
        let wave = (pose.walk_phase * 2.0 - d * 0.65 + hb.phase).sin()
            * (2.4 + 8.0 * ws + 7.5 * pose.sound_level.clamp(0.0, 1.0));
        let raw_lift = hz_bounce * 26.0 + spin_abs * 24.0 + wave;
        let lift_deg = if hb.is_bangs {
            (raw_lift * seg_gain).clamp(-3.0, 10.0)
        } else if hb.is_front_lock {
            (raw_lift * seg_gain).clamp(-2.0, 26.0)
        } else {
            (raw_lift * seg_gain).clamp(-22.0, 48.0)
        };
        apply_world_rot(&mut local_deltas, Some(hb.idx), [hb.out_y, -hb.out_x, 0.0], lift_deg);
    }

    // Propagate deltas hierarchically: world_delta[b] = world_delta[parent] * local_deltas[b]
    let mut world_deltas = vec![Mat34::identity(); nb];
    for bi in 0..nb {
        let p = model.bones[bi].parent;
        world_deltas[bi] = if p >= 0 && (p as usize) < bi {
            world_deltas[p as usize].mul(&local_deltas[bi])
        } else {
            local_deltas[bi]
        };
    }

    let _ = (model.b_neck, model.b_l_clav, model.b_r_clav, model.b_l_hand);
    PoseEval {
        bone_mats: world_deltas,
        l_thigh_fwd,
        r_thigh_fwd,
        l_thigh_out,
        r_thigh_out,
        skirt_extra_flare,
        walk_speed: ws,
    }
}

#[inline]
fn edge_fn(ax: f32, ay: f32, bx: f32, by: f32, cx: f32, cy: f32) -> f32 {
    (cx - ax) * (by - ay) - (cy - ay) * (bx - ax)
}

#[derive(Clone, Copy)]
struct ProjVert {
    sx: f32,
    sy: f32,
    inv_z: f32,
    nx: f32,
    ny: f32,
    nz: f32,
    u: f32,
    v: f32,
}

#[inline]
fn shade_cel_color(
    base: [f32; 4],
    nx: f32,
    ny: f32,
    nz: f32,
    facing_sign: f32,
    light_dir: [f32; 3],
    basewarp: &[[f32; 3]],
    unlit: bool,
) -> u32 {
    let a8 = base[3].round().clamp(0.0, 255.0) as u32;
    if unlit {
        let r = base[0].round().clamp(0.0, 255.0) as u32;
        let g = base[1].round().clamp(0.0, 255.0) as u32;
        let bcol = base[2].round().clamp(0.0, 255.0) as u32;
        return r | (g << 8) | (bcol << 16) | (a8 << 24);
    }
    let ndotl = (nx * light_dir[0] + ny * light_dir[1] + nz * light_dir[2]) * facing_sign;
    let half_lambert = (ndotl * 0.5 + 0.5).clamp(0.0, 1.0);
    let bw_len = basewarp.len();
    let (lr, lg, lb) = if bw_len > 0 {
        let bi = ((half_lambert * (bw_len - 1) as f32).round() as usize).min(bw_len - 1);
        let wcol = basewarp[bi];
        (0.56 + 0.48 * wcol[0], 0.56 + 0.48 * wcol[1], 0.56 + 0.48 * wcol[2])
    } else {
        let lit = 0.60 + 0.42 * half_lambert;
        (lit, lit, lit)
    };
    let rim = (1.0 - ny.abs()).clamp(0.0, 1.0).powi(3) * 0.08;
    let r = ((base[0] * lr + 255.0 * rim).clamp(0.0, 255.0)) as u32;
    let g = ((base[1] * lg + 245.0 * rim).clamp(0.0, 255.0)) as u32;
    let bcol = ((base[2] * lb + 250.0 * rim).clamp(0.0, 255.0)) as u32;
    r | (g << 8) | (bcol << 16) | (a8 << 24)
}

fn project_world_pt(
    wx: f32,
    wy: f32,
    wz: f32,
    wnx: f32,
    wny: f32,
    wnz: f32,
    u: f32,
    v: f32,
    center_z: f32,
    sy: f32,
    cy: f32,
    sp: f32,
    cp: f32,
    cam_dist: f32,
    fov_scale: f32,
    cx_px: f32,
    cy_px: f32,
) -> ProjVert {
    let rx = wx * cy - wy * sy;
    let ry0 = wx * sy + wy * cy;
    let dz = wz - center_z;
    let ry = ry0 * cp - dz * sp;
    let rz = ry0 * sp + dz * cp;

    let depth = cam_dist + ry;
    let inv_z = if depth > 1.0 { 1.0 / depth } else { 1.0 };
    let sx = cx_px + rx * fov_scale * inv_z;
    let sy_p = cy_px - rz * fov_scale * inv_z;

    let rnx = wnx * cy - wny * sy;
    let rny0 = wnx * sy + wny * cy;
    let rny = rny0 * cp - wnz * sp;
    let rnz = rny0 * sp + wnz * cp;
    let nlen = (rnx * rnx + rny * rny + rnz * rnz).sqrt().max(1e-5);

    ProjVert {
        sx,
        sy: sy_p,
        inv_z,
        nx: rnx / nlen,
        ny: rny / nlen,
        nz: rnz / nlen,
        u,
        v,
    }
}

pub fn render_l4d2_doki(
    model: &LoadedDokiModel,
    character_id: &str,
    mood_id: &str,
    blink: bool,
    variant: u32,
    target_height: i32,
    yaw_deg: f32,
    pitch_deg: f32,
) -> Result<RenderedFrame, String> {
    let pose = PoseParams3D::default();
    render_l4d2_doki_with_pose(
        model,
        character_id,
        mood_id,
        blink,
        variant,
        target_height,
        yaw_deg,
        pitch_deg,
        &pose,
    )
}

#[inline]
fn rasterize_tri<F>(
    v0: &ProjVert,
    v1: &ProjVert,
    v2: &ProjVert,
    ss_w: usize,
    ss_h: usize,
    depth_buf: &mut [f32],
    color_buf: &mut [u32],
    mut shade_pixel: F,
) where
    F: FnMut(&ProjVert, &ProjVert, &ProjVert, f32, f32, f32, f32, f32) -> Option<u32>,
{
    let area = edge_fn(v0.sx, v0.sy, v1.sx, v1.sy, v2.sx, v2.sy);
    if area.abs() < 0.02 {
        return;
    }
    let (a, b, c, inv_area, facing_sign) = if area > 0.0 {
        (v0, v1, v2, 1.0 / area, 1.0f32)
    } else {
        (v0, v2, v1, -1.0 / area, -1.0f32)
    };

    let min_x = a.sx.min(b.sx).min(c.sx).floor().max(0.0) as usize;
    let max_x = a.sx.max(b.sx).max(c.sx).ceil().min((ss_w - 1) as f32) as usize;
    let min_y = a.sy.min(b.sy).min(c.sy).floor().max(0.0) as usize;
    let max_y = a.sy.max(b.sy).max(c.sy).ceil().min((ss_h - 1) as f32) as usize;
    if min_x > max_x || min_y > max_y {
        return;
    }

    for py in min_y..=max_y {
        let pcy = py as f32 + 0.5;
        let mut e0 = edge_fn(b.sx, b.sy, c.sx, c.sy, min_x as f32 + 0.5, pcy);
        let mut e1 = edge_fn(c.sx, c.sy, a.sx, a.sy, min_x as f32 + 0.5, pcy);
        let mut e2 = edge_fn(a.sx, a.sy, b.sx, b.sy, min_x as f32 + 0.5, pcy);
        let de0 = c.sy - b.sy;
        let de1 = a.sy - c.sy;
        let de2 = b.sy - a.sy;
        let row_off = py * ss_w;
        for px in min_x..=max_x {
            if e0 >= -1e-4 && e1 >= -1e-4 && e2 >= -1e-4 {
                let w0 = e0 * inv_area;
                let w1 = e1 * inv_area;
                let w2 = e2 * inv_area;
                let inv_z = w0 * a.inv_z + w1 * b.inv_z + w2 * c.inv_z;
                let idx = row_off + px;
                if inv_z > depth_buf[idx] {
                    if let Some(col) = shade_pixel(a, b, c, w0, w1, w2, inv_z, facing_sign) {
                        depth_buf[idx] = inv_z;
                        color_buf[idx] = col;
                    }
                }
            }
            e0 += de0;
            e1 += de1;
            e2 += de2;
        }
    }
}

pub fn render_l4d2_doki_with_pose(
    model: &LoadedDokiModel,
    character_id: &str,
    mood_id: &str,
    blink: bool,
    variant: u32,
    target_height: i32,
    yaw_deg: f32,
    pitch_deg: f32,
    pose: &PoseParams3D,
) -> Result<RenderedFrame, String> {
    let target_h = target_height.clamp(64, 900) as usize;
    let weapon_kind = resolve_weapon_kind(&pose.weapon_id, character_id, variant);
    let pad_factor = if weapon_kind != WeaponKind::None || pose.action_id != 0 {
        1.32f32
    } else {
        0.92f32
    };
    let pad_w = ((target_h as f32 * pad_factor).round() as usize).max(64);
    let ss_w = pad_w * 2;
    let ss_h = target_h * 2;

    let pose_eval = compute_pose_deltas(model, character_id, mood_id, variant, pose, weapon_kind);
    let bone_mats = &pose_eval.bone_mats;
    let use_virtual_skirt = model.skirt_bones.is_empty();
    let deg_to_rad = std::f32::consts::PI / 180.0;

    let thigh_seg = |b_th: Option<usize>, b_cf: Option<usize>| -> Option<([f32; 3], [f32; 3])> {
        let ti = b_th?;
        let ci = b_cf?;
        let a0 = [
            model.bones[ti].fk_bind.0[0][3],
            model.bones[ti].fk_bind.0[1][3],
            model.bones[ti].fk_bind.0[2][3],
        ];
        let b0 = [
            model.bones[ci].fk_bind.0[0][3],
            model.bones[ci].fk_bind.0[1][3],
            model.bones[ci].fk_bind.0[2][3],
        ];
        Some((bone_mats[ti].transform_point(a0), bone_mats[ci].transform_point(b0)))
    };
    let l_thigh_seg = thigh_seg(model.b_l_thigh, model.b_l_calf);
    let r_thigh_seg = thigh_seg(model.b_r_thigh, model.b_r_calf);

    let center_z = 0.5 * (model.min_z + model.max_z);
    let model_span_z = (model.max_z - model.min_z).max(10.0);

    let yaw = yaw_deg * (std::f32::consts::PI / 180.0);
    let pitch = pitch_deg * (std::f32::consts::PI / 180.0);
    let (sy, cy) = yaw.sin_cos();
    let (sp, cp) = pitch.sin_cos();

    let cam_dist = model_span_z * 2.55;
    let fov_scale = (ss_h as f32 * 0.87) * (cam_dist / model_span_z);
    let cx_px = ss_w as f32 * 0.5;
    let cy_px = ss_h as f32 * 0.51;

    let lx = -0.22f32;
    let ly = -0.89f32;
    let lz = 0.39f32;
    let llen = (lx * lx + ly * ly + lz * lz).sqrt();
    let light_dir = [lx / llen, ly / llen, lz / llen];

    let mut projected = Vec::with_capacity(model.vertices.len());
    for (vi, v) in model.vertices.iter().enumerate() {
        let v_t = model.skirt_vert_t.get(vi).copied().unwrap_or(-1.0);
        let mut lpos = v.pos;
        let mut lnorm = v.norm;
        if use_virtual_skirt && v_t > 0.02 {
            let dx = lpos[0] - model.skirt_pelvis_center[0];
            let dy = lpos[1] - model.skirt_pelvis_center[1];
            let dz = lpos[2] - model.skirt_pelvis_center[2];
            let len = (dx * dx + dy * dy).sqrt();
            let (ux, uy) = if len > 0.25 {
                (dx / len, dy / len)
            } else {
                (0.0, -1.0)
            };
            let radial_deg = compute_skirt_radial_deg(
                ux,
                uy,
                uy.atan2(ux),
                1,
                pose,
                pose_eval.walk_speed,
                pose_eval.skirt_extra_flare,
                pose_eval.l_thigh_fwd,
                pose_eval.r_thigh_fwd,
                pose_eval.l_thigh_out,
                pose_eval.r_thigh_out,
            );
            let ang = radial_deg * deg_to_rad * smoothstep01(0.06, 0.34, v_t);
            let (sa, ca) = ang.sin_cos();
            let (tx, ty) = (uy, -ux);
            // Cross product t x [dx, dy, dz] where tz = 0: [-ty*dz, tx*dz, tx*dy - ty*dx]
            let cx = -ty * dz;
            let cy_v = tx * dz;
            let cz = tx * dy - ty * dx;
            let rdx = dx * ca + cx * sa;
            let rdy = dy * ca + cy_v * sa;
            let rdz = dz * ca + cz * sa;
            lpos = [
                model.skirt_pelvis_center[0] + rdx,
                model.skirt_pelvis_center[1] + rdy,
                model.skirt_pelvis_center[2] + rdz,
            ];
            // Rotate normal around t as well (Rodrigues general formula with tz = 0)
            let ndott = lnorm[0] * tx + lnorm[1] * ty;
            let ncx = -ty * lnorm[2];
            let ncy = tx * lnorm[2];
            let ncz = tx * lnorm[1] - ty * lnorm[0];
            lnorm = [
                lnorm[0] * ca + ncx * sa + tx * ndott * (1.0 - ca),
                lnorm[1] * ca + ncy * sa + ty * ndott * (1.0 - ca),
                lnorm[2] * ca + ncz * sa,
            ];
        }

        let nb = v.num_bones.min(3) as usize;
        let mut wx = 0.0f32;
        let mut wy = 0.0f32;
        let mut wz = 0.0f32;
        let mut wnx = 0.0f32;
        let mut wny = 0.0f32;
        let mut wnz = 0.0f32;
        if nb == 0 {
            wx = lpos[0];
            wy = lpos[1];
            wz = lpos[2];
            wnx = lnorm[0];
            wny = lnorm[1];
            wnz = lnorm[2];
        } else {
            for k in 0..nb {
                let w = v.weights[k];
                if w <= 1e-5 {
                    continue;
                }
                let bi = (v.bones[k] as usize).min(bone_mats.len() - 1);
                let m = &bone_mats[bi];
                let tp = m.transform_point(lpos);
                let tn = m.transform_vec(lnorm);
                wx += tp[0] * w;
                wy += tp[1] * w;
                wz += tp[2] * w;
                wnx += tn[0] * w;
                wny += tn[1] * w;
                wnz += tn[2] * w;
            }
        }

        if v_t >= 0.14 {
            let mask = smoothstep01(0.14, 0.42, v_t);
            let r_band = 2.75 + 1.07 * smoothstep01(0.18, 0.48, v_t);
            for thigh_opt in [l_thigh_seg, r_thigh_seg] {
                if let Some((a, b)) = thigh_opt {
                    let abx = b[0] - a[0];
                    let aby = b[1] - a[1];
                    let abz = b[2] - a[2];
                    let ab2 = abx * abx + aby * aby + abz * abz;
                    if ab2 > 1.0 {
                        let apx = wx - a[0];
                        let apy = wy - a[1];
                        let apz = wz - a[2];
                        let proj_t = (apx * abx + apy * aby + apz * abz) / ab2;
                        if proj_t > 0.06 && proj_t < 1.04 {
                            let tc = proj_t.clamp(0.10, 1.0);
                            let qx = a[0] + abx * tc;
                            let qy = a[1] + aby * tc;
                            let qz = a[2] + abz * tc;
                            let r_min = r_band - 0.35 * tc;
                            let dx = wx - qx;
                            let dy = wy - qy;
                            let dz = wz - qz;
                            let d = (dx * dx + dy * dy + dz * dz).sqrt();
                            if d < r_min && d > 1e-4 {
                                let push = (r_min - d) * mask;
                                let ox = dx / d;
                                let oy = dy / d;
                                let oz = dz / d;
                                wx += ox * push;
                                wy += oy * push;
                                wz += oz * push;
                                wnx += ox * 0.35 * mask;
                                wny += oy * 0.35 * mask;
                                wnz += oz * 0.35 * mask;
                            }
                        }
                    }
                }
            }
        }

        projected.push(project_world_pt(
            wx, wy, wz, wnx, wny, wnz, v.uv[0], v.uv[1], center_z, sy, cy, sp, cp, cam_dist,
            fov_scale, cx_px, cy_px,
        ));
    }

    let mut depth_buf = vec![0.0f32; ss_w * ss_h];
    let mut color_buf = vec![0u32; ss_w * ss_h];

    for mesh in &model.meshes {
        let tex = &model.textures[mesh.tex_index];
        let eye_blink_dim = blink && mesh.is_eye_or_face;
        let alpha_cutoff = if mesh.is_eye_or_face { 42.0 } else { 88.0 };

        for tri in &mesh.tris {
            let v0 = &projected[tri[0] as usize];
            let v1 = &projected[tri[1] as usize];
            let v2 = &projected[tri[2] as usize];
            rasterize_tri(
                v0,
                v1,
                v2,
                ss_w,
                ss_h,
                &mut depth_buf,
                &mut color_buf,
                |a, b, c, w0, w1, w2, inv_z, facing_sign| {
                    let z = 1.0 / inv_z;
                    let u = (w0 * a.u * a.inv_z + w1 * b.u * b.inv_z + w2 * c.u * c.inv_z) * z;
                    let v = (w0 * a.v * a.inv_z + w1 * b.v * b.inv_z + w2 * c.v * c.inv_z) * z;
                    let sample = tex.sample_bilinear(u, v);
                    if sample[3] < alpha_cutoff {
                        return None;
                    }
                    let nx = w0 * a.nx + w1 * b.nx + w2 * c.nx;
                    let ny = w0 * a.ny + w1 * b.ny + w2 * c.ny;
                    let nz = w0 * a.nz + w1 * b.nz + w2 * c.nz;
                    let mut col = shade_cel_color(
                        sample,
                        nx,
                        ny,
                        nz,
                        facing_sign,
                        light_dir,
                        &model.basewarp,
                        false,
                    );
                    if eye_blink_dim && sample[0] < 180.0 && sample[1] < 180.0 && sample[2] < 180.0 {
                        let r = (((col & 0xff) * 3 + 245) / 4).min(255);
                        let g = ((((col >> 8) & 0xff) * 3 + 215) / 4).min(255);
                        let bcol = ((((col >> 16) & 0xff) * 3 + 210) / 4).min(255);
                        let a8 = (col >> 24) & 0xff;
                        col = r | (g << 8) | (bcol << 16) | (a8 << 24);
                    }
                    Some(col)
                },
            );
        }
    }

    // Render held 3D weapon directly in the same Z-buffer
    if weapon_kind != WeaponKind::None {
        let attach_idx = model.b_weapon.or(model.b_r_hand).unwrap_or(0).min(bone_mats.len() - 1);
        let hand_mat = bone_mats[attach_idx].mul(&model.weapon_grip_mat);
        let show_flash = pose.action_id == 3 && (0.40..=0.66).contains(&pose.action_t);
        let wtris = build_weapon_mesh(weapon_kind, show_flash);
        for wt in &wtris {
            let wn = hand_mat.transform_vec(wt.norm);
            let mut pv = [ProjVert {
                sx: 0.0, sy: 0.0, inv_z: 1.0, nx: 0.0, ny: 0.0, nz: 1.0, u: 0.0, v: 0.0,
            }; 3];
            for k in 0..3 {
                let wp = hand_mat.transform_point(wt.pos[k]);
                pv[k] = project_world_pt(
                    wp[0], wp[1], wp[2], wn[0], wn[1], wn[2], 0.0, 0.0, center_z, sy, cy, sp,
                    cp, cam_dist, fov_scale, cx_px, cy_px,
                );
            }
            let base = [
                wt.color[0] as f32,
                wt.color[1] as f32,
                wt.color[2] as f32,
                wt.color[3] as f32,
            ];
            let unlit = wt.unlit;
            rasterize_tri(
                &pv[0],
                &pv[1],
                &pv[2],
                ss_w,
                ss_h,
                &mut depth_buf,
                &mut color_buf,
                |a, _b, _c, _w0, _w1, _w2, _inv_z, facing_sign| {
                    Some(shade_cel_color(
                        base,
                        a.nx,
                        a.ny,
                        a.nz,
                        facing_sign,
                        light_dir,
                        &model.basewarp,
                        unlit,
                    ))
                },
            );
        }
    }

    // Downsample 2x2 to target height, measure horizontal bounds & ground-align vertically
    let mut down = vec![[0u8; 4]; pad_w * target_h];
    let mut min_x = pad_w;
    let mut max_x = 0usize;
    let mut max_y = 0usize;

    for y in 0..target_h {
        for x in 0..pad_w {
            let s00 = color_buf[(y * 2) * ss_w + (x * 2)];
            let s10 = color_buf[(y * 2) * ss_w + (x * 2 + 1)];
            let s01 = color_buf[(y * 2 + 1) * ss_w + (x * 2)];
            let s11 = color_buf[(y * 2 + 1) * ss_w + (x * 2 + 1)];
            let mut pr = 0u32;
            let mut pg = 0u32;
            let mut pb = 0u32;
            let mut pa = 0u32;
            for s in [s00, s10, s01, s11] {
                let a = (s >> 24) & 0xff;
                if a > 0 {
                    let r = s & 0xff;
                    let g = (s >> 8) & 0xff;
                    let b = (s >> 16) & 0xff;
                    pr += r * a;
                    pg += g * a;
                    pb += b * a;
                    pa += a * 255;
                }
            }
            let a_out = ((pa + 510) / 1020).min(255) as u8;
            if a_out > 4 {
                min_x = min_x.min(x);
                max_x = max_x.max(x);
                max_y = max_y.max(y);
                let b_pm = ((pb + 510) / 1020).min(a_out as u32) as u8;
                let g_pm = ((pg + 510) / 1020).min(a_out as u32) as u8;
                let r_pm = ((pr + 510) / 1020).min(a_out as u32) as u8;
                down[y * pad_w + x] = [b_pm, g_pm, r_pm, a_out];
            }
        }
    }

    if min_x > max_x {
        min_x = 0;
        max_x = pad_w - 1;
    } else if pose.keep_center_anchor {
        let mid = pad_w / 2;
        let reach_l = mid.saturating_sub(min_x);
        let reach_r = max_x.saturating_sub(mid);
        let half = (reach_l.max(reach_r) + 6).max(8);
        min_x = mid.saturating_sub(half);
        max_x = (mid + half).min(pad_w - 1);
    } else {
        min_x = min_x.saturating_sub(6);
        max_x = (max_x + 6).min(pad_w - 1);
    }

    let out_w = (max_x - min_x + 1).max(16);
    let out_h = target_h;
    let y_shift = out_h.saturating_sub(1).saturating_sub(max_y);

    let mut data = vec![0u8; out_w * out_h * 4];
    for y in 0..=max_y.min(out_h - 1) {
        let dst_y = y + y_shift;
        if dst_y >= out_h {
            continue;
        }
        for x in 0..out_w {
            let px = down[y * pad_w + (min_x + x)];
            let off = (dst_y * out_w + x) * 4;
            data[off] = px[0];
            data[off + 1] = px[1];
            data[off + 2] = px[2];
            data[off + 3] = px[3];
        }
    }

    Ok(RenderedFrame {
        data,
        width: out_w as i32,
        height: out_h as i32,
        stride: (out_w * 4) as i32,
    })
}
