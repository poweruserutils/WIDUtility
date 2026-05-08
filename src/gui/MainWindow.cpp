#include "gui/MainWindow.h"

#include "core/Apps.h"
#include "core/Tweaks.h"
#include "core/Wim.h"

#include <windowsx.h>
#include <uxtheme.h>
#include <shobjidl.h>
#include <thread>
#include <vector>

namespace wid::gui {

namespace {

constexpr wchar_t kClassName[]   = L"WIDUtility.MainWindow";
constexpr wchar_t kPanelClass[]  = L"WIDUtility.Panel";
constexpr wchar_t kWindowTitle[] = L"WID Utility";

constexpr int kHeaderHeight = 64;
constexpr int kNavWidth     = 220;
constexpr int kPad          = 16;

constexpr WORD ID_NAV               = 1001;
constexpr WORD ID_DETAIL            = 1002;
constexpr WORD ID_STATUS            = 1003;
constexpr WORD ID_LOAD_ISO          = 2001;
constexpr WORD ID_BROWSE_SRC        = 2010;
constexpr WORD ID_EDIT_SRC          = 2012;
constexpr WORD ID_TWEAKS_LIST       = 2020;
constexpr WORD ID_APPS_LIST         = 2021;
constexpr WORD ID_BUILD_ISO         = 2030;
constexpr WORD ID_ADD_DRIVER        = 2040;
constexpr WORD ID_DEL_DRIVER        = 2041;
constexpr WORD ID_ADD_UPDATE        = 2050;
constexpr WORD ID_DEL_UPDATE        = 2051;
constexpr WORD ID_ADD_PRE           = 2060;
constexpr WORD ID_ADD_POST          = 2061;
constexpr WORD ID_DEL_PRE           = 2062;
constexpr WORD ID_DEL_POST          = 2063;
constexpr WORD ID_PENDING_DEL       = 2070;
constexpr WORD ID_PENDING_CLEAR     = 2071;

constexpr UINT WM_ISO_INSPECTED     = WM_APP + 1;

struct InspectPayload {
    std::wstring                       isoPath;
    std::vector<wid::core::WimEdition> editions;
};

struct NavEntry { Section section; const wchar_t* label; };

const std::vector<NavEntry>& navEntries() {
    static const std::vector<NavEntry> e = {
        { Section::Image,          L"Image"           },
        { Section::Editions,       L"Editions"        },
        { Section::Components,     L"Components"      },
        { Section::Features,       L"Features"        },
        { Section::Applications,   L"Applications"    },
        { Section::Tweaks,         L"Tweaks"          },
        { Section::Unattended,     L"Unattended"      },
        { Section::Commands,       L"Commands"        },
        { Section::Drivers,        L"Drivers"         },
        { Section::Updates,        L"Updates"         },
        { Section::PendingChanges, L"Pending Changes" },
        { Section::Apply,          L"Apply"           },
    };
    return e;
}

const wchar_t* sectionTitle(Section s) {
    for (const auto& e : navEntries()) if (e.section == s) return e.label;
    return L"";
}

HFONT makeFont(int pt, bool bold = false) {
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    LOGFONTW lf{};
    lf.lfHeight  = -MulDiv(pt, dpi, 72);
    lf.lfWeight  = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

LRESULT CALLBACK panelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageW(GetAncestor(h, GA_ROOT), m, w, l);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---- small UI builders ----

HWND mkLabel(HWND p, const wchar_t* text, int x, int y, int w, int h, HFONT f) {
    HWND r = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE, x, y, w, h, p, nullptr,
        (HINSTANCE)GetWindowLongPtrW(p, GWLP_HINSTANCE), nullptr);
    SendMessageW(r, WM_SETFONT, (WPARAM)f, TRUE);
    return r;
}

HWND mkEdit(HWND p, const wchar_t* text, int x, int y, int w, int h,
            HFONT f, WORD id, DWORD extra = 0) {
    HWND r = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
        x, y, w, h, p, (HMENU)(INT_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(p, GWLP_HINSTANCE), nullptr);
    SendMessageW(r, WM_SETFONT, (WPARAM)f, TRUE);
    return r;
}

HWND mkBtn(HWND p, const wchar_t* text, int x, int y, int w, int h,
           HFONT f, WORD id, DWORD style = BS_PUSHBUTTON) {
    HWND r = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
        x, y, w, h, p, (HMENU)(INT_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(p, GWLP_HINSTANCE), nullptr);
    SendMessageW(r, WM_SETFONT, (WPARAM)f, TRUE);
    return r;
}

HWND mkChk(HWND p, const wchar_t* text, int x, int y, int w, int h,
           HFONT f, WORD id, bool initial = false) {
    HWND r = mkBtn(p, text, x, y, w, h, f, id, BS_AUTOCHECKBOX);
    SendMessageW(r, BM_SETCHECK, initial ? BST_CHECKED : BST_UNCHECKED, 0);
    return r;
}

HWND mkLV(HWND p, int x, int y, int w, int h, HFONT f, WORD id, DWORD exStyle) {
    HWND r = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        x, y, w, h, p, (HMENU)(INT_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(p, GWLP_HINSTANCE), nullptr);
    ListView_SetExtendedListViewStyle(r,
        exStyle | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(r, L"Explorer", nullptr);
    SendMessageW(r, WM_SETFONT, (WPARAM)f, TRUE);
    return r;
}

void addCol(HWND lv, int idx, int w, const wchar_t* text) {
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.cx = w;
    c.pszText = (LPWSTR)text;
    ListView_InsertColumn(lv, idx, &c);
}

HWND mkListBox(HWND p, int x, int y, int w, int h, HFONT f, WORD id) {
    HWND r = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            LBS_NOTIFY | LBS_HASSTRINGS,
        x, y, w, h, p, (HMENU)(INT_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(p, GWLP_HINSTANCE), nullptr);
    SendMessageW(r, WM_SETFONT, (WPARAM)f, TRUE);
    return r;
}

} // namespace

MainWindow::MainWindow(HINSTANCE hInstance) : hInstance_(hInstance) {}

MainWindow::~MainWindow() {
    if (hFont_)      DeleteObject(hFont_);
    if (hFontBold_)  DeleteObject(hFontBold_);
    if (hFontTitle_) DeleteObject(hFontTitle_);
}

bool MainWindow::create() {
    INITCOMMONCONTROLSEX icc{ sizeof(icc),
        ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES |
        ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);

    hFont_      = makeFont(9);
    hFontBold_  = makeFont(10, true);
    hFontTitle_ = makeFont(14, true);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &MainWindow::wndProcThunk;
    wc.hInstance     = hInstance_;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    WNDCLASSEXW pwc{};
    pwc.cbSize        = sizeof(pwc);
    pwc.lpfnWndProc   = panelProc;
    pwc.hInstance     = hInstance_;
    pwc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    pwc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    pwc.lpszClassName = kPanelClass;
    RegisterClassExW(&pwc);

    hwnd_ = CreateWindowExW(
        0, kClassName, kWindowTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760,
        nullptr, nullptr, hInstance_, this);
    if (!hwnd_) return false;

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

int MainWindow::runMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK MainWindow::wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->wndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:   onCreate(hwnd); return 0;
    case WM_SIZE:     onSize(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_COMMAND:  onCommand(LOWORD(wp), (HWND)lp); return 0;
    case WM_NOTIFY:   onNotify((LPNMHDR)lp); return 0;
    case WM_ISO_INSPECTED: {
        std::unique_ptr<InspectPayload> r((InspectPayload*)lp);
        ListView_DeleteAllItems(hwndEditionsList_);
        for (size_t i = 0; i < r->editions.size(); ++i) {
            const auto& e = r->editions[i];
            wchar_t idx[16]; swprintf_s(idx, L"%d", e.index);
            wchar_t sz[64];
            if (e.sizeBytes >= (1ull << 30))
                swprintf_s(sz, L"%.2f GB", e.sizeBytes / (double)(1ull << 30));
            else
                swprintf_s(sz, L"%.0f MB", e.sizeBytes / (double)(1ull << 20));
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = (int)i;
            it.pszText = idx;
            ListView_InsertItem(hwndEditionsList_, &it);
            ListView_SetItemText(hwndEditionsList_, (int)i, 1, (LPWSTR)e.name.c_str());
            ListView_SetItemText(hwndEditionsList_, (int)i, 2, (LPWSTR)e.architecture.c_str());
            ListView_SetItemText(hwndEditionsList_, (int)i, 3, sz);
            ListView_SetItemText(hwndEditionsList_, (int)i, 4, (LPWSTR)e.description.c_str());
            ListView_SetCheckState(hwndEditionsList_, (int)i, TRUE);
        }
        EnableWindow(hwndLoadIsoBtn_, TRUE);
        if (r->editions.empty()) {
            SetWindowTextW(hwndLoadIsoBtn_, L"Load ISO...");
            SendMessageW(hwndStatus_, SB_SETTEXTW, 0,
                (LPARAM)L"ISO inspection failed. Check that the file is a Windows installation ISO.");
        } else {
            SetWindowTextW(hwndLoadIsoBtn_, L"Change ISO...");
            wchar_t msg[128];
            swprintf_s(msg, L"ISO loaded: %zu edition(s) detected",
                       r->editions.size());
            SendMessageW(hwndStatus_, SB_SETTEXTW, 0, (LPARAM)msg);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_DESTROY:  PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void MainWindow::createHeader(HWND parent) {
    hwndHeader_ = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 0, kHeaderHeight, parent, nullptr, hInstance_, nullptr);

    hwndHeaderTitle_ = mkLabel(parent, L"Windows ISO Creator",
        kPad, 12, 800, 40, hFontTitle_);

    hwndLoadIsoBtn_ = mkBtn(parent, L"Load ISO...",
        0, 18, 140, 30, hFont_, ID_LOAD_ISO);
}

// ----- panels -----

HWND MainWindow::createImagePanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Image", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Pick the Windows installation ISO you want to customize. "
        L"You will choose the destination for the built ISO later, on "
        L"the Apply page.",
        kPad, kPad + 32, 820, 60, hFont_);

    mkLabel(p, L"Source ISO:", kPad, kPad + 122, 100, 22, hFont_);
    hwndEditSourceIso_ = mkEdit(p, L"", kPad + 110, kPad + 120, 500, 24,
        hFont_, ID_EDIT_SRC);
    hwndBtnBrowseSrc_ = mkBtn(p, L"Browse...", kPad + 620, kPad + 120,
        100, 26, hFont_, ID_BROWSE_SRC);

    mkLabel(p,
        L"The source ISO is never modified. All edits are applied to a "
        L"working copy, which becomes the new ISO when you click Build "
        L"ISO on the Apply page.",
        kPad, kPad + 170, 820, 60, hFont_);

    return p;
}

HWND MainWindow::createEditionsPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Editions", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Each row is a Windows edition inside install.wim (Pro, Home, "
        L"Education, etc.). Tick the editions to keep. The list is "
        L"populated after the source ISO is loaded and inspected.",
        kPad, kPad + 32, 820, 50, hFont_);

    hwndEditionsList_ = mkLV(p, kPad, kPad + 92, 820, 380,
        hFont_, 0, LVS_EX_CHECKBOXES);
    addCol(hwndEditionsList_, 0,  60, L"Index");
    addCol(hwndEditionsList_, 1, 320, L"Name");
    addCol(hwndEditionsList_, 2, 110, L"Architecture");
    addCol(hwndEditionsList_, 3, 120, L"Size");
    addCol(hwndEditionsList_, 4, 200, L"Description");

    hwndChkTrim_ = mkChk(p,
        L"Trim unselected editions from the output ISO (recommended)",
        kPad, kPad + 484, 600, 22, hFont_, 0, true);

    return p;
}

HWND MainWindow::createComponentsPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Components", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Provisioned AppX packages and inbox apps. Tick to remove from "
        L"the image. Populated from DISM /Get-ProvisionedAppxPackages "
        L"after the ISO is mounted.",
        kPad, kPad + 32, 820, 50, hFont_);

    hwndComponentsList_ = mkLV(p, kPad, kPad + 92, 820, 480,
        hFont_, 0, LVS_EX_CHECKBOXES);
    addCol(hwndComponentsList_, 0, 360, L"Display Name");
    addCol(hwndComponentsList_, 1, 440, L"Package Name");

    return p;
}

HWND MainWindow::createFeaturesPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Features", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Optional Windows features (DISM /Get-Features). Tick to "
        L"enable; clear to disable. Populated after the ISO is mounted.",
        kPad, kPad + 32, 820, 50, hFont_);

    hwndFeaturesList_ = mkLV(p, kPad, kPad + 92, 820, 480,
        hFont_, 0, LVS_EX_CHECKBOXES);
    addCol(hwndFeaturesList_, 0, 480, L"Feature Name");
    addCol(hwndFeaturesList_, 1, 320, L"State");

    return p;
}

HWND MainWindow::createApplicationsPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Applications", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Third-party apps to silent-install during setup. Selected "
        L"apps are staged into the image and installed by SetupComplete.cmd "
        L"before the first user logon.",
        kPad, kPad + 32, 820, 50, hFont_);

    hwndAppsList_ = mkLV(p, kPad, kPad + 92, 820, 360,
        hFont_, ID_APPS_LIST, LVS_EX_CHECKBOXES);
    addCol(hwndAppsList_, 0, 220, L"Application");
    addCol(hwndAppsList_, 1, 180, L"Vendor");
    addCol(hwndAppsList_, 2, 220, L"ID");
    addCol(hwndAppsList_, 3, 200, L"Silent Args");

    hwndAppsDesc_ = mkEdit(p,
        L"Select an app to see its installer details. Antigravity, "
        L"Windsurf, Cursor, and Claude do not yet have public download "
        L"URLs in the catalog and will need a local installer path.",
        kPad, kPad + 462, 820, 100,
        hFont_, 0, ES_MULTILINE | ES_READONLY | WS_VSCROLL);

    populateApps();
    return p;
}

void MainWindow::populateApps() {
    const auto& cat = wid::core::builtinAppCatalog();
    for (size_t i = 0; i < cat.size(); ++i) {
        const auto& a = cat[i];
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = (int)i;
        it.lParam = (LPARAM)i;
        it.pszText = (LPWSTR)a.displayName.c_str();
        ListView_InsertItem(hwndAppsList_, &it);
        ListView_SetItemText(hwndAppsList_, (int)i, 1, (LPWSTR)a.vendor.c_str());
        ListView_SetItemText(hwndAppsList_, (int)i, 2, (LPWSTR)a.id.c_str());
        ListView_SetItemText(hwndAppsList_, (int)i, 3, (LPWSTR)a.silentArgs.c_str());
    }
}

HWND MainWindow::createTweaksPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Tweaks", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Toggle individual tweaks. Each enabled tweak becomes a "
        L"pending change applied to the ISO during the build.",
        kPad, kPad + 32, 820, 40, hFont_);

    hwndTweaksList_ = mkLV(p, kPad, kPad + 76, 820, 380,
        hFont_, ID_TWEAKS_LIST, LVS_EX_CHECKBOXES);
    addCol(hwndTweaksList_, 0, 380, L"Tweak");
    addCol(hwndTweaksList_, 1, 110, L"Category");
    addCol(hwndTweaksList_, 2, 80,  L"Risk");
    addCol(hwndTweaksList_, 3, 220, L"Id");

    hwndTweaksDesc_ = mkEdit(p,
        L"Select a tweak to see its description.",
        kPad, kPad + 470, 820, 110,
        hFont_, 0, ES_MULTILINE | ES_READONLY | WS_VSCROLL);

    populateTweaks();
    return p;
}

void MainWindow::populateTweaks() {
    const auto& cat = wid::core::tweakCatalog();
    for (size_t i = 0; i < cat.size(); ++i) {
        const auto& t = cat[i];
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = (int)i;
        it.lParam = (LPARAM)i;
        it.pszText = (LPWSTR)t.displayName.c_str();
        ListView_InsertItem(hwndTweaksList_, &it);
        ListView_SetItemText(hwndTweaksList_, (int)i, 1, (LPWSTR)t.category.c_str());
        ListView_SetItemText(hwndTweaksList_, (int)i, 2,
            (LPWSTR)(t.dangerous ? L"Risky" : L"Safe"));
        ListView_SetItemText(hwndTweaksList_, (int)i, 3, (LPWSTR)t.id.c_str());
    }
}

HWND MainWindow::createUnattendedPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Unattended", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"OOBE answers for autounattend.xml. Leave fields blank to let "
        L"setup ask the user.",
        kPad, kPad + 32, 820, 40, hFont_);

    int y = kPad + 80;
    int lblW = 220, edW = 320, h = 24, gap = 32;

    mkLabel(p, L"Locale:", kPad, y, lblW, h, hFont_);
    hwndEditLocale_ = mkEdit(p, L"en-US", kPad + lblW, y, edW, h, hFont_, 0);
    y += gap;

    mkLabel(p, L"Time zone:", kPad, y, lblW, h, hFont_);
    hwndEditTimezone_ = mkEdit(p, L"UTC", kPad + lblW, y, edW, h, hFont_, 0);
    y += gap;

    mkLabel(p, L"Computer name:", kPad, y, lblW, h, hFont_);
    hwndEditComputerName_ = mkEdit(p, L"", kPad + lblW, y, edW, h, hFont_, 0);
    y += gap;

    mkLabel(p, L"Administrator password:", kPad, y, lblW, h, hFont_);
    hwndEditAdminPassword_ = mkEdit(p, L"", kPad + lblW, y, edW, h,
        hFont_, 0, ES_PASSWORD);
    y += gap + 8;

    hwndChkSkipMsAccount_ = mkChk(p,
        L"Skip the Microsoft account / online account screens",
        kPad + lblW, y, 480, 22, hFont_, 0, true);
    y += gap;

    hwndChkAcceptEula_ = mkChk(p,
        L"Auto-accept the EULA (hide EULA page)",
        kPad + lblW, y, 480, 22, hFont_, 0, true);
    y += gap;

    hwndChkAutoLogon_ = mkChk(p,
        L"Auto-logon on first boot",
        kPad + lblW, y, 480, 22, hFont_, 0, false);
    y += gap;

    mkLabel(p, L"Auto-logon user:", kPad, y, lblW, h, hFont_);
    hwndEditAutoLogonUser_ = mkEdit(p, L"", kPad + lblW, y, edW, h, hFont_, 0);
    y += gap;

    mkLabel(p, L"Auto-logon password:", kPad, y, lblW, h, hFont_);
    hwndEditAutoLogonPwd_ = mkEdit(p, L"", kPad + lblW, y, edW, h,
        hFont_, 0, ES_PASSWORD);

    return p;
}

HWND MainWindow::createCommandsPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Commands", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Run arbitrary command lines during setup. Pre-logon commands "
        L"run as SYSTEM before the first user logon (SetupComplete.cmd). "
        L"Post-logon commands run after the first user signs in "
        L"(FirstLogonCommands).",
        kPad, kPad + 32, 820, 50, hFont_);

    mkLabel(p, L"Command line:", kPad, kPad + 92, 120, 22, hFont_);
    hwndCmdEdit_ = mkEdit(p, L"", kPad + 130, kPad + 90, 690, 24, hFont_, 0);

    hwndBtnAddPre_  = mkBtn(p, L"Add to Pre-logon",  kPad + 130,
        kPad + 122, 180, 28, hFont_, ID_ADD_PRE);
    hwndBtnAddPost_ = mkBtn(p, L"Add to Post-logon", kPad + 320,
        kPad + 122, 180, 28, hFont_, ID_ADD_POST);

    mkLabel(p, L"Pre-logon (SYSTEM, before first logon):",
        kPad, kPad + 168, 600, 22, hFontBold_);
    hwndPreList_  = mkListBox(p, kPad, kPad + 192, 820, 130, hFont_, 0);
    hwndBtnDelPre_ = mkBtn(p, L"Remove selected", kPad,
        kPad + 328, 160, 26, hFont_, ID_DEL_PRE);

    mkLabel(p, L"Post-logon (after first user logon):",
        kPad, kPad + 366, 600, 22, hFontBold_);
    hwndPostList_  = mkListBox(p, kPad, kPad + 390, 820, 130, hFont_, 0);
    hwndBtnDelPost_ = mkBtn(p, L"Remove selected", kPad,
        kPad + 526, 160, 26, hFont_, ID_DEL_POST);

    return p;
}

HWND MainWindow::createDriversPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Drivers", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Inject .inf drivers into the offline image (DISM /Add-Driver "
        L"/Recurse).",
        kPad, kPad + 32, 820, 24, hFont_);

    hwndDriversList_ = mkLV(p, kPad, kPad + 70, 690, 480, hFont_, 0, 0);
    addCol(hwndDriversList_, 0, 660, L"Driver .inf path");

    hwndBtnAddDriver_ = mkBtn(p, L"Add...",  kPad + 720, kPad + 70,
        120, 30, hFont_, ID_ADD_DRIVER);
    hwndBtnDelDriver_ = mkBtn(p, L"Remove", kPad + 720, kPad + 110,
        120, 30, hFont_, ID_DEL_DRIVER);

    return p;
}

HWND MainWindow::createUpdatesPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Updates", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Slipstream .msu and .cab updates into the offline image "
        L"(DISM /Add-Package).",
        kPad, kPad + 32, 820, 24, hFont_);

    hwndUpdatesList_ = mkLV(p, kPad, kPad + 70, 690, 480, hFont_, 0, 0);
    addCol(hwndUpdatesList_, 0, 660, L"Update .msu / .cab path");

    hwndBtnAddUpdate_ = mkBtn(p, L"Add...",  kPad + 720, kPad + 70,
        120, 30, hFont_, ID_ADD_UPDATE);
    hwndBtnDelUpdate_ = mkBtn(p, L"Remove", kPad + 720, kPad + 110,
        120, 30, hFont_, ID_DEL_UPDATE);

    return p;
}

HWND MainWindow::createPendingPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Pending Changes", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Every edit you make is queued here. Review or remove items "
        L"before clicking Build ISO on the Apply page.",
        kPad, kPad + 32, 820, 40, hFont_);

    hwndPendingList_ = mkLV(p, kPad, kPad + 80, 820, 440, hFont_, 0, 0);
    addCol(hwndPendingList_, 0, 360, L"Description");
    addCol(hwndPendingList_, 1, 110, L"Kind");
    addCol(hwndPendingList_, 2, 90,  L"Action");
    addCol(hwndPendingList_, 3, 220, L"Target ID");

    hwndBtnPendingDel_   = mkBtn(p, L"Remove selected", kPad,
        kPad + 530, 160, 28, hFont_, ID_PENDING_DEL);
    hwndBtnPendingClear_ = mkBtn(p, L"Clear all", kPad + 170,
        kPad + 530, 120, 28, hFont_, ID_PENDING_CLEAR);

    return p;
}

HWND MainWindow::createApplyPanel(HWND parent) {
    HWND p = CreateWindowExW(0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    mkLabel(p, L"Apply", kPad, kPad, 400, 24, hFontBold_);
    mkLabel(p,
        L"Build the customized ISO. You will be asked where to save it.",
        kPad, kPad + 32, 820, 24, hFont_);

    hwndApplySummary_ = mkEdit(p, L"",
        kPad, kPad + 70, 820, 220, hFont_, 0,
        ES_MULTILINE | ES_READONLY | WS_VSCROLL);

    hwndBtnBuildIso_ = mkBtn(p, L"Build ISO...", kPad, kPad + 310,
        180, 36, hFontBold_, ID_BUILD_ISO, BS_DEFPUSHBUTTON);

    return p;
}

void MainWindow::onCreate(HWND hwnd) {
    createHeader(hwnd);

    hwndNav_ = CreateWindowExW(0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            TVS_HASBUTTONS | TVS_SHOWSELALWAYS |
            TVS_TRACKSELECT | TVS_FULLROWSELECT | TVS_NOHSCROLL,
        0, 0, kNavWidth, 100,
        hwnd, (HMENU)(INT_PTR)ID_NAV, hInstance_, nullptr);
    SetWindowTheme(hwndNav_, L"Explorer", nullptr);
    SendMessageW(hwndNav_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hwndDetail_ = CreateWindowExW(0, kPanelClass, L"",
        WS_CHILD | WS_VISIBLE,
        kNavWidth, 0, 100, 100,
        hwnd, (HMENU)(INT_PTR)ID_DETAIL, hInstance_, nullptr);

    hwndStatus_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"Ready",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_STATUS, hInstance_, nullptr);
    SendMessageW(hwndStatus_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hwndPanelImage_      = createImagePanel       (hwndDetail_);
    hwndPanelEditions_   = createEditionsPanel    (hwndDetail_);
    hwndPanelComponents_ = createComponentsPanel  (hwndDetail_);
    hwndPanelFeatures_   = createFeaturesPanel    (hwndDetail_);
    hwndPanelApps_       = createApplicationsPanel(hwndDetail_);
    hwndPanelTweaks_     = createTweaksPanel      (hwndDetail_);
    hwndPanelUnattended_ = createUnattendedPanel  (hwndDetail_);
    hwndPanelCommands_   = createCommandsPanel    (hwndDetail_);
    hwndPanelDrivers_    = createDriversPanel     (hwndDetail_);
    hwndPanelUpdates_    = createUpdatesPanel     (hwndDetail_);
    hwndPanelPending_    = createPendingPanel     (hwndDetail_);
    hwndPanelApply_      = createApplyPanel       (hwndDetail_);

    buildNavTree();
    layoutChildren();
    showSection(Section::Image);
}

void MainWindow::buildNavTree() {
    for (const auto& e : navEntries()) {
        TVINSERTSTRUCTW tvi{};
        tvi.hParent = TVI_ROOT;
        tvi.hInsertAfter = TVI_LAST;
        tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
        tvi.item.pszText = (LPWSTR)e.label;
        tvi.item.lParam  = (LPARAM)e.section;
        TreeView_InsertItem(hwndNav_, &tvi);
    }
}

void MainWindow::onSize(int /*w*/, int /*h*/) {
    if (hwndStatus_) SendMessageW(hwndStatus_, WM_SIZE, 0, 0);
    layoutChildren();
}

void MainWindow::layoutChildren() {
    if (!hwnd_) return;
    RECT rc; GetClientRect(hwnd_, &rc);

    int statusH = 0;
    if (hwndStatus_) {
        RECT sr; GetWindowRect(hwndStatus_, &sr);
        statusH = sr.bottom - sr.top;
    }

    if (hwndHeader_) MoveWindow(hwndHeader_, 0, 0, rc.right, kHeaderHeight, TRUE);
    if (hwndLoadIsoBtn_) {
        int x = rc.right - 140 - kPad;
        MoveWindow(hwndLoadIsoBtn_, x, 18, 140, 30, TRUE);
    }

    int contentTop = kHeaderHeight;
    int contentH   = rc.bottom - contentTop - statusH;

    if (hwndNav_)
        MoveWindow(hwndNav_, 0, contentTop, kNavWidth, contentH, TRUE);
    if (hwndDetail_)
        MoveWindow(hwndDetail_, kNavWidth, contentTop,
                   rc.right - kNavWidth, contentH, TRUE);

    RECT dr; GetClientRect(hwndDetail_, &dr);
    auto fit = [&](HWND h) { if (h) MoveWindow(h, 0, 0, dr.right, dr.bottom, TRUE); };
    fit(hwndPanelImage_);
    fit(hwndPanelEditions_);
    fit(hwndPanelComponents_);
    fit(hwndPanelFeatures_);
    fit(hwndPanelApps_);
    fit(hwndPanelTweaks_);
    fit(hwndPanelUnattended_);
    fit(hwndPanelCommands_);
    fit(hwndPanelDrivers_);
    fit(hwndPanelUpdates_);
    fit(hwndPanelPending_);
    fit(hwndPanelApply_);
}

void MainWindow::onNotify(LPNMHDR n) {
    if (!n) return;
    if (n->hwndFrom == hwndNav_ && n->code == TVN_SELCHANGED) {
        auto* tv = (LPNMTREEVIEWW)n;
        showSection((Section)tv->itemNew.lParam);
    } else if (n->hwndFrom == hwndTweaksList_ && n->code == LVN_ITEMCHANGED) {
        auto* lv = (LPNMLISTVIEW)n;
        if (lv->iItem >= 0 && (lv->uChanged & LVIF_STATE)) {
            const auto& cat = wid::core::tweakCatalog();
            if ((size_t)lv->iItem < cat.size())
                SetWindowTextW(hwndTweaksDesc_, cat[lv->iItem].description.c_str());
        }
    } else if (n->hwndFrom == hwndAppsList_ && n->code == LVN_ITEMCHANGED) {
        auto* lv = (LPNMLISTVIEW)n;
        if (lv->iItem >= 0 && (lv->uChanged & LVIF_STATE)) {
            const auto& cat = wid::core::builtinAppCatalog();
            if ((size_t)lv->iItem < cat.size()) {
                const auto& a = cat[lv->iItem];
                std::wstring s;
                s += L"Vendor: " + a.vendor + L"\r\n";
                s += L"ID: " + a.id + L"\r\n";
                s += L"Silent install switches: " + a.silentArgs + L"\r\n";
                s += L"Download URL: " + (a.downloadUrl.empty()
                    ? std::wstring(L"(none in catalog; supply local installer)")
                    : a.downloadUrl);
                SetWindowTextW(hwndAppsDesc_, s.c_str());
            }
        }
    }
}

void MainWindow::onCommand(WORD id, HWND /*ctrl*/) {
    switch (id) {
    case ID_LOAD_ISO:
    case ID_BROWSE_SRC:    onBrowseSourceIso(); break;
    case ID_BUILD_ISO:     onBuildIso();        break;
    case ID_ADD_DRIVER:    onAddDriver();       break;
    case ID_DEL_DRIVER:    onRemoveDriver();    break;
    case ID_ADD_UPDATE:    onAddUpdate();       break;
    case ID_DEL_UPDATE:    onRemoveUpdate();    break;
    case ID_ADD_PRE:       onAddCommand(false); break;
    case ID_ADD_POST:      onAddCommand(true);  break;
    case ID_DEL_PRE:       onRemoveCommand(false); break;
    case ID_DEL_POST:      onRemoveCommand(true);  break;
    case ID_PENDING_DEL:   onPendingRemove();   break;
    case ID_PENDING_CLEAR: onPendingClear();    break;
    }
}

bool MainWindow::pickFile(bool save, const wchar_t* title,
                          const wchar_t* filterExt, const wchar_t* defExt,
                          std::wstring& out) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);

    IFileDialog* dlg = nullptr;
    hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
                          nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    bool ok = false;
    if (SUCCEEDED(hr) && dlg) {
        if (filterExt && *filterExt) {
            COMDLG_FILTERSPEC f[] = {
                { L"Filtered files", filterExt },
                { L"All files",      L"*.*"    } };
            dlg->SetFileTypes(2, f);
            dlg->SetFileTypeIndex(1);
        }
        if (defExt && *defExt) dlg->SetDefaultExtension(defExt);
        dlg->SetTitle(title);
        if (SUCCEEDED(dlg->Show(hwnd_))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                LPWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))
                    && path) {
                    out = path; CoTaskMemFree(path); ok = true;
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (needUninit) CoUninitialize();
    return ok;
}

void MainWindow::onBrowseSourceIso() {
    std::wstring path;
    if (!pickFile(false, L"Select source Windows ISO", L"*.iso", L"iso", path))
        return;

    SetWindowTextW(hwndEditSourceIso_, path.c_str());
    SetWindowTextW(hwndLoadIsoBtn_, L"Inspecting...");
    EnableWindow(hwndLoadIsoBtn_, FALSE);
    SendMessageW(hwndStatus_, SB_SETTEXTW, 0,
        (LPARAM)L"Mounting ISO and inspecting install.wim ...");

    HWND target = hwnd_;
    std::thread([target, path]() {
        auto* payload = new InspectPayload{};
        payload->isoPath  = path;
        payload->editions = wid::core::inspectIso(path, nullptr);
        if (!PostMessageW(target, WM_ISO_INSPECTED, 0, (LPARAM)payload))
            delete payload;
    }).detach();
}

void MainWindow::onBuildIso() {
    wchar_t src[1024]{};
    GetWindowTextW(hwndEditSourceIso_, src, (int)std::size(src));
    if (!src[0]) {
        MessageBoxW(hwnd_,
            L"Pick a source ISO on the Image page first.",
            L"WID Utility", MB_ICONINFORMATION);
        return;
    }
    std::wstring out;
    if (!pickFile(true, L"Save built ISO as", L"*.iso", L"iso", out)) return;

    std::wstring s;
    s  = L"Source ISO: "; s += src; s += L"\r\n";
    s += L"Output ISO: "; s += out; s += L"\r\n\r\n";
    s += L"Pipeline integration is not yet wired up. The 11-stage build "
         L"(extract, mount, edits, DISM, scripts, commit, trim, oscdimg, "
         L"verify) will run here once the apply path is connected.";
    SetWindowTextW(hwndApplySummary_, s.c_str());
    SendMessageW(hwndStatus_, SB_SETTEXTW, 0,
                 (LPARAM)L"Build requested (pipeline not yet wired)");
}

void MainWindow::onAddDriver() {
    std::wstring path;
    if (!pickFile(false, L"Select driver .inf", L"*.inf", L"inf", path)) return;
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = ListView_GetItemCount(hwndDriversList_);
    it.pszText = (LPWSTR)path.c_str();
    ListView_InsertItem(hwndDriversList_, &it);
}

void MainWindow::onRemoveDriver() {
    int sel = ListView_GetNextItem(hwndDriversList_, -1, LVNI_SELECTED);
    if (sel >= 0) ListView_DeleteItem(hwndDriversList_, sel);
}

void MainWindow::onAddUpdate() {
    std::wstring path;
    if (!pickFile(false, L"Select update .msu or .cab",
                  L"*.msu;*.cab", L"msu", path)) return;
    LVITEMW it{};
    it.mask = LVIF_TEXT;
    it.iItem = ListView_GetItemCount(hwndUpdatesList_);
    it.pszText = (LPWSTR)path.c_str();
    ListView_InsertItem(hwndUpdatesList_, &it);
}

void MainWindow::onRemoveUpdate() {
    int sel = ListView_GetNextItem(hwndUpdatesList_, -1, LVNI_SELECTED);
    if (sel >= 0) ListView_DeleteItem(hwndUpdatesList_, sel);
}

void MainWindow::onAddCommand(bool postLogon) {
    wchar_t buf[2048]{};
    GetWindowTextW(hwndCmdEdit_, buf, (int)std::size(buf));
    if (!buf[0]) {
        MessageBoxW(hwnd_,
            L"Type a command line first.",
            L"WID Utility", MB_ICONINFORMATION);
        return;
    }
    HWND list = postLogon ? hwndPostList_ : hwndPreList_;
    SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)buf);
    SetWindowTextW(hwndCmdEdit_, L"");
}

void MainWindow::onRemoveCommand(bool postLogon) {
    HWND list = postLogon ? hwndPostList_ : hwndPreList_;
    int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR)
        SendMessageW(list, LB_DELETESTRING, sel, 0);
}

void MainWindow::onPendingRemove() {
    int sel = ListView_GetNextItem(hwndPendingList_, -1, LVNI_SELECTED);
    if (sel >= 0) ListView_DeleteItem(hwndPendingList_, sel);
}

void MainWindow::onPendingClear() {
    ListView_DeleteAllItems(hwndPendingList_);
}

void MainWindow::showSection(Section s) {
    current_ = s;

    HWND panels[] = {
        hwndPanelImage_, hwndPanelEditions_, hwndPanelComponents_,
        hwndPanelFeatures_, hwndPanelApps_, hwndPanelTweaks_,
        hwndPanelUnattended_, hwndPanelCommands_, hwndPanelDrivers_,
        hwndPanelUpdates_, hwndPanelPending_, hwndPanelApply_,
    };
    for (HWND h : panels) if (h) ShowWindow(h, SW_HIDE);

    HWND target = nullptr;
    switch (s) {
    case Section::Image:          target = hwndPanelImage_;      break;
    case Section::Editions:       target = hwndPanelEditions_;   break;
    case Section::Components:     target = hwndPanelComponents_; break;
    case Section::Features:       target = hwndPanelFeatures_;   break;
    case Section::Applications:   target = hwndPanelApps_;       break;
    case Section::Tweaks:         target = hwndPanelTweaks_;     break;
    case Section::Unattended:     target = hwndPanelUnattended_; break;
    case Section::Commands:       target = hwndPanelCommands_;   break;
    case Section::Drivers:        target = hwndPanelDrivers_;    break;
    case Section::Updates:        target = hwndPanelUpdates_;    break;
    case Section::PendingChanges: target = hwndPanelPending_;    break;
    case Section::Apply:          target = hwndPanelApply_;      break;
    }
    if (target) ShowWindow(target, SW_SHOW);

    if (hwndStatus_)
        SendMessageW(hwndStatus_, SB_SETTEXTW, 0, (LPARAM)sectionTitle(s));
}

} // namespace wid::gui
