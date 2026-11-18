#ifndef NATSUKI_CORE_H
#define NATSUKI_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NatsukiEngine NatsukiEngine;
typedef struct NatsukiFrameHandle NatsukiFrameHandle;

typedef struct {
    const uint8_t* data;   /* Premultiplied ARGB32 (Cairo native little-endian BGRA byte order) */
    int32_t width;
    int32_t height;
    int32_t stride;
    NatsukiFrameHandle* handle;
} NatsukiFrame;

/* Lifecycle */
NatsukiEngine* natsuki_engine_new(const char* sprites_root, const char* config_path);
void natsuki_engine_free(NatsukiEngine* engine);

/* Character discovery (Natsuki, Monika, Sayori, Yuri) */
int32_t natsuki_engine_character_count(const NatsukiEngine* engine);
const char* natsuki_engine_character_id(const NatsukiEngine* engine, int32_t index);
const char* natsuki_engine_character_label(const NatsukiEngine* engine, int32_t index);

/* Style (outfit/mod pack) discovery (for active character or scoped by character_id) */
int32_t natsuki_engine_style_count(const NatsukiEngine* engine);
const char* natsuki_engine_style_id(const NatsukiEngine* engine, int32_t index);
const char* natsuki_engine_style_label(const NatsukiEngine* engine, int32_t index);

int32_t natsuki_engine_style_count_for(const NatsukiEngine* engine, const char* character_id);
const char* natsuki_engine_style_id_for(const NatsukiEngine* engine, const char* character_id, int32_t index);
const char* natsuki_engine_style_label_for(const NatsukiEngine* engine, const char* character_id, int32_t index);

/* Semantic mood discovery */
int32_t natsuki_engine_mood_count(const NatsukiEngine* engine);
const char* natsuki_engine_mood_id(const NatsukiEngine* engine, int32_t index);
const char* natsuki_engine_mood_label(const NatsukiEngine* engine, int32_t index);

/* Layered sprite composition + high-quality resize into Cairo ARGB32 */
int32_t natsuki_engine_render_frame(
    NatsukiEngine* engine,
    const char* style_id,
    const char* mood_id,
    uint8_t blink,
    uint32_t variant,
    int32_t target_height,
    NatsukiFrame* out_frame
);

int32_t natsuki_engine_render_character_frame(
    NatsukiEngine* engine,
    const char* character_id,
    const char* style_id,
    const char* mood_id,
    uint8_t blink,
    uint32_t variant,
    int32_t target_height,
    NatsukiFrame* out_frame
);

typedef struct {
    float walk_phase;
    float walk_speed;
    float hair_lag_x;
    float hair_lag_y;
    float hair_bounce_z;
    float skirt_lag_x;
    float skirt_lag_y;
    float skirt_flare;
    float spin_vel;
    float sound_level;
    int32_t action_id;
    float action_t;
    const char* weapon_id;
} NatsukiPoseParams;

int32_t natsuki_engine_render_character_frame_ex(
    NatsukiEngine* engine,
    const char* character_id,
    const char* style_id,
    const char* mood_id,
    uint8_t blink,
    uint32_t variant,
    int32_t target_height,
    float yaw_deg,
    float pitch_deg,
    NatsukiFrame* out_frame
);

int32_t natsuki_engine_render_character_frame_v2(
    NatsukiEngine* engine,
    const char* character_id,
    const char* style_id,
    const char* mood_id,
    uint8_t blink,
    uint32_t variant,
    int32_t target_height,
    float yaw_deg,
    float pitch_deg,
    const NatsukiPoseParams* pose,
    NatsukiFrame* out_frame
);

int32_t natsuki_engine_style_is_3d(const NatsukiEngine* engine, const char* style_id);
int32_t natsuki_engine_weapon_count(void);
const char* natsuki_engine_weapon_id(int32_t index);
const char* natsuki_engine_weapon_label(int32_t index);

void natsuki_frame_free(NatsukiFrameHandle* handle);

/* JSON config store (~/.config/natsuki-shimeji/config.json) */
char* natsuki_config_get_json(const NatsukiEngine* engine);
int32_t natsuki_config_update_json(NatsukiEngine* engine, const char* json_str);

/* Text sanitizer (strips emojis/pictographs cleanly) */
char* natsuki_strip_emojis(const char* text);
void natsuki_string_free(char* ptr);

#ifdef __cplusplus
}
#endif

#endif /* NATSUKI_CORE_H */
