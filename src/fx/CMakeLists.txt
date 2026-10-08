# velacut_fx: effect and transition kernels (CPU reference; GPU later) and the asset library; no MLT dependency
# (docs/ARCHITECTURE.md §6).
add_library(velacut_fx STATIC
    Image.h
    Animation.h Animation.cpp
    Audio.h Audio.cpp
    AudioVisualizer.h AudioVisualizer.cpp
    BeatEffects.h BeatEffects.cpp
    VideoEffect.h VideoEffect.cpp
    Graphic.h Graphic.cpp
    ChromaKey.h ChromaKey.cpp
    Color.h Color.cpp
    Composite.h Composite.cpp
    Enhance.h Enhance.cpp
    Grade.h Grade.cpp
    Library.h Library.cpp
    Loudness.h Loudness.cpp
    Mask.h Mask.cpp
    MotionBlur.h MotionBlur.cpp
    Reframe.h Reframe.cpp
    Stabilization.h Stabilization.cpp
    Tracking.h Tracking.cpp
    PackageManager.h PackageManager.cpp
    Transform.h Transform.cpp
    Transition.h Transition.cpp
)
target_include_directories(velacut_fx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(velacut_fx PUBLIC Qt6::Core Qt6::Gui)
# The core pack is built in: the application works without any installed data file.
set(core_pack "${CMAKE_SOURCE_DIR}/resources/packs/vedit.core")
qt_add_resources(velacut_fx "velacut_core_pack"
    PREFIX "/velacut/packs/vedit.core"
    BASE "${core_pack}"
    FILES
        "${core_pack}/pack.json"
        "${core_pack}/animations.json"
        "${core_pack}/effects.json"
        "${core_pack}/filters.json"
        "${core_pack}/stickers.json"
        "${core_pack}/text-styles.json"
        "${core_pack}/caption-styles.json"
        "${core_pack}/transitions.json"
        "${core_pack}/video-effects.json"
        "${core_pack}/templates.json"
        "${core_pack}/stickers/arrow_curved.svg"
        "${core_pack}/stickers/arrow_down.svg"
        "${core_pack}/stickers/arrow_right.svg"
        "${core_pack}/stickers/arrow_sketch.svg"
        "${core_pack}/stickers/badge_hot.svg"
        "${core_pack}/stickers/badge_sale.svg"
        "${core_pack}/stickers/badge_verified.svg"
        "${core_pack}/stickers/emoji_cool.svg"
        "${core_pack}/stickers/emoji_fire.svg"
        "${core_pack}/stickers/emoji_heart.svg"
        "${core_pack}/stickers/emoji_party.svg"
        "${core_pack}/stickers/emoji_smile.svg"
        "${core_pack}/stickers/emoji_sparkles.svg"
        "${core_pack}/stickers/emoji_thumbs_up.svg"
        "${core_pack}/stickers/heart_pulse.gif"
        "${core_pack}/stickers/shape_burst.svg"
        "${core_pack}/stickers/shape_heart.svg"
        "${core_pack}/stickers/shape_highlight.svg"
        "${core_pack}/stickers/shape_lightning.svg"
        "${core_pack}/stickers/shape_star.svg"
        "${core_pack}/stickers/social_bell.svg"
        "${core_pack}/stickers/social_like.svg"
        "${core_pack}/stickers/social_subscribe.svg"
)
velacut_target_options(velacut_fx)
