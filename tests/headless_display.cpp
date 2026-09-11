// Display implementation for device tests that do not exercise rendering.
#include <devices/video/display.h>

bool g_auto_grab_mouse = false;

class Display::Impl {};
Display::Display() = default;
Display::~Display() = default;
bool Display::configure(int, int) { return false; }
void Display::configure_dest() {}
void Display::configure_texture() {}
void Display::update_window_size() {}
void Display::blank() {}
void Display::update(std::function<void(uint8_t *, int)>,
                     std::function<void(uint8_t *, int)>, bool, int, int, bool) {}
void Display::update_skipped() {}
void Display::handle_events(const WindowEvent&) {}
void Display::setup_hw_cursor(std::function<void(uint8_t *, int)>, int, int) {}
void Display::update_window_title() {}
void Display::request_screenshot() {}
void Display::toggle_mouse_grab() {}
void Display::update_mouse_grab(bool) {}
