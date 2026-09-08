#include "app.hpp"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    valinvite::App app{instance};
    return app.run();
}
