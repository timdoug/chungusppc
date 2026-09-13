/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

(You may also contact divingkxt or powermax2286 on Discord)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <core/hostevents.h>
#include <core/coresignal.h>
#include <cpu/ppc/ppcemu.h>
#include <devices/common/adb/adbkeyboard.h>
#include <devices/floppy/floppyimg.h>
#include <loguru.hpp>
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

EventManager* EventManager::event_manager;

static int get_sdl_event_key_code(const SDL_KeyboardEvent& event, uint32_t kbd_locale);

constexpr int KMOD_ALL = KMOD_LSHIFT | KMOD_RSHIFT | KMOD_LCTRL | KMOD_RCTRL | KMOD_LALT | KMOD_RALT | KMOD_LGUI | KMOD_RGUI;

bool g_swap_command_option = false;

static AdbKey swap_command_option(AdbKey key)
{
    if (!g_swap_command_option)
        return key;
    switch (key) {
    case AdbKey_Option:      return AdbKey_Command;
    case AdbKey_RightOption: return AdbKey_Command;
    case AdbKey_Command:     return AdbKey_Option;
    default:                 return key;
    }
}

void EventManager::set_keyboard_locale(uint32_t keyboard_id) {
    this->kbd_locale = keyboard_id;
}

// ---------------------- scripted input injection ----------------------
//
// Lets the emulator be driven without a human at the controls, which is the
// only way to reach a guest that has no serial console or network yet. On
// SIGUSR2 the file below is read and its contents are fed to the guest, one
// step per event poll so the guest has time to notice each one.
//
//     text  hello world     type these characters
//     key   RETURN          press a named key
//     key   Shift+SLASH     press with modifiers held
//     mouse to 120 48       put the pointer at these screen coordinates
//     mouse by -10 0        move the pointer relative to where it is
//     mouse click           click the left button, or right / middle
//     mouse down            hold a button, for dragging
//     mouse up              release it
//     mouse step 32         pixels per report, to trade speed for accuracy
//     mouse rate 16         milliseconds between reports
//     floppy path.img       insert a floppy into the empty drive
//
// A position is only ever approximate. "mouse to" drives the pointer into the
// corner first because nothing here knows where the guest is drawing it, and
// how far the guest then moves for a given delta is up to the guest: Mac OS
// scales small ones down sharply, so a 32 pixel step tracks roughly 1:1 while
// an 8 pixel step covers barely half the distance. Take a screenshot and
// correct rather than trusting the first move to land.
#define INPUT_SCRIPT_PATH "chungusppc-input.txt"

// The largest delta an ADB report can carry, since it holds a signed 7 bit
// value per axis and quietly drops the rest. Only used to drive the pointer
// into the top left corner, where overshooting costs nothing.
constexpr int MOUSE_STEP_MAX = 63;

// How long a button is held, and the gap left after releasing it. A press and
// a release handed over back to back fall between two of the guest's polls,
// which then sees only a button that was never down.
constexpr uint32_t MOUSE_CLICK_DWELL_MS = 60;

// Far enough to drive the pointer into the top left corner from anywhere in
// any mode the emulated hardware offers, since nothing here knows the guest's
// screen size and the pointer stops at the edge regardless.
constexpr int MOUSE_HOME_TRAVEL = 3072;

static std::atomic<bool> input_script_requested(false);

void EventManager::request_input_script() {
    input_script_requested.store(true);
}

static const struct { const char *name; AdbKey key; } key_names[] = {
    {"RETURN", AdbKey_Return},     {"ENTER", AdbKey_Return},
    {"TAB", AdbKey_Tab},           {"SPACE", AdbKey_Space},
    {"ESC", AdbKey_Escape},        {"ESCAPE", AdbKey_Escape},
    {"DELETE", AdbKey_Delete},     {"BACKSPACE", AdbKey_Delete},
    {"UP", AdbKey_ArrowUp},        {"DOWN", AdbKey_ArrowDown},
    {"LEFT", AdbKey_ArrowLeft},    {"RIGHT", AdbKey_ArrowRight},
    {"HOME", AdbKey_Home},         {"END", AdbKey_End},
    {"PAGEUP", AdbKey_PageUp},     {"PAGEDOWN", AdbKey_PageDown},
    {"MINUS", AdbKey_Minus},       {"EQUAL", AdbKey_Equal},
    {"SLASH", AdbKey_Slash},       {"PERIOD", AdbKey_Period},
    {"COMMA", AdbKey_Comma},       {"SEMICOLON", AdbKey_Semicolon},
    {"QUOTE", AdbKey_Quote},       {"BACKSLASH", AdbKey_Backslash},
    {"F1", AdbKey_F1}, {"F2", AdbKey_F2}, {"F3", AdbKey_F3}, {"F4", AdbKey_F4},
    {"F5", AdbKey_F5}, {"F6", AdbKey_F6}, {"F7", AdbKey_F7}, {"F8", AdbKey_F8},
    {"F9", AdbKey_F9}, {"F10", AdbKey_F10}, {"F11", AdbKey_F11}, {"F12", AdbKey_F12},
};

static const struct { const char *name; AdbKey key; } mod_names[] = {
    {"Shift", AdbKey_Shift}, {"Control", AdbKey_Control}, {"Ctrl", AdbKey_Control},
    {"Option", AdbKey_Option}, {"Alt", AdbKey_Option}, {"Command", AdbKey_Command},
};

// ------------------ keys held down while the machine starts ------------------
//
// A Mac decides what kind of startup it is having from the keys held as it
// comes up: Shift for extensions off, C to boot from CD, Command-Option-P-R to
// zap PRAM. Holding them on the host only works if the emulator window already
// has keyboard focus when the guest first reads the keyboard, roughly a second
// and a half in, which it usually does not - the terminal the emulator was
// launched from still has it. Naming them up front sidesteps the race.
static std::vector<AdbKey> startup_keys;
static uint32_t startup_keys_release_ticks = 0;

// Long enough to cover loading a System Folder's worth of extensions, and the
// guest is in no position to want real keystrokes before then.
constexpr uint32_t STARTUP_KEYS_HOLD_MS = 10000;

/** Map a printable character to its key, and whether shift is needed. */
static bool char_to_key(char c, AdbKey *key, bool *shift) {
    static const char *unshifted = "abcdefghijklmnopqrstuvwxyz0123456789 -=[]\\;',./`";
    static const AdbKey keys[] = {
        AdbKey_A, AdbKey_B, AdbKey_C, AdbKey_D, AdbKey_E, AdbKey_F, AdbKey_G,
        AdbKey_H, AdbKey_I, AdbKey_J, AdbKey_K, AdbKey_L, AdbKey_M, AdbKey_N,
        AdbKey_O, AdbKey_P, AdbKey_Q, AdbKey_R, AdbKey_S, AdbKey_T, AdbKey_U,
        AdbKey_V, AdbKey_W, AdbKey_X, AdbKey_Y, AdbKey_Z,
        AdbKey_0, AdbKey_1, AdbKey_2, AdbKey_3, AdbKey_4,
        AdbKey_5, AdbKey_6, AdbKey_7, AdbKey_8, AdbKey_9,
        AdbKey_Space, AdbKey_Minus, AdbKey_Equal, AdbKey_LeftBracket,
        AdbKey_RightBracket, AdbKey_Backslash, AdbKey_Semicolon, AdbKey_Quote,
        AdbKey_Comma, AdbKey_Period, AdbKey_Slash, AdbKey_Grave,
    };
    static const char *shifted = "ABCDEFGHIJKLMNOPQRSTUVWXYZ)!@#$%^&*( _+{}|:\"<>?~";

    *shift = false;
    const char *p = strchr(unshifted, c);
    if (p != nullptr && c != '\0') { *key = keys[p - unshifted]; return true; }
    p = strchr(shifted, c);
    if (p != nullptr && c != '\0') { *key = keys[p - shifted]; *shift = true; return true; }
    return false;
}

/** Resolve one name from a startup key spec: a modifier, a named key, or a
    single character. */
static bool name_to_key(const std::string& name, AdbKey *key) {
    for (auto &mn : mod_names)
        if (name == mn.name) { *key = mn.key; return true; }
    for (auto &kn : key_names)
        if (name == kn.name) { *key = kn.key; return true; }
    bool shift;
    return name.size() == 1 && char_to_key(name[0], key, &shift);
}

void EventManager::set_startup_keys(const std::string& key_spec) {
    startup_keys.clear();

    std::string rest = key_spec;
    while (!rest.empty()) {
        size_t plus = rest.find('+');
        std::string name = rest.substr(0, plus);
        rest = (plus == std::string::npos) ? "" : rest.substr(plus + 1);

        AdbKey key;
        if (name.empty())
            continue;
        if (name_to_key(name, &key))
            startup_keys.push_back(key);
        else
            LOG_F(WARNING, "startup keys: unknown key \"%s\"", name.c_str());
    }
}

void EventManager::queue_key(AdbKey key, bool down) {
    InputAction a{};
    a.kind = InputAction::Kind::Key;
    a.key  = key;
    a.down = down;
    this->input_queue.push_back(a);
}

void EventManager::queue_motion(int dx, int dy) {
    InputAction a{};
    a.kind = InputAction::Kind::Motion;
    a.dx   = (int16_t)dx;
    a.dy   = (int16_t)dy;
    this->input_queue.push_back(a);
}

void EventManager::queue_button(uint8_t button, bool down) {
    InputAction a{};
    a.kind   = InputAction::Kind::Button;
    a.button = button;
    a.down   = down;
    this->input_queue.push_back(a);
}

/** Break a move into steps small enough for one ADB report to carry. */
void EventManager::queue_move_by(int dx, int dy) {
    while (dx || dy) {
        int step = this->mouse_step;
        int sx = std::max(-step, std::min(step, dx));
        int sy = std::max(-step, std::min(step, dy));
        this->queue_motion(sx, sy);
        dx -= sx;
        dy -= sy;
    }
}

void EventManager::load_input_script() {
    std::ifstream f(INPUT_SCRIPT_PATH);
    if (!f) {
        LOG_F(ERROR, "input: cannot open %s", INPUT_SCRIPT_PATH);
        return;
    }

    int queued = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream ls(line);
        std::string verb;
        ls >> verb;

        if (verb == "text") {
            std::string rest;
            std::getline(ls, rest);
            if (!rest.empty() && rest[0] == ' ')
                rest.erase(0, 1);
            for (char c : rest) {
                AdbKey key; bool shift;
                if (!char_to_key(c, &key, &shift)) {
                    LOG_F(WARNING, "input: no key for '%c'", c);
                    continue;
                }
                if (shift) this->queue_key(AdbKey_Shift, true);
                this->queue_key(key, true);
                this->queue_key(key, false);
                if (shift) this->queue_key(AdbKey_Shift, false);
                queued++;
            }
        } else if (verb == "key") {
            std::string spec;
            ls >> spec;
            std::vector<AdbKey> mods;
            size_t plus;
            while ((plus = spec.find('+')) != std::string::npos) {
                std::string m = spec.substr(0, plus);
                spec.erase(0, plus + 1);
                for (auto &mn : mod_names)
                    if (m == mn.name) { mods.push_back(mn.key); break; }
            }
            AdbKey key = (AdbKey)-1;
            for (auto &kn : key_names)
                if (spec == kn.name) { key = kn.key; break; }
            if (key == (AdbKey)-1) {
                AdbKey ck; bool sh;
                if (spec.size() == 1 && char_to_key(spec[0], &ck, &sh))
                    key = ck;
            }
            if (key == (AdbKey)-1) {
                LOG_F(WARNING, "input: unknown key \"%s\"", spec.c_str());
                continue;
            }
            for (auto m : mods) this->queue_key(m, true);
            this->queue_key(key, true);
            this->queue_key(key, false);
            for (auto it = mods.rbegin(); it != mods.rend(); ++it)
                this->queue_key(*it, false);
            queued++;
        } else if (verb == "floppy") {
            InputAction action{};
            action.kind = InputAction::Kind::Floppy;
            std::getline(ls >> std::ws, action.image_path);
            if (!action.image_path.empty()) {
                this->input_queue.push_back(action);
                queued++;
            }
        } else if (verb == "mouse") {
            std::string what;
            ls >> what;
            if (what == "to" || what == "by") {
                int x, y;
                if (!(ls >> x >> y)) {
                    LOG_F(WARNING, "input: mouse %s needs two numbers", what.c_str());
                    continue;
                }
                if (what == "to") {
                    // Nothing here knows where the guest is drawing its
                    // pointer, so start from a corner it can be driven into.
                    int steps = MOUSE_HOME_TRAVEL / MOUSE_STEP_MAX;
                    for (int i = 0; i < steps; i++)
                        this->queue_motion(-MOUSE_STEP_MAX, -MOUSE_STEP_MAX);
                }
                this->queue_move_by(x, y);
            } else if (what == "step" || what == "rate") {
                int n;
                if (!(ls >> n) || n <= 0) {
                    LOG_F(WARNING, "input: mouse %s needs a positive number", what.c_str());
                    continue;
                }
                if (what == "step")
                    this->mouse_step = std::min(n, MOUSE_STEP_MAX);
                else
                    this->mouse_rate_ms = (uint32_t)n;
            } else if (what == "click" || what == "down" || what == "up") {
                std::string which;
                ls >> which;
                uint8_t button = 0; // left
                if (which == "right")
                    button = 1;
                else if (which == "middle")
                    button = 2;
                else if (!which.empty() && which != "left") {
                    LOG_F(WARNING, "input: unknown button \"%s\"", which.c_str());
                    continue;
                }
                if (what != "up")
                    this->queue_button(button, true);
                if (what != "down")
                    this->queue_button(button, false);
            } else {
                LOG_F(WARNING, "input: unknown mouse action \"%s\"", what.c_str());
                continue;
            }
            queued++;
        } else {
            LOG_F(WARNING, "input: unknown directive \"%s\"", verb.c_str());
        }
    }

    LOG_F(INFO, "input: queued %d action(s) from %s", queued, INPUT_SCRIPT_PATH);
}

void EventManager::feed_input_script() {
    if (input_script_requested.exchange(false))
        this->load_input_script();

    if (this->input_queue.empty())
        return;

    // One step per poll: a keyboard driver needs to see each press and release
    // separately, and an ADB mouse reports whatever motion has piled up since
    // the last time it was asked, so steps handed over together are merged and
    // then clipped.
    // Mouse actions are paced against the clock rather than the poll rate,
    // which is free to run far faster than the guest reads its mouse.
    InputAction::Kind kind = this->input_queue.front().kind;
    if (kind == InputAction::Kind::Motion || kind == InputAction::Kind::Button) {
        uint32_t wait = kind == InputAction::Kind::Button ? MOUSE_CLICK_DWELL_MS
                                                          : this->mouse_rate_ms;
        uint32_t now = SDL_GetTicks();
        if (now - this->last_mouse_ticks < wait)
            return;
        this->last_mouse_ticks = now;
    }

    InputAction act = this->input_queue.front();
    this->input_queue.pop_front();

    switch (act.kind) {
    case InputAction::Kind::Floppy: {
        FloppyImageEvent event;
        event.image_path = act.image_path;
        this->post_floppy_event(event);
        if (!event.handled) LOG_F(ERROR, "No floppy drive is available");
        break;
    }
    case InputAction::Kind::Key: {
        KeyboardEvent ke{};
        ke.key   = act.key;
        ke.flags = act.down ? KEYBOARD_EVENT_DOWN : KEYBOARD_EVENT_UP;
        this->_keyboard_signal.emit(ke);
        break;
    }
    case InputAction::Kind::Motion: {
        MouseEvent me{};
        me.xrel  = act.dx;
        me.yrel  = act.dy;
        me.flags = MOUSE_EVENT_MOTION;
        this->_mouse_signal.emit(me);
        break;
    }
    case InputAction::Kind::Button: {
        MouseEvent me{};
        if (act.down)
            this->buttons_state |= 1 << act.button;
        else
            this->buttons_state &= ~(1 << act.button);
        me.buttons_state = this->buttons_state;
        me.flags = MOUSE_EVENT_BUTTON;
        this->_mouse_signal.emit(me);
        break;
    }
    }
}

void EventManager::poll_events() {
    this->feed_input_script();

    // Let go of the startup keys once the guest has had time to see them.
    if (startup_keys_release_ticks && SDL_GetTicks() >= startup_keys_release_ticks) {
        startup_keys_release_ticks = 0;
        KeyboardEvent ke{};
        for (auto it = startup_keys.rbegin(); it != startup_keys.rend(); ++it) {
            ke.key = *it;
            ke.flags = KEYBOARD_EVENT_UP;
            this->_keyboard_signal.emit(ke);
        }
        startup_keys.clear();
    }

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        events_captured++;

        switch (event.type) {
        case SDL_QUIT:
            power_off(po_quit);
            break;

        case SDL_WINDOWEVENT: {
                WindowEvent we{};
                we.sub_type  = event.window.event;
                we.window_id = event.window.windowID;
                this->_window_signal.emit(we);
            }
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
                if (event.key.repeat)
                    break;

                // Internal shortcuts, intentionally not sent to the host.
                // Control-G: mouse grab
                if (event.key.keysym.sym == SDLK_g && (event.key.keysym.mod & KMOD_ALL) == KMOD_LCTRL) {
                    if (event.type == SDL_KEYUP) {
                        WindowEvent we;
                        we.sub_type  = DPPC_WINDOWEVENT_MOUSE_GRAB_TOGGLE;
                        we.window_id = event.window.windowID;
                        this->_window_signal.emit(we);
                        we.sub_type  = DPPC_WINDOWEVENT_MOUSE_GRAB_CHANGED;
                        we.window_id = event.window.windowID;
                        this->_window_signal.emit(we);
                    }
                    return;
                }
                // Control-S: scale quality
                if (event.key.keysym.sym == SDLK_s && (event.key.keysym.mod & KMOD_ALL) == KMOD_LCTRL) {
                    if (event.type == SDL_KEYUP) {
                        WindowEvent we{};
                        we.sub_type  = DPPC_WINDOWEVENT_WINDOW_SCALE_QUALITY_TOGGLE;
                        we.window_id = event.window.windowID;
                        this->_window_signal.emit(we);
                    }
                    return;
                }
                // Control-F: fullscreen
                if (event.key.keysym.sym == SDLK_f && (event.key.keysym.mod & KMOD_ALL) == KMOD_LCTRL) {
                    if (event.type == SDL_KEYUP) {
                        WindowEvent we{};
                        we.sub_type  = DPPC_WINDOWEVENT_WINDOW_FULL_SCREEN_TOGGLE;
                        we.window_id = event.window.windowID;
                        this->_window_signal.emit(we);
                    }
                    return;
                }
                // Control-Shift-F: fullscreen reverse
                if (event.key.keysym.sym == SDLK_f && (event.key.keysym.mod & KMOD_ALL) == (KMOD_LCTRL | KMOD_LSHIFT)) {
                    if (event.type == SDL_KEYUP) {
                        WindowEvent we{};
                        we.sub_type  = DPPC_WINDOWEVENT_WINDOW_FULL_SCREEN_TOGGLE_REVERSE;
                        we.window_id = event.window.windowID;
                        this->_window_signal.emit(we);
                    }
                    return;
                }
                // Control-+: bigger
                if (event.key.keysym.sym == SDLK_EQUALS && (event.key.keysym.mod & KMOD_ALL) == KMOD_LCTRL) {
                    if (event.type == SDL_KEYUP) {
                        WindowEvent we{};
                        we.sub_type  = DPPC_WINDOWEVENT_WINDOW_BIGGER;
                        we.window_id = event.window.windowID;
                        this->_window_signal.emit(we);
                    }
                    return;
                }
                // Control--: smaller
                if (event.key.keysym.sym == SDLK_MINUS && (event.key.keysym.mod & KMOD_ALL) == KMOD_LCTRL) {
                    if (event.type == SDL_KEYUP) {
                        WindowEvent we{};
                        we.sub_type  = DPPC_WINDOWEVENT_WINDOW_SMALLER;
                        we.window_id = event.window.windowID;
                        this->_window_signal.emit(we);
                    }
                    return;
                }
                // Control-Alt-Shift-+: speed up icnt_factor
                if (event.key.keysym.sym == SDLK_EQUALS &&
                    (event.key.keysym.mod & KMOD_ALL) == (KMOD_LCTRL | KMOD_LALT | KMOD_LSHIFT)
                ) {
                    if (event.type == SDL_KEYUP) {
                        int icnt_counter_val = increment_icnt_factor();
                        LOG_F(INFO, "Incremented icnt_factor: %d", icnt_counter_val);
                    }
                    return;
                }
                // Control-Alt-Shift--: slow down icnt_factor
                if (event.key.keysym.sym == SDLK_MINUS &&
                    (event.key.keysym.mod & KMOD_ALL) == (KMOD_LCTRL | KMOD_LALT | KMOD_LSHIFT)
                ) {
                    if (event.type == SDL_KEYUP) {
                        int icnt_counter_val = decrement_icnt_factor();
                        LOG_F(INFO, "Decremented icnt_factor: %d", icnt_counter_val);
                    }
                    return;
                }

                // Control-Alt-R: g_realtime toggle
                if (event.key.keysym.sym == SDLK_r && (event.key.keysym.mod & KMOD_ALL) == (KMOD_LCTRL | KMOD_LALT)) {
                    if (event.type == SDL_KEYUP) {
                        bool g_realtime_status = toggle_g_realtime();
                        LOG_F(INFO, "g_realtime: %s", g_realtime_status ? "enabled" : "disabled");
                    }
                }

                // Control-L: log toggle
                if (event.key.keysym.sym == SDLK_l && (event.key.keysym.mod & KMOD_ALL) == KMOD_LCTRL) {
                    if (event.type == SDL_KEYUP) {
                        loguru::Verbosity new_verbosity = loguru::g_stderr_verbosity;
                        if (new_verbosity < loguru::Verbosity_INFO)
                            new_verbosity = loguru::Verbosity_INFO;
                        else if (new_verbosity < loguru::Verbosity_MAX)
                            new_verbosity = loguru::Verbosity_MAX;
                        else
                            new_verbosity = loguru::Verbosity_OFF;
                        loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
                        LOG_F(INFO, "g_stderr_verbosity: %d", new_verbosity);
                        loguru::g_stderr_verbosity = new_verbosity;
                    }
                    return;
                }
                // Control-D: debugger
                if (event.key.keysym.sym == SDLK_d && (event.key.keysym.mod & KMOD_ALL) == KMOD_LCTRL) {
                    if (event.type == SDL_KEYUP) {
                        power_off(po_enter_debugger);
                    }
                    return;
                }
                // Ralt+delete => ctrl+alt+del
                if (event.key.keysym.sym == SDLK_DELETE && ((event.key.keysym.mod & KMOD_ALL) == KMOD_RALT) != 0) {
                    KeyboardEvent ke{};
                    ke.key = AdbKey_Control;

                    if (event.type == SDL_KEYDOWN) {
                        ke.flags = KEYBOARD_EVENT_DOWN;
                        key_downs++;
                    } else {
                        ke.flags = KEYBOARD_EVENT_UP;
                        key_ups++;
                    }

                    this->_keyboard_signal.emit(ke);
                    ke.key = AdbKey_Delete;
                    this->_keyboard_signal.emit(ke);
                    return;
                }
                int key_code = get_sdl_event_key_code(event.key, this->kbd_locale);
                if (key_code != -1) {
                    KeyboardEvent ke{};
                    ke.key = key_code;
                    if (event.type == SDL_KEYDOWN) {
                        ke.flags = KEYBOARD_EVENT_DOWN;
                        key_downs++;
                    } else {
                        ke.flags = KEYBOARD_EVENT_UP;
                        key_ups++;
                    }
                    // Caps Lock is a special case, since it's a toggle key
                    if (ke.key == AdbKey_CapsLock) {
                        ke.flags = event.key.keysym.mod & KMOD_CAPS ?
                            KEYBOARD_EVENT_DOWN : KEYBOARD_EVENT_UP;
                    }
                    this->_keyboard_signal.emit(ke);
                } else {
                    LOG_F(WARNING, "Unknown key %x pressed", event.key.keysym.sym);
                }
            }
            break;

        case SDL_MOUSEMOTION: {
                MouseEvent me{};
                me.xrel  = event.motion.xrel;
                me.yrel  = event.motion.yrel;
                me.xabs  = event.motion.x;
                me.yabs  = event.motion.y;
                me.flags = MOUSE_EVENT_MOTION;
                this->_mouse_signal.emit(me);
            }
            break;

        case SDL_MOUSEBUTTONDOWN: {
                MouseEvent me{};
                Uint8 adb_button;
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT   : adb_button = 0; break;
                    case SDL_BUTTON_MIDDLE : adb_button = 2; break;
                    case SDL_BUTTON_RIGHT  : adb_button = 1; break;
                    default                : adb_button = event.button.button - 1;
                }
                me.buttons_state = (this->buttons_state |= (1 << adb_button));
                me.xabs  = event.button.x;
                me.yabs  = event.button.y;
                me.flags = MOUSE_EVENT_BUTTON;
                this->_mouse_signal.emit(me);
            }
            break;

        case SDL_MOUSEBUTTONUP: {
                MouseEvent me{};
                Uint8 adb_button;
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT   : adb_button = 0; break;
                    case SDL_BUTTON_MIDDLE : adb_button = 2; break;
                    case SDL_BUTTON_RIGHT  : adb_button = 1; break;
                    default                : adb_button = event.button.button - 1;
                }
                me.buttons_state = (this->buttons_state &= ~(1 << adb_button));
                me.xabs  = event.button.x;
                me.yabs  = event.button.y;
                me.flags = MOUSE_EVENT_BUTTON;
                this->_mouse_signal.emit(me);
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN: {
                GamepadEvent ge{};
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_BACK:          ge.button = GamepadButton::FrontLeft;    break;
                    case SDL_CONTROLLER_BUTTON_GUIDE:         ge.button = GamepadButton::FrontMiddle;  break;
                    case SDL_CONTROLLER_BUTTON_START:         ge.button = GamepadButton::FrontRight;   break;
                    case SDL_CONTROLLER_BUTTON_Y:             ge.button = GamepadButton::Blue;         break;
                    case SDL_CONTROLLER_BUTTON_X:             ge.button = GamepadButton::Yellow;       break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:       ge.button = GamepadButton::Up;           break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     ge.button = GamepadButton::Left;         break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    ge.button = GamepadButton::Right;        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     ge.button = GamepadButton::Down;         break;
                    case SDL_CONTROLLER_BUTTON_A:             ge.button = GamepadButton::Red;          break;
                    case SDL_CONTROLLER_BUTTON_B:             ge.button = GamepadButton::Green;        break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: ge.button = GamepadButton::RightTrigger; break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  ge.button = GamepadButton::LeftTrigger;  break;
                }
                ge.gamepad_id = event.cbutton.which;
                ge.flags = GAMEPAD_EVENT_DOWN;
                this->_gamepad_signal.emit(ge);
            }
            break;

        case SDL_CONTROLLERBUTTONUP: {
                GamepadEvent ge{};
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_BACK:          ge.button = GamepadButton::FrontLeft;    break;
                    case SDL_CONTROLLER_BUTTON_GUIDE:         ge.button = GamepadButton::FrontMiddle;  break;
                    case SDL_CONTROLLER_BUTTON_START:         ge.button = GamepadButton::FrontRight;   break;
                    case SDL_CONTROLLER_BUTTON_Y:             ge.button = GamepadButton::Blue;         break;
                    case SDL_CONTROLLER_BUTTON_X:             ge.button = GamepadButton::Yellow;       break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:       ge.button = GamepadButton::Up;           break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     ge.button = GamepadButton::Left;         break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    ge.button = GamepadButton::Right;        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     ge.button = GamepadButton::Down;         break;
                    case SDL_CONTROLLER_BUTTON_A:             ge.button = GamepadButton::Red;          break;
                    case SDL_CONTROLLER_BUTTON_B:             ge.button = GamepadButton::Green;        break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: ge.button = GamepadButton::RightTrigger; break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  ge.button = GamepadButton::LeftTrigger;  break;
                }
                ge.gamepad_id = event.cbutton.which;
                ge.flags = GAMEPAD_EVENT_UP;
                this->_gamepad_signal.emit(ge);
            }
            break;

        case SDL_DROPFILE: {
                const char* path = event.drop.file;
                if (is_floppy_image(path)) {
                    FloppyImageEvent floppy_event;
                    floppy_event.image_path = path;
                    this->post_floppy_event(floppy_event);
                    if (!floppy_event.handled) LOG_F(ERROR, "No floppy drive is available");
                    SDL_free(event.drop.file);
                    break;
                }
                CdromImageEvent cdrom_event{};
                cdrom_event.image_path = path;
                this->post_cdrom_event(cdrom_event);

                switch (cdrom_event.result) {
                case CdromInsertionResult::SUCCESS:
                    LOG_F(INFO, "Inserted CD-ROM image: %s", path);
                    break;
                case CdromInsertionResult::NO_DRIVE:
                    LOG_F(ERROR, "Cannot insert CD-ROM image; no CD-ROM drive is available: %s",
                          path);
                    break;
                case CdromInsertionResult::MEDIA_PRESENT:
                    LOG_F(ERROR, "Cannot insert CD-ROM image; eject the current media first: %s",
                          path);
                    break;
                case CdromInsertionResult::IMAGE_OPEN_FAILED:
                    LOG_F(ERROR, "Cannot insert CD-ROM image: %s", path);
                    break;
                }

                SDL_free(event.drop.file);
            }
            break;

        default:
            unhandled_events++;
        }
    }

    // perform post-processing
    this->_post_signal.emit();
}

void EventManager::post_keyboard_state_events() {
    int count;
    int numkeys;
    const Uint8 *states = SDL_GetKeyboardState(&numkeys);
    int modstate = SDL_GetModState();

    SDL_KeyboardEvent keyevent = { .type = SDL_KEYDOWN };
    SDL_Scancode scancode;
    KeyboardEvent ke{};

    typedef struct {
        SDL_Scancode scancode;
        SDL_Keymod   keymod;
        AdbKey       adbkey;
    } Modifier_t;

    static Modifier_t modifiers[] = {
        { SDL_SCANCODE_LSHIFT       , KMOD_LSHIFT , AdbKey_Shift        },
        { SDL_SCANCODE_RSHIFT       , KMOD_RSHIFT , AdbKey_RightShift   },
        { SDL_SCANCODE_LCTRL        , KMOD_LCTRL  , AdbKey_Control      },
        { SDL_SCANCODE_RCTRL        , KMOD_RCTRL  , AdbKey_RightControl },
        { SDL_SCANCODE_LALT         , KMOD_LALT   , AdbKey_Option       },
        { SDL_SCANCODE_RALT         , KMOD_RALT   , AdbKey_RightOption  },
        { SDL_SCANCODE_LGUI         , KMOD_LGUI   , AdbKey_Command      },
        { SDL_SCANCODE_RGUI         , KMOD_RGUI   , AdbKey_Command      },
//      { SDL_SCANCODE_NUMLOCKCLEAR , KMOD_NUM    , AdbKey_KeypadClear  },
        { SDL_SCANCODE_CAPSLOCK     , KMOD_CAPS   , AdbKey_CapsLock     },
//      { SDL_SCANCODE_MODE         , KMOD_MODE   , AdbKey_????         },
//      { SDL_SCANCODE_SCROLLLOCK   , KMOD_SCROLL , AdbKey_F14          },
        { SDL_SCANCODE_UNKNOWN                                          },
    };

    LOG_F(INFO, "Current keyboard state:");

    LOG_F(INFO, "    Modifiers:");
    count = 0;
    for (Modifier_t *mod = modifiers; mod->scancode != SDL_SCANCODE_UNKNOWN; mod++) {
        if (!(modstate & mod->keymod))
            continue;
        LOG_F(INFO, "        Modifier: %s", SDL_GetScancodeName(mod->scancode));
        count++;
        ke.key = swap_command_option(mod->adbkey);
        ke.flags = KEYBOARD_EVENT_DOWN;
        this->_keyboard_signal.emit(ke);
    }
    for (AdbKey key : startup_keys) {
        LOG_F(INFO, "        Held from the command line: 0x%02X", key);
        count++;
        ke.key = key;
        ke.flags = KEYBOARD_EVENT_DOWN;
        this->_keyboard_signal.emit(ke);
    }
    if (!startup_keys.empty())
        startup_keys_release_ticks = SDL_GetTicks() + STARTUP_KEYS_HOLD_MS;

    if (!count)
        LOG_F(INFO, "        (none)");

    LOG_F(INFO, "    Keys and Modifiers:");
    count = 0;
    for (int i = 0; i < numkeys; i++) {
        if (!states[i])
            continue;

        count++;
        scancode = (SDL_Scancode)i;

        Modifier_t *mod = modifiers;
        for (; mod->scancode != SDL_SCANCODE_UNKNOWN && mod->scancode != scancode; mod++);
        if (mod->scancode == scancode) {
            LOG_F(INFO, "        Modifier: %s", SDL_GetScancodeName(scancode));
            continue;
        }

        LOG_F(INFO, "        Key: %s", SDL_GetScancodeName(scancode));
        keyevent.keysym.scancode = scancode;
        keyevent.keysym.sym = SDL_GetKeyFromScancode(scancode);
        keyevent.keysym.mod = modstate;

        int key_code = get_sdl_event_key_code(keyevent, this->kbd_locale);
        if (key_code != -1) {
            ke.key = key_code;
            ke.flags = KEYBOARD_EVENT_DOWN;
            this->_keyboard_signal.emit(ke);
        } else {
            LOG_F(WARNING, "        Unknown key %x pressed", keyevent.keysym.sym);
        }
    }
    if (!count)
        LOG_F(INFO, "        (none)");
}

static int get_sdl_event_key_code(const SDL_KeyboardEvent &event, uint32_t kbd_locale)
{
    switch (event.keysym.sym) {
    case SDLK_a:            return AdbKey_A;
    case SDLK_b:            return AdbKey_B;
    case SDLK_c:            return AdbKey_C;
    case SDLK_d:            return AdbKey_D;
    case SDLK_e:            return AdbKey_E;
    case SDLK_f:            return AdbKey_F;
    case SDLK_g:            return AdbKey_G;
    case SDLK_h:            return AdbKey_H;
    case SDLK_i:            return AdbKey_I;
    case SDLK_j:            return AdbKey_J;
    case SDLK_k:            return AdbKey_K;
    case SDLK_l:            return AdbKey_L;
    case SDLK_m:            return AdbKey_M;
    case SDLK_n:            return AdbKey_N;
    case SDLK_o:            return AdbKey_O;
    case SDLK_p:            return AdbKey_P;
    case SDLK_q:            return AdbKey_Q;
    case SDLK_r:            return AdbKey_R;
    case SDLK_s:            return AdbKey_S;
    case SDLK_t:            return AdbKey_T;
    case SDLK_u:            return AdbKey_U;
    case SDLK_v:            return AdbKey_V;
    case SDLK_w:            return AdbKey_W;
    case SDLK_x:            return AdbKey_X;
    case SDLK_y:            return AdbKey_Y;
    case SDLK_z:            return AdbKey_Z;

    case SDLK_1:            return AdbKey_1;
    case SDLK_2:            return AdbKey_2;
    case SDLK_3:            return AdbKey_3;
    case SDLK_4:            return AdbKey_4;
    case SDLK_5:            return AdbKey_5;
    case SDLK_6:            return AdbKey_6;
    case SDLK_7:            return AdbKey_7;
    case SDLK_8:            return AdbKey_8;
    case SDLK_9:            return AdbKey_9;
    case SDLK_0:            return AdbKey_0;

    case SDLK_ESCAPE:       return AdbKey_Escape;
    case SDLK_BACKQUOTE:    return AdbKey_Grave;
    case SDLK_MINUS:        return AdbKey_Minus;
    case SDLK_EQUALS:       return AdbKey_Equal;
    case SDLK_LEFTBRACKET:  return AdbKey_LeftBracket;
    case SDLK_RIGHTBRACKET: return AdbKey_RightBracket;
    case SDLK_BACKSLASH:    return AdbKey_Backslash;
    case SDLK_SEMICOLON:    return AdbKey_Semicolon;
    case SDLK_QUOTE:        return AdbKey_Quote;
    case SDLK_COMMA:        return AdbKey_Comma;
    case SDLK_PERIOD:       return AdbKey_Period;
    case SDLK_SLASH:        return AdbKey_Slash;

    // Convert shifted variants to unshifted
    case SDLK_EXCLAIM:      return AdbKey_1;
    case SDLK_AT:           return AdbKey_2;
    case SDLK_HASH:         return AdbKey_3;
    case SDLK_DOLLAR:       return AdbKey_4;
    case SDLK_UNDERSCORE:   return AdbKey_Minus;
    case SDLK_PLUS:         return AdbKey_Equal;
    case SDLK_COLON:        return AdbKey_Semicolon;
    case SDLK_QUOTEDBL:     return AdbKey_Quote;
    case SDLK_LESS:         return AdbKey_Comma;
    case SDLK_GREATER:      return AdbKey_Period;
    case SDLK_QUESTION:     return AdbKey_Slash;

    case SDLK_TAB:          return AdbKey_Tab;
    case SDLK_RETURN:       return AdbKey_Return;
    case SDLK_SPACE:        return AdbKey_Space;
    case SDLK_BACKSPACE:    return AdbKey_Delete;

    case SDLK_DELETE:       return AdbKey_ForwardDelete;
    case SDLK_INSERT:       return AdbKey_Help;
    case SDLK_HOME:         return AdbKey_Home;
    case SDLK_HELP:         return AdbKey_Home;
    case SDLK_END:          return AdbKey_End;
    case SDLK_PAGEUP:       return AdbKey_PageUp;
    case SDLK_PAGEDOWN:     return AdbKey_PageDown;

    case SDLK_LCTRL:        return AdbKey_Control;
    case SDLK_RCTRL:        return AdbKey_RightControl;
    case SDLK_LSHIFT:       return AdbKey_Shift;
    case SDLK_RSHIFT:       return AdbKey_RightShift;
    case SDLK_LALT:         return swap_command_option(AdbKey_Option);
    case SDLK_RALT:         return swap_command_option(AdbKey_RightOption);
    case SDLK_LGUI:         return swap_command_option(AdbKey_Command);
    case SDLK_RGUI:         return swap_command_option(AdbKey_Command);
    case SDLK_MENU:         return AdbKey_Grave;
    case SDLK_CAPSLOCK:     return AdbKey_CapsLock;

    case SDLK_UP:           return AdbKey_ArrowUp;
    case SDLK_DOWN:         return AdbKey_ArrowDown;
    case SDLK_LEFT:         return AdbKey_ArrowLeft;
    case SDLK_RIGHT:        return AdbKey_ArrowRight;

    case SDLK_KP_0:         return AdbKey_Keypad0;
    case SDLK_KP_1:         return AdbKey_Keypad1;
    case SDLK_KP_2:         return AdbKey_Keypad2;
    case SDLK_KP_3:         return AdbKey_Keypad3;
    case SDLK_KP_4:         return AdbKey_Keypad4;
    case SDLK_KP_5:         return AdbKey_Keypad5;
    case SDLK_KP_6:         return AdbKey_Keypad6;
    case SDLK_KP_7:         return AdbKey_Keypad7;
    case SDLK_KP_9:         return AdbKey_Keypad9;
    case SDLK_KP_8:         return AdbKey_Keypad8;
    case SDLK_KP_PERIOD:    return AdbKey_KeypadDecimal;
    case SDLK_KP_PLUS:      return AdbKey_KeypadPlus;
    case SDLK_KP_MINUS:     return AdbKey_KeypadMinus;
    case SDLK_KP_MULTIPLY:  return AdbKey_KeypadMultiply;
    case SDLK_KP_DIVIDE:    return AdbKey_KeypadDivide;
    case SDLK_KP_ENTER:     return AdbKey_KeypadEnter;
    case SDLK_KP_EQUALS:    return AdbKey_KeypadEquals;
    case SDLK_NUMLOCKCLEAR: return AdbKey_KeypadClear;

    case SDLK_F1:           return AdbKey_F1;
    case SDLK_F2:           return AdbKey_F2;
    case SDLK_F3:           return AdbKey_F3;
    case SDLK_F4:           return AdbKey_F4;
    case SDLK_F5:           return AdbKey_F5;
    case SDLK_F6:           return AdbKey_F6;
    case SDLK_F7:           return AdbKey_F7;
    case SDLK_F8:           return AdbKey_F8;
    case SDLK_F9:           return AdbKey_F9;
    case SDLK_F10:          return AdbKey_F10;
    case SDLK_F11:          return AdbKey_F11;
    case SDLK_F12:          return AdbKey_F12;
    case SDLK_PRINTSCREEN:  return AdbKey_F13;
    case SDLK_SCROLLLOCK:   return AdbKey_F14;
    case SDLK_PAUSE:        return AdbKey_F15;

    // International keyboard support

    // Japanese keyboard
    case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_INTERNATIONAL3):
        if (kbd_locale == Jpn_JPN)
            return AdbKey_JIS_Yen;
        else
            return -1;
    case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_INTERNATIONAL1):
        return AdbKey_JIS_Underscore;
    case 0XBC:
        return AdbKey_JIS_KP_Comma;
    case 0X89:
        return AdbKey_JIS_Eisu;
    case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_INTERNATIONAL2):
        return AdbKey_JIS_Kana;

    // German keyboard
    case 0XB4:        return AdbKey_Slash;
    case 0X5E:        return AdbKey_ISO1;
    case 0XDF:        return AdbKey_Minus;       // Eszett
    case 0XE4:        return AdbKey_LeftBracket; // A-umlaut
    case 0XF6:        return AdbKey_Semicolon;   // O-umlaut
    case 0XFC:        return AdbKey_LeftBracket; // U-umlaut

    // French keyboard
    case 0X29:        return AdbKey_Minus;             // Right parenthesis
    case 0X43:        return AdbKey_KeypadMultiply;    // Star/Mu
    // 0XB2 is superscript 2. Which Mac key should this one map to?
    case 0XF9:        return AdbKey_Quote;             // U-grave

    // Italian keyboard
    case 0XE0:        return AdbKey_9;              // A-grave
    case 0XE8:        return AdbKey_6;              // E-grave
    case 0XEC:        return AdbKey_LeftBracket;    // I-grave
    case 0XF2:        return AdbKey_KeypadMultiply; // O-grave

    // Spanish keyboard
    case 0XA1:        return AdbKey_Comma;        // Inverted question mark
    case 0XBA:        return AdbKey_6;            // Backslash
    case 0XE7:        return AdbKey_Slash;        // C-cedilla
    case 0XF1:        return AdbKey_Semicolon;    // N-tilde
    case 0X4000002f:
        return AdbKey_LeftBracket;    // Acute
    case 0X40000034:
        return AdbKey_Semicolon;    // Acute
    }
    return -1;
}
