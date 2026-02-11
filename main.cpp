#include "tui.hpp"
#include <iostream>

int main() {
    try {
        App app;

        Rect screen = app.full_screen();

        uint32_t login_w = 40;
        uint32_t login_h = 12;

        uint32_t top_pad = (screen.height > login_h) ? (screen.height - login_h) / 2 : 0;
        uint32_t left_pad = (screen.width > login_w) ? (screen.width - login_w) / 2 : 0;

        Rect login_rect = { left_pad, top_pad, login_w, login_h };

        app.add_widget<Box>(screen, " System Access ");

        app.add_widget<LoginWidget>(login_rect);

        app.run();

    } catch (const std::exception& e) {
        std::cerr << "Critical Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
