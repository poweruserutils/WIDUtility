#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <unordered_map>

namespace wid::gui {

enum class Section {
    Image,
    Editions,
    Components,
    Features,
    Applications,
    Tweaks,
    Unattended,
    Commands,
    Drivers,
    Updates,
    PendingChanges,
    Apply,
};

class MainWindow {
public:
    explicit MainWindow(HINSTANCE hInstance);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool create();
    int  runMessageLoop();

private:
    static LRESULT CALLBACK wndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);

    void onCreate(HWND);
    void onSize(int width, int height);
    void onCommand(WORD id, HWND ctrl);
    void onNotify(LPNMHDR);
    void layoutChildren();
    void buildNavTree();
    void showSection(Section);

    // Header / chrome
    void createHeader(HWND parent);

    // Per-section panels
    HWND createImagePanel(HWND parent);
    HWND createTweaksPanel(HWND parent);
    HWND createPlaceholderPanel(HWND parent, const wchar_t* title, const wchar_t* body);
    void populateTweaks();

    // Browse helpers
    void onBrowseSourceIso();
    void onBrowseOutputIso();
    bool pickFile(bool save, const wchar_t* title,
                  const wchar_t* filter, std::wstring& out);

    HINSTANCE hInstance_;
    HFONT     hFont_       = nullptr;
    HFONT     hFontBold_   = nullptr;
    HFONT     hFontTitle_  = nullptr;

    HWND hwnd_         = nullptr;
    HWND hwndHeader_   = nullptr;
    HWND hwndHeaderTitle_ = nullptr;
    HWND hwndLoadIsoBtn_  = nullptr;
    HWND hwndNav_      = nullptr;
    HWND hwndDetail_   = nullptr;     // container hosting the per-section panels
    HWND hwndStatus_   = nullptr;

    // Image panel
    HWND hwndPanelImage_     = nullptr;
    HWND hwndEditSourceIso_  = nullptr;
    HWND hwndEditOutputIso_  = nullptr;
    HWND hwndBtnBrowseSrc_   = nullptr;
    HWND hwndBtnBrowseDst_   = nullptr;

    // Tweaks panel
    HWND hwndPanelTweaks_    = nullptr;
    HWND hwndTweaksList_     = nullptr;
    HWND hwndTweaksDesc_     = nullptr;

    // Placeholder panels per section
    std::unordered_map<int, HWND> placeholders_;

    Section current_ = Section::Image;
};

} // namespace wid::gui
