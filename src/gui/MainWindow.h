#pragma once

#include "gui/ProgressWindow.h"

#include <windows.h>
#include <commctrl.h>
#include <memory>
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
    void createHeader(HWND parent);

    HWND createImagePanel       (HWND parent);
    HWND createEditionsPanel    (HWND parent);
    HWND createComponentsPanel  (HWND parent);
    HWND createFeaturesPanel    (HWND parent);
    HWND createApplicationsPanel(HWND parent);
    HWND createTweaksPanel      (HWND parent);
    HWND createUnattendedPanel  (HWND parent);
    HWND createCommandsPanel    (HWND parent);
    HWND createDriversPanel     (HWND parent);
    HWND createUpdatesPanel     (HWND parent);
    HWND createPendingPanel     (HWND parent);
    HWND createApplyPanel       (HWND parent);

    void populateTweaks();
    void populateApps();

    // Browse / actions
    void onBrowseSourceIso();
    void onBuildIso();
    void onAddDriver();
    void onRemoveDriver();
    void onAddUpdate();
    void onRemoveUpdate();
    void onAddCommand(bool postLogon);
    void onRemoveCommand(bool postLogon);
    void onPendingRemove();
    void onPendingClear();

    bool pickFile(bool save, const wchar_t* title,
                  const wchar_t* filter, const wchar_t* defExt,
                  std::wstring& out);

    HINSTANCE hInstance_;
    HFONT     hFont_       = nullptr;
    HFONT     hFontBold_   = nullptr;
    HFONT     hFontTitle_  = nullptr;

    HWND hwnd_              = nullptr;
    HWND hwndHeader_        = nullptr;
    HWND hwndHeaderTitle_   = nullptr;
    HWND hwndLoadIsoBtn_    = nullptr;
    HWND hwndNav_           = nullptr;
    HWND hwndDetail_        = nullptr;
    HWND hwndStatus_        = nullptr;

    // Image
    HWND hwndPanelImage_     = nullptr;
    HWND hwndEditSourceIso_  = nullptr;
    HWND hwndBtnBrowseSrc_   = nullptr;

    // Editions
    HWND hwndPanelEditions_  = nullptr;
    HWND hwndEditionsList_   = nullptr;
    HWND hwndChkTrim_        = nullptr;

    // Components
    HWND hwndPanelComponents_ = nullptr;
    HWND hwndComponentsList_  = nullptr;

    // Features
    HWND hwndPanelFeatures_  = nullptr;
    HWND hwndFeaturesList_   = nullptr;

    // Applications
    HWND hwndPanelApps_      = nullptr;
    HWND hwndAppsList_       = nullptr;
    HWND hwndAppsDesc_       = nullptr;

    // Tweaks
    HWND hwndPanelTweaks_    = nullptr;
    HWND hwndTweaksList_     = nullptr;
    HWND hwndTweaksDesc_     = nullptr;

    // Unattended
    HWND hwndPanelUnattended_     = nullptr;
    HWND hwndEditLocale_          = nullptr;
    HWND hwndEditTimezone_        = nullptr;
    HWND hwndEditComputerName_    = nullptr;
    HWND hwndEditAdminPassword_   = nullptr;
    HWND hwndEditAutoLogonUser_   = nullptr;
    HWND hwndEditAutoLogonPwd_    = nullptr;
    HWND hwndChkSkipMsAccount_    = nullptr;
    HWND hwndChkAcceptEula_       = nullptr;
    HWND hwndChkAutoLogon_        = nullptr;

    // Commands
    HWND hwndPanelCommands_  = nullptr;
    HWND hwndPreList_        = nullptr;
    HWND hwndPostList_       = nullptr;
    HWND hwndCmdEdit_        = nullptr;
    HWND hwndBtnAddPre_      = nullptr;
    HWND hwndBtnAddPost_     = nullptr;
    HWND hwndBtnDelPre_      = nullptr;
    HWND hwndBtnDelPost_     = nullptr;

    // Drivers
    HWND hwndPanelDrivers_   = nullptr;
    HWND hwndDriversList_    = nullptr;
    HWND hwndBtnAddDriver_   = nullptr;
    HWND hwndBtnDelDriver_   = nullptr;

    // Updates
    HWND hwndPanelUpdates_   = nullptr;
    HWND hwndUpdatesList_    = nullptr;
    HWND hwndBtnAddUpdate_   = nullptr;
    HWND hwndBtnDelUpdate_   = nullptr;

    // Pending Changes
    HWND hwndPanelPending_   = nullptr;
    HWND hwndPendingList_    = nullptr;
    HWND hwndBtnPendingDel_  = nullptr;
    HWND hwndBtnPendingClear_= nullptr;

    // Apply
    HWND hwndPanelApply_     = nullptr;
    HWND hwndApplySummary_   = nullptr;
    HWND hwndBtnBuildIso_    = nullptr;

    Section current_ = Section::Image;

    std::unique_ptr<ProgressWindow> progress_;
};

} // namespace wid::gui
