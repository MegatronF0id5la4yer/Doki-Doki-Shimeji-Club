use crate::compositor::CropBox;
use crate::config::normalize_character_id;
use std::ffi::CString;
use std::path::{Path, PathBuf};

pub const CHARACTERS: &[(&str, &str)] = &[
    ("natsuki", "Natsuki"),
    ("monika", "Monika"),
    ("sayori", "Sayori"),
    ("yuri", "Yuri"),
];

#[derive(Debug, Clone)]
pub struct CharacterEntry {
    pub id: &'static str,
    pub id_c: CString,
    pub label_c: CString,
}

pub fn build_characters() -> Vec<CharacterEntry> {
    CHARACTERS
        .iter()
        .map(|(id, label)| CharacterEntry {
            id: *id,
            id_c: CString::new(*id).unwrap(),
            label_c: CString::new(*label).unwrap(),
        })
        .collect()
}

#[derive(Debug, Clone)]
pub struct MoodEntry {
    pub id: CString,
    pub label: CString,
}

#[derive(Debug, Clone)]
pub enum StyleKind {
    Salvation {
        outfit_dir: &'static str,
        face_dir: &'static str,
    },
    SalvationSingleBody {
        outfit_dir: &'static str,
        body_candidates: &'static [&'static str],
        face_dir: &'static str,
    },
    VanillaLayered {
        body_dir: &'static str,
        face_dir: &'static str,
        left_tpl: &'static str,  // "{1}l.png" / "{2}l.png" or custom
        left2_tpl: &'static str,
        right_tpl: &'static str,
        right2_tpl: &'static str,
        face_prefix: &'static str,
        tall_crop: bool,
        wide_crop: bool,
    },
    VanillaCasualT {
        // Natsuki vanilla casual (1t/2t + 2ta..2ti)
    },
    VanillaSinglePose {
        rel_primary: &'static str,
        rel_alt: &'static str,
        wide_crop: bool,
    },
    NewEyes {
        prefix: &'static str,
    },
    JustNatsuki {
        clothes: &'static str,
        hair: &'static str,
        accessory: &'static str,
        headgear: &'static str,
        necklace: &'static str,
        back: &'static str,
    },
    MasSitting {
        clothes: &'static str,
    },
    L4d2ThreeD,
}

#[derive(Debug, Clone)]
pub struct StyleEntry {
    pub character_id: &'static str,
    pub id: &'static str,
    pub id_c: CString,
    pub label_c: CString,
    pub kind: StyleKind,
}

pub const MOODS: &[(&str, &str)] = &[
    ("neutral", "Neutral"),
    ("happy", "Feliz"),
    ("tsun", "Tsundere"),
    ("angry", "Enojada"),
    ("shy", "Timida"),
    ("blush", "Sonrojada"),
    ("smug", "Presumida"),
    ("surprised", "Sorprendida"),
    ("sad", "Triste"),
    ("excited", "Emocionada"),
    ("thinking", "Pensativa"),
    ("sleepy", "Con sueno"),
    ("laugh", "Riendo"),
    ("cheeky", "Burlona"),
];

pub fn build_moods() -> Vec<MoodEntry> {
    MOODS
        .iter()
        .map(|(id, label)| MoodEntry {
            id: CString::new(*id).unwrap(),
            label: CString::new(*label).unwrap(),
        })
        .collect()
}

pub fn normalize_mood(mood: &str) -> &'static str {
    let m = mood.trim().to_ascii_lowercase();
    for &(id, _) in MOODS {
        if m == id {
            return id;
        }
    }
    match m.as_str() {
        "pout" | "annoyed" | "tsundere" => "tsun",
        "embarrassed" | "flustered" => "blush",
        "joy" | "smile" | "cheerful" => "happy",
        "shocked" | "amazed" => "surprised",
        "tired" | "bored" => "sleepy",
        "teasing" | "playful" => "cheeky",
        _ => "neutral",
    }
}

pub fn char_dir(root: &Path, character_id: &str) -> PathBuf {
    let cid = normalize_character_id(character_id);
    let sub = match cid {
        "monika" => "Monika",
        "sayori" => "Sayori",
        "yuri" => "Yuri",
        _ => "Natsuki",
    };
    let candidate = root.join(sub);
    if candidate.is_dir() {
        candidate
    } else if cid == "natsuki" {
        root.to_path_buf()
    } else {
        candidate
    }
}

pub fn discover_styles(root: &Path) -> Vec<StyleEntry> {
    let n_root = char_dir(root, "natsuki");
    let m_root = char_dir(root, "monika");
    let s_root = char_dir(root, "sayori");
    let y_root = char_dir(root, "yuri");

    let mut out = Vec::new();
    let mut push = |character_id: &'static str,
                    id: &'static str,
                    label: &'static str,
                    check_path: PathBuf,
                    kind: StyleKind| {
        if check_path.exists() {
            out.push(StyleEntry {
                character_id,
                id,
                id_c: CString::new(id).unwrap(),
                label_c: CString::new(label).unwrap(),
                kind,
            });
        }
    };

    // ================= STEAM L4D2 3D MODELS (DEFAULT WHEN INSTALLED) =================
    for (cid, sid, lbl) in [
        ("natsuki", "natsuki_l4d2_3d", "L4D2 3D: Natsuki (Steam)"),
        ("monika", "monika_l4d2_3d", "L4D2 3D: Monika (Steam)"),
        ("sayori", "sayori_l4d2_3d", "L4D2 3D: Sayori (Steam)"),
        ("yuri", "yuri_l4d2_3d", "L4D2 3D: Yuri (Steam)"),
    ] {
        if let Some(vpk) = crate::source3d::find_vpk_path(cid) {
            push(cid, sid, lbl, vpk, StyleKind::L4d2ThreeD);
        }
    }

    // ================= NATSUKI =================
    push(
        "natsuki",
        "salvation_uniform",
        "Salvation: Uniforme Escolar",
        n_root.join("Doki Doki Salvation Remake/character_images/1_uniform"),
        StyleKind::Salvation {
            outfit_dir: "1_uniform",
            face_dir: "2_face_def",
        },
    );
    push(
        "natsuki",
        "salvation_casual",
        "Salvation: Ropa Casual",
        n_root.join("Doki Doki Salvation Remake/character_images/1_casual"),
        StyleKind::Salvation {
            outfit_dir: "1_casual",
            face_dir: "2_face_def",
        },
    );
    push(
        "natsuki",
        "salvation_swimsuit",
        "Salvation: Traje de Bano",
        n_root.join("Doki Doki Salvation Remake/character_images/1_swimsuit"),
        StyleKind::Salvation {
            outfit_dir: "1_swimsuit",
            face_dir: "2_face_def",
        },
    );
    for (id, lbl, clothes, hair, acc, head, neck, back) in [
        ("jn_casual", "Just Natsuki: Sentada Casual", "jn_clothes_casual", "jn_hair_down", "jn_accessory_pink_heart_hairpin", "jn_none", "jn_none", "jn_none"),
        ("jn_uniform", "Just Natsuki: Sentada Uniforme", "jn_clothes_school_uniform", "jn_hair_twintails", "jn_accessory_double_white_hairbands", "jn_none", "jn_none", "jn_none"),
        ("jn_hoodie", "Just Natsuki: Hoodie Not-Cute", "jn_clothes_hoodie_not_cute", "jn_hair_messy_bun", "jn_none", "jn_headgear_ahoge_heart", "jn_none", "jn_none"),
        ("jn_nya_cat", "Just Natsuki: Nya + Orejas Gato", "jn_clothes_nya_sweater", "jn_hair_twintails_white_ribbons", "jn_none", "jn_headgear_cat_ears", "jn_necklace_bell_collar", "jn_back_cat_tail"),
        ("jn_bunny_pj", "Just Natsuki: Pijama Conejo", "jn_clothes_bunny_pajamas", "jn_hair_wavy", "jn_accessory_pink_star_hairpin", "jn_none", "jn_necklace_bunny_necklace", "jn_none"),
        ("jn_goth", "Just Natsuki: Pastel Goth", "jn_clothes_pastel_goth_overalls", "jn_hair_pigtails", "jn_none", "jn_headgear_black_beanie", "jn_necklace_black_choker", "jn_none"),
        ("jn_magical", "Just Natsuki: Chica Magica", "jn_clothes_magical_girl", "jn_hair_twin_buns", "jn_accessory_gold_star_hairpin", "jn_none", "jn_none", "jn_none"),
        ("jn_star_pj", "Just Natsuki: Pijama Estrellas", "jn_clothes_star_pajamas", "jn_hair_bun", "jn_accessory_hairband_stars", "jn_none", "jn_none", "jn_none"),
    ] {
        push(
            "natsuki",
            id,
            lbl,
            n_root.join("Just Natsuki/clothes").join(clothes),
            StyleKind::JustNatsuki {
                clothes,
                hair,
                accessory: acc,
                headgear: head,
                necklace: neck,
                back,
            },
        );
    }
    push(
        "natsuki",
        "vanilla_uniform",
        "DDLC Base: Uniforme Clasico",
        n_root.join("00 - DDLC Base (Vanilla)/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "00 - DDLC Base (Vanilla)",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: false,
            wide_crop: false,
        },
    );
    push(
        "natsuki",
        "vanilla_casual",
        "DDLC Base: Casual Clasico",
        n_root.join("00 - DDLC Base (Vanilla)/1t.png"),
        StyleKind::VanillaCasualT {},
    );
    push(
        "natsuki",
        "blueskies_winter",
        "Blue Skies: Sueter Invierno",
        n_root.join("Doki Doki Blue Skies ES/winter/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Doki Doki Blue Skies ES/winter",
            face_dir: "Doki Doki Blue Skies ES/winter",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: false,
            wide_crop: false,
        },
    );
    push(
        "natsuki",
        "blueskies_halloween",
        "Blue Skies: Halloween",
        n_root.join("Doki Doki Blue Skies ES/halloween/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Doki Doki Blue Skies ES/halloween",
            face_dir: "Doki Doki Blue Skies ES/halloween",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: false,
            wide_crop: false,
        },
    );
    for (id, lbl, sub) in [
        ("exitmusic_coat", "Exit Music: Abrigo", "Exit Music/coat"),
        ("exitmusic_scarf", "Exit Music: Bufanda", "Exit Music/scarf"),
        ("exitmusic_jumper", "Exit Music: Jumper", "Exit Music/jumper"),
    ] {
        push(
            "natsuki",
            id,
            lbl,
            n_root.join(sub).join("1l.png"),
            StyleKind::VanillaLayered {
                body_dir: sub,
                face_dir: "00 - DDLC Base (Vanilla)",
                left_tpl: "1l.png",
                left2_tpl: "2l.png",
                right_tpl: "1r.png",
                right2_tpl: "2r.png",
                face_prefix: "",
                tall_crop: false,
                wide_crop: false,
            },
        );
    }
    push(
        "natsuki",
        "neweyes_uniform",
        "New Eyes: Uniforme",
        n_root.join("Doki Doki New Eyes/natsuki normal.png"),
        StyleKind::NewEyes { prefix: "natsuki" },
    );
    push(
        "natsuki",
        "neweyes_casual",
        "New Eyes: Casual",
        n_root.join("Doki Doki New Eyes/natsukic normal.png"),
        StyleKind::NewEyes { prefix: "natsukic" },
    );

    // ================= MONIKA =================
    push(
        "monika",
        "monika_salv_uniform",
        "Salvation: Uniforme Escolar",
        m_root.join("Doki Doki Salvation Remake/character_images/1_uniform"),
        StyleKind::Salvation {
            outfit_dir: "1_uniform",
            face_dir: "2_face",
        },
    );
    push(
        "monika",
        "monika_salv_casual",
        "Salvation: Ropa Casual",
        m_root.join("Doki Doki Salvation Remake/character_images/1_casual"),
        StyleKind::Salvation {
            outfit_dir: "1_casual",
            face_dir: "2_face",
        },
    );
    push(
        "monika",
        "monika_salv_lean",
        "Salvation: Pose Inclinada",
        m_root.join("Doki Doki Salvation Remake/character_images/1_lean/1_body/base.png"),
        StyleKind::SalvationSingleBody {
            outfit_dir: "1_lean",
            body_candidates: &["base.png"],
            face_dir: "3_alt_face",
        },
    );
    for (id, lbl, clothes) in [
        ("monika_mas_uniform", "MAS: Sentada Uniforme", "def"),
        ("monika_mas_blazerless", "MAS: Sin Saco", "blazerless"),
        ("monika_mas_hoodie", "MAS: Hoodie Verde", "finale_hoodie_green"),
        ("monika_mas_kimono", "MAS: Kimono Rosa", "mayjay_pink_kimono"),
        ("monika_mas_blackdress", "MAS: Vestido Negro", "blackdress"),
        ("monika_mas_2b", "MAS: Cosplay 2B", "hatana_2b"),
        ("monika_mas_miku", "MAS: Cosplay Miku", "orcaramelo_hatsune_miku"),
        ("monika_mas_jp", "MAS: Camiseta Jurassic Park", "briaryoung_tshirt_jurassic_park"),
    ] {
        push(
            "monika",
            id,
            lbl,
            m_root.join("Monika After Story/clothes").join(clothes),
            StyleKind::MasSitting { clothes },
        );
    }
    push(
        "monika",
        "monika_vanilla_uniform",
        "DDLC Base: Uniforme Clasico",
        m_root.join("00 - DDLC Base (Vanilla)/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "00 - DDLC Base (Vanilla)",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: true,
        },
    );
    push(
        "monika",
        "monika_vanilla_lean",
        "DDLC Base: Pose Inclinada",
        m_root.join("00 - DDLC Base (Vanilla)/3a.png"),
        StyleKind::VanillaSinglePose {
            rel_primary: "00 - DDLC Base (Vanilla)/3a.png",
            rel_alt: "00 - DDLC Base (Vanilla)/3b.png",
            wide_crop: true,
        },
    );
    push(
        "monika",
        "monika_blueskies_hw",
        "Blue Skies: Halloween",
        m_root.join("Doki Doki Blue Skies ES/halloween/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Doki Doki Blue Skies ES/halloween",
            face_dir: "Doki Doki Blue Skies ES/halloween",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: true,
        },
    );
    push(
        "monika",
        "monika_exitmusic_casual",
        "Exit Music: Casual",
        m_root.join("Exit Music/casual/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Exit Music/casual",
            face_dir: "Exit Music/casual",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "u",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "monika",
        "monika_exitmusic_funeral",
        "Exit Music: Formal Negro",
        m_root.join("Exit Music/funeral/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Exit Music/funeral",
            face_dir: "Exit Music/funeral",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: true,
        },
    );
    push(
        "monika",
        "monika_neweyes",
        "New Eyes: Monika",
        m_root.join("Doki Doki New Eyes/monika normal.png"),
        StyleKind::NewEyes { prefix: "monika" },
    );

    // ================= SAYORI =================
    for (id, lbl, dir) in [
        ("sayori_salv_uniform", "Salvation: Uniforme Abierto", "1_uniform"),
        ("sayori_salv_buttoned", "Salvation: Uniforme Abotonado", "1_buttoned_uniform"),
        ("sayori_salv_casual", "Salvation: Ropa Casual", "1_casual"),
        ("sayori_salv_sweater", "Salvation: Sueter", "1_sweater"),
        ("sayori_salv_pajamas", "Salvation: Pijama", "1_pajamas"),
    ] {
        push(
            "sayori",
            id,
            lbl,
            s_root.join("Doki Doki Salvation Remake/character_images").join(dir),
            StyleKind::Salvation {
                outfit_dir: dir,
                face_dir: "2_face",
            },
        );
    }
    push(
        "sayori",
        "sayori_salv_tap",
        "Salvation: Tocando Dedos",
        s_root.join("Doki Doki Salvation Remake/character_images/1_tapping_fingers/1_body/u_def.png"),
        StyleKind::SalvationSingleBody {
            outfit_dir: "1_tapping_fingers",
            body_candidates: &["u_def.png", "c.png"],
            face_dir: "3_alt_face",
        },
    );
    push(
        "sayori",
        "sayori_vanilla_uniform",
        "DDLC Base: Uniforme Clasico",
        s_root.join("00 - DDLC Base (Vanilla)/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "00 - DDLC Base (Vanilla)",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "sayori",
        "sayori_vanilla_casual",
        "DDLC Base: Casual Rosa",
        s_root.join("00 - DDLC Base (Vanilla)/1bl.png"),
        StyleKind::VanillaLayered {
            body_dir: "00 - DDLC Base (Vanilla)",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1bl.png",
            left2_tpl: "2bl.png",
            right_tpl: "1br.png",
            right2_tpl: "2br.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "sayori",
        "sayori_blueskies_winter",
        "Blue Skies: Sueter Invierno",
        s_root.join("Doki Doki Blue Skies ES/winter/1lw.png"),
        StyleKind::VanillaLayered {
            body_dir: "Doki Doki Blue Skies ES/winter",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1lw.png",
            left2_tpl: "2lw.png",
            right_tpl: "1rw.png",
            right2_tpl: "2rw.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "sayori",
        "sayori_blueskies_formal",
        "Blue Skies: Vestido Rojo",
        s_root.join("Doki Doki Blue Skies ES/formal/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Doki Doki Blue Skies ES/formal",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "sayori",
        "sayori_blueskies_hw",
        "Blue Skies: Halloween",
        s_root.join("Doki Doki Blue Skies ES/halloween/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Doki Doki Blue Skies ES/halloween",
            face_dir: "Doki Doki Blue Skies ES/halloween",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "sayori",
        "sayori_blueskies_pj",
        "Blue Skies: Pijama",
        s_root.join("Doki Doki Blue Skies ES/pajamas/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Doki Doki Blue Skies ES/pajamas",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1l.png",
            left2_tpl: "1l.png",
            right_tpl: "1r.png",
            right2_tpl: "1r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "sayori",
        "sayori_neweyes_u",
        "New Eyes: Uniforme",
        s_root.join("Doki Doki New Eyes/sayori normal.png"),
        StyleKind::NewEyes { prefix: "sayori" },
    );
    push(
        "sayori",
        "sayori_neweyes_c",
        "New Eyes: Casual",
        s_root.join("Doki Doki New Eyes/sayoric normal.png"),
        StyleKind::NewEyes { prefix: "sayoric" },
    );

    // ================= YURI =================
    for (id, lbl, dir) in [
        ("yuri_salv_uniform", "Salvation: Uniforme Escolar", "1_uniform"),
        ("yuri_salv_casual", "Salvation: Sueter Casual", "1_casual"),
        ("yuri_salv_hoodie", "Salvation: Hoodie", "1_hoodie"),
        ("yuri_salv_maid", "Salvation: Maid", "1_maid"),
    ] {
        push(
            "yuri",
            id,
            lbl,
            y_root.join("Doki Doki Salvation Remake/character_images").join(dir),
            StyleKind::Salvation {
                outfit_dir: dir,
                face_dir: "2_face",
            },
        );
    }
    push(
        "yuri",
        "yuri_salv_shy",
        "Salvation: Pose Timida",
        y_root.join("Doki Doki Salvation Remake/character_images/1_shy/1_body/c.png"),
        StyleKind::SalvationSingleBody {
            outfit_dir: "1_shy",
            body_candidates: &["c.png", "h.png", "u.png", "m.png"],
            face_dir: "3_alt_face",
        },
    );
    push(
        "yuri",
        "yuri_vanilla_uniform",
        "DDLC Base: Uniforme Clasico",
        y_root.join("00 - DDLC Base (Vanilla)/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "00 - DDLC Base (Vanilla)",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "yuri",
        "yuri_vanilla_casual",
        "DDLC Base: Sueter Clasico",
        y_root.join("00 - DDLC Base (Vanilla)/1bl.png"),
        StyleKind::VanillaLayered {
            body_dir: "00 - DDLC Base (Vanilla)",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1bl.png",
            left2_tpl: "2bl.png",
            right_tpl: "1br.png",
            right2_tpl: "2br.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "yuri",
        "yuri_blueskies_winter",
        "Blue Skies: Abrigo Invierno",
        y_root.join("Doki Doki Blue Skies ES/winter/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Doki Doki Blue Skies ES/winter",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "yuri",
        "yuri_blueskies_hw",
        "Blue Skies: Bruja Halloween",
        y_root.join("Doki Doki Blue Skies ES/halloween/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Doki Doki Blue Skies ES/halloween",
            face_dir: "Doki Doki Blue Skies ES/halloween",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: true,
        },
    );
    push(
        "yuri",
        "yuri_exitmusic_unbutton",
        "Exit Music: Sueter Relajado",
        y_root.join("Exit Music/unbutton/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Exit Music/unbutton",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "yuri",
        "yuri_exitmusic_funeral",
        "Exit Music: Formal Negro",
        y_root.join("Exit Music/funeral/1l.png"),
        StyleKind::VanillaLayered {
            body_dir: "Exit Music/funeral",
            face_dir: "00 - DDLC Base (Vanilla)",
            left_tpl: "1l.png",
            left2_tpl: "2l.png",
            right_tpl: "1r.png",
            right2_tpl: "2r.png",
            face_prefix: "",
            tall_crop: true,
            wide_crop: false,
        },
    );
    push(
        "yuri",
        "yuri_neweyes",
        "New Eyes: Yuri",
        y_root.join("Doki Doki New Eyes/yuri normal.png"),
        StyleKind::NewEyes { prefix: "yuri" },
    );

    out
}

pub struct Recipe {
    pub canvas_w: u32,
    pub canvas_h: u32,
    pub crop: CropBox,
    pub layers: Vec<PathBuf>,
}

fn push_existing(layers: &mut Vec<PathBuf>, primary: PathBuf, fallback: PathBuf) {
    if primary.exists() {
        layers.push(primary);
    } else if fallback.exists() {
        layers.push(fallback);
    }
}

fn vanilla_letter_for(character_id: &str, mood: &str, blink: bool, variant: u32) -> &'static str {
    if character_id == "monika" {
        // Monika Vanilla has faces a..r
        if blink {
            return match mood {
                "happy" | "laugh" | "excited" | "smug" | "cheeky" => "k",
                _ => "n",
            };
        }
        return match mood {
            "happy" => if variant % 2 == 0 { "b" } else { "j" },
            "laugh" => if variant % 2 == 0 { "d" } else { "k" },
            "excited" => if variant % 2 == 0 { "k" } else { "d" },
            "tsun" => if variant % 2 == 0 { "i" } else { "r" },
            "angry" => if variant % 2 == 0 { "r" } else { "p" },
            "shy" => if variant % 2 == 0 { "o" } else { "n" },
            "blush" => if variant % 2 == 0 { "l" } else { "m" },
            "smug" => if variant % 2 == 0 { "e" } else { "h" },
            "surprised" => if variant % 2 == 0 { "c" } else { "g" },
            "sad" => if variant % 2 == 0 { "f" } else { "q" },
            "thinking" => if variant % 2 == 0 { "g" } else { "h" },
            "sleepy" => "n",
            "cheeky" => if variant % 2 == 0 { "e" } else { "j" },
            _ => if variant % 2 == 0 { "a" } else { "b" },
        };
    }

    if character_id == "yuri" {
        if blink {
            return match mood {
                "happy" | "laugh" | "excited" | "smug" => "d",
                _ => "v",
            };
        }
        return match mood {
            "happy" => if variant % 2 == 0 { "c" } else { "d" },
            "laugh" => if variant % 2 == 0 { "d" } else { "c" },
            "excited" => if variant % 2 == 0 { "n" } else { "k" },
            "tsun" | "angry" => if variant % 2 == 0 { "r" } else { "p" },
            "shy" => if variant % 2 == 0 { "t" } else { "u" },
            "blush" => if variant % 2 == 0 { "q" } else { "s" },
            "smug" | "cheeky" => if variant % 2 == 0 { "e" } else { "g" },
            "surprised" => if variant % 2 == 0 { "h" } else { "m" },
            "sad" => if variant % 2 == 0 { "w" } else { "v" },
            "thinking" => if variant % 2 == 0 { "f" } else { "l" },
            "sleepy" => "v",
            _ => if variant % 2 == 0 { "a" } else { "b" },
        };
    }

    // Natsuki & Sayori share classic DDLC face range a..y (Natsuki also has z)
    if blink {
        return match mood {
            "happy" | "laugh" | "excited" | "smug" => if character_id == "natsuki" { "z" } else { "y" },
            "sad" | "sleepy" => "w",
            "angry" | "tsun" => "x",
            _ => "y",
        };
    }
    match mood {
        "happy" => if variant % 2 == 0 { "d" } else { "j" },
        "laugh" => if variant % 2 == 0 { if character_id == "natsuki" { "z" } else { "r" } } else { "l" },
        "excited" => if variant % 2 == 0 { "l" } else { "e" },
        "tsun" => if variant % 2 == 0 { "m" } else { "i" },
        "angry" => if variant % 2 == 0 { "f" } else { "p" },
        "shy" => if variant % 2 == 0 { "h" } else { "q" },
        "blush" => if variant % 2 == 0 { "s" } else { "j" },
        "smug" => if variant % 2 == 0 { "t" } else { "y" },
        "surprised" => if variant % 2 == 0 { "b" } else { "v" },
        "sad" => if variant % 2 == 0 { "u" } else { "m" },
        "thinking" => if variant % 2 == 0 { "g" } else { "c" },
        "sleepy" => "w",
        "cheeky" => if variant % 2 == 0 { "y" } else { "t" },
        _ => if variant % 2 == 0 { "a" } else { "c" },
    }
}

fn body_lr_select<'a>(
    mood: &str,
    variant: u32,
    l1: &'a str,
    l2: &'a str,
    r1: &'a str,
    r2: &'a str,
) -> (&'a str, &'a str) {
    match mood {
        "angry" | "tsun" | "smug" | "cheeky" => (l2, r2),
        "excited" | "happy" => if variant % 2 == 0 { (l1, r2) } else { (l2, r2) },
        "shy" | "sad" => (l1, r1),
        _ => match variant % 3 {
            0 => (l1, r1),
            1 => (l1, r2),
            _ => (l2, r1),
        },
    }
}

fn salvation_face_parts(character_id: &str, mood: &str, blink: bool, variant: u32) -> (&'static str, &'static str, &'static str) {
    match character_id {
        "monika" => {
            let (mut eyes, mouth, brows) = match mood {
                "happy" => ("2.png", if variant % 2 == 0 { "2.png" } else { "6.png" }, "1.png"),
                "laugh" => ("3.png", "6.png", "4.png"),
                "excited" => ("s.png", "2.png", "4.png"),
                "tsun" => ("f.png", if variant % 2 == 0 { "c.png" } else { "e.png" }, "2.png"),
                "angry" => ("c.png", if variant % 2 == 0 { "5.png" } else { "9.png" }, "2.png"),
                "shy" => ("d.png", "4.png", "3.png"),
                "blush" => ("5.png", "b.png", "4.png"),
                "smug" => ("9.png", "6.png", "5.png"),
                "surprised" => ("7.png", "8.png", "6.png"),
                "sad" => ("e.png", "c.png", "3.png"),
                "thinking" => ("d.png", "3.png", "5.png"),
                "sleepy" => ("4.png", "4.png", "1.png"),
                "cheeky" => ("5.png", "b.png", "5.png"),
                _ => ("1.png", "1.png", "1.png"),
            };
            if blink {
                eyes = if matches!(mood, "happy" | "laugh" | "excited" | "smug" | "cheeky") {
                    "3.png"
                } else {
                    "4.png"
                };
            }
            (eyes, mouth, brows)
        }
        "sayori" => {
            let (mut eyes, mouth, brows) = match mood {
                "happy" => ("2.png", if variant % 2 == 0 { "2.png" } else { "b.png" }, "4.png"),
                "laugh" => ("3.png", "9.png", "4.png"),
                "excited" => ("s.png", "e.png", "4.png"),
                "tsun" => ("6.png", "5.png", "2.png"),
                "angry" => ("d.png", if variant % 2 == 0 { "c.png" } else { "7.png" }, "2.png"),
                "shy" => ("c.png", "4.png", "3.png"),
                "blush" => ("5.png", "b.png", "3.png"),
                "smug" => ("7.png", "f.png", "5.png"),
                "surprised" => ("e.png", "d.png", "6.png"),
                "sad" => ("9.png", "3.png", "3.png"),
                "thinking" => ("a.png", "a.png", "5.png"),
                "sleepy" => ("9.png", "4.png", "1.png"),
                "cheeky" => ("8.png", "f.png", "5.png"),
                _ => ("1.png", "1.png", "1.png"),
            };
            if blink {
                eyes = if matches!(mood, "happy" | "laugh" | "excited" | "smug" | "cheeky") {
                    "3.png"
                } else {
                    "4.png"
                };
            }
            (eyes, mouth, brows)
        }
        "yuri" => {
            let (mut eyes, mouth, brows) = match mood {
                "happy" => ("2.png", if variant % 2 == 0 { "9.png" } else { "c.png" }, "1.png"),
                "laugh" => ("6.png", "d.png", "4.png"),
                "excited" => ("s.png", "a.png", "4.png"),
                "tsun" => ("e.png", "7.png", "2.png"),
                "angry" => ("8.png", if variant % 2 == 0 { "7.png" } else { "8.png" }, "2.png"),
                "shy" => ("4.png", "2.png", "3.png"),
                "blush" => ("2.png", "2.png", "3.png"),
                "smug" => ("3.png", "e.png", "5.png"),
                "surprised" => ("8.png", "f.png", "6.png"),
                "sad" => ("b.png", "5.png", "3.png"),
                "thinking" => ("5.png", "3.png", "5.png"),
                "sleepy" => ("7.png", "4.png", "1.png"),
                "cheeky" => ("9.png", "e.png", "5.png"),
                _ => ("1.png", "1.png", "1.png"),
            };
            if blink {
                eyes = if matches!(mood, "happy" | "laugh" | "excited" | "smug" | "cheeky") {
                    "6.png"
                } else {
                    "7.png"
                };
            }
            (eyes, mouth, brows)
        }
        _ => {
            let (mut eyes, mouth, brows) = match mood {
                "happy" => ("1.png", if variant % 2 == 0 { "2.png" } else { "8.png" }, "3.png"),
                "laugh" => ("4.png", "c.png", "5.png"),
                "excited" => ("s.png", "2.png", "5.png"),
                "tsun" => ("b.png", if variant % 2 == 0 { "9.png" } else { "5.png" }, "2.png"),
                "angry" => ("b.png", if variant % 2 == 0 { "5.png" } else { "7.png" }, "2.png"),
                "shy" => ("3.png", "d.png", "3.png"),
                "blush" => ("6.png", "3.png", "4.png"),
                "smug" => ("e.png", "8.png", "5.png"),
                "surprised" => ("7.png", "3.png", "3.png"),
                "sad" => ("3.png", "6.png", "4.png"),
                "thinking" => ("2.png", "9.png", "6.png"),
                "sleepy" => ("h.png", "4.png", "8.png"),
                "cheeky" => ("8.png", "2.png", "5.png"),
                _ => ("1.png", "1.png", "1.png"),
            };
            if blink {
                eyes = match mood {
                    "happy" | "laugh" | "excited" | "smug" | "cheeky" => "4.png",
                    _ => "5.png",
                };
            }
            (eyes, mouth, brows)
        }
    }
}

fn jn_face_parts(
    mood: &str,
    blink: bool,
    variant: u32,
) -> (
    &'static str,
    &'static str,
    &'static str,
    &'static str,
    Option<&'static str>,
    Option<&'static str>,
) {
    let pose = match mood {
        "tsun" | "angry" | "smug" => "arms_crossed_body",
        "thinking" | "sleepy" => "hand_on_chin",
        "excited" | "cheeky" => "pointy_finger",
        "shy" | "blush" => "finger_touching",
        _ => "sitting",
    };

    let (mut eyes, mouth, brows, blush, emote) = match mood {
        "happy" => ("warm.png", if variant % 2 == 0 { "big_smile.png" } else { "smile.png" }, "normal.png", None, Some("merry.png")),
        "laugh" => ("closed_happy.png", "laugh.png", "up.png", Some("light.png"), Some("laughter.png")),
        "excited" => ("sparkle.png", "grin.png", "up.png", Some("light.png"), Some("dazzle.png")),
        "tsun" => ("baka.png", "pout.png", "furrowed.png", Some("light.png"), Some("anger.png")),
        "angry" => ("squint.png", "furious.png", "furrowed.png", None, Some("anger.png")),
        "shy" => ("down_left.png", "embarrassed.png", "concerned.png", Some("full.png"), Some("sweat_small.png")),
        "blush" => ("pleading.png", "flustered.png", "concerned.png", Some("full.png"), Some("affection.png")),
        "smug" => ("smug.png", "cat_smug.png", "up.png", None, Some("smug.png")),
        "surprised" => ("shocked.png", "gasp.png", "up.png", None, Some("exclamation.png")),
        "sad" => ("pained.png", "frown.png", "concerned.png", None, Some("sad.png")),
        "thinking" => ("think_right.png", "focus.png", "think.png", None, Some("question_mark.png")),
        "sleepy" => ("closed_sad.png", "small.png", "concerned.png", None, Some("sleepy.png")),
        "cheeky" => ("wink_left.png", "blep.png", "up.png", None, Some("smug.png")),
        _ => ("normal.png", "small_smile.png", "normal.png", None, None),
    };

    if blink {
        eyes = match mood {
            "happy" | "laugh" | "excited" | "smug" | "cheeky" => "closed_happy.png",
            _ => "closed_sad.png",
        };
    }

    (pose, eyes, mouth, brows, blush, emote)
}

fn mas_face_parts(mood: &str, blink: bool) -> (&'static str, &'static str, &'static str, Option<&'static str>, &'static str) {
    let (mut eyes, mouth, brows, blush, arms) = match mood {
        "happy" => ("face-eyes-soft.png", "face-mouth-smile.png", "face-eyebrows-up.png", None, "arms-steepling-10.png"),
        "laugh" => ("face-eyes-closedhappy.png", "face-mouth-big.png", "face-eyebrows-up.png", Some("face-blush-shade.png"), "arms-steepling-10.png"),
        "excited" => ("face-eyes-sparkle.png", "face-mouth-wide.png", "face-eyebrows-up.png", Some("face-blush-lines.png"), "arms-steepling-10.png"),
        "tsun" => ("face-eyes-left.png", "face-mouth-pout.png", "face-eyebrows-furrowed.png", Some("face-blush-lines.png"), "arms-crossed-10.png"),
        "angry" => ("face-eyes-normal.png", "face-mouth-angry.png", "face-eyebrows-furrowed.png", None, "arms-crossed-10.png"),
        "shy" => ("face-eyes-left.png", "face-mouth-small.png", "face-eyebrows-knit.png", Some("face-blush-full.png"), "arms-leaning-def-left-def-10.png"),
        "blush" => ("face-eyes-soft.png", "face-mouth-smile.png", "face-eyebrows-knit.png", Some("face-blush-full.png"), "arms-steepling-10.png"),
        "smug" => ("face-eyes-smug.png", "face-mouth-smug.png", "face-eyebrows-mid.png", None, "arms-crossed-10.png"),
        "surprised" => ("face-eyes-wide.png", "face-mouth-gasp.png", "face-eyebrows-up.png", None, "arms-leaning-def-right-def-10.png"),
        "sad" => ("face-eyes-closedsad.png", "face-mouth-small.png", "face-eyebrows-knit.png", None, "arms-leaning-def-left-def-10.png"),
        "thinking" => ("face-eyes-right.png", "face-mouth-small.png", "face-eyebrows-think.png", None, "arms-leaning-def-left-def-10.png"),
        "sleepy" => ("face-eyes-closedsad.png", "face-mouth-small.png", "face-eyebrows-mid.png", None, "arms-crossed-5.png"),
        "cheeky" => ("face-eyes-winkleft.png", "face-mouth-smirk.png", "face-eyebrows-up.png", None, "arms-steepling-10.png"),
        _ => ("face-eyes-normal.png", "face-mouth-smile.png", "face-eyebrows-mid.png", None, "arms-steepling-10.png"),
    };
    if blink {
        eyes = if matches!(mood, "happy" | "laugh" | "excited" | "smug" | "cheeky") {
            "face-eyes-closedhappy.png"
        } else {
            "face-eyes-closedsad.png"
        };
    }
    (eyes, mouth, brows, blush, arms)
}

pub fn build_recipe(root: &Path, style: &StyleEntry, mood: &str, blink: bool, variant: u32) -> Recipe {
    let mood = normalize_mood(mood);
    let croot = char_dir(root, style.character_id);
    let mut layers = Vec::new();

    match &style.kind {
        StyleKind::Salvation { outfit_dir, face_dir } => {
            let base = croot.join("Doki Doki Salvation Remake/character_images");
            let odir = base.join(outfit_dir);
            let left_dir = odir.join("1_body_left");
            let right_dir = odir.join("2_body_right");

            let want_l = match mood {
                "excited" | "cheeky" | "laugh" if left_dir.join("3.png").exists() => "3.png",
                "angry" | "tsun" | "smug" if left_dir.join("2.png").exists() => "2.png",
                _ if variant % 3 == 1 && left_dir.join("2.png").exists() => "2.png",
                _ => "1.png",
            };
            let want_r = match mood {
                "angry" | "tsun" | "smug" | "cheeky" if right_dir.join("2.png").exists() => "2.png",
                _ if variant % 2 == 1 && right_dir.join("2.png").exists() => "2.png",
                _ => "1.png",
            };

            layers.push(left_dir.join(want_l));
            layers.push(right_dir.join(want_r));

            let fdir = base.join(face_dir);
            let (eyes, mouth, brows) = salvation_face_parts(style.character_id, mood, blink, variant);
            layers.push(fdir.join("base.png"));
            push_existing(&mut layers, fdir.join("2_nose/1.png"), fdir.join("2_nose/2.png"));
            push_existing(&mut layers, fdir.join("3_eyes").join(eyes), fdir.join("3_eyes/1.png"));
            push_existing(&mut layers, fdir.join("1_mouth").join(mouth), fdir.join("1_mouth/1.png"));
            push_existing(&mut layers, fdir.join("4_eyebrows").join(brows), fdir.join("4_eyebrows/1.png"));

            let crop = match style.character_id {
                "monika" => CropBox { x: 185, y: 40, w: 625, h: 920 },
                "sayori" => CropBox { x: 185, y: 80, w: 590, h: 880 },
                "yuri" => CropBox { x: 180, y: 45, w: 600, h: 915 },
                _ => CropBox { x: 205, y: 155, w: 550, h: 805 },
            };
            Recipe { canvas_w: 960, canvas_h: 960, crop, layers }
        }
        StyleKind::SalvationSingleBody { outfit_dir, body_candidates, face_dir } => {
            let base = croot.join("Doki Doki Salvation Remake/character_images");
            let bdir = base.join(outfit_dir).join("1_body");
            if let Some(found) = body_candidates.iter().map(|f| bdir.join(f)).find(|p| p.exists()) {
                layers.push(found);
            }
            let fdir = base.join(face_dir);
            let (eyes, mouth, brows) = salvation_face_parts(style.character_id, mood, blink, variant);
            layers.push(fdir.join("base.png"));
            push_existing(&mut layers, fdir.join("2_nose/1.png"), fdir.join("2_nose/2.png"));
            push_existing(&mut layers, fdir.join("3_eyes").join(eyes), fdir.join("3_eyes/1.png"));
            push_existing(&mut layers, fdir.join("1_mouth").join(mouth), fdir.join("1_mouth/1.png"));
            push_existing(&mut layers, fdir.join("4_eyebrows").join(brows), fdir.join("4_eyebrows/1.png"));

            let crop = match style.character_id {
                "monika" => CropBox { x: 150, y: 55, w: 740, h: 905 },
                "sayori" => CropBox { x: 185, y: 75, w: 580, h: 885 },
                _ => CropBox { x: 180, y: 45, w: 600, h: 915 },
            };
            Recipe { canvas_w: 960, canvas_h: 960, crop, layers }
        }
        StyleKind::VanillaLayered {
            body_dir,
            face_dir,
            left_tpl,
            left2_tpl,
            right_tpl,
            right2_tpl,
            face_prefix,
            tall_crop,
            wide_crop,
        } => {
            let bdir = croot.join(body_dir);
            let fdir = croot.join(face_dir);
            let fallback_fdir = croot.join("00 - DDLC Base (Vanilla)");
            let (l, r) = body_lr_select(mood, variant, left_tpl, left2_tpl, right_tpl, right2_tpl);
            let letter = vanilla_letter_for(style.character_id, mood, blink, variant);
            push_existing(&mut layers, bdir.join(l), bdir.join(left_tpl));
            push_existing(&mut layers, bdir.join(r), bdir.join(right_tpl));

            let primary_face = fdir.join(format!("{face_prefix}{letter}.png"));
            let same_dir_default = fdir.join(format!("{face_prefix}a.png"));
            if primary_face.exists() {
                layers.push(primary_face);
            } else if same_dir_default.exists() {
                layers.push(same_dir_default);
            } else {
                push_existing(&mut layers, fallback_fdir.join(format!("{letter}.png")), fallback_fdir.join("a.png"));
            }

            let crop = match (*tall_crop, *wide_crop) {
                (true, true) => CropBox { x: 175, y: 15, w: 650, h: 945 },
                (true, false) => CropBox { x: 185, y: 50, w: 600, h: 910 },
                (false, true) => CropBox { x: 190, y: 90, w: 610, h: 870 },
                (false, false) => CropBox { x: 215, y: 155, w: 530, h: 805 },
            };
            Recipe { canvas_w: 960, canvas_h: 960, crop, layers }
        }
        StyleKind::VanillaCasualT {} => {
            let vdir = croot.join("00 - DDLC Base (Vanilla)");
            let body = if matches!(mood, "tsun" | "angry" | "smug") { "2t.png" } else { "1t.png" };
            let fchar = match mood {
                "happy" | "laugh" | "excited" => if blink { "2ti" } else { "2tb" },
                "tsun" | "angry" => "2tg",
                "shy" | "blush" | "sad" => "2th",
                "surprised" => "2tc",
                "smug" | "cheeky" => "2te",
                _ => if blink { "2ti" } else { "2ta" },
            };
            layers.push(vdir.join(body));
            layers.push(vdir.join(format!("{fchar}.png")));
            Recipe {
                canvas_w: 960,
                canvas_h: 960,
                crop: CropBox { x: 245, y: 170, w: 460, h: 790 },
                layers,
            }
        }
        StyleKind::VanillaSinglePose { rel_primary, rel_alt, wide_crop } => {
            let p1 = croot.join(rel_primary);
            let p2 = croot.join(rel_alt);
            if variant % 2 == 1 && p2.exists() {
                layers.push(p2);
            } else {
                layers.push(p1);
            }
            let crop = if *wide_crop {
                CropBox { x: 165, y: 60, w: 735, h: 900 }
            } else {
                CropBox { x: 190, y: 60, w: 590, h: 900 }
            };
            Recipe { canvas_w: 960, canvas_h: 960, crop, layers }
        }
        StyleKind::NewEyes { prefix } => {
            let ndir = croot.join("Doki Doki New Eyes");
            let fname = match mood {
                "happy" | "laugh" | "smug" => "happy",
                "excited" | "cheeky" => "shyhappy",
                "angry" | "tsun" => "angry",
                "shy" => "shy",
                "blush" => "embarassed",
                "surprised" => "surprised",
                "sad" => "worried",
                "sleepy" => "shynormal",
                _ => "normal",
            };
            push_existing(
                &mut layers,
                ndir.join(format!("{prefix} {fname}.png")),
                ndir.join(format!("{prefix} normal.png")),
            );
            let crop = match style.character_id {
                "sayori" => CropBox { x: 20, y: 55, w: 450, h: 665 },
                "monika" | "yuri" => CropBox { x: 15, y: 50, w: 396, h: 670 },
                _ => CropBox { x: 20, y: 115, w: 396, h: 605 },
            };
            Recipe {
                canvas_w: if style.character_id == "sayori" { 490 } else { 426 },
                canvas_h: 720,
                crop,
                layers,
            }
        }
        StyleKind::JustNatsuki { clothes, hair, accessory, headgear, necklace, back } => {
            let jn = croot.join("Just Natsuki");
            let (pose, eyes, mouth, brows, blush, emote) = jn_face_parts(mood, blink, variant);

            if *back != "jn_none" {
                layers.push(jn.join("back").join(back).join("sitting.png"));
            }
            layers.push(jn.join("hair").join(hair).join("sitting/back.png"));
            layers.push(jn.join("base").join(pose).join("body.png"));
            layers.push(jn.join("clothes").join(clothes).join(format!("{pose}.png")));
            if *necklace != "jn_none" {
                layers.push(jn.join("necklace").join(necklace).join("sitting.png"));
            }
            layers.push(jn.join("base").join(pose).join("head.png"));
            layers.push(jn.join("face/nose/sitting/nose.png"));
            if let Some(b) = blush {
                layers.push(jn.join("face/blush/sitting").join(b));
            }
            layers.push(jn.join("face/eyebrows/sitting").join(brows));
            layers.push(jn.join("face/eyes/sitting").join(eyes));
            layers.push(jn.join("face/mouth/sitting").join(mouth));
            layers.push(jn.join("hair").join(hair).join("sitting/bangs.png"));
            layers.push(jn.join("arms").join(format!("{pose}.png")));
            layers.push(jn.join("sleeves").join(clothes).join(format!("{pose}.png")));
            if *accessory != "jn_none" {
                layers.push(jn.join("accessory").join(accessory).join("sitting.png"));
            }
            if *headgear != "jn_none" {
                layers.push(jn.join("headgear").join(headgear).join("sitting.png"));
            }
            if let Some(e) = emote {
                layers.push(jn.join("emote/sitting").join(e));
            }

            Recipe {
                canvas_w: 1280,
                canvas_h: 720,
                crop: CropBox { x: 365, y: 68, w: 550, h: 616 },
                layers,
            }
        }
        StyleKind::MasSitting { clothes } => {
            let mas = croot.join("Monika After Story");
            let cdir = mas.join("clothes").join(clothes);
            let (eyes, mouth, brows, blush, arms) = mas_face_parts(mood, blink);

            layers.push(mas.join("hair/def/def-10.png"));
            layers.push(mas.join("accessories/ribbon_def/0.png"));
            layers.push(mas.join("body/body-def-0.png"));
            layers.push(mas.join("body/body-def-1.png"));
            layers.push(cdir.join("body-def-0.png"));
            layers.push(cdir.join("body-def-1.png"));
            layers.push(mas.join("body/body-def-head.png"));
            layers.push(mas.join("face/face-nose-def.png"));
            if let Some(b) = blush {
                layers.push(mas.join("face").join(b));
            }
            layers.push(mas.join("face").join(mouth));
            layers.push(mas.join("face").join(eyes));
            layers.push(mas.join("face").join(brows));
            layers.push(mas.join("hair/def/10.png"));
            layers.push(mas.join("body").join(arms));
            layers.push(cdir.join(arms));

            Recipe {
                canvas_w: 1280,
                canvas_h: 850,
                crop: CropBox { x: 390, y: 20, w: 560, h: 740 },
                layers,
            }
        }
        StyleKind::L4d2ThreeD => Recipe {
            canvas_w: 1,
            canvas_h: 1,
            crop: CropBox { x: 0, y: 0, w: 1, h: 1 },
            layers,
        },
    }
}
