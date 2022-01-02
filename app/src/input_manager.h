#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

#include "common.h"

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "controller.h"
#include "fps_counter.h"
#include "options.h"
#include "screen.h"
#include "trait/key_processor.h"
#include "trait/mouse_processor.h"

struct sc_joystick_down {
    bool up;
    bool down;
    bool left;
    bool right;
    SDL_Keycode started_by;
};

struct input_manager
{
    struct controller *controller;
    struct screen *screen;

    struct sc_key_processor *kp;
    struct sc_mouse_processor *mp;

    // Joystick mode specifics
    struct sc_point joystick_pos;
    struct sc_point crouch_btn_pos;
    struct sc_point jump_btn_pos;
    struct sc_point reload_btn_pos;
    struct sc_point switch_wpn_btn_pos;
    struct sc_joystick_down joystick_down;
    struct sc_point camera_pos;
    int js_mv_offset;
    bool joystick_mode;
    bool vjoystick_moving;

    bool control;
    bool forward_all_clicks;
    bool legacy_paste;
    bool clipboard_autosync;

    struct {
        unsigned data[SC_MAX_SHORTCUT_MODS];
        unsigned count;
    } sdl_shortcut_mods;

    bool vfinger_down;

    // Tracks the number of identical consecutive shortcut key down events.
    // Not to be confused with event->repeat, which counts the number of
    // system-generated repeated key presses.
    unsigned key_repeat;
    SDL_Keycode last_keycode;
    uint16_t last_mod;

    uint64_t next_sequence; // used for request acknowledgements
};

void
input_manager_init(struct input_manager *im, struct controller *controller,
                   struct screen *screen, struct sc_key_processor *kp,
                   struct sc_mouse_processor *mp,
                   const struct scrcpy_options *options);

bool
input_manager_handle_event(struct input_manager *im, SDL_Event *event);

#endif
