#include <cairo.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <gtk-layer-shell.h>
#include <gtk/gtk.h>
#include <librsvg/rsvg.h>
#include <nlohmann/json.hpp>
#include <pango/pangocairo.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_reactive.hpp"
#include "brain_client.hpp"
#include "natsuki_core.h"
#include "svg_icons.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

struct AudioNoteParticle {
    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double size = 14.0;
    double life = 0.0;
    double max_life = 1.4;
    int kind = 0;
};

enum class DrawerKind {
    None,
    Chat,
    Wardrobe,
    Settings,
    NamePrompt,
};

struct FrameSurface {
    NatsukiFrame raw{};
    cairo_surface_t* surface = nullptr;

    ~FrameSurface() {
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        if (raw.handle) {
            natsuki_frame_free(raw.handle);
            raw.handle = nullptr;
        }
    }
};

struct DokiTheme {
    const char* id;
    const char* label;
    const char* primary_hex;
    const char* deep_hex;
    const char* light_hex;
    const char* ink_hex;
    double r;
    double g;
    double b;
    double light_r;
    double light_g;
    double light_b;
};

constexpr DokiTheme kDokiThemes[] = {
    {"natsuki", "Natsuki", "#ff5a96", "#e0246b", "#ffdbe8", "#57132f", 1.00, 0.35, 0.59, 1.00, 0.86, 0.91},
    {"monika",  "Monika",  "#2ea86f", "#147548", "#d7f6e6", "#0e3d26", 0.18, 0.66, 0.44, 0.84, 0.96, 0.90},
    {"sayori",  "Sayori",  "#ff7369", "#dc473d", "#ffe3df", "#541b17", 1.00, 0.45, 0.41, 1.00, 0.89, 0.87},
    {"yuri",    "Yuri",    "#8a5cf6", "#5c31c4", "#ece3ff", "#2a145c", 0.54, 0.36, 0.96, 0.93, 0.89, 1.00},
};

constexpr DokiTheme kMonoDarkTheme{
    "mono_dark", "Monocromatico Oscuro", "#d4d4d8", "#f4f4f5", "#27272a", "#f4f4f5",
    0.92, 0.92, 0.94, 0.22, 0.22, 0.25
};

constexpr DokiTheme kMonoLightTheme{
    "mono_light", "Monocromatico Claro", "#27272a", "#18181b", "#e4e4e7", "#18181b",
    0.16, 0.16, 0.18, 0.88, 0.88, 0.91
};

inline std::string normalize_ui_theme(const std::string& raw) {
    if (raw == "mono_dark" || raw == "monochrome_dark" || raw == "dark") return "mono_dark";
    if (raw == "mono_light" || raw == "monochrome_light" || raw == "light") return "mono_light";
    return "auto";
}

const DokiTheme& theme_for_character(const std::string& cid) {
    for (const auto& t : kDokiThemes) {
        if (cid == t.id) return t;
    }
    return kDokiThemes[0];
}

struct SwarmActor {
    std::string character_id;
    std::shared_ptr<FrameSurface> frame;
    double x = 0.0;
    double y_base = 0.0;
    double vx = 0.0;
    double amp = 24.0;
    double freq = 2.6;
    double phase = 0.0;
    double tilt = 0.0;
    double scale = 1.0;
    bool flip_h = false;
};

struct SwarmParticle {
    std::string icon_name;
    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double size = 22.0;
    double phase = 0.0;
    double r = 1.0;
    double g = 0.4;
    double b = 0.7;
};

std::string executable_dir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return fs::current_path().string();
    buf[n] = '\0';
    return fs::path(buf).parent_path().string();
}

std::string resolve_project_root() {
    std::vector<fs::path> candidates = {
        fs::path(executable_dir()),
        fs::current_path(),
        fs::path(executable_dir()).parent_path(),
    };
    for (const auto& p : candidates) {
        if (fs::exists(p / "assets" / "icons") && fs::exists(p / "brain" / "natsuki_brain.py")) {
            return p.string();
        }
    }
    return executable_dir();
}

bool parse_hex_color(const std::string& hex, double& r, double& g, double& b) {
    if (hex.size() != 7 || hex[0] != '#') return false;
    unsigned int rv = 0, gv = 0, bv = 0;
    if (std::sscanf(hex.c_str(), "#%02x%02x%02x", &rv, &gv, &bv) != 3) return false;
    r = rv / 255.0;
    g = gv / 255.0;
    b = bv / 255.0;
    return true;
}

std::string color_to_hex(const GdkRGBA& rgba) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                  static_cast<int>(std::round(std::clamp(rgba.red, 0.0, 1.0) * 255.0)),
                  static_cast<int>(std::round(std::clamp(rgba.green, 0.0, 1.0) * 255.0)),
                  static_cast<int>(std::round(std::clamp(rgba.blue, 0.0, 1.0) * 255.0)));
    return std::string(buf);
}

void add_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double radius) {
    double r = std::min(radius, std::min(w, h) * 0.5);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}

void draw_tinted_svg(cairo_t* cr, const std::string& svg_path, double x, double y, double size,
                     double r, double g, double b, double alpha) {
    GError* err = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_file(svg_path.c_str(), &err);
    if (!handle) {
        if (err) g_error_free(err);
        return;
    }
    int isz = std::max(8, static_cast<int>(std::ceil(size)));
    cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, isz, isz);
    cairo_t* tcr = cairo_create(tmp);
    RsvgRectangle vp{0.0, 0.0, static_cast<double>(isz), static_cast<double>(isz)};
    rsvg_handle_render_document(handle, tcr, &vp, nullptr);
    // Re-color stroke/fill mask using SOURCE_IN
    cairo_set_operator(tcr, CAIRO_OPERATOR_SOURCE);
    cairo_set_operator(tcr, CAIRO_OPERATOR_IN);
    cairo_set_source_rgba(tcr, r, g, b, alpha);
    cairo_paint(tcr);
    cairo_destroy(tcr);
    g_object_unref(handle);

    cairo_save(cr);
    cairo_set_source_surface(cr, tmp, x, y);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(tmp);
}

bool contains_swarm_trigger_local(const std::string& text) {
    std::string lower;
    lower.reserve(text.size());
    for (unsigned char c : text) {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }
    static const std::regex kPattern(
        R"((?:\*\s*)?amo\s+(?:x|a\s+las?|a)?\s*(?:doki|dokis|natsuki|monika|sayori|yuri)(?:\s*\*)?)",
        std::regex_constants::ECMAScript);
    return std::regex_search(lower, kPattern);
}

}  // namespace

class DokiApp {
public:
    explicit DokiApp(bool self_test_mode)
        : self_test_(self_test_mode),
          project_root_(resolve_project_root()),
          icons_(fs::path(project_root_) / "assets" / "icons") {}

    ~DokiApp() {
        cleanup_resources();
    }

    void cleanup_resources() {
        audio_monitor_.stop();
        if (talk_timer_id_) { g_source_remove(talk_timer_id_); talk_timer_id_ = 0; }
        if (blink_timer_id_) { g_source_remove(blink_timer_id_); blink_timer_id_ = 0; }
        if (anim_timer_id_) { g_source_remove(anim_timer_id_); anim_timer_id_ = 0; }
        if (bubble_timer_id_) { g_source_remove(bubble_timer_id_); bubble_timer_id_ = 0; }
        if (swarm_timer_id_) { g_source_remove(swarm_timer_id_); swarm_timer_id_ = 0; }
        if (brain_) {
            brain_->stop();
            brain_.reset();
        }
        current_frame_.reset();
        swarm_actors_.clear();
        if (engine_) {
            natsuki_engine_free(engine_);
            engine_ = nullptr;
        }
    }

    int run(int argc, char** argv) {
        gtk_init(&argc, &argv);

        std::string sprites_root;
        std::string config_path;
        if (const char* env_spr = std::getenv("DDLC_SHIMEJI_SPRITES")) {
            sprites_root = env_spr;
        }
        engine_ = natsuki_engine_new(sprites_root.empty() ? nullptr : sprites_root.c_str(),
                                     config_path.empty() ? nullptr : config_path.c_str());
        if (!engine_) {
            std::cerr << "[doki-shimeji] Failed to initialize Rust core engine.\n";
            return 1;
        }

        load_config_from_engine();
        collect_catalog();
        if (companion_random_ && !self_test_ && !std::getenv("DOKI_NO_STARTUP_RANDOM") && !characters_.empty()) {
            const auto& picked = characters_[g_random_int_range(0, static_cast<gint32>(characters_.size()))];
            apply_character_choice(picked.id, false);
        }
        install_css();

        build_stage_window();
        build_dock_window();
        build_drawer_window();
        build_swarm_window();
        if (GdkDisplay* dpy = gdk_display_get_default()) {
            g_signal_connect(dpy, "monitor-added", G_CALLBACK(+[](GdkDisplay*, GdkMonitor*, gpointer d) {
                static_cast<DokiApp*>(d)->on_monitors_changed();
            }), this);
            g_signal_connect(dpy, "monitor-removed", G_CALLBACK(+[](GdkDisplay*, GdkMonitor*, gpointer d) {
                static_cast<DokiApp*>(d)->on_monitors_changed();
            }), this);
        }
        sync_audio_monitor_state();

        render_sprite_now();
        update_layout_geometry();

        std::string brain_script = (fs::path(project_root_) / "brain" / "natsuki_brain.py").string();
        brain_ = std::make_unique<BrainClient>(brain_script);
        brain_->start([this](const json& j) {
            handle_brain_json(j);
        });
        send_brain_config();

        g_unix_signal_add(SIGINT, [](gpointer) -> gboolean { gtk_main_quit(); return G_SOURCE_REMOVE; }, nullptr);
        g_unix_signal_add(SIGTERM, [](gpointer) -> gboolean { gtk_main_quit(); return G_SOURCE_REMOVE; }, nullptr);

        if (!self_test_) {
            blink_timer_id_ = g_timeout_add(4200, [](gpointer data) -> gboolean {
                return static_cast<DokiApp*>(data)->on_blink_tick() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
            }, this);
            talk_timer_id_ = g_timeout_add(1000, [](gpointer data) -> gboolean {
                return static_cast<DokiApp*>(data)->on_talk_scheduler() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
            }, this);

            g_timeout_add(750, [](gpointer data) -> gboolean {
                auto* self = static_cast<DokiApp*>(data);
                if (self->user_name_.empty()) {
                    self->show_bubble(
                        self->tr(
                            "Hi! Right-click me for the menu (2D/3D, chat, corner, Your name or Quit).",
                            "Hola! Clic derecho sobre mi para abrir el menu (2D/3D, chat, esquina, Tu nombre o Salir)."),
                        "happy");
                } else {
                    self->request_thought("startup");
                }
                return G_SOURCE_REMOVE;
            }, this);
            gtk_main();
        } else {
            int rc = run_self_test();
            cleanup_resources();
            return rc;
        }
        return 0;
    }

private:
    bool self_test_ = false;
    std::string project_root_;
    SvgIconCache icons_;
    NatsukiEngine* engine_ = nullptr;
    std::unique_ptr<BrainClient> brain_;

    // App state & config
    json cfg_json_;
    std::string character_id_ = "natsuki";
    std::string style_id_ = "natsuki_l4d2_3d";
    std::string side_ = "left";
    std::string target_monitor_ = "auto";
    std::string user_name_;
    std::string language_ = "en";
    const char* tr(const char* en, const char* es) const {
        return language_ == "es" ? es : en;
    }
    std::string gemini_api_key_;
    std::string gemini_model_ = "gemini-3.8-flash";
    std::string custom_system_prompt_;
    std::string speech_bg_ = "#fff8fb";
    std::string speech_fg_ = "#2f1522";
    std::string speech_border_ = "#ff5a96";
    std::string ui_theme_ = "auto";
    double speech_opacity_ = 0.94;
    int target_height_ = 280;
    int talk_interval_s_ = 45;
    bool muted_ = false;
    bool autonomous_ = true;
    bool always_on_top_ = true;
    bool paused_ = false;
    bool companion_random_ = true;
    int auto_switch_sec_ = 120;
    bool roam_enabled_ = true;
    bool auto_antics_ = true;
    std::string weapon_id_ = "auto";
    bool screen_watch_enabled_ = true;
    int screen_watch_sec_ = 30;
    bool drawer_syncing_ = false;

    // 3D & Sprite state
    std::string current_mood_ = "neutral";
    bool is_blinking_ = false;
    uint32_t variant_idx_ = 0;
    float model_yaw_deg_ = -14.0f;
    float model_pitch_deg_ = 0.0f;
    std::shared_ptr<FrameSurface> current_frame_;
    double sprite_alpha_ = 1.0;
    bool sprite_fading_ = false;
    double bounce_offset_ = 0.0;
    double bounce_vel_ = 0.0;

    // 3D Walking, Spring Physics & Antics ("babosadas") state
    double walk_x_ = -1.0;
    double walk_dir_ = 1.0;
    bool roam_walking_ = false;
    double walk_state_timer_ = 2.0;
    float walk_phase_ = 0.0f;
    float walk_speed_norm_ = 0.0f;
    int32_t action_id_ = 0;
    double action_timer_ = 0.0;
    double action_duration_ = 2.2;
    std::string action_temp_weapon_;
    double next_antic_timer_ = 9.0;
    float action_base_yaw_ = -14.0f;
    double action_hover_px_ = 0.0;
    int auto_switch_elapsed_ = 0;
    int screen_watch_elapsed_ = 0;
    bool screen_watch_inflight_ = false;
    gint64 screen_watch_inflight_since_us_ = 0;

    double hair_x_ = 0.0, hair_vx_ = 0.0;
    double hair_y_ = 0.0, hair_vy_ = 0.0;
    double hair_z_ = 0.0, hair_vz_ = 0.0;
    double skirt_x_ = 0.0, skirt_vx_ = 0.0;
    double skirt_y_ = 0.0, skirt_vy_ = 0.0;
    double skirt_flare_ = 0.0, skirt_vflare_ = 0.0;
    double spin_vel_ = 0.0;
    double lifted_y_ = 0.0;
    gint64 last_3d_pose_render_us_ = 0;
    int last_shape_sx_ = -99999;

    // Interactive 3D & 2D drag tracking
    enum class DragMode { None, Rotate, Lift, Move2D };
    double manual_2d_x_ = -1.0;
    bool pointer_pressed_ = false;
    DragMode drag_mode_ = DragMode::None;
    double drag_start_x_ = 0.0;
    double drag_start_y_ = 0.0;
    double drag_last_x_ = 0.0;
    float drag_start_yaw_ = -14.0f;
    double drag_start_walk_x_ = 120.0;
    double breath_phase_ = 0.0;
    double breath_amount_ = 0.0;
    gint64 last_tick_us_ = 0;
    int poke_clicks_ = 0;
    bool self_test_angry_seen_ = false;

    // Speech bubble state
    std::string bubble_text_;
    bool bubble_visible_ = false;
    int bubble_countdown_ = 0;
    int talk_elapsed_ = 0;
    cairo_rectangle_int_t sprite_rect_{10, 80, 180, 280};
    cairo_rectangle_int_t bubble_rect_{0, 0, 0, 0};

    // Catalog
    struct CatalogItem {
        std::string id;
        std::string label;
    };
    std::vector<CatalogItem> characters_;
    std::vector<CatalogItem> styles_for_char_;
    std::vector<CatalogItem> moods_;

    // GTK widgets & timers
    GtkCssProvider* css_provider_ = nullptr;
    GtkWidget* window_ = nullptr;
    GtkWidget* stage_area_ = nullptr;
    GtkWidget* dock_window_ = nullptr;
    GtkWidget* drawer_window_ = nullptr;
    GtkWidget* drawer_stack_ = nullptr;
    GtkWidget* lbl_drawer_title_ = nullptr;
    GtkWidget* lbl_drawer_tagline_ = nullptr;

    // Dock widgets
    GtkWidget* btn_pause_ = nullptr;
    GtkWidget* btn_swap_ = nullptr;
    GtkWidget* btn_user_ = nullptr;
    GtkWidget* btn_mode_toggle_ = nullptr;
    bool dock_visible_ = false;
    DrawerKind active_drawer_ = DrawerKind::None;

    // Wardrobe widgets
    std::unordered_map<std::string, GtkWidget*> doki_pills_;
    GtkWidget* check_random_doki_ = nullptr;
    GtkWidget* combo_style_ = nullptr;
    GtkWidget* combo_weapon_ = nullptr;
    GtkWidget* check_roam_ = nullptr;
    GtkWidget* check_antics_ = nullptr;
    GtkWidget* box_3d_controls_ = nullptr;
    GtkWidget* combo_mood_ = nullptr;
    GtkWidget* box_rot_3d_ = nullptr;
    GtkWidget* lbl_rot_3d_ = nullptr;

    // Chat widgets
    GtkWidget* chat_history_view_ = nullptr;
    GtkWidget* entry_chat_ = nullptr;

    // Settings widgets
    GtkWidget* entry_user_name_settings_ = nullptr;
    GtkWidget* btn_side_toggle_ = nullptr;
    GtkWidget* combo_monitor_ = nullptr;
    GtkWidget* btn_layer_toggle_ = nullptr;
    GtkWidget* entry_api_key_ = nullptr;
    GtkWidget* combo_model_ = nullptr;
    GtkWidget* combo_lang_ = nullptr;
    GtkWidget* scale_height_ = nullptr;
    GtkWidget* scale_interval_ = nullptr;
    GtkWidget* check_random_doki_settings_ = nullptr;
    GtkWidget* scale_switch_sec_ = nullptr;
    GtkWidget* scale_screen_sec_ = nullptr;
    GtkWidget* scale_opacity_ = nullptr;
    GtkWidget* check_muted_ = nullptr;
    GtkWidget* check_auto_ = nullptr;
    GtkWidget* check_screen_watch_ = nullptr;
    GtkWidget* check_sound_ = nullptr;
    GtkWidget* check_voice_ = nullptr;
    GtkWidget* check_voice_wake_ = nullptr;
    GtkWidget* combo_audio_source_ = nullptr;
    GtkWidget* scale_audio_sens_ = nullptr;
    GtkWidget* btn_sound_ = nullptr;
    GtkWidget* btn_voice_ = nullptr;
    GtkWidget* btn_theme_ = nullptr;
    GtkWidget* btn_quit_ = nullptr;
    GtkWidget* btn_drawer_close_ = nullptr;
    GtkWidget* drawer_badge_ = nullptr;
    GtkWidget* combo_ui_theme_ = nullptr;
    GtkWidget* color_bg_ = nullptr;
    GtkWidget* color_fg_ = nullptr;
    GtkWidget* color_border_ = nullptr;
    std::vector<std::pair<GtkWidget*, std::string>> dock_icon_buttons_;

    // Name prompt drawer widgets
    GtkWidget* entry_name_prompt_ = nullptr;

    // Swarm fullscreen overlay state (`*amo x doki*`)
    GtkWidget* swarm_window_ = nullptr;
    GtkWidget* swarm_area_ = nullptr;
    guint swarm_timer_id_ = 0;
    double swarm_elapsed_ = 0.0;
    bool swarm_active_ = false;
    gint64 last_swarm_trigger_us_ = 0;
    std::vector<SwarmActor> swarm_actors_;
    std::vector<SwarmParticle> swarm_particles_;

    // Sound-reactive & local voice-command state (PipeWire / Vosk)
    bool sound_reactive_ = false;
    bool voice_commands_enabled_ = true;
    bool voice_wake_word_ = false;
    std::string audio_source_ = "monitor";
    double audio_sensitivity_ = 1.35;
    AudioReactiveMonitor audio_monitor_;
    AudioSnapshot audio_snap_{};
    std::vector<AudioNoteParticle> audio_notes_;
    double audio_sway_phase_ = 0.0;
    double audio_sway_rad_ = 0.0;
    double audio_squash_ = 0.0;
    uint32_t last_beat_seq_ = 0;
    gint64 last_audio_react_us_ = 0;

    int win_w_ = 520;
    int win_h_ = 440;
    guint talk_timer_id_ = 0;
    guint blink_timer_id_ = 0;
    guint anim_timer_id_ = 0;
    guint bubble_timer_id_ = 0;

    // Diagnostics / self-test
    int last_brain_cmd_count_ = 0;
    bool self_test_bubble_seen_ = false;
    bool self_test_weird_seen_ = false;
    bool self_test_screen_seen_ = false;

    void load_config_from_engine() {
        char* raw = natsuki_config_get_json(engine_);
        if (raw) {
            try {
                cfg_json_ = json::parse(raw);
            } catch (...) {
                cfg_json_ = json::object();
            }
            natsuki_string_free(raw);
        }
        character_id_ = cfg_json_.value("character_id", "natsuki");
        style_id_ = cfg_json_.value("style_id", "natsuki_l4d2_3d");
        side_ = cfg_json_.value("side", "left");
        if (side_ != "right") side_ = "left";
        target_monitor_ = cfg_json_.value("target_monitor", "auto");
        if (target_monitor_.empty()) target_monitor_ = "auto";
        user_name_ = cfg_json_.value("user_name", "");
        language_ = cfg_json_.value("language", "en");
        if (language_ != "es") language_ = "en";
        gemini_api_key_ = cfg_json_.value("gemini_api_key", "");
        gemini_model_ = cfg_json_.value("gemini_model", cfg_json_.value("model", "gemini-3.8-flash"));
        custom_system_prompt_ = cfg_json_.value("custom_system_prompt", "");
        target_height_ = std::clamp(cfg_json_.value("sprite_height_px", 280), 150, 520);
        talk_interval_s_ = std::clamp(cfg_json_.value("talk_interval_s", 45), 10, 600);
        speech_opacity_ = std::clamp(cfg_json_.value("speech_opacity", 0.94), 0.35, 1.0);
        ui_theme_ = normalize_ui_theme(cfg_json_.value("ui_theme", "auto"));
        speech_bg_ = cfg_json_.value("speech_bg", "#fff8fb");
        speech_fg_ = cfg_json_.value("speech_fg", "#2f1522");
        speech_border_ = cfg_json_.value("speech_border", theme_for_character(character_id_).primary_hex);
        if (ui_theme_ != "auto" && speech_bg_ == "#fff8fb" && speech_fg_ == "#2f1522") {
            apply_bubble_palette_for_theme(ui_theme_);
        }
        muted_ = cfg_json_.value("muted", false);
        autonomous_ = cfg_json_.value("autonomous", true);
        always_on_top_ = cfg_json_.value("always_on_top", true);
        companion_random_ = cfg_json_.value("companion_random", true);
        auto_switch_sec_ = std::clamp(cfg_json_.value("auto_switch_sec", 120), 15, 900);
        roam_enabled_ = cfg_json_.value("roam_enabled", true);
        auto_antics_ = cfg_json_.value("auto_antics", true);
        weapon_id_ = cfg_json_.value("weapon_id", "auto");
        screen_watch_enabled_ = cfg_json_.value("screen_watch_enabled", true);
        screen_watch_sec_ = std::clamp(cfg_json_.value("screen_watch_sec", 30), 5, 600);
        sound_reactive_ = cfg_json_.value("sound_reactive", false);
        voice_commands_enabled_ = cfg_json_.value("voice_commands_enabled", true);
        voice_wake_word_ = cfg_json_.value("voice_wake_word", false);
        audio_source_ = (cfg_json_.value("audio_source", "monitor") == "mic") ? "mic" : "monitor";
        audio_sensitivity_ = std::clamp(cfg_json_.value("audio_sensitivity", 1.35), 0.4, 3.5);
    }

    void save_config() {
        if (self_test_) return;
        cfg_json_["character_id"] = character_id_;
        cfg_json_["style_id"] = style_id_;
        cfg_json_["side"] = side_;
        cfg_json_["target_monitor"] = target_monitor_;
        cfg_json_["user_name"] = user_name_;
        cfg_json_["language"] = language_;
        cfg_json_["gemini_api_key"] = gemini_api_key_;
        cfg_json_["gemini_model"] = gemini_model_;
        cfg_json_["model"] = gemini_model_;
        cfg_json_["custom_system_prompt"] = custom_system_prompt_;
        cfg_json_["sprite_height_px"] = target_height_;
        cfg_json_["talk_interval_s"] = talk_interval_s_;
        cfg_json_["speech_opacity"] = speech_opacity_;
        cfg_json_["ui_theme"] = ui_theme_;
        cfg_json_["speech_bg"] = speech_bg_;
        cfg_json_["speech_fg"] = speech_fg_;
        cfg_json_["speech_border"] = speech_border_;
        cfg_json_["muted"] = muted_;
        cfg_json_["autonomous"] = autonomous_;
        cfg_json_["always_on_top"] = always_on_top_;
        cfg_json_["companion_random"] = companion_random_;
        cfg_json_["auto_switch_sec"] = auto_switch_sec_;
        cfg_json_["roam_enabled"] = roam_enabled_;
        cfg_json_["auto_antics"] = auto_antics_;
        cfg_json_["weapon_id"] = weapon_id_;
        cfg_json_["screen_watch_enabled"] = screen_watch_enabled_;
        cfg_json_["screen_watch_sec"] = screen_watch_sec_;
        cfg_json_["sound_reactive"] = sound_reactive_;
        cfg_json_["voice_commands_enabled"] = voice_commands_enabled_;
        cfg_json_["voice_wake_word"] = voice_wake_word_;
        cfg_json_["audio_source"] = audio_source_;
        cfg_json_["audio_sensitivity"] = audio_sensitivity_;
        if (!cfg_json_.contains("character_styles") || !cfg_json_["character_styles"].is_object()) {
            cfg_json_["character_styles"] = json::object();
        }
        cfg_json_["character_styles"][character_id_] = style_id_;

        std::string dump = cfg_json_.dump();
        natsuki_config_update_json(engine_, dump.c_str());
    }

    void collect_catalog() {
        characters_.clear();
        int nc = natsuki_engine_character_count(engine_);
        for (int i = 0; i < nc; ++i) {
            const char* id = natsuki_engine_character_id(engine_, i);
            const char* lbl = natsuki_engine_character_label(engine_, i);
            if (id && lbl) characters_.push_back({id, lbl});
        }
        collect_styles_for_current_char();

        moods_.clear();
        int nm = natsuki_engine_mood_count(engine_);
        for (int i = 0; i < nm; ++i) {
            const char* id = natsuki_engine_mood_id(engine_, i);
            const char* lbl = natsuki_engine_mood_label(engine_, i);
            if (id && lbl) moods_.push_back({id, lbl});
        }
    }

    void collect_styles_for_current_char() {
        styles_for_char_.clear();
        int ns = natsuki_engine_style_count_for(engine_, character_id_.c_str());
        for (int i = 0; i < ns; ++i) {
            const char* id = natsuki_engine_style_id_for(engine_, character_id_.c_str(), i);
            const char* lbl = natsuki_engine_style_label_for(engine_, character_id_.c_str(), i);
            if (id && lbl) styles_for_char_.push_back({id, lbl});
        }
        bool found = false;
        for (const auto& s : styles_for_char_) {
            if (s.id == style_id_) {
                found = true;
                break;
            }
        }
        if (!found && !styles_for_char_.empty()) {
            style_id_ = styles_for_char_[0].id;
        }
    }

    void install_css() {
        css_provider_ = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider_),
            GTK_STYLE_PROVIDER_PRIORITY_USER);
        apply_character_css();
    }

    static const char* doki_role_tagline(const std::string& cid) {
        if (cid == "monika") return "Club de Literatura - Presidencia";
        if (cid == "sayori") return "Club de Literatura - Vicepresidencia";
        if (cid == "yuri") return "Club de Literatura - Poesia";
        return "Club de Literatura - Reposteria";
    }

    bool is_monochrome_theme() const {
        return ui_theme_ == "mono_dark" || ui_theme_ == "mono_light";
    }

    const DokiTheme& effective_theme() const {
        if (ui_theme_ == "mono_dark") return kMonoDarkTheme;
        if (ui_theme_ == "mono_light") return kMonoLightTheme;
        return theme_for_character(character_id_);
    }

    void sync_bubble_color_choosers() {
        bool prev_sync = drawer_syncing_;
        drawer_syncing_ = true;
        auto set_btn = [](GtkWidget* btn, const std::string& hex) {
            if (!btn) return;
            GdkRGBA c{};
            if (gdk_rgba_parse(&c, hex.c_str())) {
                gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(btn), &c);
            }
        };
        set_btn(color_bg_, speech_bg_);
        set_btn(color_fg_, speech_fg_);
        set_btn(color_border_, speech_border_);
        drawer_syncing_ = prev_sync;
    }

    void apply_bubble_palette_for_theme(const std::string& theme) {
        if (theme == "mono_dark") {
            speech_bg_ = "#141416";
            speech_fg_ = "#f4f4f5";
            speech_border_ = "#d4d4d8";
        } else if (theme == "mono_light") {
            speech_bg_ = "#fafafa";
            speech_fg_ = "#18181b";
            speech_border_ = "#27272a";
        } else {
            speech_bg_ = "#fff8fb";
            speech_fg_ = "#2f1522";
            speech_border_ = theme_for_character(character_id_).primary_hex;
        }
        sync_bubble_color_choosers();
    }

    void refresh_dock_icons_theme() {
        const auto& th = effective_theme();
        for (const auto& entry : dock_icon_buttons_) {
            GtkWidget* btn = entry.first;
            const std::string& icon_name = entry.second;
            if (!btn) continue;
            const std::string& eff_icon = (btn == btn_pause_ && paused_) ? std::string("play") : icon_name;
            if (GtkWidget* img = icons_.make_gtk_image(eff_icon, 18, th.r, th.g, th.b)) {
                gtk_button_set_image(GTK_BUTTON(btn), img);
            }
        }
        if (btn_theme_) {
            GtkStyleContext* ctx = gtk_widget_get_style_context(btn_theme_);
            if (is_monochrome_theme()) {
                gtk_style_context_add_class(ctx, "doki-btn-active");
            } else {
                gtk_style_context_remove_class(ctx, "doki-btn-active");
            }
        }
        if (btn_quit_) {
            double qr = (ui_theme_ == "mono_dark") ? 0.94 : (ui_theme_ == "mono_light" ? 0.14 : 0.54);
            double qg = (ui_theme_ == "mono_dark") ? 0.94 : (ui_theme_ == "mono_light" ? 0.14 : 0.08);
            double qb = (ui_theme_ == "mono_dark") ? 0.96 : (ui_theme_ == "mono_light" ? 0.16 : 0.21);
            if (GtkWidget* img = icons_.make_gtk_image("power", 15, qr, qg, qb)) {
                gtk_button_set_image(GTK_BUTTON(btn_quit_), img);
            }
        }
        if (btn_drawer_close_) {
            double cr = (ui_theme_ == "mono_dark") ? 0.94 : (ui_theme_ == "mono_light" ? 0.14 : 0.56);
            double cg = (ui_theme_ == "mono_dark") ? 0.94 : (ui_theme_ == "mono_light" ? 0.14 : 0.11);
            double cb = (ui_theme_ == "mono_dark") ? 0.96 : (ui_theme_ == "mono_light" ? 0.16 : 0.25);
            if (GtkWidget* img = icons_.make_gtk_image("close", 15, cr, cg, cb)) {
                gtk_button_set_image(GTK_BUTTON(btn_drawer_close_), img);
            }
        }
        if (drawer_badge_) {
            if (GdkPixbuf* pb = icons_.make_pixbuf("sparkle", 18, th.r, th.g, th.b)) {
                gtk_image_set_from_pixbuf(GTK_IMAGE(drawer_badge_), pb);
                g_object_unref(pb);
            }
        }
    }

    void set_ui_theme(const std::string& raw_theme, bool announce) {
        std::string next = normalize_ui_theme(raw_theme);
        ui_theme_ = next;
        apply_bubble_palette_for_theme(ui_theme_);
        apply_character_css();
        refresh_dock_icons_theme();
        if (combo_ui_theme_) {
            bool prev_sync = drawer_syncing_;
            drawer_syncing_ = true;
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_ui_theme_), ui_theme_.c_str());
            drawer_syncing_ = prev_sync;
        }
        save_config();
        if (stage_area_) gtk_widget_queue_draw(stage_area_);
        if (dock_window_) gtk_widget_queue_draw(dock_window_);
        if (drawer_window_) gtk_widget_queue_draw(drawer_window_);
        if (announce) {
            const char* msg = (ui_theme_ == "mono_dark")
                ? "Tema monocromatico oscuro activado."
                : (ui_theme_ == "mono_light" ? "Tema monocromatico claro activado."
                                             : "Tema original Doki restaurado.");
            show_bubble(msg, "happy");
        }
    }

    void cycle_ui_theme() {
        std::string next = (ui_theme_ == "auto")
            ? "mono_dark"
            : ((ui_theme_ == "mono_dark") ? "mono_light" : "auto");
        set_ui_theme(next, true);
    }

    void apply_character_css() {
        if (!css_provider_) return;
        const bool mono_dark = (ui_theme_ == "mono_dark");
        const bool mono_light = (ui_theme_ == "mono_light");
        const auto& th = effective_theme();

        std::string dock_bg = mono_dark
            ? "linear-gradient(135deg, #1b1b1f 0%, #101013 100%)"
            : (mono_light ? "linear-gradient(135deg, #ffffff 0%, #e4e4e7 100%)"
                          : ("linear-gradient(135deg, #fff8fb 0%, " + std::string(th.light_hex) + " 100%)"));
        std::string drawer_bg = mono_dark
            ? "linear-gradient(160deg, #18181b 0%, #0f0f12 100%)"
            : (mono_light ? "linear-gradient(160deg, #ffffff 0%, #e4e4e7 100%)"
                          : ("linear-gradient(160deg, #fffbfd 0%, " + std::string(th.light_hex) + " 100%)"));
        std::string card_fg = mono_dark ? "#f4f4f5" : (mono_light ? "#18181b" : "#2b1824");
        std::string sub_fg = mono_dark ? "#a1a1aa" : (mono_light ? "#52525b" : "#543a47");
        std::string tag_fg = mono_dark ? "#a1a1aa" : (mono_light ? "#3f3f46" : "#6c465c");
        std::string surf_bg = mono_dark ? "#121215" : "#ffffff";
        std::string surf_fg = mono_dark ? "#f4f4f5" : (mono_light ? "#18181b" : "#261420");
        std::string ctrl_bg = mono_dark ? "#202026" : "#ffffff";
        std::string hover_bg = mono_dark ? "#2e2e36" : th.light_hex;
        std::string icon_idle_bg = mono_dark ? "rgba(38,38,44,0.82)"
                                             : (mono_light ? "rgba(255,255,255,0.84)" : "rgba(255,255,255,0.65)");
        std::string active_bg = mono_dark ? "#3f3f46" : (mono_light ? "#18181b" : th.primary_hex);
        std::string active_fg = mono_dark ? "#ffffff" : (mono_light ? "#ffffff" : "#ffffff");
        std::string active_icon_bg = mono_dark ? "#32323a" : (mono_light ? "#d4d4d8" : th.light_hex);
        std::string btn_border = mono_dark ? "#52525b" : (mono_light ? "#3f3f46" : th.primary_hex);
        std::string card_border = mono_dark ? "#d4d4d8" : (mono_light ? "#27272a" : th.primary_hex);
        std::string close_border = mono_dark ? "#71717a" : (mono_light ? "#3f3f46" : "#e06b8b");
        std::string close_bg = mono_dark ? "#27272a" : (mono_light ? "#f4f4f5" : "#fff0f4");
        std::string close_fg = mono_dark ? "#f4f4f5" : (mono_light ? "#18181b" : "#8f1d3f");
        std::string close_hover = mono_dark ? "#3f3f46" : (mono_light ? "#e4e4e7" : "#ffdce6");
        std::string quit_border = mono_dark ? "#a1a1aa" : (mono_light ? "#27272a" : "#d94c70");
        std::string quit_bg = mono_dark ? "#27272a" : (mono_light ? "#e4e4e7" : "#ffe4ec");
        std::string quit_fg = mono_dark ? "#fafafa" : (mono_light ? "#18181b" : "#8a1535");
        std::string quit_hover = mono_dark ? "#3f3f46" : (mono_light ? "#d4d4d8" : "#ffcad9");

        std::string css = std::string() +
            "window#doki-stage, window#doki-dock, window#doki-drawer, window#doki-swarm {"
            "  background-color: transparent; background-image: none; border: none; box-shadow: none; }"
            "#doki-dock-card { background: " + dock_bg + ";"
            "  border: 2px solid " + card_border + "; border-radius: 16px; padding: 5px 8px;"
            "  box-shadow: 0 6px 18px rgba(10, 10, 14, 0.34); }"
            "#doki-drawer-card { background: " + drawer_bg + ";"
            "  border: 2.5px solid " + card_border + "; border-radius: 18px; padding: 12px 14px;"
            "  color: " + card_fg + "; box-shadow: 0 10px 28px rgba(10, 10, 14, 0.40); }"
            "#doki-drawer-card label { color: " + card_fg + "; }"
            "#doki-drawer-card scrolledwindow, #doki-drawer-card viewport {"
            "  background-color: " + surf_bg + "; border-radius: 12px; border: 1.5px solid " + th.light_hex + "; }"
            "#doki-drawer-card textview, #doki-drawer-card textview text {"
            "  background-color: " + surf_bg + "; color: " + surf_fg + "; caret-color: " + th.deep_hex + "; }"
            "#doki-drawer-card checkbutton, #doki-drawer-card checkbutton label { color: " + card_fg + "; font-weight: 600; }"
            "#doki-drawer-card combobox button { background: " + ctrl_bg + "; color: " + surf_fg + ";"
            "  border: 1.5px solid " + btn_border + "; border-radius: 10px; padding: 4px 8px; }"
            ".doki-title { font-weight: 800; font-size: 14px; color: " + th.deep_hex + "; }"
            "#doki-drawer-card label.doki-title { color: " + th.deep_hex + "; }"
            ".doki-tagline { font-size: 11px; font-weight: 600; color: " + tag_fg + "; }"
            "#doki-drawer-card label.doki-tagline { color: " + tag_fg + "; }"
            ".doki-subtitle { font-weight: 700; font-size: 12px; color: " + sub_fg + "; }"
            "#doki-drawer-card label.doki-subtitle { color: " + sub_fg + "; }"
            ".doki-btn { border-radius: 10px; border: 1.5px solid " + btn_border + "; background: " + ctrl_bg + ";"
            "  color: " + th.ink_hex + "; padding: 4px 10px; font-weight: 700; min-height: 28px; }"
            ".doki-btn label { color: " + th.ink_hex + "; }"
            ".doki-btn:hover { background: " + hover_bg + "; }"
            ".doki-btn-active { background: " + active_bg + "; color: " + active_fg + "; border-color: " + th.deep_hex + "; }"
            ".doki-btn-active label { color: " + active_fg + "; }"
            ".dock-icon-btn { border-radius: 10px; border: 1.5px solid transparent; background: " + icon_idle_bg + ";"
            "  color: " + card_fg + "; padding: 4px 6px; min-width: 30px; min-height: 30px; }"
            ".dock-icon-btn:hover { background: " + hover_bg + "; border-color: " + card_border + "; }"
            ".dock-icon-btn.doki-btn-active { background: " + active_icon_bg + "; border-color: " + th.deep_hex + "; box-shadow: inset 0 0 0 1px " + card_border + "; }"
            ".dock-mode-btn { border-radius: 10px; border: 1.5px solid " + btn_border + "; background: " + ctrl_bg + ";"
            "  color: " + th.deep_hex + "; padding: 3px 8px; font-weight: 800; font-size: 11px; min-height: 28px; }"
            ".dock-mode-btn label { color: " + th.deep_hex + "; font-weight: 800; }"
            ".dock-mode-btn:hover { background: " + hover_bg + "; }"
            ".doki-close-btn { border-radius: 10px; border: 1.5px solid " + close_border + "; background: " + close_bg + ";"
            "  color: " + close_fg + "; font-weight: 800; font-size: 12px; padding: 3px 9px; min-height: 28px; }"
            ".doki-close-btn label { color: " + close_fg + "; font-weight: 800; }"
            ".doki-close-btn:hover { background: " + close_hover + "; }"
            ".doki-quit-btn { border-radius: 10px; border: 1.5px solid " + quit_border + "; background: " + quit_bg + ";"
            "  color: " + quit_fg + "; font-weight: 800; font-size: 11px; padding: 3px 8px; min-height: 30px; }"
            ".doki-quit-btn label { color: " + quit_fg + "; font-weight: 800; }"
            ".doki-quit-btn:hover { background: " + quit_hover + "; }"
            ".doki-entry { border-radius: 10px; border: 1.5px solid " + btn_border + "; padding: 6px 10px;"
            "  background: " + surf_bg + "; color: " + surf_fg + "; caret-color: " + th.deep_hex + "; }";
        gtk_css_provider_load_from_data(css_provider_, css.c_str(), -1, nullptr);
    }

    GtkWidget* make_transparent_layer_window(const char* ns, GtkLayerShellLayer layer,
                                             GtkLayerShellKeyboardMode kb_mode, const char* widget_name) {
        GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
        gtk_widget_set_app_paintable(win, TRUE);
        if (widget_name) gtk_widget_set_name(win, widget_name);

        GdkScreen* scr = gtk_widget_get_screen(win);
        if (GdkVisual* vis = gdk_screen_get_rgba_visual(scr)) {
            gtk_widget_set_visual(win, vis);
        }

        // Wayland GTK3 leaves app_paintable GtkWindows pitch black unless cleared to transparent RGBA first.
        g_signal_connect(win, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer) -> gboolean {
            cairo_save(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
            cairo_paint(cr);
            cairo_restore(cr);
            return FALSE;
        }), nullptr);

        gtk_layer_init_for_window(GTK_WINDOW(win));
        gtk_layer_set_namespace(GTK_WINDOW(win), ns);
        if (GdkMonitor* mon = resolve_target_monitor()) {
            gtk_layer_set_monitor(GTK_WINDOW(win), mon);
        }
        gtk_layer_set_layer(GTK_WINDOW(win), layer);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(win), kb_mode);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        return win;
    }

    GtkLayerShellLayer target_floating_layer() const {
        return always_on_top_ ? GTK_LAYER_SHELL_LAYER_OVERLAY : GTK_LAYER_SHELL_LAYER_BOTTOM;
    }

    static std::string monitor_id_for(GdkMonitor* m, int idx) {
        if (!m) return "idx:" + std::to_string(idx);
        const char* model = gdk_monitor_get_model(m);
        if (model && *model) return std::string("model:") + model;
        return "idx:" + std::to_string(idx);
    }

    GdkMonitor* resolve_target_monitor() const {
        GdkDisplay* dpy = gdk_display_get_default();
        if (!dpy) return nullptr;
        int n = gdk_display_get_n_monitors(dpy);
        if (const char* mx = std::getenv("DOKI_MONITOR_X")) {
            int want_x = std::atoi(mx);
            for (int i = 0; i < n; ++i) {
                if (GdkMonitor* m = gdk_display_get_monitor(dpy, i)) {
                    GdkRectangle geo{};
                    gdk_monitor_get_geometry(m, &geo);
                    if (geo.x == want_x) return m;
                }
            }
        }
        if (target_monitor_ != "auto" && !target_monitor_.empty()) {
            for (int i = 0; i < n; ++i) {
                if (GdkMonitor* m = gdk_display_get_monitor(dpy, i)) {
                    if (monitor_id_for(m, i) == target_monitor_) return m;
                }
            }
        }
        // In "auto" mode: if multiple monitors are connected, prefer the largest external monitor
        // (index 0 / x==0 is typically the built-in laptop panel eDP-1, which may be slightly wider than a 1360x768 TV).
        if (n > 1) {
            GdkMonitor* best_ext = nullptr;
            long long best_ext_area = -1;
            GdkMonitor* best = nullptr;
            long long best_area = -1;
            for (int i = 0; i < n; ++i) {
                if (GdkMonitor* m = gdk_display_get_monitor(dpy, i)) {
                    GdkRectangle geo{};
                    gdk_monitor_get_geometry(m, &geo);
                    long long area = static_cast<long long>(geo.width) * geo.height;
                    if (area > best_area || (area == best_area && i > 0)) {
                        best_area = area;
                        best = m;
                    }
                    if ((i > 0 || geo.x != 0) && area > best_ext_area) {
                        best_ext_area = area;
                        best_ext = m;
                    }
                }
            }
            if (best_ext) return best_ext;
            if (best) return best;
        }
        if (GdkMonitor* pm = gdk_display_get_primary_monitor(dpy)) return pm;
        return (n > 0) ? gdk_display_get_monitor(dpy, 0) : nullptr;
    }

    int detect_monitor_width() const {
        if (GdkMonitor* mon = resolve_target_monitor()) {
            GdkRectangle geo{};
            gdk_monitor_get_geometry(mon, &geo);
            if (geo.width >= 640) return geo.width;
        }
        return 1366;
    }

    int detect_monitor_x() const {
        if (GdkMonitor* mon = resolve_target_monitor()) {
            GdkRectangle geo{};
            gdk_monitor_get_geometry(mon, &geo);
            return geo.x;
        }
        return 0;
    }

    void apply_monitor_to_windows() {
        GdkMonitor* mon = resolve_target_monitor();
        if (!mon) return;
        for (GtkWidget* w : {window_, dock_window_, drawer_window_, swarm_window_}) {
            if (!w) continue;
            if (gtk_widget_get_realized(w)) {
                bool was_vis = gtk_widget_get_visible(w);
                gtk_widget_hide(w);
                gtk_widget_unrealize(w);
                gtk_layer_set_monitor(GTK_WINDOW(w), mon);
                if (was_vis) {
                    gtk_widget_show_all(w);
                }
            } else {
                gtk_layer_set_monitor(GTK_WINDOW(w), mon);
            }
        }
        walk_x_ = -1.0;
        update_layout_geometry();
        render_sprite_now();
    }

    void refresh_monitor_combo() {
        if (!combo_monitor_) return;
        GdkDisplay* dpy = gdk_display_get_default();
        if (!dpy) return;
        drawer_syncing_ = true;
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combo_monitor_));
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_monitor_), "auto", "Auto (externa / principal)");
        int n = gdk_display_get_n_monitors(dpy);
        for (int i = 0; i < n; ++i) {
            if (GdkMonitor* m = gdk_display_get_monitor(dpy, i)) {
                GdkRectangle geo{};
                gdk_monitor_get_geometry(m, &geo);
                const char* model = gdk_monitor_get_model(m);
                std::string mid = monitor_id_for(m, i);
                std::string lbl = "Pantalla " + std::to_string(i + 1);
                if (model && *model) lbl += std::string(": ") + model;
                lbl += " (" + std::to_string(geo.width) + "x" + std::to_string(geo.height) + ")";
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_monitor_), mid.c_str(), lbl.c_str());
            }
        }
        if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_monitor_), target_monitor_.c_str())) {
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_monitor_), "auto");
        }
        drawer_syncing_ = false;
    }

    void cycle_monitor() {
        GdkDisplay* dpy = gdk_display_get_default();
        if (!dpy) return;
        int n = gdk_display_get_n_monitors(dpy);
        if (n <= 1) {
            show_bubble(tr("Only 1 monitor detected.", "Solo detecto 1 pantalla conectada."), "neutral");
            return;
        }
        GdkMonitor* cur = resolve_target_monitor();
        int cur_idx = 0;
        for (int i = 0; i < n; ++i) {
            if (gdk_display_get_monitor(dpy, i) == cur) {
                cur_idx = i;
                break;
            }
        }
        int next_idx = (cur_idx + 1) % n;
        GdkMonitor* next_m = gdk_display_get_monitor(dpy, next_idx);
        target_monitor_ = monitor_id_for(next_m, next_idx);
        save_config();
        refresh_monitor_combo();
        apply_monitor_to_windows();
        GdkRectangle geo{};
        if (next_m) gdk_monitor_get_geometry(next_m, &geo);
        show_bubble(
            (language_ == "es" ? "Me movi a Pantalla " : "Moved to Display ") +
                std::to_string(next_idx + 1) + " (" +
                std::to_string(geo.width) + "x" + std::to_string(geo.height) + ")!",
            "happy");
    }

    void on_monitors_changed() {
        refresh_monitor_combo();
        apply_monitor_to_windows();
    }

    void build_stage_window() {
        window_ = make_transparent_layer_window(
            "doki-shimeji-stage",
            target_floating_layer(),
            GTK_LAYER_SHELL_KEYBOARD_MODE_NONE, "doki-stage");
        stage_area_ = gtk_drawing_area_new();
        gtk_widget_add_events(stage_area_,
                              GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
        gtk_container_add(GTK_CONTAINER(window_), stage_area_);

        g_signal_connect(window_, "realize", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->update_input_shape();
        }), this);
        g_signal_connect(window_, "map", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->update_input_shape();
        }), this);

        g_signal_connect(stage_area_, "draw",
                         G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer d) -> gboolean {
                             static_cast<DokiApp*>(d)->on_stage_draw(cr);
                             return FALSE;
                         }),
                         this);
        g_signal_connect(stage_area_, "button-press-event",
                         G_CALLBACK(+[](GtkWidget*, GdkEventButton* ev, gpointer d) -> gboolean {
                             return static_cast<DokiApp*>(d)->on_stage_press(ev);
                         }),
                         this);
        g_signal_connect(stage_area_, "button-release-event",
                         G_CALLBACK(+[](GtkWidget*, GdkEventButton* ev, gpointer d) -> gboolean {
                             return static_cast<DokiApp*>(d)->on_stage_release(ev);
                         }),
                         this);
        g_signal_connect(stage_area_, "motion-notify-event",
                         G_CALLBACK(+[](GtkWidget*, GdkEventMotion* ev, gpointer d) -> gboolean {
                             return static_cast<DokiApp*>(d)->on_stage_motion(ev);
                         }),
                         this);

        if (!self_test_) {
            gtk_widget_show_all(window_);
            start_anim_timer();
        }
    }

    GtkWidget* make_dock_btn(const char* icon_name, const char* tooltip, GCallback cb) {
        GtkWidget* btn = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "dock-icon-btn");
        gtk_widget_set_tooltip_text(btn, tooltip);
        const auto& th = effective_theme();
        if (GtkWidget* img = icons_.make_gtk_image(icon_name, 18, th.r, th.g, th.b)) {
            gtk_button_set_image(GTK_BUTTON(btn), img);
            gtk_button_set_always_show_image(GTK_BUTTON(btn), TRUE);
        }
        dock_icon_buttons_.emplace_back(btn, icon_name ? icon_name : "");
        g_signal_connect(btn, "clicked", cb, this);
        return btn;
    }

    void build_dock_window() {
        dock_window_ = make_transparent_layer_window("doki-shimeji-dock", target_floating_layer(),
                                                     GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND, "doki-dock");
        g_signal_connect(dock_window_, "key-press-event",
                         G_CALLBACK(+[](GtkWidget*, GdkEventKey* ev, gpointer d) -> gboolean {
                             if (ev && ev->keyval == GDK_KEY_Escape) {
                                 static_cast<DokiApp*>(d)->close_all_panels();
                                 return TRUE;
                             }
                             return FALSE;
                         }),
                         this);

        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_name(card, "doki-dock-card");
        gtk_container_add(GTK_CONTAINER(dock_window_), card);

        gtk_box_pack_start(GTK_BOX(card),
                           make_dock_btn("refresh", "Nuevo dialogo", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                               static_cast<DokiApp*>(d)->request_thought("manual");
                           })),
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card),
                           make_dock_btn("chat", "Hablar con ella (chat)", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                               static_cast<DokiApp*>(d)->toggle_drawer(DrawerKind::Chat);
                           })),
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card),
                           make_dock_btn("users", "Dokis, Armas, Ropa y Rotacion 3D", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                               static_cast<DokiApp*>(d)->toggle_drawer(DrawerKind::Wardrobe);
                           })),
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card),
                           make_dock_btn("sparkle", "Otra Doki al azar (4 Dokis)", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                               static_cast<DokiApp*>(d)->randomize_character(true);
                           })),
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card),
                           make_dock_btn("bolt", "Hacer babosada / Pose 3D", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                               static_cast<DokiApp*>(d)->trigger_random_action();
                           })),
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card),
                           make_dock_btn("eye", "Mirar pantalla y decir babosada (Gemini)", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                               static_cast<DokiApp*>(d)->request_screen_comment(true);
                           })),
                           FALSE, FALSE, 0);

        bool init_3d = natsuki_engine_style_is_3d(engine_, style_id_.c_str()) != 0;
        btn_mode_toggle_ = gtk_button_new_with_label(init_3d ? "3D" : "2D");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_mode_toggle_), "dock-mode-btn");
        gtk_widget_set_tooltip_text(btn_mode_toggle_, "Alternar rapido entre modelo 3D y skin 2D PNG");
        g_signal_connect(btn_mode_toggle_, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->toggle_2d_3d_mode();
        }), this);
        gtk_box_pack_start(GTK_BOX(card), btn_mode_toggle_, FALSE, FALSE, 0);

        btn_swap_ = make_dock_btn("swap", "Cambiar de esquina (Izquierda / Derecha)",
                                  G_CALLBACK(+[](GtkWidget*, gpointer d) {
                                      static_cast<DokiApp*>(d)->toggle_side();
                                  }));
        gtk_box_pack_start(GTK_BOX(card), btn_swap_, FALSE, FALSE, 0);

        GtkWidget* btn_mon = gtk_button_new_with_label("Pantalla");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_mon), "dock-mode-btn");
        gtk_widget_set_tooltip_text(btn_mon, "Mover shimeji a la otra pantalla / monitor");
        g_signal_connect(btn_mon, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->cycle_monitor();
        }), this);
        gtk_box_pack_start(GTK_BOX(card), btn_mon, FALSE, FALSE, 0);

        btn_user_ = make_dock_btn("user", "Cambiar tu nombre", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->toggle_drawer(DrawerKind::NamePrompt);
        }));
        gtk_box_pack_start(GTK_BOX(card), btn_user_, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(card),
                           make_dock_btn("sliders", "Ajustes, Capa y Gemini API", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                               static_cast<DokiApp*>(d)->toggle_drawer(DrawerKind::Settings);
                           })),
                           FALSE, FALSE, 0);

        btn_pause_ = make_dock_btn("pause", "Pausar / Reanudar animacion y dialogos",
                                   G_CALLBACK(+[](GtkWidget*, gpointer d) {
                                       static_cast<DokiApp*>(d)->toggle_pause();
                                   }));
        gtk_box_pack_start(GTK_BOX(card), btn_pause_, FALSE, FALSE, 0);

        btn_sound_ = make_dock_btn("music", "Modo reactivo al sonido (Musica / Mic)",
                                   G_CALLBACK(+[](GtkWidget*, gpointer d) {
                                       static_cast<DokiApp*>(d)->toggle_sound_reactive();
                                   }));
        if (sound_reactive_) {
            gtk_style_context_add_class(gtk_widget_get_style_context(btn_sound_), "doki-btn-active");
        }
        gtk_box_pack_start(GTK_BOX(card), btn_sound_, FALSE, FALSE, 0);

        btn_voice_ = make_dock_btn("mic", "Comandos de voz por microfono (Vosk)",
                                   G_CALLBACK(+[](GtkWidget*, gpointer d) {
                                       static_cast<DokiApp*>(d)->toggle_voice_commands();
                                   }));
        if (voice_commands_enabled_) {
            gtk_style_context_add_class(gtk_widget_get_style_context(btn_voice_), "doki-btn-active");
        }
        gtk_box_pack_start(GTK_BOX(card), btn_voice_, FALSE, FALSE, 0);

        btn_theme_ = make_dock_btn("moon", "Tema barra y globo: Color Doki / Mono Oscuro / Mono Claro",
                                   G_CALLBACK(+[](GtkWidget*, gpointer d) {
                                       static_cast<DokiApp*>(d)->cycle_ui_theme();
                                   }));
        if (is_monochrome_theme()) {
            gtk_style_context_add_class(gtk_widget_get_style_context(btn_theme_), "doki-btn-active");
        }
        gtk_box_pack_start(GTK_BOX(card), btn_theme_, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(card),
                           make_dock_btn("eye-off", "Ocultar barra (Esc)", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                               static_cast<DokiApp*>(d)->close_all_panels();
                           })),
                           FALSE, FALSE, 0);

        btn_quit_ = gtk_button_new_with_label("Salir");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_quit_), "doki-quit-btn");
        gtk_widget_set_tooltip_text(btn_quit_, "Cerrar Doki Shimeji por completo");
        g_signal_connect(btn_quit_, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
            gtk_main_quit();
        }), this);
        gtk_button_set_always_show_image(GTK_BUTTON(btn_quit_), TRUE);
        gtk_box_pack_start(GTK_BOX(card), btn_quit_, FALSE, FALSE, 0);

        refresh_dock_icons_theme();
        gtk_widget_hide(dock_window_);
    }

    void build_drawer_window() {
        drawer_window_ = make_transparent_layer_window("doki-shimeji-drawer", target_floating_layer(),
                                                       GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND, "doki-drawer");
        g_signal_connect(drawer_window_, "key-press-event",
                         G_CALLBACK(+[](GtkWidget*, GdkEventKey* ev, gpointer d) -> gboolean {
                             if (ev && ev->keyval == GDK_KEY_Escape) {
                                 static_cast<DokiApp*>(d)->open_drawer(DrawerKind::None);
                                 return TRUE;
                             }
                             return FALSE;
                         }),
                         this);

        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_name(card, "doki-drawer-card");
        gtk_widget_set_size_request(card, 354, -1);
        gtk_container_add(GTK_CONTAINER(drawer_window_), card);

        GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        const auto& th = effective_theme();
        if (GtkWidget* badge = icons_.make_gtk_image("sparkle", 18, th.r, th.g, th.b)) {
            drawer_badge_ = badge;
            gtk_box_pack_start(GTK_BOX(header), badge, FALSE, FALSE, 0);
        }

        GtkWidget* title_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        lbl_drawer_title_ = gtk_label_new("Chat");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_drawer_title_), "doki-title");
        gtk_widget_set_halign(lbl_drawer_title_, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(title_col), lbl_drawer_title_, FALSE, FALSE, 0);

        lbl_drawer_tagline_ = gtk_label_new(doki_role_tagline(character_id_));
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_drawer_tagline_), "doki-tagline");
        gtk_widget_set_halign(lbl_drawer_tagline_, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(title_col), lbl_drawer_tagline_, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(header), title_col, TRUE, TRUE, 0);

        btn_drawer_close_ = gtk_button_new_with_label("Cerrar");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_drawer_close_), "doki-close-btn");
        gtk_widget_set_tooltip_text(btn_drawer_close_, "Cerrar este panel (Esc)");
        gtk_button_set_always_show_image(GTK_BUTTON(btn_drawer_close_), TRUE);
        g_signal_connect(btn_drawer_close_, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->open_drawer(DrawerKind::None);
        }), this);
        gtk_box_pack_end(GTK_BOX(header), btn_drawer_close_, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card), header, FALSE, FALSE, 0);
        refresh_dock_icons_theme();

        drawer_stack_ = gtk_stack_new();
        gtk_stack_set_homogeneous(GTK_STACK(drawer_stack_), FALSE);
        gtk_box_pack_start(GTK_BOX(card), drawer_stack_, TRUE, TRUE, 0);

        build_chat_page();
        build_wardrobe_page();
        build_settings_page();
        build_name_prompt_page();

        gtk_widget_hide(drawer_window_);
    }

    void build_chat_page() {
        GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(scroll, -1, 185);
        chat_history_view_ = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_history_view_), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_history_view_), GTK_WRAP_WORD_CHAR);
        gtk_container_add(GTK_CONTAINER(scroll), chat_history_view_);
        gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);

        GtkWidget* tip = gtk_label_new("Tip: escribe *amo x doki* para invocar la lluvia de Dokis!");
        gtk_style_context_add_class(gtk_widget_get_style_context(tip), "doki-subtitle");
        gtk_label_set_line_wrap(GTK_LABEL(tip), TRUE);
        gtk_widget_set_halign(tip, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(page), tip, FALSE, FALSE, 0);

        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        entry_chat_ = gtk_entry_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(entry_chat_), "doki-entry");
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry_chat_), "Escribe algo o *amo x doki*...");
        g_signal_connect(entry_chat_, "activate", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->on_chat_send();
        }), this);
        gtk_box_pack_start(GTK_BOX(row), entry_chat_, TRUE, TRUE, 0);

        GtkWidget* btn_send = gtk_button_new_with_label("Enviar");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_send), "doki-btn");
        g_signal_connect(btn_send, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->on_chat_send();
        }), this);
        gtk_box_pack_end(GTK_BOX(row), btn_send, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page), row, FALSE, FALSE, 0);

        gtk_stack_add_named(GTK_STACK(drawer_stack_), page, "chat");
    }

    void build_wardrobe_page() {
        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(scroll, -1, 325);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

        GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
        gtk_container_add(GTK_CONTAINER(scroll), page);

        GtkWidget* rand_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        check_random_doki_ = gtk_check_button_new_with_label("Cambio auto (4 Dokis)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_random_doki_), companion_random_);
        g_signal_connect(check_random_doki_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->set_companion_random(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_random_doki_)));
        }), this);
        GtkWidget* btn_rand_now = gtk_button_new_with_label("Otra al azar");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_rand_now), "doki-btn");
        g_signal_connect(btn_rand_now, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->randomize_character(true);
        }), this);
        gtk_box_pack_start(GTK_BOX(rand_row), check_random_doki_, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(rand_row), btn_rand_now, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page), rand_row, FALSE, FALSE, 0);

        GtkWidget* lbl_c = gtk_label_new("Elige una Doki:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_c), "doki-subtitle");
        gtk_widget_set_halign(lbl_c, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(page), lbl_c, FALSE, FALSE, 0);

        GtkWidget* pill_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        for (const auto& c : characters_) {
            GtkWidget* b = gtk_button_new_with_label(c.label.c_str());
            gtk_style_context_add_class(gtk_widget_get_style_context(b), "doki-btn");
            std::string* cid_heap = new std::string(c.id);
            g_object_set_data_full(G_OBJECT(b), "cid", cid_heap, [](gpointer p) {
                delete static_cast<std::string*>(p);
            });
            g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkWidget* w, gpointer d) {
                auto* cid = static_cast<std::string*>(g_object_get_data(G_OBJECT(w), "cid"));
                if (cid) static_cast<DokiApp*>(d)->select_character(*cid);
            }), this);
            gtk_box_pack_start(GTK_BOX(pill_row), b, TRUE, TRUE, 0);
            doki_pills_[c.id] = b;
        }
        gtk_box_pack_start(GTK_BOX(page), pill_row, FALSE, FALSE, 0);

        GtkWidget* lbl_s = gtk_label_new("Modelo / Ropa (L4D2 3D o Sprites 2D):");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_s), "doki-subtitle");
        gtk_widget_set_halign(lbl_s, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(page), lbl_s, FALSE, FALSE, 0);

        combo_style_ = gtk_combo_box_text_new();
        g_signal_connect(combo_style_, "changed", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->on_style_changed();
        }), this);
        gtk_box_pack_start(GTK_BOX(page), combo_style_, FALSE, FALSE, 0);

        // 3D Controls container (shown automatically when an L4D2 3D model is active)
        box_3d_controls_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

        GtkWidget* lbl_w = gtk_label_new("Arma en 3D:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_w), "doki-subtitle");
        gtk_widget_set_halign(lbl_w, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box_3d_controls_), lbl_w, FALSE, FALSE, 0);

        combo_weapon_ = gtk_combo_box_text_new();
        int nw = natsuki_engine_weapon_count();
        for (int i = 0; i < nw; ++i) {
            const char* wid = natsuki_engine_weapon_id(i);
            const char* wlbl = natsuki_engine_weapon_label(i);
            if (wid && wlbl) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_weapon_), wid, wlbl);
        }
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_weapon_), weapon_id_.c_str());
        g_signal_connect(combo_weapon_, "changed", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->on_weapon_changed();
        }), this);
        gtk_box_pack_start(GTK_BOX(box_3d_controls_), combo_weapon_, FALSE, FALSE, 0);

        box_rot_3d_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* btn_rl = gtk_button_new_with_label("Rotar -25");
        GtkWidget* btn_r0 = gtk_button_new_with_label("Frente");
        GtkWidget* btn_rr = gtk_button_new_with_label("Rotar +25");
        lbl_rot_3d_ = gtk_label_new("0 deg");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_rl), "doki-btn");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_r0), "doki-btn");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_rr), "doki-btn");
        g_signal_connect(btn_rl, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->adjust_3d_yaw(-25.0f);
        }), this);
        g_signal_connect(btn_r0, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->reset_3d_yaw();
        }), this);
        g_signal_connect(btn_rr, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->adjust_3d_yaw(25.0f);
        }), this);
        gtk_box_pack_start(GTK_BOX(box_rot_3d_), btn_rl, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box_rot_3d_), btn_r0, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box_rot_3d_), btn_rr, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(box_rot_3d_), lbl_rot_3d_, FALSE, FALSE, 4);
        gtk_box_pack_start(GTK_BOX(box_3d_controls_), box_rot_3d_, FALSE, FALSE, 0);

        GtkWidget* lbl_act = gtk_label_new("Babosadas / Poses 3D:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_act), "doki-subtitle");
        gtk_widget_set_halign(lbl_act, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box_3d_controls_), lbl_act, FALSE, FALSE, 0);

        struct ActBtnDef { const char* label; int32_t id; };
        static constexpr std::array<ActBtnDef, 7> kActBtns = {{
            {"Tropezon", 1}, {"Helicoptero", 2}, {"Disparar", 3}, {"Mortal", 4},
            {"Pose JoJo", 5}, {"Sentar", 6}, {"Baile", 7},
        }};
        GtkWidget* act_row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget* act_row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        for (size_t i = 0; i < kActBtns.size(); ++i) {
            GtkWidget* b = gtk_button_new_with_label(kActBtns[i].label);
            gtk_style_context_add_class(gtk_widget_get_style_context(b), "doki-btn");
            g_object_set_data(G_OBJECT(b), "aid", GINT_TO_POINTER(kActBtns[i].id));
            g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkWidget* w, gpointer d) {
                int32_t aid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "aid"));
                static_cast<DokiApp*>(d)->start_action(aid, true);
            }), this);
            gtk_box_pack_start(GTK_BOX(i < 4 ? act_row1 : act_row2), b, TRUE, TRUE, 0);
        }
        gtk_box_pack_start(GTK_BOX(box_3d_controls_), act_row1, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box_3d_controls_), act_row2, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(page), box_3d_controls_, FALSE, FALSE, 0);

        GtkWidget* roam_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        check_roam_ = gtk_check_button_new_with_label("Caminar en pantalla (solo 3D)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_roam_), roam_enabled_);
        g_signal_connect(check_roam_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->roam_enabled_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_roam_));
            s->walk_speed_norm_ = 0.0f;
            s->roam_walking_ = false;
            s->save_config();
            s->update_layout_geometry();
            s->render_sprite_now();
        }), this);
        check_antics_ = gtk_check_button_new_with_label("Babosadas auto");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_antics_), auto_antics_);
        g_signal_connect(check_antics_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->auto_antics_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_antics_));
            s->save_config();
        }), this);
        gtk_box_pack_start(GTK_BOX(roam_row), check_roam_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(roam_row), check_antics_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(page), roam_row, FALSE, FALSE, 0);

        GtkWidget* lbl_m = gtk_label_new("Probar expresion / animo:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_m), "doki-subtitle");
        gtk_widget_set_halign(lbl_m, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(page), lbl_m, FALSE, FALSE, 0);

        combo_mood_ = gtk_combo_box_text_new();
        for (const auto& m : moods_) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_mood_), m.id.c_str(), m.label.c_str());
        }
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_mood_), current_mood_.c_str());
        g_signal_connect(combo_mood_, "changed", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->on_mood_combo_changed();
        }), this);
        gtk_box_pack_start(GTK_BOX(page), combo_mood_, FALSE, FALSE, 0);

        refresh_wardrobe_controls();
        gtk_stack_add_named(GTK_STACK(drawer_stack_), scroll, "wardrobe");
    }

    void build_settings_page() {
        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(scroll, -1, 310);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

        GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_container_add(GTK_CONTAINER(scroll), page);

        // Side switcher button
        GtkWidget* side_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* lbl_side = gtk_label_new("Esquina:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_side), "doki-subtitle");
        btn_side_toggle_ = gtk_button_new_with_label(side_ == "right" ? "Derecha (cambiar a Izq)" : "Izquierda (cambiar a Der)");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_side_toggle_), "doki-btn");
        g_signal_connect(btn_side_toggle_, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->toggle_side();
        }), this);
        gtk_box_pack_start(GTK_BOX(side_row), lbl_side, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(side_row), btn_side_toggle_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(page), side_row, FALSE, FALSE, 0);

        // Monitor selector row
        GtkWidget* mon_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* lbl_mon = gtk_label_new("Pantalla:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_mon), "doki-subtitle");
        combo_monitor_ = gtk_combo_box_text_new();
        refresh_monitor_combo();
        g_signal_connect(combo_monitor_, "changed", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            const char* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(s->combo_monitor_));
            if (id && *id) {
                s->target_monitor_ = id;
                s->save_config();
                s->apply_monitor_to_windows();
            }
        }), this);
        GtkWidget* btn_mon_next = gtk_button_new_with_label("Mover");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_mon_next), "doki-btn");
        g_signal_connect(btn_mon_next, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->cycle_monitor();
        }), this);
        gtk_box_pack_start(GTK_BOX(mon_row), lbl_mon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mon_row), combo_monitor_, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(mon_row), btn_mon_next, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page), mon_row, FALSE, FALSE, 0);

        // User name input in Settings
        GtkWidget* name_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* lbl_name = gtk_label_new("Tu nombre:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_name), "doki-subtitle");
        entry_user_name_settings_ = gtk_entry_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(entry_user_name_settings_), "doki-entry");
        gtk_entry_set_text(GTK_ENTRY(entry_user_name_settings_), user_name_.c_str());
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry_user_name_settings_), "Ej. Jugador, Batman, Monika");
        GtkWidget* btn_save_name = gtk_button_new_with_label("Ok");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_save_name), "doki-btn");
        g_signal_connect(btn_save_name, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* self = static_cast<DokiApp*>(d);
            self->apply_user_name(gtk_entry_get_text(GTK_ENTRY(self->entry_user_name_settings_)), true);
        }), this);
        gtk_box_pack_start(GTK_BOX(name_row), lbl_name, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(name_row), entry_user_name_settings_, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(name_row), btn_save_name, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page), name_row, FALSE, FALSE, 0);

        // Size / Interval / Opacity sliders
        auto add_slider = [&](const char* label, double minv, double maxv, double step, double val,
                              GtkWidget** out, GCallback cb) {
            GtkWidget* l = gtk_label_new(label);
            gtk_style_context_add_class(gtk_widget_get_style_context(l), "doki-subtitle");
            gtk_widget_set_halign(l, GTK_ALIGN_START);
            gtk_box_pack_start(GTK_BOX(page), l, FALSE, FALSE, 0);
            GtkWidget* s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, minv, maxv, step);
            gtk_range_set_value(GTK_RANGE(s), val);
            g_signal_connect(s, "value-changed", cb, this);
            gtk_box_pack_start(GTK_BOX(page), s, FALSE, FALSE, 0);
            *out = s;
        };

        add_slider("Tamano (px)", 150, 520, 10, target_height_, &scale_height_,
                   G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<DokiApp*>(d)->on_settings_sliders_changed(); }));
        add_slider("Intervalo de dialogo (s)", 10, 300, 5, talk_interval_s_, &scale_interval_,
                   G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<DokiApp*>(d)->on_settings_sliders_changed(); }));
        check_random_doki_settings_ = gtk_check_button_new_with_label("Cambio automatico entre Dokis");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_random_doki_settings_), companion_random_);
        g_signal_connect(check_random_doki_settings_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->set_companion_random(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_random_doki_settings_)));
        }), this);
        gtk_box_pack_start(GTK_BOX(page), check_random_doki_settings_, FALSE, FALSE, 0);
        add_slider("Cambio aleatorio entre Dokis (s)", 15, 600, 15, auto_switch_sec_, &scale_switch_sec_,
                   G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<DokiApp*>(d)->on_settings_sliders_changed(); }));
        gtk_widget_set_sensitive(scale_switch_sec_, companion_random_ ? TRUE : FALSE);
        add_slider("Ver pantalla con IA cada (s)", 5, 300, 5, screen_watch_sec_, &scale_screen_sec_,
                   G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<DokiApp*>(d)->on_settings_sliders_changed(); }));
        add_slider("Opacidad del globo", 0.35, 1.0, 0.05, speech_opacity_, &scale_opacity_,
                   G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<DokiApp*>(d)->on_settings_sliders_changed(); }));

        GtkWidget* layer_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* lbl_layer = gtk_label_new("Capa:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_layer), "doki-subtitle");
        btn_layer_toggle_ = gtk_button_new_with_label(
            always_on_top_ ? "Encima de todo (Fullscreen)" : "Fondo (Detras de apps)");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_layer_toggle_), "doki-btn");
        g_signal_connect(btn_layer_toggle_, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->toggle_layer_mode();
        }), this);
        gtk_box_pack_start(GTK_BOX(layer_row), lbl_layer, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(layer_row), btn_layer_toggle_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(page), layer_row, FALSE, FALSE, 0);

        check_muted_ = gtk_check_button_new_with_label("Silenciar globos automaticos");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_muted_), muted_);
        g_signal_connect(check_muted_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->muted_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_muted_));
            s->save_config();
            s->send_brain_config();
        }), this);
        gtk_box_pack_start(GTK_BOX(page), check_muted_, FALSE, FALSE, 0);

        check_auto_ = gtk_check_button_new_with_label("Pensamientos autonomos (Gemini / offline)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_auto_), autonomous_);
        g_signal_connect(check_auto_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->autonomous_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_auto_));
            s->save_config();
            s->send_brain_config();
        }), this);
        gtk_box_pack_start(GTK_BOX(page), check_auto_, FALSE, FALSE, 0);

        check_screen_watch_ = gtk_check_button_new_with_label("Ver pantalla cada X s y decir babosadas (API Key)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_screen_watch_), screen_watch_enabled_);
        g_signal_connect(check_screen_watch_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->screen_watch_enabled_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_screen_watch_));
            s->screen_watch_elapsed_ = 0;
            s->save_config();
        }), this);
        gtk_box_pack_start(GTK_BOX(page), check_screen_watch_, FALSE, FALSE, 0);

        check_sound_ = gtk_check_button_new_with_label("Modo reactivo al sonido (musica / microfono)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_sound_), sound_reactive_);
        g_signal_connect(check_sound_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->sound_reactive_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_sound_));
            s->save_config();
            s->sync_audio_monitor_state();
        }), this);
        gtk_box_pack_start(GTK_BOX(page), check_sound_, FALSE, FALSE, 0);

        GtkWidget* audio_src_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* lbl_audio_src = gtk_label_new("Fuente de audio:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_audio_src), "doki-subtitle");
        combo_audio_source_ = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_audio_source_), "monitor", "Audio del PC (Musica / Juegos)");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_audio_source_), "mic", "Microfono");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_audio_source_), audio_source_.c_str());
        g_signal_connect(combo_audio_source_, "changed", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            if (const char* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(s->combo_audio_source_))) {
                s->audio_source_ = (std::string(id) == "mic") ? "mic" : "monitor";
                s->save_config();
                s->sync_audio_monitor_state();
            }
        }), this);
        gtk_box_pack_start(GTK_BOX(audio_src_row), lbl_audio_src, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(audio_src_row), combo_audio_source_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(page), audio_src_row, FALSE, FALSE, 0);

        add_slider("Sensibilidad de sonido", 0.4, 3.5, 0.1, audio_sensitivity_, &scale_audio_sens_,
                   G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<DokiApp*>(d)->on_settings_sliders_changed(); }));

        check_voice_ = gtk_check_button_new_with_label("Comandos de voz por microfono (Vosk local)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_voice_), voice_commands_enabled_);
        g_signal_connect(check_voice_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->voice_commands_enabled_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_voice_));
            s->sync_voice_ui_state();
            s->save_config();
            s->send_brain_config();
        }), this);
        gtk_box_pack_start(GTK_BOX(page), check_voice_, FALSE, FALSE, 0);

        check_voice_wake_ = gtk_check_button_new_with_label("Pedir palabra clave ('Oye' / 'Computadora') antes del comando");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_voice_wake_), voice_wake_word_);
        gtk_widget_set_sensitive(check_voice_wake_, voice_commands_enabled_ ? TRUE : FALSE);
        g_signal_connect(check_voice_wake_, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            s->voice_wake_word_ = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->check_voice_wake_));
            s->save_config();
            s->send_brain_config();
        }), this);
        gtk_box_pack_start(GTK_BOX(page), check_voice_wake_, FALSE, FALSE, 0);

        // UI Theme selector (barra y burbuja de texto)
        GtkWidget* theme_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(theme_row), gtk_label_new("Tema:"), FALSE, FALSE, 0);
        combo_ui_theme_ = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_ui_theme_), "auto", "Color Doki (Original)");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_ui_theme_), "mono_dark", "Monocromatico Oscuro");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_ui_theme_), "mono_light", "Monocromatico Claro");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_ui_theme_), ui_theme_.c_str());
        g_signal_connect(combo_ui_theme_, "changed", G_CALLBACK(+[](GtkComboBox* cb, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            if (const char* id = gtk_combo_box_get_active_id(cb)) {
                s->set_ui_theme(id, false);
            }
        }), this);
        gtk_box_pack_start(GTK_BOX(theme_row), combo_ui_theme_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(page), theme_row, FALSE, FALSE, 0);

        // Colors row
        GtkWidget* col_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        auto make_color_btn = [&](const std::string& hex, GCallback cb) {
            GdkRGBA c{};
            gdk_rgba_parse(&c, hex.c_str());
            GtkWidget* b = gtk_color_button_new_with_rgba(&c);
            g_signal_connect(b, "color-set", cb, this);
            return b;
        };
        color_bg_ = make_color_btn(speech_bg_, G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<DokiApp*>(d)->on_bubble_colors_changed(); }));
        color_fg_ = make_color_btn(speech_fg_, G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<DokiApp*>(d)->on_bubble_colors_changed(); }));
        color_border_ = make_color_btn(speech_border_, G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<DokiApp*>(d)->on_bubble_colors_changed(); }));
        gtk_box_pack_start(GTK_BOX(col_row), gtk_label_new("Globo:"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(col_row), color_bg_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(col_row), color_fg_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(col_row), color_border_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(page), col_row, FALSE, FALSE, 0);

        // Gemini API key + model + lang
        GtkWidget* lbl_api = gtk_label_new("Gemini API Key:");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl_api), "doki-subtitle");
        gtk_widget_set_halign(lbl_api, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(page), lbl_api, FALSE, FALSE, 0);

        entry_api_key_ = gtk_entry_new();
        gtk_entry_set_visibility(GTK_ENTRY(entry_api_key_), FALSE);
        gtk_style_context_add_class(gtk_widget_get_style_context(entry_api_key_), "doki-entry");
        gtk_entry_set_text(GTK_ENTRY(entry_api_key_), gemini_api_key_.c_str());
        gtk_box_pack_start(GTK_BOX(page), entry_api_key_, FALSE, FALSE, 0);

        combo_model_ = gtk_combo_box_text_new();
        bool found_model = false;
        for (const char* m : {
                 "gemini-3.8-flash",
                 "gemini-flash-latest",
                 "gemini-3.5-flash",
                 "gemini-3.1-flash-lite",
                 "gemini-2.5-flash",
                 "gemini-2.5-flash-lite",
                 "gemini-2.5-pro",
             }) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_model_), m, m);
            if (gemini_model_ == m) found_model = true;
        }
        if (!found_model && !gemini_model_.empty()) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_model_), gemini_model_.c_str(), gemini_model_.c_str());
        }
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_model_), gemini_model_.c_str());
        gtk_box_pack_start(GTK_BOX(page), combo_model_, FALSE, FALSE, 0);

        combo_lang_ = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_lang_), "en", "English");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_lang_), "es", "Espanol");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_lang_), language_.c_str());
        g_signal_connect(combo_lang_, "changed", G_CALLBACK(+[](GtkComboBox* cb, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            if (s->drawer_syncing_) return;
            if (const char* l = gtk_combo_box_get_active_id(cb)) {
                s->language_ = (std::string(l) == "es") ? "es" : "en";
                s->save_config();
                s->send_brain_config();
            }
        }), this);
        gtk_box_pack_start(GTK_BOX(page), combo_lang_, FALSE, FALSE, 0);

        GtkWidget* btn_save_api = gtk_button_new_with_label("Guardar ajustes Gemini");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_save_api), "doki-btn");
        g_signal_connect(btn_save_api, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            static_cast<DokiApp*>(d)->on_save_api_clicked();
        }), this);
        gtk_box_pack_start(GTK_BOX(page), btn_save_api, FALSE, FALSE, 0);

        GtkWidget* btn_look_now = gtk_button_new_with_label("Mirar pantalla y decir babosada ya");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_look_now), "doki-btn");
        g_signal_connect(btn_look_now, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            s->close_all_panels();
            s->request_screen_comment(true);
        }), this);
        gtk_box_pack_start(GTK_BOX(page), btn_look_now, FALSE, FALSE, 0);

        gtk_stack_add_named(GTK_STACK(drawer_stack_), scroll, "settings");
    }

    void build_name_prompt_page() {
        GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        GtkWidget* lbl = gtk_label_new(
            "Como te llamas? Las Dokis recordaran tu nombre en sus dialogos.\n"
            "(Prueba escribir tu nombre real... o algo raro como Batman o Monika!)");
        gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(page), lbl, FALSE, FALSE, 0);

        entry_name_prompt_ = gtk_entry_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(entry_name_prompt_), "doki-entry");
        gtk_entry_set_text(GTK_ENTRY(entry_name_prompt_), user_name_.c_str());
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry_name_prompt_), "Tu nombre...");
        g_signal_connect(entry_name_prompt_, "activate", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            s->apply_user_name(gtk_entry_get_text(GTK_ENTRY(s->entry_name_prompt_)), true);
            s->open_drawer(DrawerKind::None);
        }), this);
        gtk_box_pack_start(GTK_BOX(page), entry_name_prompt_, FALSE, FALSE, 0);

        GtkWidget* btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* btn_ok = gtk_button_new_with_label("Guardar nombre");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_ok), "doki-btn-active");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_ok), "doki-btn");
        g_signal_connect(btn_ok, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            s->apply_user_name(gtk_entry_get_text(GTK_ENTRY(s->entry_name_prompt_)), true);
            s->open_drawer(DrawerKind::None);
        }), this);
        GtkWidget* btn_skip = gtk_button_new_with_label("Luego");
        gtk_style_context_add_class(gtk_widget_get_style_context(btn_skip), "doki-btn");
        g_signal_connect(btn_skip, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) {
            auto* s = static_cast<DokiApp*>(d);
            s->open_drawer(DrawerKind::None);
            s->request_thought("startup");
        }), this);
        gtk_box_pack_start(GTK_BOX(btn_row), btn_ok, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(btn_row), btn_skip, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page), btn_row, FALSE, FALSE, 0);

        gtk_stack_add_named(GTK_STACK(drawer_stack_), page, "name_prompt");
    }

    void build_swarm_window() {
        swarm_window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(swarm_window_), FALSE);
        gtk_widget_set_app_paintable(swarm_window_, TRUE);
        gtk_widget_set_name(swarm_window_, "doki-swarm");
        if (GdkVisual* vis = gdk_screen_get_rgba_visual(gtk_widget_get_screen(swarm_window_))) {
            gtk_widget_set_visual(swarm_window_, vis);
        }
        g_signal_connect(swarm_window_, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer) -> gboolean {
            cairo_save(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
            cairo_paint(cr);
            cairo_restore(cr);
            return FALSE;
        }), nullptr);

        gtk_layer_init_for_window(GTK_WINDOW(swarm_window_));
        gtk_layer_set_namespace(GTK_WINDOW(swarm_window_), "doki-shimeji-swarm");
        if (GdkMonitor* mon = resolve_target_monitor()) {
            gtk_layer_set_monitor(GTK_WINDOW(swarm_window_), mon);
        }
        gtk_layer_set_layer(GTK_WINDOW(swarm_window_), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(swarm_window_), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_anchor(GTK_WINDOW(swarm_window_), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(swarm_window_), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(swarm_window_), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(swarm_window_), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

        swarm_area_ = gtk_drawing_area_new();
        gtk_container_add(GTK_CONTAINER(swarm_window_), swarm_area_);
        g_signal_connect(swarm_area_, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer d) -> gboolean {
            static_cast<DokiApp*>(d)->on_swarm_draw(cr);
            return FALSE;
        }), this);
        g_signal_connect(swarm_window_, "realize", G_CALLBACK(+[](GtkWidget* w, gpointer) {
            cairo_region_t* empty = cairo_region_create();
            gtk_widget_input_shape_combine_region(w, empty);
            cairo_region_destroy(empty);
        }), nullptr);

        gtk_widget_hide(swarm_window_);
    }

    NatsukiPoseParams build_live_pose_params() const {
        NatsukiPoseParams p{};
        p.walk_phase = walk_phase_;
        p.walk_speed = walk_speed_norm_;
        p.hair_lag_x = static_cast<float>(hair_x_);
        p.hair_lag_y = static_cast<float>(hair_y_);
        p.hair_bounce_z = static_cast<float>(hair_z_);
        p.skirt_lag_x = static_cast<float>(skirt_x_);
        p.skirt_lag_y = static_cast<float>(skirt_y_);
        p.skirt_flare = static_cast<float>(skirt_flare_);
        p.spin_vel = static_cast<float>(spin_vel_);
        p.sound_level = sound_reactive_ ? audio_snap_.level : 0.0f;
        p.action_id = action_id_;
        p.action_t = (action_id_ > 0 && action_duration_ > 0.01)
            ? static_cast<float>(std::clamp(action_timer_ / action_duration_, 0.0, 1.0))
            : 0.0f;
        if (action_id_ == 3 && weapon_id_ == "none" && !action_temp_weapon_.empty()) {
            p.weapon_id = action_temp_weapon_.c_str();
        } else {
            p.weapon_id = weapon_id_.c_str();
        }
        return p;
    }

    std::shared_ptr<FrameSurface> render_doki_frame(const std::string& cid, const std::string& sid,
                                                    const std::string& mood, bool blink,
                                                    uint32_t variant, int height,
                                                    float yaw_deg, float pitch_deg,
                                                    const NatsukiPoseParams* pose_opt = nullptr) {
        auto fs = std::make_shared<FrameSurface>();
        NatsukiPoseParams default_pose{};
        default_pose.weapon_id = weapon_id_.c_str();
        const NatsukiPoseParams* p = pose_opt ? pose_opt : &default_pose;
        int rc = natsuki_engine_render_character_frame_v2(
            engine_, cid.c_str(), sid.c_str(), mood.c_str(), blink ? 1 : 0,
            variant, height, yaw_deg, pitch_deg, p, &fs->raw);
        if (rc != 0 || !fs->raw.data || fs->raw.width <= 0 || fs->raw.height <= 0) {
            return nullptr;
        }
        fs->surface = cairo_image_surface_create_for_data(
            const_cast<uint8_t*>(fs->raw.data), CAIRO_FORMAT_ARGB32,
            fs->raw.width, fs->raw.height, fs->raw.stride);
        return fs;
    }

    void render_sprite_now() {
        NatsukiPoseParams pose = build_live_pose_params();
        std::shared_ptr<FrameSurface> next = render_doki_frame(
            character_id_, style_id_, current_mood_, is_blinking_,
            variant_idx_, target_height_, model_yaw_deg_, model_pitch_deg_, &pose);
        if (next) {
            current_frame_ = std::move(next);
            int sw = current_frame_->raw.width;
            int sh = current_frame_->raw.height;
            int sx = current_sprite_x(sw);
            sprite_rect_ = {sx, std::max(0, win_h_ - sh), sw, sh};
            if (std::abs(last_shape_sx_ - sx) >= 3) {
                update_input_shape();
            }
            if (stage_area_) gtk_widget_queue_draw(stage_area_);
        }
    }

    bool is_style_3d() const {
        return engine_ && natsuki_engine_style_is_3d(engine_, style_id_.c_str()) != 0;
    }

    bool is_roaming_active() const {
        return roam_enabled_ && is_style_3d();
    }

    bool spans_monitor_width() const {
        return is_roaming_active() || !is_style_3d();
    }

    void reset_2d_motion_if_needed() {
        if (is_style_3d()) return;
        walk_speed_norm_ = 0.0f;
        roam_walking_ = false;
        walk_x_ = -1.0;
        action_id_ = 0;
        action_timer_ = 0.0;
        action_hover_px_ = 0.0;
        action_temp_weapon_.clear();
        lifted_y_ = 0.0;
        if (drag_mode_ == DragMode::Lift) {
            drag_mode_ = DragMode::None;
        }
    }

    void set_companion_random(bool enabled) {
        companion_random_ = enabled;
        auto_switch_elapsed_ = 0;
        drawer_syncing_ = true;
        if (check_random_doki_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_random_doki_), companion_random_);
        }
        if (check_random_doki_settings_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_random_doki_settings_), companion_random_);
        }
        if (scale_switch_sec_) {
            gtk_widget_set_sensitive(scale_switch_sec_, companion_random_ ? TRUE : FALSE);
        }
        drawer_syncing_ = false;
        save_config();
    }

    int current_sprite_x(int sw) const {
        if (is_roaming_active()) {
            double cx = (walk_x_ >= 0.0) ? walk_x_ : ((side_ == "right") ? (win_w_ - sw * 0.5 - 12.0) : (sw * 0.5 + 12.0));
            int sx = static_cast<int>(std::round(cx - sw * 0.5));
            return std::clamp(sx, 4, std::max(4, win_w_ - sw - 4));
        }
        if (!is_style_3d() && manual_2d_x_ >= 0.0) {
            int sx = static_cast<int>(std::round(manual_2d_x_ - sw * 0.5));
            return std::clamp(sx, 4, std::max(4, win_w_ - sw - 4));
        }
        return (side_ == "right") ? std::max(4, win_w_ - sw - 10) : 10;
    }

    bool is_on_right_half(int sx, int sw) const {
        if (spans_monitor_width()) {
            return (sx + sw * 0.5) > (win_w_ * 0.5);
        }
        return (side_ == "right");
    }

    void apply_side_anchors() {
        int sw = current_frame_ ? current_frame_->raw.width : 170;
        int sh = current_frame_ ? current_frame_->raw.height : target_height_;
        int sx = current_sprite_x(sw);
        const bool right_side = is_on_right_half(sx, sw);

        if (window_) {
            if (spans_monitor_width()) {
                gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
                gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
                gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_LEFT, 0);
                gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
                gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
            } else {
                gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_LEFT, right_side ? FALSE : TRUE);
                gtk_layer_set_anchor(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, right_side ? TRUE : FALSE);
                gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_LEFT, right_side ? 0 : 8);
                gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_RIGHT, right_side ? 8 : 0);
                gtk_layer_set_margin(GTK_WINDOW(window_), GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
            }
        }

        auto apply_panel_lr = [&](GtkWidget* win, int margin_bottom) {
            if (!win) return;
            int raw_margin = right_side ? (win_w_ - sx + 12) : (sx + sw + 12);
            int max_m = std::max(14, win_w_ - 390);
            int margin_h = std::clamp(raw_margin, 14, max_m);
            gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, right_side ? FALSE : TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, right_side ? TRUE : FALSE);
            gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, right_side ? 0 : margin_h);
            gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, right_side ? margin_h : 0);
            gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, margin_bottom);
        };

        apply_panel_lr(dock_window_, 10);
        apply_panel_lr(drawer_window_, 68);

        sprite_rect_ = {sx, std::max(0, win_h_ - sh), sw, sh};

        if (btn_side_toggle_) {
            gtk_button_set_label(GTK_BUTTON(btn_side_toggle_),
                                 right_side ? "Derecha (cambiar a Izq)" : "Izquierda (cambiar a Der)");
        }
    }

    void update_layout_geometry() {
        int sw = current_frame_ ? current_frame_->raw.width : 170;
        int sh = current_frame_ ? current_frame_->raw.height : target_height_;
        if (spans_monitor_width()) {
            win_w_ = detect_monitor_width();
            if (stage_area_ && gtk_widget_get_realized(stage_area_)) {
                int aw = gtk_widget_get_allocated_width(stage_area_);
                if (aw >= 500) win_w_ = aw;
            }
            if (is_roaming_active()) {
                if (walk_x_ < 0.0) {
                    walk_x_ = (side_ == "right") ? (win_w_ - sw * 0.5 - 32.0) : (sw * 0.5 + 32.0);
                }
                walk_x_ = std::clamp(walk_x_, sw * 0.5 + 14.0, std::max(sw * 0.5 + 14.0, win_w_ - sw * 0.5 - 14.0));
            } else if (manual_2d_x_ >= 0.0) {
                manual_2d_x_ = std::clamp(manual_2d_x_, sw * 0.5 + 4.0, std::max(sw * 0.5 + 4.0, win_w_ - sw * 0.5 - 4.0));
            }
        } else {
            win_w_ = std::max(460, sw + 340);
        }
        win_h_ = std::max(sh + 165, 380);
        if (window_) {
            gtk_window_resize(GTK_WINDOW(window_), win_w_, win_h_);
            gtk_widget_set_size_request(stage_area_, spans_monitor_width() ? -1 : win_w_, win_h_);
        }
        apply_side_anchors();
        update_input_shape();
        if (stage_area_) gtk_widget_queue_draw(stage_area_);
    }

    void update_input_shape() {
        if (!window_ || !gtk_widget_get_realized(window_)) return;
        last_shape_sx_ = sprite_rect_.x;
        cairo_region_t* region = cairo_region_create();
        cairo_rectangle_int_t sr = sprite_rect_;
        int lift_pad = static_cast<int>(std::ceil(lifted_y_ + std::max(0.0, -bounce_offset_) + action_hover_px_));
        sr.x = std::max(0, sr.x - 14);
        sr.y = std::max(0, sr.y - 14 - lift_pad);
        sr.width += 28;
        sr.height += 28 + lift_pad;
        cairo_region_union_rectangle(region, &sr);
        if (bubble_visible_ && bubble_rect_.width > 0 && bubble_rect_.height > 0) {
            cairo_region_union_rectangle(region, &bubble_rect_);
        }
        gtk_widget_input_shape_combine_region(window_, region);
        cairo_region_destroy(region);
    }

    void toggle_side() {
        side_ = (side_ == "right") ? "left" : "right";
        manual_2d_x_ = -1.0;
        int sw = current_frame_ ? current_frame_->raw.width : 170;
        walk_x_ = (side_ == "right") ? (win_w_ - sw * 0.5 - 32.0) : (sw * 0.5 + 32.0);
        walk_dir_ = (side_ == "right") ? -1.0 : 1.0;
        reset_2d_motion_if_needed();
        if (std::abs(model_yaw_deg_) <= 25.0f) {
            model_yaw_deg_ = (side_ == "right") ? 14.0f : -14.0f;
            render_sprite_now();
        }
        save_config();
        update_layout_geometry();
    }

    void apply_character_choice(const std::string& cid, bool save) {
        character_id_ = cid;
        if (cfg_json_.contains("character_styles") && cfg_json_["character_styles"].is_object() &&
            cfg_json_["character_styles"].contains(cid)) {
            style_id_ = cfg_json_["character_styles"][cid].get<std::string>();
        }
        collect_styles_for_current_char();
        reset_2d_motion_if_needed();
        if (!is_monochrome_theme()) {
            const auto& th = theme_for_character(character_id_);
            speech_border_ = th.primary_hex;
            sync_bubble_color_choosers();
        }
        if (save) {
            save_config();
        }
    }

    void select_character(const std::string& cid, bool random_switch = false) {
        if (cid == character_id_ && !random_switch) return;
        apply_character_choice(cid, true);
        auto_switch_elapsed_ = 0;
        apply_character_css();
        refresh_dock_icons_theme();
        refresh_wardrobe_controls();
        render_sprite_now();
        update_layout_geometry();
        trigger_bounce(-4.5);
        skirt_flare_ = 0.65;
        hair_vz_ = 3.2;
        send_brain_cmd("character_changed", json{
            {"character", character_id_},
            {"random_switch", random_switch},
        });
    }

    void randomize_character(bool announce) {
        if (characters_.empty()) return;
        std::vector<std::string> pool;
        for (const auto& c : characters_) {
            if (c.id != character_id_ || characters_.size() == 1) {
                pool.push_back(c.id);
            }
        }
        if (pool.empty()) return;
        const std::string& next_cid = pool[g_random_int_range(0, static_cast<gint32>(pool.size()))];
        if (!announce) {
            apply_character_choice(next_cid, true);
            apply_character_css();
            refresh_dock_icons_theme();
            refresh_wardrobe_controls();
            render_sprite_now();
            update_layout_geometry();
        } else {
            select_character(next_cid, true);
        }
    }

    static const char* action_name_key(int32_t aid) {
        switch (aid) {
            case 1: return "trip";
            case 2: return "heli";
            case 3: return "weapon_show";
            case 4: return "flip";
            case 5: return "jojo";
            case 6: return "sit";
            case 7: return "dance";
            default: return "dance";
        }
    }

    void start_action(int32_t aid, bool announce) {
        if (!is_style_3d()) return;
        if (aid < 1 || aid > 7) return;
        action_id_ = aid;
        action_timer_ = 0.0;
        roam_walking_ = false;
        walk_speed_norm_ = 0.0f;
        action_base_yaw_ = model_yaw_deg_;
        action_hover_px_ = 0.0;
        action_temp_weapon_.clear();
        switch (aid) {
            case 1: action_duration_ = 2.25; skirt_flare_ = 0.55; hair_vy_ = -2.2; break;
            case 2: action_duration_ = 2.65; spin_vel_ = 2.8; break;
            case 3: {
                action_duration_ = 1.75;
                if (weapon_id_ == "none") action_temp_weapon_ = "auto";
                break;
            }
            case 4: action_duration_ = 1.65; hair_vz_ = 3.5; break;
            case 5: action_duration_ = 2.30; hair_vx_ = 1.8; break;
            case 6: action_duration_ = 3.40; break;
            case 7: action_duration_ = 2.75; break;
            default: action_duration_ = 2.20; break;
        }
        start_anim_timer();
        render_sprite_now();
        if (announce && !muted_) {
            send_brain_cmd("action_react", json{{"action", action_name_key(aid)}});
        }
    }

    void trigger_random_action() {
        int32_t aid = static_cast<int32_t>(g_random_int_range(1, 8));
        start_action(aid, true);
    }

    void on_weapon_changed() {
        if (drawer_syncing_ || !combo_weapon_) return;
        const char* active = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo_weapon_));
        if (!active || weapon_id_ == active) return;
        weapon_id_ = active;
        save_config();
        render_sprite_now();
        trigger_bounce(-3.2);
        if (!muted_ && weapon_id_ != "none") {
            send_brain_cmd("action_react", json{{"action", "weapon"}, {"weapon_id", weapon_id_}});
        }
    }

    void refresh_wardrobe_controls() {
        drawer_syncing_ = true;
        for (const auto& kv : doki_pills_) {
            GtkStyleContext* ctx = gtk_widget_get_style_context(kv.second);
            if (kv.first == character_id_) {
                gtk_style_context_add_class(ctx, "doki-btn-active");
            } else {
                gtk_style_context_remove_class(ctx, "doki-btn-active");
            }
        }
        bool is_3d = is_style_3d();
        if (check_random_doki_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_random_doki_), companion_random_);
        }
        if (check_random_doki_settings_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_random_doki_settings_), companion_random_);
        }
        if (scale_switch_sec_) {
            gtk_widget_set_sensitive(scale_switch_sec_, companion_random_ ? TRUE : FALSE);
        }
        if (check_roam_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_roam_), roam_enabled_);
            gtk_widget_set_sensitive(check_roam_, is_3d ? TRUE : FALSE);
            gtk_widget_set_tooltip_text(
                check_roam_,
                is_3d ? "Caminar libremente por la pantalla (solo shimejis 3D)"
                      : "Los shimejis 2D son fijos (usa Izquierda / Derecha)");
        }
        if (check_antics_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_antics_), auto_antics_);
            gtk_widget_set_sensitive(check_antics_, is_3d ? TRUE : FALSE);
        }
        if (combo_style_) {
            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combo_style_));
            for (const auto& s : styles_for_char_) {
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_style_), s.id.c_str(), s.label.c_str());
            }
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_style_), style_id_.c_str());
        }
        if (combo_weapon_) {
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_weapon_), weapon_id_.c_str());
        }
        if (box_3d_controls_) {
            gtk_widget_set_visible(box_3d_controls_, is_3d ? TRUE : FALSE);
        }
        if (box_rot_3d_) {
            gtk_widget_set_visible(box_rot_3d_, is_3d ? TRUE : FALSE);
            if (lbl_rot_3d_) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d deg", static_cast<int>(std::round(model_yaw_deg_)));
                gtk_label_set_text(GTK_LABEL(lbl_rot_3d_), buf);
            }
        }
        if (btn_mode_toggle_) {
            gtk_button_set_label(GTK_BUTTON(btn_mode_toggle_), is_3d ? "3D" : "2D");
        }
        if (btn_layer_toggle_) {
            gtk_button_set_label(GTK_BUTTON(btn_layer_toggle_),
                                 always_on_top_ ? "Encima de todo (Fullscreen)" : "Fondo (Detras de apps)");
        }
        if (check_screen_watch_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_screen_watch_), screen_watch_enabled_);
        }
        if (scale_screen_sec_) {
            gtk_range_set_value(GTK_RANGE(scale_screen_sec_), screen_watch_sec_);
        }
        if (lbl_drawer_tagline_) {
            gtk_label_set_text(GTK_LABEL(lbl_drawer_tagline_), doki_role_tagline(character_id_));
        }
        drawer_syncing_ = false;
    }

    void adjust_3d_yaw(float delta) {
        model_yaw_deg_ += delta;
        while (model_yaw_deg_ > 180.0f) model_yaw_deg_ -= 360.0f;
        while (model_yaw_deg_ < -180.0f) model_yaw_deg_ += 360.0f;
        if (lbl_rot_3d_) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d deg", static_cast<int>(std::round(model_yaw_deg_)));
            gtk_label_set_text(GTK_LABEL(lbl_rot_3d_), buf);
        }
        render_sprite_now();
    }

    void reset_3d_yaw() {
        model_yaw_deg_ = 0.0f;
        adjust_3d_yaw(0.0f);
    }

    void on_style_changed() {
        if (drawer_syncing_ || !combo_style_) return;
        const char* active = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo_style_));
        if (!active || style_id_ == active) return;
        style_id_ = active;
        reset_2d_motion_if_needed();
        refresh_wardrobe_controls();
        save_config();
        render_sprite_now();
        update_layout_geometry();
    }

    void on_mood_combo_changed() {
        if (drawer_syncing_ || !combo_mood_) return;
        const char* active = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo_mood_));
        if (!active) return;
        current_mood_ = active;
        variant_idx_ = (variant_idx_ + 1) % 6;
        render_sprite_now();
    }

    void on_settings_sliders_changed() {
        if (drawer_syncing_) return;
        target_height_ = static_cast<int>(std::round(gtk_range_get_value(GTK_RANGE(scale_height_))));
        talk_interval_s_ = static_cast<int>(std::round(gtk_range_get_value(GTK_RANGE(scale_interval_))));
        if (scale_switch_sec_) {
            auto_switch_sec_ = static_cast<int>(std::round(gtk_range_get_value(GTK_RANGE(scale_switch_sec_))));
        }
        if (scale_screen_sec_) {
            screen_watch_sec_ = std::clamp(static_cast<int>(std::round(gtk_range_get_value(GTK_RANGE(scale_screen_sec_)))), 5, 600);
            screen_watch_elapsed_ = 0;
        }
        speech_opacity_ = gtk_range_get_value(GTK_RANGE(scale_opacity_));
        if (scale_audio_sens_) {
            audio_sensitivity_ = gtk_range_get_value(GTK_RANGE(scale_audio_sens_));
            audio_monitor_.set_sensitivity(audio_sensitivity_);
        }
        save_config();
        render_sprite_now();
        update_layout_geometry();
        send_brain_config();
    }

    void on_bubble_colors_changed() {
        if (drawer_syncing_) return;
        GdkRGBA c{};
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_bg_), &c);
        speech_bg_ = color_to_hex(c);
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_fg_), &c);
        speech_fg_ = color_to_hex(c);
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_border_), &c);
        speech_border_ = color_to_hex(c);
        save_config();
        gtk_widget_queue_draw(stage_area_);
    }

    void on_save_api_clicked() {
        gemini_api_key_ = gtk_entry_get_text(GTK_ENTRY(entry_api_key_));
        if (const char* m = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo_model_))) gemini_model_ = m;
        if (const char* l = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo_lang_))) language_ = l;
        save_config();
        send_brain_config();
        show_bubble(tr("Settings saved.", "Ajustes guardados."), "happy");
    }

    void apply_user_name(const std::string& raw, bool announce) {
        char* stripped = natsuki_strip_emojis(raw.c_str());
        std::string cleaned = stripped ? std::string(stripped) : raw;
        if (stripped) natsuki_string_free(stripped);
        while (!cleaned.empty() && std::isspace(static_cast<unsigned char>(cleaned.front()))) cleaned.erase(cleaned.begin());
        while (!cleaned.empty() && std::isspace(static_cast<unsigned char>(cleaned.back()))) cleaned.pop_back();

        user_name_ = cleaned;
        if (entry_user_name_settings_) {
            gtk_entry_set_text(GTK_ENTRY(entry_user_name_settings_), user_name_.c_str());
        }
        if (entry_name_prompt_) {
            gtk_entry_set_text(GTK_ENTRY(entry_name_prompt_), user_name_.c_str());
        }
        save_config();
        send_brain_config();
        if (announce && !user_name_.empty()) {
            send_brain_cmd("name_changed", json{{"user_name", user_name_}});
        }
    }

    void toggle_pause() {
        paused_ = !paused_;
        if (btn_pause_) {
            const auto& th = effective_theme();
            if (GtkWidget* img = icons_.make_gtk_image(paused_ ? "play" : "pause", 18, th.r, th.g, th.b)) {
                gtk_button_set_image(GTK_BUTTON(btn_pause_), img);
            }
        }
        if (!paused_) {
            last_tick_us_ = g_get_monotonic_time();
            start_anim_timer();
        }
    }

    void toggle_2d_3d_mode() {
        bool currently_3d = natsuki_engine_style_is_3d(engine_, style_id_.c_str()) != 0;
        std::string next_style;
        for (const auto& s : styles_for_char_) {
            bool s_is_3d = natsuki_engine_style_is_3d(engine_, s.id.c_str()) != 0;
            if (currently_3d != s_is_3d) {
                if (next_style.empty()) next_style = s.id;
                if (currently_3d && (s.id == "classic" || s.id == "uniform")) {
                    next_style = s.id;
                    break;
                }
            }
        }
        if (next_style.empty() || next_style == style_id_) return;
        style_id_ = next_style;
        reset_2d_motion_if_needed();
        refresh_wardrobe_controls();
        save_config();
        render_sprite_now();
        update_layout_geometry();
        trigger_bounce(-4.0);
    }

    void apply_layer_policy() {
        if (self_test_) return;
        GtkLayerShellLayer layer = target_floating_layer();
        for (GtkWidget* w : {window_, dock_window_, drawer_window_}) {
            if (w) gtk_layer_set_layer(GTK_WINDOW(w), layer);
        }
        if (swarm_window_) {
            gtk_layer_set_layer(GTK_WINDOW(swarm_window_), GTK_LAYER_SHELL_LAYER_OVERLAY);
        }
    }

    void toggle_layer_mode() {
        always_on_top_ = !always_on_top_;
        if (!self_test_) {
            apply_layer_policy();
        } else {
            // Mirror runtime layer update cleanly during headless verification
            for (GtkWidget* w : {window_, dock_window_, drawer_window_}) {
                if (w) gtk_layer_set_layer(GTK_WINDOW(w), target_floating_layer());
            }
        }
        save_config();
        refresh_wardrobe_controls();
        show_bubble(always_on_top_
                        ? tr("OVERLAY layer: now floating even over fullscreen games!",
                             "Capa OVERLAY: ahora floto hasta encima de juegos fullscreen!")
                        : tr("Background mode: tucked behind your apps.",
                             "Modo fondo: ahora quedo detras de tus apps."),
                    "happy");
    }

    void sync_audio_monitor_state() {
        audio_monitor_.set_sensitivity(audio_sensitivity_);
        if (sound_reactive_ && !self_test_) {
            audio_monitor_.start(audio_source_);
        } else {
            audio_monitor_.stop();
        }
        if (btn_sound_) {
            GtkStyleContext* ctx = gtk_widget_get_style_context(btn_sound_);
            if (sound_reactive_) gtk_style_context_add_class(ctx, "doki-btn-active");
            else gtk_style_context_remove_class(ctx, "doki-btn-active");
        }
        if (check_sound_) {
            drawer_syncing_ = true;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_sound_), sound_reactive_);
            drawer_syncing_ = false;
        }
        if (!sound_reactive_) {
            audio_snap_ = AudioSnapshot{};
            audio_squash_ = 0.0;
            audio_sway_rad_ = 0.0;
        }
        if (stage_area_) gtk_widget_queue_draw(stage_area_);
    }

    void toggle_sound_reactive() {
        sound_reactive_ = !sound_reactive_;
        save_config();
        sync_audio_monitor_state();
        if (sound_reactive_) {
            last_audio_react_us_ = g_get_monotonic_time();
            spawn_audio_notes(2);
            show_bubble(audio_source_ == "mic"
                            ? tr("Sound mode active! Listening to your mic!",
                                 "Modo sonido activo! Escuchando tu microfono!")
                            : tr("Sound mode active! Vibing to your PC audio!",
                                 "Modo sonido activo! Bailare al ritmo de tu PC!"),
                        "happy");
            trigger_bounce(-4.5);
        } else {
            show_bubble(tr("Sound mode off.", "Modo sonido desactivado."), "neutral");
        }
    }

    void sync_voice_ui_state() {
        if (btn_voice_) {
            GtkStyleContext* ctx = gtk_widget_get_style_context(btn_voice_);
            if (voice_commands_enabled_) gtk_style_context_add_class(ctx, "doki-btn-active");
            else gtk_style_context_remove_class(ctx, "doki-btn-active");
        }
        drawer_syncing_ = true;
        if (check_voice_) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_voice_), voice_commands_enabled_);
        }
        if (check_voice_wake_) {
            gtk_widget_set_sensitive(check_voice_wake_, voice_commands_enabled_ ? TRUE : FALSE);
        }
        drawer_syncing_ = false;
    }

    void toggle_voice_commands() {
        voice_commands_enabled_ = !voice_commands_enabled_;
        sync_voice_ui_state();
        save_config();
        send_brain_config();
        if (voice_commands_enabled_) {
            show_bubble(
                tr("Voice commands active! Say 'abrir librewolf', 'subir el volumen', 'pausa'...",
                   "Comandos de voz activos! Di 'abrir librewolf', 'subir el volumen', 'pausa'..."),
                "happy");
            trigger_bounce(-4.0);
        } else {
            show_bubble(tr("Voice commands paused.", "Comandos de voz desactivados."), "neutral");
        }
    }

    void spawn_audio_notes(int count) {
        int sw = current_frame_ ? current_frame_->raw.width : 170;
        int sh = current_frame_ ? current_frame_->raw.height : target_height_;
        int sx = current_sprite_x(sw);
        for (int i = 0; i < count; ++i) {
            if (audio_notes_.size() >= 26) break;
            uint32_t seed = static_cast<uint32_t>(audio_notes_.size() * 73 + last_beat_seq_ * 31 + i * 19);
            AudioNoteParticle p;
            double spread = ((seed % 100) / 100.0 - 0.5) * std::max(54.0, sw * 0.72);
            p.x = sx + sw * 0.5 + spread;
            p.y = std::max(28.0, static_cast<double>(win_h_) - sh * (0.62 + ((seed >> 3) % 30) / 100.0));
            p.vx = (((seed >> 6) % 100) / 100.0 - 0.5) * 26.0;
            p.vy = -(34.0 + ((seed >> 9) % 24));
            p.size = 11.0 + ((seed >> 11) % 7);
            p.life = 0.0;
            p.max_life = 1.15 + ((seed >> 13) % 45) / 100.0;
            p.kind = static_cast<int>(seed % 3);
            audio_notes_.push_back(p);
        }
    }

    void close_all_panels() {
        dock_visible_ = false;
        open_drawer(DrawerKind::None);
        if (dock_window_ && !self_test_) {
            gtk_widget_hide(dock_window_);
        }
    }

    void toggle_dock() {
        dock_visible_ = !dock_visible_;
        if (!dock_visible_) {
            open_drawer(DrawerKind::None);
        }
        if (!self_test_ && dock_window_) {
            if (dock_visible_) {
                gtk_widget_show_all(dock_window_);
                gtk_window_present(GTK_WINDOW(dock_window_));
            } else {
                gtk_widget_hide(dock_window_);
            }
        }
    }

    void toggle_drawer(DrawerKind kind) {
        open_drawer(active_drawer_ == kind ? DrawerKind::None : kind);
    }

    void open_drawer(DrawerKind kind) {
        active_drawer_ = kind;
        if (!drawer_window_) return;
        if (kind == DrawerKind::None) {
            gtk_widget_hide(drawer_window_);
            update_layout_geometry();
            return;
        }
        const char* page = "chat";
        const char* title = "Chat";
        if (kind == DrawerKind::Wardrobe) {
            page = "wardrobe";
            title = "Dokis, Ropa y 3D";
            refresh_wardrobe_controls();
        } else if (kind == DrawerKind::Settings) {
            page = "settings";
            title = "Ajustes";
        } else if (kind == DrawerKind::NamePrompt) {
            page = "name_prompt";
            title = "Como te llamas?";
        }
        gtk_label_set_text(GTK_LABEL(lbl_drawer_title_), title);
        gtk_stack_set_visible_child_name(GTK_STACK(drawer_stack_), page);
        update_layout_geometry();
        if (!self_test_) {
            gtk_widget_show_all(drawer_window_);
            refresh_wardrobe_controls();  // ensure box_rot_3d_ visibility stays accurate after show_all
        }
    }

    void append_chat_line(const std::string& speaker, const std::string& text) {
        if (!chat_history_view_) return;
        GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_history_view_));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buf, &end);
        std::string line = speaker + ": " + text + "\n";
        gtk_text_buffer_insert(buf, &end, line.c_str(), -1);
    }

    void on_chat_send() {
        if (!entry_chat_) return;
        std::string msg = gtk_entry_get_text(GTK_ENTRY(entry_chat_));
        while (!msg.empty() && std::isspace(static_cast<unsigned char>(msg.front()))) msg.erase(msg.begin());
        while (!msg.empty() && std::isspace(static_cast<unsigned char>(msg.back()))) msg.pop_back();
        if (msg.empty()) return;
        gtk_entry_set_text(GTK_ENTRY(entry_chat_), "");
        append_chat_line(user_name_.empty() ? tr("You", "Tu") : user_name_, msg);
        if (contains_swarm_trigger_local(msg)) {
            trigger_doki_swarm();
        }
        send_brain_cmd("chat", json{{"text", msg}});
    }

    void send_brain_config() {
        send_brain_cmd("set_config", json{{"config", {
            {"gemini_api_key", gemini_api_key_},
            {"gemini_model", gemini_model_},
            {"model", gemini_model_},
            {"language", language_},
            {"character", character_id_},
            {"user_name", user_name_},
            {"side", side_},
            {"autonomous", autonomous_},
            {"custom_system_prompt", custom_system_prompt_},
            {"voice_commands_enabled", voice_commands_enabled_ && !self_test_},
            {"voice_wake_word", voice_wake_word_},
        }}});
    }

    void send_brain_cmd(const std::string& cmd, json extra = json::object()) {
        if (!brain_) return;
        extra["cmd"] = cmd;
        extra["character"] = character_id_;
        extra["language"] = language_;
        extra["user_name"] = user_name_;
        extra["side"] = side_;
        extra["current_mood"] = current_mood_;
        for (const auto& s : styles_for_char_) {
            if (s.id == style_id_) {
                extra["style_label"] = s.label;
                break;
            }
        }
        brain_->send(extra);
    }

    void request_thought(const std::string& trigger) {
        if (trigger == "poke_zone1") {
            send_brain_cmd("poke", json{{"zone", "zone1"}, {"clicks", ++poke_clicks_}});
            return;
        }
        if (trigger == "poke_zone2") {
            send_brain_cmd("poke", json{{"zone", "zone2"}, {"clicks", ++poke_clicks_}});
            return;
        }
        if (trigger == "poke_head") {
            send_brain_cmd("poke", json{{"zone", "head"}, {"clicks", ++poke_clicks_}});
            return;
        }
        if (trigger == "click" || trigger == "poke") {
            send_brain_cmd("poke", json{{"zone", "normal"}, {"clicks", ++poke_clicks_}});
            return;
        }
        if (paused_ && trigger != "manual") return;
        send_brain_cmd("spontaneous", json{{"manual", trigger == "manual"}, {"trigger", trigger}});
    }

    void request_screen_comment(bool manual) {
        if (screen_watch_inflight_ && !manual) return;
        screen_watch_inflight_ = true;
        screen_watch_inflight_since_us_ = g_get_monotonic_time();
        screen_watch_elapsed_ = 0;
        send_brain_cmd("screen_comment", json{{"manual", manual}, {"monitor_x", detect_monitor_x()}});
    }

    void start_anim_timer() {
        if (anim_timer_id_ || self_test_) return;
        last_tick_us_ = g_get_monotonic_time();
        anim_timer_id_ = g_timeout_add(16, [](gpointer d) -> gboolean {
            return static_cast<DokiApp*>(d)->on_anim_tick() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
        }, this);
    }

    void trigger_bounce(double initial_vel) {
        bounce_vel_ = initial_vel;
        start_anim_timer();
    }

    static void spring_step(double& pos, double& vel, double target, double k, double damp, double dt) {
        double force = (target - pos) * k - vel * damp;
        vel += force * dt;
        pos += vel * dt;
    }

    bool on_anim_tick() {
        gint64 now = g_get_monotonic_time();
        double dt = (last_tick_us_ > 0) ? (now - last_tick_us_) / 1000000.0 : (1.0 / 60.0);
        last_tick_us_ = now;
        if (dt < 0.0) dt = 0.0;
        if (dt > 0.05) dt = 0.05;

        if (is_roaming_active() && stage_area_ && gtk_widget_get_realized(stage_area_)) {
            int aw = gtk_widget_get_allocated_width(stage_area_);
            if (aw >= 500 && aw != win_w_) {
                win_w_ = aw;
            }
        }

        int sw = current_frame_ ? current_frame_->raw.width : 170;
        bool active_bounce = (std::abs(bounce_vel_) > 0.02 || std::abs(bounce_offset_) > 0.05);
        if (active_bounce) {
            double step = dt * 60.0;
            double prev_vel = bounce_vel_;
            bounce_offset_ += bounce_vel_ * step;
            bounce_vel_ += 0.72 * step;
            if (bounce_offset_ > 0.0) {
                bounce_offset_ = 0.0;
                if (bounce_vel_ > 2.0) {
                    bounce_vel_ = -bounce_vel_ * 0.28;
                    skirt_vflare_ += 4.2;
                    hair_vz_ -= 2.4;
                } else {
                    bounce_vel_ = 0.0;
                    if (prev_vel > 1.0) {
                        skirt_vflare_ += 2.2;
                    }
                }
            }
        } else {
            bounce_offset_ = 0.0;
            bounce_vel_ = 0.0;
        }

        const bool is_3d = natsuki_engine_style_is_3d(engine_, style_id_.c_str()) != 0;
        const bool walker_paused = paused_ || dock_visible_ || active_drawer_ != DrawerKind::None || pointer_pressed_;
        bool force_pose_render = false;

        if (!paused_) {
            // Smooth 60fps time-based breathing (~2.9s cycle) painted with sub-pixel Cairo bilinear filter.
            breath_phase_ += dt * 2.15;
            if (breath_phase_ > 628.318530718) breath_phase_ -= 628.318530718;
            breath_amount_ = 0.5 - 0.5 * std::cos(breath_phase_);

            // Autonomous goofy antics ("babosadas", 1..7) & roaming are strictly for 3D shimejis;
            // 2D shimejis remain anchored left/right (`side_`).
            if (!is_3d) {
                if (action_id_ > 0 || walk_speed_norm_ > 0.0f || roam_walking_) {
                    reset_2d_motion_if_needed();
                }
            } else if (action_id_ > 0) {
                action_timer_ += dt;
                double t = (action_duration_ > 0.01) ? std::clamp(action_timer_ / action_duration_, 0.0, 1.0) : 1.0;
                if (action_id_ == 2) {
                    model_yaw_deg_ = action_base_yaw_ + static_cast<float>(t * 720.0);
                    while (model_yaw_deg_ > 180.0f) model_yaw_deg_ -= 360.0f;
                    while (model_yaw_deg_ < -180.0f) model_yaw_deg_ += 360.0f;
                    spin_vel_ = 2.8;
                    action_hover_px_ = std::sin(t * M_PI) * 28.0;
                } else if (action_id_ == 4) {
                    action_hover_px_ = (t > 0.16 && t < 0.84) ? std::sin((t - 0.16) / 0.68 * M_PI) * 62.0 : 0.0;
                } else {
                    action_hover_px_ = 0.0;
                }
                if (action_timer_ >= action_duration_) {
                    if (action_id_ == 2) {
                        model_yaw_deg_ = action_base_yaw_;
                    }
                    action_id_ = 0;
                    action_timer_ = 0.0;
                    action_hover_px_ = 0.0;
                    action_temp_weapon_.clear();
                    trigger_bounce(-2.5);
                    force_pose_render = true;
                }
            } else if (is_roaming_active() && !walker_paused) {
                if (auto_antics_ && !self_test_) {
                    next_antic_timer_ -= dt;
                    if (next_antic_timer_ <= 0.0) {
                        next_antic_timer_ = 12.0 + g_random_int_range(0, 14);
                        trigger_random_action();
                    }
                }
                if (action_id_ == 0) {
                    walk_state_timer_ -= dt;
                    if (walk_state_timer_ <= 0.0) {
                        roam_walking_ = !roam_walking_;
                        if (roam_walking_) {
                            walk_state_timer_ = 2.8 + g_random_int_range(0, 36) / 10.0;
                            double min_x = sw * 0.5 + 24.0;
                            double max_x = std::max(min_x + 20.0, win_w_ - sw * 0.5 - 24.0);
                            if (walk_x_ <= min_x + 40.0) walk_dir_ = 1.0;
                            else if (walk_x_ >= max_x - 40.0) walk_dir_ = -1.0;
                            else if (g_random_int_range(0, 100) < 42) walk_dir_ = -walk_dir_;
                        } else {
                            walk_state_timer_ = 1.9 + g_random_int_range(0, 26) / 10.0;
                        }
                    }
                    if (roam_walking_) {
                        walk_speed_norm_ = std::min(1.0f, walk_speed_norm_ + static_cast<float>(dt * 3.4));
                        double speed_px = 104.0 * walk_speed_norm_;
                        walk_x_ += walk_dir_ * speed_px * dt;
                        double min_x = sw * 0.5 + 18.0;
                        double max_x = std::max(min_x, win_w_ - sw * 0.5 - 18.0);
                        if (walk_x_ <= min_x) {
                            walk_x_ = min_x;
                            walk_dir_ = 1.0;
                        } else if (walk_x_ >= max_x) {
                            walk_x_ = max_x;
                            walk_dir_ = -1.0;
                        }
                        walk_phase_ += static_cast<float>(dt * 7.2 * walk_speed_norm_);
                        if (walk_phase_ > 628.3185f) walk_phase_ -= 628.3185f;
                        float tgt_yaw = (walk_dir_ > 0.0) ? 46.0f : -46.0f;
                        float dyaw = tgt_yaw - model_yaw_deg_;
                        while (dyaw > 180.0f) dyaw -= 360.0f;
                        while (dyaw < -180.0f) dyaw += 360.0f;
                        model_yaw_deg_ += dyaw * static_cast<float>(std::min(1.0, dt * 7.5));
                    } else {
                        walk_speed_norm_ = std::max(0.0f, walk_speed_norm_ - static_cast<float>(dt * 4.5));
                        if (walk_speed_norm_ <= 0.01f && std::abs(model_yaw_deg_) > 32.0f) {
                            float idle_yaw = is_on_right_half(current_sprite_x(sw), sw) ? 16.0f : -16.0f;
                            model_yaw_deg_ += (idle_yaw - model_yaw_deg_) * static_cast<float>(std::min(1.0, dt * 4.5));
                        }
                    }
                }
            } else {
                walk_speed_norm_ = std::max(0.0f, walk_speed_norm_ - static_cast<float>(dt * 5.0));
                roam_walking_ = false;
            }

            if (sound_reactive_) {
                audio_snap_ = audio_monitor_.snapshot();
                audio_sway_phase_ += dt * (4.0 + 5.2 * audio_snap_.level);
                if (audio_sway_phase_ > 628.318530718) audio_sway_phase_ -= 628.318530718;
                double tgt_sway = (audio_snap_.level > 0.04f)
                    ? std::sin(audio_sway_phase_) * (0.010 + 0.066 * audio_snap_.level)
                    : 0.0;
                audio_sway_rad_ += (tgt_sway - audio_sway_rad_) * std::min(1.0, dt * 14.0);
                audio_squash_ += (audio_snap_.bass - audio_squash_) * std::min(1.0, dt * 18.0);

                if (audio_snap_.beat_seq != last_beat_seq_) {
                    last_beat_seq_ = audio_snap_.beat_seq;
                    spawn_audio_notes((audio_snap_.beat_strength > 0.65f) ? 2 : 1);
                    if (std::abs(bounce_offset_) < 1.5 && bounce_vel_ == 0.0) {
                        bounce_vel_ = -(2.6 + 2.4 * audio_snap_.beat_strength);
                        hair_vz_ += 2.2 * audio_snap_.beat_strength;
                        skirt_vflare_ += 2.0 * audio_snap_.beat_strength;
                    }
                    if (!muted_ && now - last_audio_react_us_ > 24000000 &&
                        (audio_snap_.level > 0.72f || audio_snap_.beat_strength > 0.78f)) {
                        last_audio_react_us_ = now;
                        send_brain_cmd("sound_react", json{
                            {"loud", audio_snap_.level > 0.85f},
                            {"audio_source", audio_source_},
                        });
                    }
                }
            } else {
                audio_snap_ = AudioSnapshot{};
                audio_sway_rad_ *= 0.82;
                audio_squash_ *= 0.82;
            }

            // 60Hz Skirt & Hair Spring-Damper Physics
            double yaw_rad = model_yaw_deg_ * M_PI / 180.0;
            double trail_screen_x = -walk_dir_ * walk_speed_norm_ * 0.92;
            double tx_model = trail_screen_x * std::cos(yaw_rad) + 0.14 * std::sin(breath_phase_ * 0.85);
            double ty_model = -trail_screen_x * std::sin(yaw_rad) + 0.08 * std::cos(breath_phase_ * 0.65);
            double tgt_z = std::clamp(-bounce_vel_ * 0.09 + (lifted_y_ > 2.0 ? -0.45 : 0.0) +
                                          (sound_reactive_ ? audio_snap_.bass * 0.55 : 0.0),
                                      -1.2, 1.2);
            double tgt_flare = std::clamp(walk_speed_norm_ * 0.34 + std::abs(spin_vel_) * 0.38 +
                                              (lifted_y_ > 2.0 ? 0.35 : 0.0) +
                                              (sound_reactive_ ? audio_snap_.bass * 0.40 : 0.0),
                                          0.0, 1.5);
            spring_step(hair_x_, hair_vx_, std::clamp(tx_model, -1.4, 1.4), 56.0, 5.2, dt);
            spring_step(hair_y_, hair_vy_, std::clamp(ty_model, -1.4, 1.4), 56.0, 5.2, dt);
            spring_step(hair_z_, hair_vz_, tgt_z, 64.0, 5.8, dt);
            spring_step(skirt_x_, skirt_vx_, std::clamp(tx_model * 0.85, -1.3, 1.3), 68.0, 6.2, dt);
            spring_step(skirt_y_, skirt_vy_, std::clamp(ty_model * 0.85, -1.3, 1.3), 68.0, 6.2, dt);
            spring_step(skirt_flare_, skirt_vflare_, tgt_flare, 58.0, 5.8, dt);
            spin_vel_ *= std::exp(-3.4 * dt);
            if (std::abs(spin_vel_) < 0.02) spin_vel_ = 0.0;
        } else {
            audio_sway_rad_ *= 0.82;
            audio_squash_ *= 0.82;
            spin_vel_ = 0.0;
        }

        double spring_energy = std::abs(hair_vx_) + std::abs(hair_vy_) + std::abs(hair_vz_) +
                               std::abs(skirt_vx_) + std::abs(skirt_vy_) + std::abs(spin_vel_) +
                               std::abs(hair_x_) + std::abs(skirt_flare_);
        bool needs_3d_pose = is_3d && !paused_ &&
                             (walk_speed_norm_ > 0.02f || action_id_ > 0 || lifted_y_ > 1.0 ||
                              (sound_reactive_ && audio_snap_.level > 0.05f) || spring_energy > 0.25);
        if (force_pose_render || (needs_3d_pose && (now - last_3d_pose_render_us_ >= 66000))) {
            last_3d_pose_render_us_ = now;
            render_sprite_now();
        }

        // Keep sprite hitbox synchronized while walking / jumping
        int sx = current_sprite_x(sw);
        int sh = current_frame_ ? current_frame_->raw.height : target_height_;
        sprite_rect_ = {sx, std::max(0, win_h_ - sh), sw, sh};
        if (std::abs(last_shape_sx_ - sx) >= 3) {
            update_input_shape();
        }

        for (auto it = audio_notes_.begin(); it != audio_notes_.end();) {
            it->life += dt;
            if (it->life >= it->max_life) {
                it = audio_notes_.erase(it);
                continue;
            }
            it->x += (it->vx + std::sin(it->life * 5.2) * 10.0) * dt;
            it->y += it->vy * dt;
            ++it;
        }

        if (stage_area_) gtk_widget_queue_draw(stage_area_);
        if (paused_ && !active_bounce && audio_notes_.empty()) {
            anim_timer_id_ = 0;
            return false;
        }
        return true;
    }

    bool on_blink_tick() {
        if (paused_) return true;
        is_blinking_ = true;
        render_sprite_now();
        g_timeout_add(150, [](gpointer d) -> gboolean {
            auto* self = static_cast<DokiApp*>(d);
            self->is_blinking_ = false;
            self->render_sprite_now();
            return G_SOURCE_REMOVE;
        }, this);
        return true;
    }

    bool on_talk_scheduler() {
        if (paused_) return true;
        if (screen_watch_inflight_ && (g_get_monotonic_time() - screen_watch_inflight_since_us_) > 25000000LL) {
            screen_watch_inflight_ = false;
        }
        if (companion_random_ && auto_switch_sec_ > 0 && active_drawer_ == DrawerKind::None && !dock_visible_) {
            if (++auto_switch_elapsed_ >= auto_switch_sec_) {
                auto_switch_elapsed_ = 0;
                randomize_character(true);
            }
        }
        if (muted_) return true;
        if (screen_watch_enabled_ && !gemini_api_key_.empty() && active_drawer_ == DrawerKind::None && !dock_visible_) {
            if (++screen_watch_elapsed_ >= std::max(5, screen_watch_sec_)) {
                screen_watch_elapsed_ = 0;
                request_screen_comment(false);
            }
        }
        if (!autonomous_) return true;
        if (++talk_elapsed_ >= talk_interval_s_) {
            talk_elapsed_ = 0;
            request_thought("idle");
        }
        return true;
    }

    void show_bubble(const std::string& text, const std::string& mood) {
        char* stripped = natsuki_strip_emojis(text.c_str());
        std::string clean = stripped ? std::string(stripped) : text;
        if (stripped) natsuki_string_free(stripped);
        if (clean.empty()) return;

        bubble_text_ = clean;
        bubble_visible_ = true;
        bubble_countdown_ = 11;
        talk_elapsed_ = 0;
        self_test_bubble_seen_ = true;

        if (!mood.empty()) {
            current_mood_ = mood;
            variant_idx_ = (variant_idx_ + 1) % 6;
            render_sprite_now();
        }
        trigger_bounce(-4.0);
        if (!bubble_timer_id_ && !self_test_) {
            bubble_timer_id_ = g_timeout_add(1000, [](gpointer d) -> gboolean {
                auto* s = static_cast<DokiApp*>(d);
                if (s->bubble_visible_ && --s->bubble_countdown_ <= 0) {
                    s->bubble_visible_ = false;
                    s->bubble_rect_ = {0, 0, 0, 0};
                    s->update_input_shape();
                    gtk_widget_queue_draw(s->stage_area_);
                    s->bubble_timer_id_ = 0;
                    return G_SOURCE_REMOVE;
                }
                return G_SOURCE_CONTINUE;
            }, this);
        }
        if (stage_area_) gtk_widget_queue_draw(stage_area_);
        update_input_shape();
    }

    void handle_brain_json(const json& j) {
        try {
            last_brain_cmd_count_++;
            if (j.value("screen_watch", false)) {
                screen_watch_inflight_ = false;
                self_test_screen_seen_ = true;
                if (j.value("ok", false)) {
                    talk_elapsed_ = 0;
                }
            }
            if (!j.value("ok", false)) return;
            std::string text = j.value("text", "");
            std::string mood = j.value("mood", "neutral");
            std::string easter = j.value("easter", "");
            if (easter == "batman" || easter == "monika") {
                self_test_weird_seen_ = true;
            }
            if (mood == "angry") {
                self_test_angry_seen_ = true;
            }
            if (j.value("swarm", false)) {
                trigger_doki_swarm();
            }
            const bool is_voice = j.contains("voice_command");
            if (is_voice && !self_test_) {
                trigger_bounce(-3.8);
            }
            if (!text.empty()) {
                append_chat_line(theme_for_character(character_id_).label, text);
                if (is_voice || !muted_ || active_drawer_ == DrawerKind::Chat || mood == "angry") {
                    show_bubble(text, mood);
                }
            }
        } catch (...) {
        }
    }

    void trigger_doki_swarm() {
        gint64 now = g_get_monotonic_time();
        if (now - last_swarm_trigger_us_ < 900000 && swarm_active_) {
            return;
        }
        last_swarm_trigger_us_ = now;
        if (!swarm_window_) return;

        int mon_w = 1920;
        int mon_h = 1080;
        if (GdkDisplay* disp = gdk_display_get_default()) {
            GdkMonitor* mon = gdk_display_get_primary_monitor(disp);
            if (!mon && gdk_display_get_n_monitors(disp) > 0) {
                mon = gdk_display_get_monitor(disp, 0);
            }
            if (mon) {
                GdkRectangle geo{};
                gdk_monitor_get_geometry(mon, &geo);
                if (geo.width > 200 && geo.height > 200) {
                    mon_w = geo.width;
                    mon_h = geo.height;
                }
            }
        }

        swarm_actors_.clear();
        swarm_particles_.clear();
        const std::array<std::string, 4> doki_ids = {"natsuki", "monika", "sayori", "yuri"};
        for (int i = 0; i < 8; ++i) {
            const std::string& cid = doki_ids[i % 4];
            std::string preferred_3d = cid + "_l4d2_3d";
            float yaw = (i % 2 == 0) ? -28.0f : 28.0f;
            auto frame = render_doki_frame(cid, preferred_3d, "fly", false, static_cast<uint32_t>(i),
                                           185, yaw, 8.0f);
            if (!frame) continue;
            bool left_to_right = (i % 2 == 0);
            SwarmActor act;
            act.character_id = cid;
            act.frame = std::move(frame);
            act.x = left_to_right ? (-180.0 - i * 95.0) : (mon_w + 80.0 + i * 95.0);
            act.y_base = mon_h * (0.14 + 0.10 * (i % 6));
            act.vx = left_to_right ? (520.0 + (i % 3) * 85.0) : -(520.0 + (i % 3) * 85.0);
            act.amp = 28.0 + (i % 4) * 11.0;
            act.freq = 2.6 + (i % 3) * 0.65;
            act.phase = i * 0.9;
            act.tilt = left_to_right ? 0.16 : -0.16;
            act.scale = 0.88 + (i % 3) * 0.10;
            swarm_actors_.push_back(std::move(act));
        }

        for (int p = 0; p < 28; ++p) {
            const auto& th = theme_for_character(doki_ids[p % 4]);
            SwarmParticle pt;
            pt.icon_name = (p % 2 == 0) ? "heart" : "sparkle";
            pt.x = (static_cast<double>(p) / 28.0) * mon_w;
            pt.y = mon_h * (0.10 + 0.78 * ((p * 37) % 100) / 100.0);
            pt.vx = ((p % 2 == 0) ? 1.0 : -1.0) * (90.0 + (p % 5) * 38.0);
            pt.vy = -45.0 - (p % 4) * 22.0;
            pt.size = 18.0 + (p % 4) * 5.0;
            pt.phase = p * 0.6;
            pt.r = th.r;
            pt.g = th.g;
            pt.b = th.b;
            swarm_particles_.push_back(std::move(pt));
        }

        swarm_elapsed_ = 0.0;
        swarm_active_ = true;
        if (!self_test_) {
            gtk_widget_show_all(swarm_window_);
            if (!swarm_timer_id_) {
                swarm_timer_id_ = g_timeout_add(16, [](gpointer d) -> gboolean {
                    return static_cast<DokiApp*>(d)->on_swarm_tick() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
                }, this);
            }
        }
    }

    bool on_swarm_tick() {
        if (!swarm_active_) {
            swarm_timer_id_ = 0;
            return false;
        }
        const double dt = 0.016;
        swarm_elapsed_ += dt;
        for (auto& a : swarm_actors_) {
            a.x += a.vx * dt;
        }
        for (auto& p : swarm_particles_) {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
        }
        if (swarm_area_) gtk_widget_queue_draw(swarm_area_);
        if (swarm_elapsed_ >= 5.2) {
            swarm_active_ = false;
            swarm_timer_id_ = 0;
            gtk_widget_hide(swarm_window_);
            return false;
        }
        return true;
    }

    void on_swarm_draw(cairo_t* cr) {
        if (!swarm_active_) return;
        double fade = 1.0;
        if (swarm_elapsed_ > 4.4) {
            fade = std::max(0.0, 1.0 - (swarm_elapsed_ - 4.4) / 0.8);
        } else if (swarm_elapsed_ < 0.35) {
            fade = swarm_elapsed_ / 0.35;
        }

        for (const auto& pt : swarm_particles_) {
            double pulse = 0.65 + 0.35 * std::sin(swarm_elapsed_ * 6.0 + pt.phase);
            std::string svg = (fs::path(project_root_) / "assets" / "icons" / (pt.icon_name + ".svg")).string();
            draw_tinted_svg(cr, svg, pt.x, pt.y, pt.size, pt.r, pt.g, pt.b, fade * pulse);
        }

        for (const auto& act : swarm_actors_) {
            if (!act.frame || !act.frame->surface) continue;
            double y = act.y_base + std::sin(swarm_elapsed_ * act.freq + act.phase) * act.amp;
            cairo_save(cr);
            double cx = act.x + act.frame->raw.width * 0.5;
            double cy = y + act.frame->raw.height * 0.5;
            cairo_translate(cr, cx, cy);
            cairo_rotate(cr, act.tilt + std::cos(swarm_elapsed_ * act.freq + act.phase) * 0.06);
            cairo_scale(cr, act.scale, act.scale);
            cairo_translate(cr, -act.frame->raw.width * 0.5, -act.frame->raw.height * 0.5);
            cairo_set_source_surface(cr, act.frame->surface, 0.0, 0.0);
            cairo_paint_with_alpha(cr, fade);
            cairo_restore(cr);
        }
    }

    bool on_stage_press(GdkEventButton* ev) {
        if (ev->button == 3 || ev->button == 2) {
            toggle_dock();
            return TRUE;
        }
        if (ev->button == 1) {
            pointer_pressed_ = true;
            drag_mode_ = DragMode::None;
            drag_start_x_ = ev->x;
            drag_start_y_ = ev->y;
            drag_last_x_ = ev->x;
            drag_start_yaw_ = model_yaw_deg_;
            drag_start_walk_x_ = (walk_x_ >= 0.0) ? walk_x_ : (sprite_rect_.x + sprite_rect_.width * 0.5);
            return TRUE;
        }
        return FALSE;
    }

    bool on_stage_motion(GdkEventMotion* ev) {
        if (!pointer_pressed_) return FALSE;
        bool is_3d = natsuki_engine_style_is_3d(engine_, style_id_.c_str()) != 0;
        double dx = ev->x - drag_start_x_;
        double dy = ev->y - drag_start_y_;
        double step_dx = ev->x - drag_last_x_;
        drag_last_x_ = ev->x;

        if (drag_mode_ == DragMode::None) {
            if (!is_3d && std::hypot(dx, dy) >= 6.0) {
                drag_mode_ = DragMode::Move2D;
            } else if (is_roaming_active() && -dy > 14.0 && std::abs(dy) > std::abs(dx)) {
                drag_mode_ = DragMode::Lift;
                current_mood_ = "fly";
            } else if (is_3d && std::abs(dx) >= 10.0) {
                drag_mode_ = DragMode::Rotate;
            }
        }

        if (drag_mode_ == DragMode::Move2D) {
            int sw = current_frame_ ? current_frame_->raw.width : 170;
            double max_lift = std::max(40.0, win_h_ - sprite_rect_.height - 16.0);
            lifted_y_ = std::clamp(-dy, 0.0, max_lift);
            manual_2d_x_ = std::clamp(drag_start_walk_x_ + dx, sw * 0.5 + 4.0, std::max(sw * 0.5 + 4.0, win_w_ - sw * 0.5 - 4.0));
            apply_side_anchors();
            update_input_shape();
            if (stage_area_) gtk_widget_queue_draw(stage_area_);
            return TRUE;
        }

        if (drag_mode_ == DragMode::Lift) {
            double max_lift = std::max(40.0, win_h_ - sprite_rect_.height - 16.0);
            lifted_y_ = std::clamp(-dy, 0.0, max_lift);
            walk_x_ = drag_start_walk_x_ + dx;
            hair_vx_ += -step_dx * 0.14;
            skirt_vx_ += -step_dx * 0.12;
            skirt_flare_ = std::max(skirt_flare_, 0.45);
            gint64 now = g_get_monotonic_time();
            if (is_3d && now - last_3d_pose_render_us_ >= 45000) {
                last_3d_pose_render_us_ = now;
                render_sprite_now();
            }
            if (stage_area_) gtk_widget_queue_draw(stage_area_);
            return TRUE;
        }

        if (drag_mode_ == DragMode::Rotate && is_3d) {
            model_yaw_deg_ = drag_start_yaw_ + static_cast<float>(dx * 0.65);
            while (model_yaw_deg_ > 180.0f) model_yaw_deg_ -= 360.0f;
            while (model_yaw_deg_ < -180.0f) model_yaw_deg_ += 360.0f;
            spin_vel_ = std::clamp(step_dx * 0.25, -3.2, 3.2);
            skirt_flare_ = std::max(skirt_flare_, std::min(1.2, std::abs(spin_vel_) * 0.4));
            if (lbl_rot_3d_) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d deg", static_cast<int>(std::round(model_yaw_deg_)));
                gtk_label_set_text(GTK_LABEL(lbl_rot_3d_), buf);
            }
            gint64 now = g_get_monotonic_time();
            if (now - last_3d_pose_render_us_ >= 38000) {
                last_3d_pose_render_us_ = now;
                render_sprite_now();
            }
            return TRUE;
        }
        return FALSE;
    }

    bool on_stage_release(GdkEventButton* ev) {
        if (ev->button != 1) return FALSE;
        pointer_pressed_ = false;
        if (drag_mode_ == DragMode::Lift) {
            bounce_offset_ = -lifted_y_;
            lifted_y_ = 0.0;
            bounce_vel_ = 0.0;
            current_mood_ = "blush";
            trigger_bounce(1.5);
            render_sprite_now();
            update_input_shape();
            drag_mode_ = DragMode::None;
            return TRUE;
        }
        if (drag_mode_ == DragMode::Move2D) {
            bounce_offset_ = -lifted_y_;
            lifted_y_ = 0.0;
            bounce_vel_ = 0.0;
            int sw = current_frame_ ? current_frame_->raw.width : 170;
            side_ = is_on_right_half(current_sprite_x(sw), sw) ? "right" : "left";
            trigger_bounce(1.1);
            apply_side_anchors();
            update_input_shape();
            save_config();
            if (stage_area_) gtk_widget_queue_draw(stage_area_);
            drag_mode_ = DragMode::None;
            return TRUE;
        }
        if (drag_mode_ == DragMode::Rotate) {
            drag_mode_ = DragMode::None;
            return TRUE;
        }
        // Check bubble click to dismiss cleanly
        if (bubble_visible_ && ev->x >= bubble_rect_.x && ev->x <= bubble_rect_.x + bubble_rect_.width &&
            ev->y >= bubble_rect_.y && ev->y <= bubble_rect_.y + bubble_rect_.height) {
            bubble_visible_ = false;
            bubble_rect_ = {0, 0, 0, 0};
            update_input_shape();
            gtk_widget_queue_draw(stage_area_);
            return TRUE;
        }

        // Check normalized sprite hitbox
        double rx = (ev->x - sprite_rect_.x) / std::max(1, sprite_rect_.width);
        double ry = (ev->y - sprite_rect_.y) / std::max(1, sprite_rect_.height);
        if (rx < 0.0 || rx > 1.0 || ry < 0.0 || ry > 1.0) return FALSE;

        if (rx >= 0.18 && rx <= 0.82 && ry >= 0.26 && ry <= 0.50) {
            request_thought("poke_zone1");
        } else if (rx >= 0.20 && rx <= 0.80 && ry > 0.50 && ry <= 0.74) {
            request_thought("poke_zone2");
        } else if (ry < 0.26) {
            request_thought("poke_head");
        } else {
            request_thought("click");
        }
        return TRUE;
    }

    void on_stage_draw(cairo_t* cr) {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        int sw = current_frame_ ? current_frame_->raw.width : 170;
        int sh = current_frame_ ? current_frame_->raw.height : target_height_;
        int sx = current_sprite_x(sw);
        const bool right_side = is_on_right_half(sx, sw);
        const double vertical_lift = std::max(0.0, -bounce_offset_) + lifted_y_ + action_hover_px_;
        [[maybe_unused]] int sy = static_cast<int>(win_h_ - sh - vertical_lift);

        sprite_rect_ = {sx, std::max(0, win_h_ - sh), sw, sh};

        if (sound_reactive_) {
            const auto& th = effective_theme();
            const double bar_w = 5.0;
            const double gap = 3.0;
            const double eq_total_w = 8 * bar_w + 7 * gap;
            const double eq_x0 = sx + sw * 0.5 - eq_total_w * 0.5;
            const double eq_base_y = static_cast<double>(win_h_) - 4.0;
            for (size_t i = 0; i < 8; ++i) {
                double energy = std::clamp(static_cast<double>(audio_snap_.bands[i]), 0.0, 1.0);
                double bh = 4.0 + energy * 24.0;
                double bx = eq_x0 + i * (bar_w + gap);
                double by = eq_base_y - bh;
                double mix = 0.25 + 0.75 * energy;
                add_rounded_rect(cr, bx, by, bar_w, bh, 2.5);
                cairo_set_source_rgba(cr,
                                      std::min(1.0, th.r + 0.14 * (1.0 - mix)),
                                      std::min(1.0, th.g + 0.14 * (1.0 - mix)),
                                      std::min(1.0, th.b + 0.14 * (1.0 - mix)),
                                      0.32 + 0.56 * energy);
                cairo_fill(cr);
            }
        }

        if (current_frame_ && current_frame_->surface) {
            const bool is_3d = natsuki_engine_style_is_3d(engine_, style_id_.c_str()) != 0;
            const double walk_hop = (!is_3d && walk_speed_norm_ > 0.05f)
                ? std::abs(std::sin(walk_phase_)) * 7.0 * walk_speed_norm_
                : 0.0;
            const double foot_x = sx + sw * 0.5;
            const double foot_y = static_cast<double>(win_h_) - vertical_lift - walk_hop;
            const double breath_sy = 1.0 + 0.011 * breath_amount_ + 0.042 * audio_squash_;
            const double breath_sx = (1.0 - 0.003 * breath_amount_ - 0.016 * audio_squash_) *
                                     ((!is_3d && roam_walking_ && walk_dir_ > 0.0) ? -1.0 : 1.0);
            cairo_save(cr);
            cairo_translate(cr, foot_x, foot_y);
            if (std::abs(audio_sway_rad_) > 0.0005) {
                cairo_rotate(cr, audio_sway_rad_);
            }
            cairo_scale(cr, breath_sx, breath_sy);
            cairo_translate(cr, -sw * 0.5, -static_cast<double>(sh));
            cairo_set_source_surface(cr, current_frame_->surface, 0.0, 0.0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
            cairo_paint_with_alpha(cr, sprite_alpha_);
            cairo_restore(cr);
        }

        if (!audio_notes_.empty()) {
            const auto& th = effective_theme();
            for (const auto& pt : audio_notes_) {
                double t = pt.life / std::max(0.1, pt.max_life);
                double alpha = (t < 0.2) ? (t / 0.2) : std::max(0.0, 1.0 - (t - 0.2) / 0.8);
                alpha *= 0.92;
                double sz = pt.size;
                cairo_save(cr);
                cairo_translate(cr, pt.x, pt.y);
                cairo_set_source_rgba(cr, th.r, th.g, th.b, alpha);
                if (pt.kind == 2) {
                    cairo_move_to(cr, 0.0, -sz * 0.72);
                    cairo_line_to(cr, sz * 0.22, -sz * 0.22);
                    cairo_line_to(cr, sz * 0.72, 0.0);
                    cairo_line_to(cr, sz * 0.22, sz * 0.22);
                    cairo_line_to(cr, 0.0, sz * 0.72);
                    cairo_line_to(cr, -sz * 0.22, sz * 0.22);
                    cairo_line_to(cr, -sz * 0.72, 0.0);
                    cairo_line_to(cr, -sz * 0.22, -sz * 0.22);
                    cairo_close_path(cr);
                    cairo_fill(cr);
                } else {
                    cairo_set_line_width(cr, std::max(1.5, sz * 0.14));
                    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
                    cairo_save(cr);
                    cairo_translate(cr, -sz * 0.22, sz * 0.28);
                    cairo_rotate(cr, -0.4);
                    cairo_scale(cr, 1.25, 0.85);
                    cairo_arc(cr, 0.0, 0.0, sz * 0.20, 0.0, 2.0 * M_PI);
                    cairo_fill(cr);
                    cairo_restore(cr);
                    if (pt.kind == 1) {
                        cairo_save(cr);
                        cairo_translate(cr, sz * 0.28, sz * 0.16);
                        cairo_rotate(cr, -0.4);
                        cairo_scale(cr, 1.25, 0.85);
                        cairo_arc(cr, 0.0, 0.0, sz * 0.19, 0.0, 2.0 * M_PI);
                        cairo_fill(cr);
                        cairo_restore(cr);
                        cairo_move_to(cr, -sz * 0.04, sz * 0.22);
                        cairo_line_to(cr, -sz * 0.04, -sz * 0.42);
                        cairo_line_to(cr, sz * 0.45, -sz * 0.54);
                        cairo_line_to(cr, sz * 0.45, sz * 0.10);
                        cairo_stroke(cr);
                    } else {
                        cairo_move_to(cr, -sz * 0.03, sz * 0.22);
                        cairo_line_to(cr, -sz * 0.03, -sz * 0.48);
                        cairo_curve_to(cr, sz * 0.35, -sz * 0.38, sz * 0.38, -sz * 0.12, sz * 0.18, 0.0);
                        cairo_stroke(cr);
                    }
                }
                cairo_restore(cr);
            }
        }

        if (bubble_visible_ && !bubble_text_.empty()) {
            PangoLayout* layout = pango_cairo_create_layout(cr);
            pango_layout_set_width(layout, 240 * PANGO_SCALE);
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
            PangoFontDescription* desc = pango_font_description_from_string("Sans 10");
            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, bubble_text_.c_str(), -1);
            int tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);
            pango_font_description_free(desc);

            double pad_x = 14.0;
            double pad_y = 11.0;
            double bw = std::max(145.0, tw + pad_x * 2.0);
            double bh = th + pad_y * 2.0 + 18.0;

            double bx = (active_drawer_ == DrawerKind::None)
                ? (right_side ? (sx - bw - 14.0) : (sx + sw + 14.0))
                : (sx + sw * 0.5 - bw * 0.5);
            bx = std::clamp(bx, 12.0, std::max(12.0, win_w_ - bw - 12.0));

            double by = (active_drawer_ == DrawerKind::None)
                ? std::clamp(sy + 20.0, 6.0, std::max(6.0, win_h_ - bh - 6.0))
                : std::max(6.0, sy - bh - 12.0);

            bubble_rect_ = {static_cast<int>(bx) - 12, static_cast<int>(by) - 2,
                            static_cast<int>(bw) + 24, static_cast<int>(bh) + 6};

            double br = 0.99, bg = 0.96, bb = 0.98;
            double fr = 0.18, fg = 0.08, fb = 0.13;
            double lr = 1.0, lg = 0.35, lb = 0.59;
            parse_hex_color(speech_bg_, br, bg, bb);
            parse_hex_color(speech_fg_, fr, fg, fb);
            parse_hex_color(speech_border_, lr, lg, lb);

            add_rounded_rect(cr, bx, by, bw, bh, 14.0);
            cairo_set_source_rgba(cr, br, bg, bb, speech_opacity_);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, lr, lg, lb, speech_opacity_);
            cairo_set_line_width(cr, 2.2);
            cairo_stroke(cr);

            if (active_drawer_ == DrawerKind::None) {
                double ty = std::min(by + bh - 18.0, std::max(by + 16.0, by + 26.0));
                if (right_side) {
                    cairo_move_to(cr, bx + bw, ty - 7.0);
                    cairo_line_to(cr, bx + bw + 10.0, ty);
                    cairo_line_to(cr, bx + bw, ty + 7.0);
                } else {
                    cairo_move_to(cr, bx, ty - 7.0);
                    cairo_line_to(cr, bx - 10.0, ty);
                    cairo_line_to(cr, bx, ty + 7.0);
                }
                cairo_close_path(cr);
                cairo_set_source_rgba(cr, br, bg, bb, speech_opacity_);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, lr, lg, lb, speech_opacity_);
                cairo_set_line_width(cr, 1.8);
                cairo_stroke(cr);
            }

            // Speaker header
            PangoLayout* hdr = pango_cairo_create_layout(cr);
            PangoFontDescription* hdesc = pango_font_description_from_string("Sans Bold 9");
            pango_layout_set_font_description(hdr, hdesc);
            pango_layout_set_text(hdr, theme_for_character(character_id_).label, -1);
            cairo_set_source_rgba(cr, lr, lg, lb, 1.0);
            cairo_move_to(cr, bx + pad_x, by + 7.0);
            pango_cairo_show_layout(cr, hdr);
            pango_font_description_free(hdesc);
            g_object_unref(hdr);

            cairo_set_source_rgba(cr, fr, fg, fb, 1.0);
            cairo_move_to(cr, bx + pad_x, by + 24.0);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        } else {
            bubble_rect_ = {0, 0, 0, 0};
        }
    }

    int run_self_test() {
        auto pump_events = [&](int ms) {
            gint64 end = g_get_monotonic_time() + static_cast<gint64>(ms) * 1000;
            while (g_get_monotonic_time() < end) {
                while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
                g_usleep(5000);
            }
        };

        // 1. Dock starts hidden
        if (gtk_widget_get_visible(dock_window_)) {
            std::cerr << "[self-test] FAIL: dock should start hidden\n";
            return 2;
        }
        // 2. Verify all 4 Dokis render L4D2 3D frames AND at least one 2D PNG skin per Doki
        for (const char* cid : {"natsuki", "monika", "sayori", "yuri"}) {
            select_character(cid);
            if (!current_frame_ || current_frame_->raw.width < 32 || current_frame_->raw.height < 120) {
                std::cerr << "[self-test] FAIL: 3D/default frame missing for " << cid << "\n";
                return 3;
            }
            // Also verify switching to a 2D PNG skin works cleanly for each Doki
            for (const auto& s : styles_for_char_) {
                if (natsuki_engine_style_is_3d(engine_, s.id.c_str()) == 0) {
                    auto f2d = render_doki_frame(cid, s.id, "happy", false, 0, target_height_, 0.0f, 0.0f);
                    if (!f2d || f2d->raw.width < 40 || f2d->raw.height != target_height_) {
                        std::cerr << "[self-test] FAIL: 2D PNG skin failed for " << cid << ":" << s.id << "\n";
                        return 3;
                    }
                    break;
                }
            }
        }
        // Switch Natsuki back to default style
        select_character("natsuki");
        side_ = "left";
        update_layout_geometry();
        toggle_side();
        if (side_ != "right") {
            std::cerr << "[self-test] FAIL: toggle_side failed to switch to right\n";
            return 4;
        }
        toggle_side();
        if (side_ != "left") {
            std::cerr << "[self-test] FAIL: toggle_side failed to restore left\n";
            return 5;
        }
        // 3. Test 3D rotation
        float before_yaw = model_yaw_deg_;
        adjust_3d_yaw(25.0f);
        if (std::abs(model_yaw_deg_ - before_yaw) < 10.0f) {
            std::cerr << "[self-test] FAIL: 3D rotation didn't update\n";
            return 6;
        }
        // 4. Test NamePrompt + batman weird easter egg via Brain IPC
        apply_user_name("Batman", true);
        pump_events(600);
        if (!self_test_weird_seen_) {
            std::cerr << "[self-test] FAIL: Batman weird trigger not observed from brain\n";
            return 7;
        }
        // 5. Test *amo x doki* swarm trigger
        gtk_entry_set_text(GTK_ENTRY(entry_chat_), "*amo x doki*");
        on_chat_send();
        pump_events(400);
        if (!swarm_active_ || swarm_actors_.size() < 4) {
            std::cerr << "[self-test] FAIL: *amo x doki* swarm did not activate\n";
            return 8;
        }
        // 6. Test drawers, OVERLAY fullscreen layer policy, 3D weapons/antics/randomizer, 2D/3D quick toggle & breathing tick
        if (always_on_top_ && (
                gtk_layer_get_layer(GTK_WINDOW(window_)) != GTK_LAYER_SHELL_LAYER_OVERLAY ||
                gtk_layer_get_layer(GTK_WINDOW(dock_window_)) != GTK_LAYER_SHELL_LAYER_OVERLAY ||
                gtk_layer_get_layer(GTK_WINDOW(drawer_window_)) != GTK_LAYER_SHELL_LAYER_OVERLAY ||
                gtk_layer_get_layer(GTK_WINDOW(swarm_window_)) != GTK_LAYER_SHELL_LAYER_OVERLAY)) {
            std::cerr << "[self-test] FAIL: always_on_top layer window does not use LAYER_OVERLAY\n";
            return 9;
        }
        if (natsuki_engine_weapon_count() < 7) {
            std::cerr << "[self-test] FAIL: expected at least 7 3D weapons\n";
            return 9;
        }
        {
            NatsukiPoseParams wp{};
            wp.walk_phase = 1.2f;
            wp.walk_speed = 1.0f;
            wp.action_id = 3;
            wp.action_t = 0.52f;
            wp.skirt_flare = 0.6f;
            wp.hair_lag_x = 0.8f;
            wp.weapon_id = "shotgun";
            auto wf = render_doki_frame("monika", "monika_l4d2_3d", "happy", false, 0, target_height_, -25.0f, 0.0f, &wp);
            if (!wf || wf->raw.width < 40 || wf->raw.height != target_height_) {
                std::cerr << "[self-test] FAIL: v2 3D shotgun/action render failed\n";
                return 9;
            }
        }
        {
            std::string before_cid = character_id_;
            randomize_character(false);
            if (character_id_ == before_cid && characters_.size() > 1) {
                std::cerr << "[self-test] FAIL: randomize_character did not pick another Doki\n";
                return 9;
            }
            if (!is_style_3d()) {
                toggle_2d_3d_mode();
            }
            start_action(2, false);
            for (int k = 0; k < 5; ++k) on_anim_tick();
            if (action_timer_ <= 0.0) {
                std::cerr << "[self-test] FAIL: 3D antic action timer did not advance\n";
                return 9;
            }
            action_id_ = 0;
            action_hover_px_ = 0.0;
            select_character("natsuki");
        }
        bool was_3d = natsuki_engine_style_is_3d(engine_, style_id_.c_str()) != 0;
        toggle_2d_3d_mode();
        bool now_3d = natsuki_engine_style_is_3d(engine_, style_id_.c_str()) != 0;
        if (now_3d == was_3d) {
            std::cerr << "[self-test] FAIL: toggle_2d_3d_mode did not switch between 3D and 2D\n";
            return 9;
        }
        if (!now_3d) {
            if (is_roaming_active()) {
                std::cerr << "[self-test] FAIL: 2D style must never roam\n";
                return 9;
            }
            std::string prev_side = side_;
            side_ = "left";
            update_layout_geometry();
            int sw2d = current_frame_ ? current_frame_->raw.width : 170;
            if (current_sprite_x(sw2d) != 10) {
                std::cerr << "[self-test] FAIL: 2D left anchor mismatch\n";
                return 9;
            }
            toggle_side();
            if (side_ != "right" || current_sprite_x(sw2d) != std::max(4, win_w_ - sw2d - 10)) {
                std::cerr << "[self-test] FAIL: 2D right anchor mismatch after toggle_side\n";
                return 9;
            }
            manual_2d_x_ = win_w_ * 0.5;
            update_layout_geometry();
            int expected_mid_x = static_cast<int>(std::lround(manual_2d_x_));
            if (is_roaming_active() || current_sprite_x(sw2d) != expected_mid_x) {
                std::cerr << "[self-test] FAIL: 2D manual drag position mismatch\n";
                return 9;
            }
            manual_2d_x_ = -1.0;
            side_ = prev_side;
            update_layout_geometry();
        }
        toggle_2d_3d_mode();
        bool prev_rand = companion_random_;
        set_companion_random(false);
        if (companion_random_ ||
            (check_random_doki_ && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_random_doki_))) ||
            (check_random_doki_settings_ && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_random_doki_settings_))) ||
            (scale_switch_sec_ && gtk_widget_get_sensitive(scale_switch_sec_))) {
            std::cerr << "[self-test] FAIL: set_companion_random(false) did not sync controls\n";
            return 9;
        }
        set_companion_random(true);
        if (!companion_random_ ||
            (check_random_doki_ && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_random_doki_))) ||
            (check_random_doki_settings_ && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_random_doki_settings_))) ||
            (scale_switch_sec_ && !gtk_widget_get_sensitive(scale_switch_sec_))) {
            std::cerr << "[self-test] FAIL: set_companion_random(true) did not sync controls\n";
            return 9;
        }
        set_companion_random(prev_rand);
        bool init_top = always_on_top_;
        toggle_layer_mode();
        if (always_on_top_ == init_top) {
            std::cerr << "[self-test] FAIL: toggle_layer_mode failed\n";
            return 9;
        }
        if (!always_on_top_ && gtk_layer_get_layer(GTK_WINDOW(window_)) != GTK_LAYER_SHELL_LAYER_BOTTOM) {
            std::cerr << "[self-test] FAIL: toggle_layer_mode did not switch to LAYER_BOTTOM\n";
            return 9;
        }
        toggle_layer_mode();

        open_drawer(DrawerKind::Chat);
        if (active_drawer_ != DrawerKind::Chat) {
            std::cerr << "[self-test] FAIL: open_drawer(Chat) failed\n";
            return 9;
        }
        close_all_panels();
        if (active_drawer_ != DrawerKind::None || dock_visible_) {
            std::cerr << "[self-test] FAIL: close_all_panels failed to close drawer/dock\n";
            return 9;
        }

        double before_breath = breath_phase_;
        on_anim_tick();
        if (breath_phase_ <= before_breath) {
            std::cerr << "[self-test] FAIL: breathing phase did not advance\n";
            return 9;
        }

        // 7. Test sound-reactive DSP, beat notes, squash, brain sound_react reply & voice toggle
        if (sound_reactive_) {
            toggle_sound_reactive();
        }
        toggle_sound_reactive();
        if (!sound_reactive_) {
            std::cerr << "[self-test] FAIL: toggle_sound_reactive did not enable\n";
            return 9;
        }
        if (!icons_.get("mic")) {
            std::cerr << "[self-test] FAIL: missing mic.svg icon\n";
            return 9;
        }
        if (!icons_.get("moon")) {
            std::cerr << "[self-test] FAIL: missing moon.svg icon\n";
            return 9;
        }
        const std::string saved_theme = ui_theme_;
        set_ui_theme("mono_dark", false);
        if (ui_theme_ != "mono_dark" || speech_bg_ != "#141416" || speech_fg_ != "#f4f4f5" || speech_border_ != "#d4d4d8") {
            std::cerr << "[self-test] FAIL: mono_dark theme did not update bubble/dock palette\n";
            return 9;
        }
        set_ui_theme("mono_light", false);
        if (ui_theme_ != "mono_light" || speech_bg_ != "#fafafa" || speech_fg_ != "#18181b" || speech_border_ != "#27272a") {
            std::cerr << "[self-test] FAIL: mono_light theme did not update bubble/dock palette\n";
            return 9;
        }
        apply_character_choice("monika", false);
        if (speech_border_ != "#27272a") {
            std::cerr << "[self-test] FAIL: character switch overwrote monochrome speech_border_\n";
            return 9;
        }
        set_ui_theme(saved_theme, false);
        bool prev_voice = voice_commands_enabled_;
        toggle_voice_commands();
        if (voice_commands_enabled_ == prev_voice) {
            std::cerr << "[self-test] FAIL: toggle_voice_commands did not flip state\n";
            return 9;
        }
        toggle_voice_commands();
        std::vector<int16_t> synth_kick(1024, 0);
        for (size_t i = 0; i < synth_kick.size(); ++i) {
            double t = static_cast<double>(i) / 24000.0;
            synth_kick[i] = static_cast<int16_t>(std::sin(2.0 * M_PI * 85.0 * t) * 22000.0);
        }
        audio_monitor_.process_pcm_samples(synth_kick.data(), synth_kick.size());
        AudioSnapshot snap = audio_monitor_.snapshot();
        if (snap.level <= 0.1f || snap.bass <= 0.1f || snap.beat_seq == 0) {
            std::cerr << "[self-test] FAIL: audio DSP failed to detect synthetic bass beat\n";
            return 9;
        }
        on_anim_tick();
        if (audio_notes_.empty() || audio_squash_ <= 0.01) {
            std::cerr << "[self-test] FAIL: audio reactive visuals did not respond to beat\n";
            return 9;
        }
        self_test_bubble_seen_ = false;
        send_brain_cmd("sound_react", json{{"loud", true}, {"audio_source", "monitor"}});
        pump_events(350);
        if (!self_test_bubble_seen_) {
            std::cerr << "[self-test] FAIL: brain sound_react reply not observed\n";
            return 9;
        }
        toggle_sound_reactive();
        if (sound_reactive_) {
            std::cerr << "[self-test] FAIL: toggle_sound_reactive did not disable cleanly\n";
            return 9;
        }

        open_drawer(DrawerKind::Settings);
        gtk_range_set_value(GTK_RANGE(scale_height_), 310);
        if (target_height_ != 310) {
            std::cerr << "[self-test] FAIL: height slider did not update\n";
            return 9;
        }
        gtk_range_set_value(GTK_RANGE(scale_screen_sec_), 25);
        if (screen_watch_sec_ != 25) {
            std::cerr << "[self-test] FAIL: screen watch interval slider did not update\n";
            return 9;
        }
        close_all_panels();
        std::string saved_key = gemini_api_key_;
        gemini_api_key_.clear();
        send_brain_config();
        self_test_screen_seen_ = false;
        self_test_bubble_seen_ = false;
        request_screen_comment(true);
        pump_events(500);
        gemini_api_key_ = saved_key;
        send_brain_config();
        if (!self_test_screen_seen_ || !self_test_bubble_seen_) {
            std::cerr << "[self-test] FAIL: screen_comment roundtrip not observed from brain\n";
            return 10;
        }
        self_test_bubble_seen_ = false;
        self_test_angry_seen_ = false;
        request_thought("poke_zone1");
        request_thought("poke_zone2");
        pump_events(500);
        if (!self_test_bubble_seen_ || !self_test_angry_seen_ || last_brain_cmd_count_ < 3) {
            std::cerr << "[self-test] FAIL: poke zone replies/angry mood not observed from brain\n";
            return 10;
        }

        std::cout << "CPP UI SELF-TEST PASSED (4 Dokis 3D/2D, OVERLAY fullscreen layers, 3D walking/weapons/physics/antics, randomizer, screen_vision, L/R side swap, smooth sub-pixel breathing, sound-reactive beat/EQ/notes, angry poke zones, Batman/Monika name easter eggs, *amo x doki* swarm="
                  << swarm_actors_.size() << " actors, brain cmds=" << last_brain_cmd_count_ << ")\n";
        return 0;
    }
};

int main(int argc, char** argv) {
    bool self_test = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self-test") == 0) {
            self_test = true;
        }
    }
    DokiApp app(self_test);
    return app.run(argc, argv);
}
