#include "gui/MainWindow.h"

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE /*hPrevInstance*/,
                      LPWSTR    /*lpCmdLine*/,
                      int       /*nCmdShow*/) {
    wid::gui::MainWindow window(hInstance);
    if (!window.create()) {
        MessageBoxW(nullptr, L"Failed to create main window.",
                    L"WID Utility", MB_ICONERROR);
        return 1;
    }
    return window.runMessageLoop();
}
