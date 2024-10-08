#include "input_manager.h"
#include "clock.h"

#include <assert.h>
#include <SDL2/SDL_keycode.h>

#include "util/log.h"

static const int ACTION_DOWN = 1;
static const int ACTION_UP = 1 << 1;

#define SC_SDL_SHORTCUT_MODS_MASK (KMOD_CTRL | KMOD_ALT | KMOD_GUI)

static inline uint16_t
to_sdl_mod(unsigned mod) {
    uint16_t sdl_mod = 0;
    if (mod & SC_MOD_LCTRL) {
        sdl_mod |= KMOD_LCTRL;
    }
    if (mod & SC_MOD_RCTRL) {
        sdl_mod |= KMOD_RCTRL;
    }
    if (mod & SC_MOD_LALT) {
        sdl_mod |= KMOD_LALT;
    }
    if (mod & SC_MOD_RALT) {
        sdl_mod |= KMOD_RALT;
    }
    if (mod & SC_MOD_LSUPER) {
        sdl_mod |= KMOD_LGUI;
    }
    if (mod & SC_MOD_RSUPER) {
        sdl_mod |= KMOD_RGUI;
    }
    return sdl_mod;
}

static bool
is_shortcut_mod(struct input_manager *im, uint16_t sdl_mod) {
    // keep only the relevant modifier keys
    sdl_mod &= SC_SDL_SHORTCUT_MODS_MASK;

    assert(im->sdl_shortcut_mods.count);
    assert(im->sdl_shortcut_mods.count < SC_MAX_SHORTCUT_MODS);
    for (unsigned i = 0; i < im->sdl_shortcut_mods.count; ++i) {
        if (im->sdl_shortcut_mods.data[i] == sdl_mod) {
            return true;
        }
    }

    return false;
}

void
input_manager_init(struct input_manager *im, struct controller *controller,
                   struct screen *screen, struct sc_key_processor *kp,
                   struct sc_mouse_processor *mp,
                   const struct scrcpy_options *options) {
    assert(!options->control || (kp && kp->ops));
    assert(!options->control || (mp && mp->ops));

    im->joystick_pos.x = 340;
    im->joystick_pos.y = 865;
    im->js_mv_offset = 250;

    im->joystick_down.up = false;
    im->joystick_down.down = false;
    im->joystick_down.left = false;
    im->joystick_down.right = false;

    im->ads_btn_pos.x = 2000;
    im->ads_btn_pos.y = 790;

    im->crouch_btn_pos.x = 2032;
    im->crouch_btn_pos.y = 973;

    im->jump_btn_pos.x = 2209;
    im->jump_btn_pos.y = 890;

    im->reload_btn_pos.x = 2255;
    im->reload_btn_pos.y = 713;

    im->switch_wpn_btn_pos.x = 1290;
    im->switch_wpn_btn_pos.y = 964;

    im->scorestreak_btn_pos.x = 1013;
    im->scorestreak_btn_pos.y = 957;
    im->scorestreak_offset = 120;
    im->skill_btn_pos.x = 2247;
    im->skill_btn_pos.y = 405;

    im->chat_btn_pos.x = 2072;
    im->chat_btn_pos.y = 343;

    im->throwable_btn_pos.x = 1619;
    im->throwable_btn_pos.y = 932;

    im->camera_pos.x = 1250;
    im->camera_pos.y = 542;
    im->camera_sensitivity_normal = 1.25f;
    im->camera_sensitivity_shooting = 1.25f;

    im->joystick_mode = false;
    im->vjoystick_moving = false;
    im->vjoystick_shooting = false;
    im->controller = controller;
    im->screen = screen;
    im->kp = kp;
    im->mp = mp;

    im->control = options->control;
    im->forward_all_clicks = options->forward_all_clicks;
    im->legacy_paste = options->legacy_paste;
    im->clipboard_autosync = options->clipboard_autosync;

    const struct sc_shortcut_mods *shortcut_mods = &options->shortcut_mods;
    assert(shortcut_mods->count);
    assert(shortcut_mods->count < SC_MAX_SHORTCUT_MODS);
    for (unsigned i = 0; i < shortcut_mods->count; ++i) {
        uint16_t sdl_mod = to_sdl_mod(shortcut_mods->data[i]);
        assert(sdl_mod);
        im->sdl_shortcut_mods.data[i] = sdl_mod;
    }
    im->sdl_shortcut_mods.count = shortcut_mods->count;

    im->vfinger_down = false;

    im->last_keycode = SDLK_UNKNOWN;
    im->last_mod = 0;
    im->key_repeat = 0;

    im->next_sequence = 1; // 0 is reserved for SC_SEQUENCE_INVALID
}

static void
send_keycode(struct controller *controller, enum android_keycode keycode,
             int actions, const char *name) {
    // send DOWN event
    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_INJECT_KEYCODE;
    msg.inject_keycode.keycode = keycode;
    msg.inject_keycode.metastate = 0;
    msg.inject_keycode.repeat = 0;

    if (actions & ACTION_DOWN) {
        msg.inject_keycode.action = AKEY_EVENT_ACTION_DOWN;
        if (!controller_push_msg(controller, &msg)) {
            LOGW("Could not request 'inject %s (DOWN)'", name);
            return;
        }
    }

    if (actions & ACTION_UP) {
        msg.inject_keycode.action = AKEY_EVENT_ACTION_UP;
        if (!controller_push_msg(controller, &msg)) {
            LOGW("Could not request 'inject %s (UP)'", name);
        }
    }
}

static inline void
action_home(struct controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_HOME, actions, "HOME");
}

static inline void
action_back(struct controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_BACK, actions, "BACK");
}

static inline void
action_app_switch(struct controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_APP_SWITCH, actions, "APP_SWITCH");
}

static inline void
action_power(struct controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_POWER, actions, "POWER");
}

static inline void
action_volume_up(struct controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_VOLUME_UP, actions, "VOLUME_UP");
}

static inline void
action_volume_down(struct controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_VOLUME_DOWN, actions, "VOLUME_DOWN");
}

static inline void
action_menu(struct controller *controller, int actions) {
    send_keycode(controller, AKEYCODE_MENU, actions, "MENU");
}

// turn the screen on if it was off, press BACK otherwise
// If the screen is off, it is turned on only on ACTION_DOWN
static void
press_back_or_turn_screen_on(struct controller *controller, int actions) {
    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON;

    if (actions & ACTION_DOWN) {
        msg.back_or_screen_on.action = AKEY_EVENT_ACTION_DOWN;
        if (!controller_push_msg(controller, &msg)) {
            LOGW("Could not request 'press back or turn screen on'");
            return;
        }
    }

    if (actions & ACTION_UP) {
        msg.back_or_screen_on.action = AKEY_EVENT_ACTION_UP;
        if (!controller_push_msg(controller, &msg)) {
            LOGW("Could not request 'press back or turn screen on'");
        }
    }
}

static void
expand_notification_panel(struct controller *controller) {
    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL;

    if (!controller_push_msg(controller, &msg)) {
        LOGW("Could not request 'expand notification panel'");
    }
}

static void
expand_settings_panel(struct controller *controller) {
    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL;

    if (!controller_push_msg(controller, &msg)) {
        LOGW("Could not request 'expand settings panel'");
    }
}

static void
collapse_panels(struct controller *controller) {
    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_COLLAPSE_PANELS;

    if (!controller_push_msg(controller, &msg)) {
        LOGW("Could not request 'collapse notification panel'");
    }
}

static bool
get_device_clipboard(struct controller *controller,
                     enum get_clipboard_copy_key copy_key) {
    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_GET_CLIPBOARD;
    msg.get_clipboard.copy_key = copy_key;

    if (!controller_push_msg(controller, &msg)) {
        LOGW("Could not request 'get device clipboard'");
        return false;
    }

    return true;
}

static bool
set_device_clipboard(struct controller *controller, bool paste,
                     uint64_t sequence) {
    char *text = SDL_GetClipboardText();
    if (!text) {
        LOGW("Could not get clipboard text: %s", SDL_GetError());
        return false;
    }

    char *text_dup = strdup(text);
    SDL_free(text);
    if (!text_dup) {
        LOGW("Could not strdup input text");
        return false;
    }

    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_SET_CLIPBOARD;
    msg.set_clipboard.sequence = sequence;
    msg.set_clipboard.text = text_dup;
    msg.set_clipboard.paste = paste;

    if (!controller_push_msg(controller, &msg)) {
        free(text_dup);
        LOGW("Could not request 'set device clipboard'");
        return false;
    }

    return true;
}

static void
set_screen_power_mode(struct controller *controller,
                      enum screen_power_mode mode) {
    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE;
    msg.set_screen_power_mode.mode = mode;

    if (!controller_push_msg(controller, &msg)) {
        LOGW("Could not request 'set screen power mode'");
    }
}

static void
switch_fps_counter_state(struct fps_counter *fps_counter) {
    // the started state can only be written from the current thread, so there
    // is no ToCToU issue
    if (fps_counter_is_started(fps_counter)) {
        fps_counter_stop(fps_counter);
        LOGI("FPS counter stopped");
    } else {
        if (fps_counter_start(fps_counter)) {
            LOGI("FPS counter started");
        } else {
            LOGE("FPS counter starting failed");
        }
    }
}

static void
clipboard_paste(struct controller *controller) {
    char *text = SDL_GetClipboardText();
    if (!text) {
        LOGW("Could not get clipboard text: %s", SDL_GetError());
        return;
    }
    if (!*text) {
        // empty text
        SDL_free(text);
        return;
    }

    char *text_dup = strdup(text);
    SDL_free(text);
    if (!text_dup) {
        LOGW("Could not strdup input text");
        return;
    }

    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_INJECT_TEXT;
    msg.inject_text.text = text_dup;
    if (!controller_push_msg(controller, &msg)) {
        free(text_dup);
        LOGW("Could not request 'paste clipboard'");
    }
}

static void
rotate_device(struct controller *controller) {
    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_ROTATE_DEVICE;

    if (!controller_push_msg(controller, &msg)) {
        LOGW("Could not request device rotation");
    }
}

static void
rotate_client_left(struct screen *screen) {
    unsigned new_rotation = (screen->rotation + 1) % 4;
    screen_set_rotation(screen, new_rotation);
}

static void
rotate_client_right(struct screen *screen) {
    unsigned new_rotation = (screen->rotation + 3) % 4;
    screen_set_rotation(screen, new_rotation);
}

static void
input_manager_process_text_input(struct input_manager *im,
                                 const SDL_TextInputEvent *event) {
    if (is_shortcut_mod(im, SDL_GetModState())) {
        // A shortcut must never generate text events
        return;
    }

    im->kp->ops->process_text(im->kp, event);
}

static bool
simulate_virtual_finger_pid(struct input_manager *im,
                        enum android_motionevent_action action,
                        struct sc_point point,
                        uint64_t pointer_id) {
    bool up = action == AMOTION_EVENT_ACTION_UP;

    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
    msg.inject_touch_event.action = action;
    msg.inject_touch_event.position.screen_size = im->screen->frame_size;
    msg.inject_touch_event.position.point = point;
    msg.inject_touch_event.pointer_id = pointer_id;
    msg.inject_touch_event.pressure = up ? 0.0f : 1.0f;
    msg.inject_touch_event.buttons = 0;

    if (!controller_push_msg(im->controller, &msg)) {
        LOGW("Could not request 'inject virtual finger event'");
        return false;
    }

    return true;
}

static bool
simulate_virtual_finger(struct input_manager *im,
                        enum android_motionevent_action action,
                        struct sc_point point) {
    bool up = action == AMOTION_EVENT_ACTION_UP;

    struct control_msg msg;
    msg.type = CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
    msg.inject_touch_event.action = action;
    msg.inject_touch_event.position.screen_size = im->screen->frame_size;
    msg.inject_touch_event.position.point = point;
    msg.inject_touch_event.pointer_id = POINTER_ID_VIRTUAL_FINGER;
    msg.inject_touch_event.pressure = up ? 0.0f : 1.0f;
    msg.inject_touch_event.buttons = 0;

    if (!controller_push_msg(im->controller, &msg)) {
        LOGW("Could not request 'inject virtual finger event'");
        return false;
    }

    return true;
}

static struct sc_point
inverse_point(struct sc_point point, struct sc_size size) {
    point.x = size.width - point.x;
    point.y = size.height - point.y;
    return point;
}

static void
input_manager_process_key(struct input_manager *im,
                          const SDL_KeyboardEvent *event) {
    // control: indicates the state of the command-line option --no-control
    bool control = im->control;

    struct controller *controller = im->controller;

    SDL_Keycode keycode = event->keysym.sym;
    uint16_t mod = event->keysym.mod;
    bool down = event->type == SDL_KEYDOWN;
    bool ctrl = event->keysym.mod & KMOD_CTRL;
    bool shift = event->keysym.mod & KMOD_SHIFT;
    bool repeat = event->repeat;

    bool smod = is_shortcut_mod(im, mod);

    if (down && !repeat) {
        if (keycode == im->last_keycode && mod == im->last_mod) {
            ++im->key_repeat;
        } else {
            im->key_repeat = 0;
            im->last_keycode = keycode;
            im->last_mod = mod;
        }
    }

    // The shortcut modifier is pressed
    if (smod) {
        int action = down ? ACTION_DOWN :
                ACTION_UP;

        switch (keycode)
        {
            case SDLK_q:
                // Enable or disable joystick mode
                if (down && control && !shift && !repeat)
                {
                    im->joystick_mode = !im->joystick_mode;

                    im->joystick_pos.x = 340;
                    im->joystick_pos.y = 865;
                    im->camera_pos.x = 1450;
                    im->camera_pos.y = 542;

                    // Toggle camera
                    simulate_virtual_finger_pid(
                        im,
                        im->joystick_mode ? AMOTION_EVENT_ACTION_DOWN : AMOTION_EVENT_ACTION_UP,
                        im->camera_pos,
                        2
                    );

                    sc_msleep(50);

                    // Mouse trap camera
                    SDL_SetRelativeMouseMode(
                        im->joystick_mode ? SDL_TRUE : SDL_FALSE
                    );

                    LOGI("Joystick mode %s", im->joystick_mode ? "enabled" : "disabled");
                }
                return;
            case SDLK_h:
                if (control && !shift && !repeat)
                {
                    action_home(controller, action);
                }
                return;
            case SDLK_b: // fall-through
            case SDLK_BACKSPACE:
                if (control && !shift && !repeat)
                {
                    action_back(controller, action);
                }
                return;
            case SDLK_s:
                if (control && !shift && !repeat)
                {
                    action_app_switch(controller, action);
                }
                return;
            case SDLK_m:
                if (control && !shift && !repeat)
                {
                    action_menu(controller, action);
                }
                return;
            case SDLK_p:
                if (control && !shift && !repeat)
                {
                    action_power(controller, action);
                }
                return;
            case SDLK_o:
                if (control && !repeat && down)
                {
                    enum screen_power_mode mode = shift
                                                        ? SCREEN_POWER_MODE_NORMAL
                                                        : SCREEN_POWER_MODE_OFF;
                    set_screen_power_mode(controller, mode);
                }
                return;
            case SDLK_DOWN:
                if (control && !shift)
                {
                    // forward repeated events
                    action_volume_down(controller, action);
                }
                return;
            case SDLK_UP:
                if (control && !shift)
                {
                    // forward repeated events
                    action_volume_up(controller, action);
                }
                return;
            case SDLK_LEFT:
                if (!shift && !repeat && down)
                {
                    rotate_client_left(im->screen);
                }
                return;
            case SDLK_RIGHT:
                if (!shift && !repeat && down)
                {
                    rotate_client_right(im->screen);
                }
                return;
            case SDLK_c:
                if (control && !shift && !repeat && down)
                {
                    get_device_clipboard(controller,
                                            GET_CLIPBOARD_COPY_KEY_COPY);
                }
                return;
            case SDLK_x:
                if (control && !shift && !repeat && down)
                {
                    get_device_clipboard(controller,
                                            GET_CLIPBOARD_COPY_KEY_CUT);
                }
                return;
            case SDLK_v:
                if (control && !repeat && down)
                {
                    if (shift || im->legacy_paste)
                    {
                        // inject the text as input events
                        clipboard_paste(controller);
                    }
                    else
                    {
                        // store the text in the device clipboard and paste,
                        // without requesting an acknowledgment
                        set_device_clipboard(controller, true,
                                                SC_SEQUENCE_INVALID);
                    }
                }
                return;
            case SDLK_f:
                if (!shift && !repeat && down)
                {
                    screen_switch_fullscreen(im->screen);
                }
                return;
            case SDLK_w:
                if (!shift && !repeat && down)
                {
                    screen_resize_to_fit(im->screen);
                }
                return;
            case SDLK_g:
                if (!shift && !repeat && down)
                {
                    screen_resize_to_pixel_perfect(im->screen);
                }
                return;
            case SDLK_i:
                if (!shift && !repeat && down)
                {
                    switch_fps_counter_state(&im->screen->fps_counter);
                }
                return;
            case SDLK_n:
                if (control && !repeat && down)
                {
                    if (shift)
                    {
                        collapse_panels(controller);
                    }
                    else if (im->key_repeat == 0)
                    {
                        expand_notification_panel(controller);
                    }
                    else
                    {
                        expand_settings_panel(controller);
                    }
                }
                return;
            case SDLK_r:
                if (control && !shift && !repeat && down)
                {
                    rotate_device(controller);
                }
                return;
        }

        return;
    }

    // Custom, joystick-mode specifics
    if (im->joystick_mode)
    {
        switch (keycode) {
            case SDLK_ESCAPE:
                // Reset joystick pos to the first value
                im->joystick_pos.x = 340;
                im->joystick_pos.y = 865;
                return;
            case SDLK_LSHIFT:
                // Crouch
                if (down) {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->crouch_btn_pos, 3);
                } else {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->crouch_btn_pos, 3);
                }

                return;
            case SDLK_SPACE:
                // Jump
                if (down) {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->jump_btn_pos, 4);
                } else {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->jump_btn_pos, 4);
                }

                return;
            case SDLK_r:
                // Reload
                if (down) {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->reload_btn_pos, 5);
                } else {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->reload_btn_pos, 5);
                }

                return;
            case SDLK_e:
                // Switch weapon
                if (down) {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->switch_wpn_btn_pos, 6);
                } else {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->switch_wpn_btn_pos, 6);
                }

                return;
            case SDLK_1:
                if (down) {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->scorestreak_btn_pos, 7);
                } else {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->scorestreak_btn_pos, 7);
                }

                return;
            case SDLK_2:
                // Modify scorestrea to substract scorestreak_offset from x
                if (down) {
                    im->scorestreak_btn_pos.x -= im->scorestreak_offset;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->scorestreak_btn_pos, 8);
                    im->scorestreak_btn_pos.x += im->scorestreak_offset;
                } else {
                    im->scorestreak_btn_pos.x -= im->scorestreak_offset;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->scorestreak_btn_pos, 8);
                    im->scorestreak_btn_pos.x += im->scorestreak_offset;
                }

                return;
            case SDLK_3:
                // Modify scorestreak to add scorestreak_offset to x
                if (down) {
                    im->scorestreak_btn_pos.x -= im->scorestreak_offset * 2;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->scorestreak_btn_pos, 9);
                    im->scorestreak_btn_pos.x += im->scorestreak_offset  * 2;
                } else {
                    im->scorestreak_btn_pos.x -= im->scorestreak_offset  * 2;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->scorestreak_btn_pos, 9);
                    im->scorestreak_btn_pos.x += im->scorestreak_offset  * 2;
                }

                return;
            case SDLK_q:
                // Press the operator skill button
                if (down) {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->skill_btn_pos, 10);
                } else {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->skill_btn_pos, 10);
                }

                return;
            case SDLK_f:
                // Press the throwable button
                if (down) {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->throwable_btn_pos, 11);
                } else {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->throwable_btn_pos, 11);
                }

                return;
            case SDLK_c:
                // Press the chat button
                if (down) {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->chat_btn_pos, 12);
                } else {
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->chat_btn_pos, 12);
                }

                return;
            case SDLK_w:
                // Move
                if (im->vjoystick_moving) {
                    bool is_up = im->joystick_down.up;
                    if (!is_up) {
                        // Combine the positions
                        im->joystick_pos.y -= im->js_mv_offset;
                        im->joystick_down.up = true;
                    }
                    else if (!down)
                    {
                        // Release
                        im->joystick_pos.y += im->js_mv_offset;
                        im->joystick_down.up = false;
                    }

                    if (!is_up || !down) {
                        simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_MOVE, im->joystick_pos, 1);
                        return;
                    }
                }

                if (down && !im->vjoystick_moving) {
                    LOGI("Moving forward");
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->joystick_pos, 1);
                    sc_msleep(35); // Sleep is required, otherwise the events may overlap
                    im->joystick_pos.y -= im->js_mv_offset;
                    im->joystick_down.up = true;
                    im->joystick_down.started_by = keycode;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_MOVE, im->joystick_pos, 1);
                    im->vjoystick_moving = true;
                    return;
                }

                if (!down && im->joystick_down.started_by == keycode) {
                    LOGI("Releasing movement");
                    im->joystick_pos.x = 340;
                    im->joystick_pos.y = 865;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->joystick_pos, 1);
                    im->vjoystick_moving = false;

                    return;
                }

                return;
            case SDLK_a:
                // Move
                if (im->vjoystick_moving) {
                    bool is_left = im->joystick_down.left;
                    if (!is_left) {
                        // Combine the positions
                        im->joystick_pos.x -= im->js_mv_offset;
                        im->joystick_down.left = true;
                    }
                    else if (!down)
                    {
                        // Release
                        im->joystick_pos.x += im->js_mv_offset;
                        im->joystick_down.left = false;
                    }

                    if (!is_left || !down) {
                        simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_MOVE, im->joystick_pos, 1);
                        return;
                    }
                }

                if (down && !im->vjoystick_moving) {
                    LOGI("Moving left");
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->joystick_pos, 1);
                    sc_msleep(35); // Sleep is required, otherwise the events may overlap
                    im->joystick_pos.x -= im->js_mv_offset;
                    im->joystick_down.left = true;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_MOVE, im->joystick_pos, 1);
                    im->vjoystick_moving = true;
                    return;
                }

                if (!down && im->joystick_down.started_by == keycode) {
                    LOGI("Releasing movement");
                    im->joystick_pos.x = 340;
                    im->joystick_pos.y = 865;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->joystick_pos, 1);
                    im->vjoystick_moving = false;
                    return;
                }

                return;
            case SDLK_s:
                // Move
                if (im->vjoystick_moving) {
                    bool is_down = im->joystick_down.down;
                    if (!is_down) {
                        // Combine the positions
                        im->joystick_pos.y += im->js_mv_offset;
                        im->joystick_down.down = true;
                    }
                    else if (!down)
                    {
                        // Release
                        im->joystick_pos.y -= im->js_mv_offset;
                        im->joystick_down.down = false;
                    }

                    if (!is_down || !down) {
                        simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_MOVE, im->joystick_pos, 1);
                        return;
                    }
                }

                if (down && !im->vjoystick_moving) {
                    LOGI("Moving backward");
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->joystick_pos, 1);
                    sc_msleep(35); // Sleep is required, otherwise the events may overlap
                    im->joystick_pos.y += im->js_mv_offset;
                    im->joystick_down.down = true;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_MOVE, im->joystick_pos, 1);
                    im->vjoystick_moving = true;
                    return;
                }

                if (!down && im->joystick_down.started_by == keycode) {
                    LOGI("Releasing movement");
                    im->joystick_pos.x = 340;
                    im->joystick_pos.y = 865;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->joystick_pos, 1);
                    im->vjoystick_moving = false;
                    return;
                }

                return;
            case SDLK_d:
                // Move
                if (im->vjoystick_moving) {
                    bool is_right = im->joystick_down.right;
                    if (!is_right) {
                        // Combine the positions
                        im->joystick_pos.x += im->js_mv_offset;
                        im->joystick_down.right = true;
                    }
                    else if (!down)
                    {
                        // Release
                        im->joystick_pos.x -= im->js_mv_offset;
                        im->joystick_down.right = false;
                    }

                    if (!is_right || !down) {
                        simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_MOVE, im->joystick_pos, 1);
                        return;
                    }
                }

                if (down && !im->vjoystick_moving) {
                    LOGI("Moving right");
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->joystick_pos, 1);
                    sc_msleep(35); // Sleep is required, otherwise the events may overlap
                    im->joystick_pos.x += 100;
                    im->joystick_down.right = true;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_MOVE, im->joystick_pos, 1);
                    im->vjoystick_moving = true;
                    return;
                }

                if (!down && im->joystick_down.started_by == keycode) {
                    LOGI("Releasing movement");
                    im->joystick_pos.x = 340;
                    im->joystick_pos.y = 865;
                    simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->joystick_pos, 1);
                    im->vjoystick_moving = false;
                    return;
                }

                return;
        }
    }

    if (!control) {
        return;
    }

    uint64_t ack_to_wait = SC_SEQUENCE_INVALID;
    bool is_ctrl_v = ctrl && !shift && keycode == SDLK_v && down && !repeat;
    if (im->clipboard_autosync && is_ctrl_v) {
        if (im->legacy_paste) {
            // inject the text as input events
            clipboard_paste(controller);
            return;
        }

        // Request an acknowledgement only if necessary
        uint64_t sequence = im->kp->async_paste ? im->next_sequence
                                                : SC_SEQUENCE_INVALID;

        // Synchronize the computer clipboard to the device clipboard before
        // sending Ctrl+v, to allow seamless copy-paste.
        bool ok = set_device_clipboard(controller, false, sequence);
        if (!ok) {
            LOGW("Clipboard could not be synchronized, Ctrl+v not injected");
            return;
        }

        if (im->kp->async_paste) {
            // The key processor must wait for this ack before injecting Ctrl+v
            ack_to_wait = sequence;
            // Increment only when the request succeeded
            ++im->next_sequence;
        }
    }

    im->kp->ops->process_key(im->kp, event, ack_to_wait);
}

static void
input_manager_process_mouse_motion(struct input_manager *im,
                                   const SDL_MouseMotionEvent *event) {
    uint32_t mask = SDL_BUTTON_LMASK;

    // Joystick mode specific handling
    if (im->joystick_mode) {
        // In joystick mode, we move the camera
        im->camera_pos.x += (int) (
            event->xrel *
            (im->vjoystick_shooting
            ? im->camera_sensitivity_shooting
            : im->camera_sensitivity_normal)
        );

        im->camera_pos.y += (int) (
            event->yrel *
            (im->vjoystick_shooting
            ? im->camera_sensitivity_shooting
            : im->camera_sensitivity_normal)
        );

        simulate_virtual_finger_pid(
            im,
            AMOTION_EVENT_ACTION_MOVE,
            im->camera_pos,
            2
        );

        return;
    }

    if (im->forward_all_clicks)
    {
        mask |= SDL_BUTTON_MMASK | SDL_BUTTON_RMASK;
    }
    if (!(event->state & mask)) {
        // do not send motion events when no click is pressed
        return;
    }
    if (event->which == SDL_TOUCH_MOUSEID) {
        // simulated from touch events, so it's a duplicate
        return;
    }

    im->mp->ops->process_mouse_motion(im->mp, event);

    if (im->vfinger_down) {
        struct sc_point mouse =
            screen_convert_window_to_frame_coords(im->screen, event->x,
                                                  event->y);
        struct sc_point vfinger = inverse_point(mouse, im->screen->frame_size);
        simulate_virtual_finger(im, AMOTION_EVENT_ACTION_MOVE, vfinger);
    }
}

static void
input_manager_process_touch(struct input_manager *im,
                            const SDL_TouchFingerEvent *event) {
    im->mp->ops->process_touch(im->mp, event);
}

static void
input_manager_process_mouse_button(struct input_manager *im,
                                   const SDL_MouseButtonEvent *event) {
    bool control = im->control;

    if (event->which == SDL_TOUCH_MOUSEID) {
        // simulated from touch events, so it's a duplicate
        return;
    }

    bool down = event->type == SDL_MOUSEBUTTONDOWN;

    // Joystick mode specifics
    if (im->joystick_mode) {
        if (down) {
            // Start shooting
            LOGI("Shooting!");
            simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_DOWN, im->ads_btn_pos, 13);
            im->vjoystick_shooting = true;
        } else {
            // Stop shooting
            LOGI("Stopping fire");
            sc_msleep(25); // Sleep is required, otherwise the events may overlap
            simulate_virtual_finger_pid(im, AMOTION_EVENT_ACTION_UP, im->ads_btn_pos, 13);
            im->vjoystick_shooting = false;
        }
        return;
    }

    if (!im->forward_all_clicks) {
        int action = down ? ACTION_DOWN : ACTION_UP;

        if (control && event->button == SDL_BUTTON_X1) {
            action_app_switch(im->controller, action);
            return;
        }
        if (control && event->button == SDL_BUTTON_X2 && down) {
            if (event->clicks < 2) {
                expand_notification_panel(im->controller);
            } else {
                expand_settings_panel(im->controller);
            }
            return;
        }
        if (control && event->button == SDL_BUTTON_RIGHT) {
            press_back_or_turn_screen_on(im->controller, action);
            return;
        }
        if (control && event->button == SDL_BUTTON_MIDDLE) {
            action_home(im->controller, action);
            return;
        }

        // double-click on black borders resize to fit the device screen
        if (event->button == SDL_BUTTON_LEFT && event->clicks == 2) {
            int32_t x = event->x;
            int32_t y = event->y;
            screen_hidpi_scale_coords(im->screen, &x, &y);
            SDL_Rect *r = &im->screen->rect;
            bool outside = x < r->x || x >= r->x + r->w
                        || y < r->y || y >= r->y + r->h;
            if (outside) {
                if (down) {
                    screen_resize_to_fit(im->screen);
                }
                return;
            }
        }
        // otherwise, send the click event to the device
    }

    if (!control) {
        return;
    }

    im->mp->ops->process_mouse_button(im->mp, event);

    // Pinch-to-zoom simulation.
    //
    // If Ctrl is hold when the left-click button is pressed, then
    // pinch-to-zoom mode is enabled: on every mouse event until the left-click
    // button is released, an additional "virtual finger" event is generated,
    // having a position inverted through the center of the screen.
    //
    // In other words, the center of the rotation/scaling is the center of the
    // screen.
#define CTRL_PRESSED (SDL_GetModState() & (KMOD_LCTRL | KMOD_RCTRL))
    if ((down && !im->vfinger_down && CTRL_PRESSED)
            || (!down && im->vfinger_down)) {
        struct sc_point mouse =
            screen_convert_window_to_frame_coords(im->screen, event->x,
                                                              event->y);
        struct sc_point vfinger = inverse_point(mouse, im->screen->frame_size);
        enum android_motionevent_action action = down
                                               ? AMOTION_EVENT_ACTION_DOWN
                                               : AMOTION_EVENT_ACTION_UP;
        if (!simulate_virtual_finger(im, action, vfinger)) {
            return;
        }
        im->vfinger_down = down;
    }
}

static void
input_manager_process_mouse_wheel(struct input_manager *im,
                                  const SDL_MouseWheelEvent *event) {
    im->mp->ops->process_mouse_wheel(im->mp, event);
}

bool
input_manager_handle_event(struct input_manager *im, SDL_Event *event) {
    switch (event->type) {
        case SDL_TEXTINPUT:
            if (!im->control) {
                return true;
            }
            input_manager_process_text_input(im, &event->text);
            return true;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            // some key events do not interact with the device, so process the
            // event even if control is disabled
            input_manager_process_key(im, &event->key);
            return true;
        case SDL_MOUSEMOTION:
            if (!im->control) {
                break;
            }
            input_manager_process_mouse_motion(im, &event->motion);
            return true;
        case SDL_MOUSEWHEEL:
            if (!im->control) {
                break;
            }
            input_manager_process_mouse_wheel(im, &event->wheel);
            return true;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            // some mouse events do not interact with the device, so process
            // the event even if control is disabled
            input_manager_process_mouse_button(im, &event->button);
            return true;
        case SDL_FINGERMOTION:
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
            input_manager_process_touch(im, &event->tfinger);
            return true;
    }

    return false;
}
