#include "gui/MainWindow.h"

#include "core/Tweaks.h"

#include <windowsx.h>
#include <uxtheme.h>
#include <shobjidl.h>
#include <vector>

namespace wid::gui {

namespace {

constexpr wchar_t kClassName[]   = L"WIDUtility.MainWindow";
constexpr wchar_t kPanelClass[]  = L"WIDUtility.Panel";
constexpr wchar_t kWindowTitle[] = L"WID Utility";

constexpr int kHeaderHeight = 64;
constexpr int kNavWidth     = 220;
constexpr int kPad          = 16;

// Control IDs
constexpr WORD ID_NAV          = 1001;
constexpr WORD ID_DETAIL       = 1002;
constexpr WORD ID_STATUS       = 1003;
constexpr WORD ID_LOAD_ISO     = 2001;
constexpr WORD ID_BROWSE_SRC   = 2010;
constexpr WORD ID_EDIT_SRC     = 2012;
constexpr WORD ID_TWEAKS_LIST  = 2020;
constexpr WORD ID_BUILD_ISO    = 2030;

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
    lf.lfHeight = -MulDiv(pt, dpi, 72);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

void setFontRecursive(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        setFontRecursive(child, font);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

// Panel class proc: forwards WM_COMMAND / WM_NOTIFY to the parent
// (the main window), and paints a window-colored background so child
// controls blend with it.
LRESULT CALLBACK panelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageW(GetParent(h), m, w, l);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)w;
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }
    }
    return DefWindowProcW(h, m, w, l);
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
    hwndHeader_ = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        0, 0, 0, kHeaderHeight,
        parent, nullptr, hInstance_, nullptr);

    hwndHeaderTitle_ = CreateWindowExW(
        0, L"STATIC", L"Windows ISO Creator",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
        kPad, 12, 800, 40,
        parent, nullptr, hInstance_, nullptr);
    SendMessageW(hwndHeaderTitle_, WM_SETFONT, (WPARAM)hFontTitle_, TRUE);

    hwndLoadIsoBtn_ = CreateWindowExW(
        0, L"BUTTON", L"Load ISO...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 14, 120, 28,
        parent, (HMENU)(INT_PTR)ID_LOAD_ISO, hInstance_, nullptr);
    SendMessageW(hwndLoadIsoBtn_, WM_SETFONT, (WPARAM)hFont_, TRUE);
}

HWND MainWindow::createImagePanel(HWND parent) {
    HWND panel = CreateWindowExW(
        0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    HWND lblTitle = CreateWindowExW(0, L"STATIC", L"Image",
        WS_CHILD | WS_VISIBLE, kPad, kPad, 400, 24,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblTitle, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    HWND lblHelp = CreateWindowExW(0, L"STATIC",
        L"Pick the Windows installation ISO you want to customize. "
        L"You will choose the destination for the built ISO later, on the Apply page.",
        WS_CHILD | WS_VISIBLE,
        kPad, kPad + 32, 820, 64,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblHelp, WM_SETFONT, (WPARAM)hFont_, TRUE);

    HWND lblSrc = CreateWindowExW(0, L"STATIC", L"Source ISO:",
        WS_CHILD | WS_VISIBLE,
        kPad, kPad + 122, 100, 22,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblSrc, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hwndEditSourceIso_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        kPad + 110, kPad + 120, 500, 24,
        panel, (HMENU)(INT_PTR)ID_EDIT_SRC, hInstance_, nullptr);
    SendMessageW(hwndEditSourceIso_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hwndBtnBrowseSrc_ = CreateWindowExW(
        0, L"BUTTON", L"Browse...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kPad + 620, kPad + 120, 100, 26,
        panel, (HMENU)(INT_PTR)ID_BROWSE_SRC, hInstance_, nullptr);
    SendMessageW(hwndBtnBrowseSrc_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    HWND lblNote = CreateWindowExW(0, L"STATIC",
        L"The source ISO is never modified. All edits are applied to a "
        L"working copy, which becomes the new ISO when you click "
        L"Build ISO on the Apply page.",
        WS_CHILD | WS_VISIBLE,
        kPad, kPad + 170, 820, 80,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblNote, WM_SETFONT, (WPARAM)hFont_, TRUE);

    return panel;
}

HWND MainWindow::createApplyPanel(HWND parent) {
    HWND panel = CreateWindowExW(
        0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    HWND lblTitle = CreateWindowExW(0, L"STATIC", L"Apply",
        WS_CHILD | WS_VISIBLE, kPad, kPad, 400, 24,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblTitle, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    HWND lblHelp = CreateWindowExW(0, L"STATIC",
        L"Build the customized ISO. You will be asked where to save it.",
        WS_CHILD | WS_VISIBLE,
        kPad, kPad + 32, 820, 24,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblHelp, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hwndApplySummary_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
        kPad, kPad + 70, 820, 220,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(hwndApplySummary_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hwndBtnBuildIso_ = CreateWindowExW(
        0, L"BUTTON", L"Build ISO...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        kPad, kPad + 310, 180, 36,
        panel, (HMENU)(INT_PTR)ID_BUILD_ISO, hInstance_, nullptr);
    SendMessageW(hwndBtnBuildIso_, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    return panel;
}

HWND MainWindow::createTweaksPanel(HWND parent) {
    HWND panel = CreateWindowExW(
        0, kPanelClass, L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    HWND lblTitle = CreateWindowExW(0, L"STATIC", L"Tweaks",
        WS_CHILD | WS_VISIBLE, kPad, kPad, 400, 24,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblTitle, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    HWND lblHelp = CreateWindowExW(0, L"STATIC",
        L"Toggle individual tweaks. Each enabled tweak becomes a "
        L"pending change applied to the ISO during the build.",
        WS_CHILD | WS_VISIBLE,
        kPad, kPad + 28, 700, 36,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblHelp, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hwndTweaksList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kPad, kPad + 76, 800, 380,
        panel, (HMENU)(INT_PTR)ID_TWEAKS_LIST, hInstance_, nullptr);
    ListView_SetExtendedListViewStyle(hwndTweaksList_,
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(hwndTweaksList_, L"Explorer", nullptr);
    SendMessageW(hwndTweaksList_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 380; col.pszText = (LPWSTR)L"Tweak";
    ListView_InsertColumn(hwndTweaksList_, 0, &col);
    col.cx = 110; col.pszText = (LPWSTR)L"Category";
    ListView_InsertColumn(hwndTweaksList_, 1, &col);
    col.cx = 80;  col.pszText = (LPWSTR)L"Risk";
    ListView_InsertColumn(hwndTweaksList_, 2, &col);
    col.cx = 220; col.pszText = (LPWSTR)L"Id";
    ListView_InsertColumn(hwndTweaksList_, 3, &col);

    hwndTweaksDesc_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"Select a tweak to see its description.",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
        kPad, kPad + 470, 800, 110,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(hwndTweaksDesc_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    populateTweaks();
    return panel;
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

HWND MainWindow::createPlaceholderPanel(HWND parent, const wchar_t* title, const wchar_t* body) {
    HWND panel = CreateWindowExW(0, kPanelClass, L"",
        WS_CHILD, 0, 0, 0, 0, parent, nullptr, hInstance_, nullptr);

    HWND lblTitle = CreateWindowExW(0, L"STATIC", title,
        WS_CHILD | WS_VISIBLE, kPad, kPad, 600, 24,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblTitle, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    HWND lblBody = CreateWindowExW(0, L"STATIC", body,
        WS_CHILD | WS_VISIBLE,
        kPad, kPad + 32, 800, 200,
        panel, nullptr, hInstance_, nullptr);
    SendMessageW(lblBody, WM_SETFONT, (WPARAM)hFont_, TRUE);

    return panel;
}

void MainWindow::onCreate(HWND hwnd) {
    createHeader(hwnd);

    hwndNav_ = CreateWindowExW(
        0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            TVS_HASBUTTONS | TVS_SHOWSELALWAYS |
            TVS_TRACKSELECT | TVS_FULLROWSELECT | TVS_NOHSCROLL,
        0, 0, kNavWidth, 100,
        hwnd, (HMENU)(INT_PTR)ID_NAV, hInstance_, nullptr);
    SetWindowTheme(hwndNav_, L"Explorer", nullptr);
    SendMessageW(hwndNav_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hwndDetail_ = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_WHITERECT,
        kNavWidth, 0, 100, 100,
        hwnd, (HMENU)(INT_PTR)ID_DETAIL, hInstance_, nullptr);

    hwndStatus_ = CreateWindowExW(
        0, STATUSCLASSNAMEW, L"Ready",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_STATUS, hInstance_, nullptr);
    SendMessageW(hwndStatus_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // Build all panels up front; show the active one on selection.
    hwndPanelImage_  = createImagePanel(hwndDetail_);
    hwndPanelTweaks_ = createTweaksPanel(hwndDetail_);
    hwndPanelApply_  = createApplyPanel(hwndDetail_);

    auto mkPlaceholder = [&](Section s, const wchar_t* title, const wchar_t* body) {
        placeholders_[(int)s] = createPlaceholderPanel(hwndDetail_, title, body);
    };
    mkPlaceholder(Section::Editions, L"Editions",
        L"After loading an ISO, this panel will list every Windows edition "
        L"in install.wim. Pick one or many; unselected editions can be "
        L"trimmed from the output ISO.");
    mkPlaceholder(Section::Components, L"Components",
        L"Provisioned AppX packages and inbox components removable from "
        L"the image. Populated from DISM /Get-ProvisionedAppxPackages.");
    mkPlaceholder(Section::Features, L"Features",
        L"Optional Windows features. Populated from DISM /Get-Features.");
    mkPlaceholder(Section::Applications, L"Applications",
        L"Third-party app catalog: Antigravity, Brave, Edge, VS Code, "
        L"Windsurf, Cursor, Claude. Selected apps are silent-installed by "
        L"SetupComplete.cmd before first user logon.");
    mkPlaceholder(Section::Unattended, L"Unattended",
        L"OOBE answers: locale, timezone, computer name, admin password, "
        L"skip Microsoft account, accept EULA, auto-logon.");
    mkPlaceholder(Section::Commands, L"Commands",
        L"Pre-logon and post-logon command lists. Unlimited entries each. "
        L"Pre-logon goes into SetupComplete.cmd (SYSTEM context). "
        L"Post-logon goes into FirstLogonCommands.");
    mkPlaceholder(Section::Drivers, L"Drivers",
        L"Inject .inf drivers into the offline image via DISM /Add-Driver.");
    mkPlaceholder(Section::Updates, L"Updates",
        L"Slipstream .msu / .cab updates via DISM /Add-Package.");
    mkPlaceholder(Section::PendingChanges, L"Pending Changes",
        L"Every edit you make is queued here. Review, reorder, or remove "
        L"items before clicking Apply. The queue can be saved and re-loaded "
        L"as a build profile.");
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

    // Header strip across the top
    if (hwndHeader_)
        MoveWindow(hwndHeader_, 0, 0, rc.right, kHeaderHeight, TRUE);
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

    // Resize each panel to fill the detail container
    RECT dr; GetClientRect(hwndDetail_, &dr);
    auto fit = [&](HWND h) { if (h) MoveWindow(h, 0, 0, dr.right, dr.bottom, TRUE); };
    fit(hwndPanelImage_);
    fit(hwndPanelTweaks_);
    fit(hwndPanelApply_);
    for (auto& kv : placeholders_) fit(kv.second);
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
            if ((size_t)lv->iItem < cat.size()) {
                SetWindowTextW(hwndTweaksDesc_, cat[lv->iItem].description.c_str());
            }
        }
    }
}

void MainWindow::onCommand(WORD id, HWND /*ctrl*/) {
    switch (id) {
    case ID_LOAD_ISO:
    case ID_BROWSE_SRC: onBrowseSourceIso(); break;
    case ID_BUILD_ISO:  onBuildIso();        break;
    }
}

bool MainWindow::pickFile(bool save, const wchar_t* title, std::wstring& out) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);

    IFileDialog* dlg = nullptr;
    hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
                          nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&dlg));
    bool ok = false;
    if (SUCCEEDED(hr) && dlg) {
        COMDLG_FILTERSPEC f[] = { { L"ISO image", L"*.iso" },
                                  { L"All files", L"*.*"   } };
        dlg->SetFileTypes(2, f);
        dlg->SetFileTypeIndex(1);
        dlg->SetDefaultExtension(L"iso");
        dlg->SetTitle(title);
        if (SUCCEEDED(dlg->Show(hwnd_))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                LPWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    out = path;
                    CoTaskMemFree(path);
                    ok = true;
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
    if (pickFile(false, L"Select source Windows ISO", path)) {
        SetWindowTextW(hwndEditSourceIso_, path.c_str());
        showSection(Section::Image);
        SendMessageW(hwndStatus_, SB_SETTEXTW, 0, (LPARAM)L"Source ISO selected");
    }
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
    if (!pickFile(true, L"Save built ISO as", out)) return;

    std::wstring summary;
    summary  = L"Source ISO: "; summary += src; summary += L"\r\n";
    summary += L"Output ISO: "; summary += out; summary += L"\r\n\r\n";
    summary += L"Pipeline integration is not yet wired up. The 11-stage "
               L"build (extract, mount, edits, DISM, scripts, commit, "
               L"trim, oscdimg, verify) will run here once the apply path "
               L"is connected.";
    SetWindowTextW(hwndApplySummary_, summary.c_str());
    SendMessageW(hwndStatus_, SB_SETTEXTW, 0,
                 (LPARAM)L"Build requested (pipeline not yet wired)");
}

void MainWindow::showSection(Section s) {
    current_ = s;

    auto hide = [](HWND h) { if (h) ShowWindow(h, SW_HIDE); };
    hide(hwndPanelImage_);
    hide(hwndPanelTweaks_);
    hide(hwndPanelApply_);
    for (auto& kv : placeholders_) hide(kv.second);

    HWND target = nullptr;
    switch (s) {
    case Section::Image:  target = hwndPanelImage_;  break;
    case Section::Tweaks: target = hwndPanelTweaks_; break;
    case Section::Apply:  target = hwndPanelApply_;  break;
    default: {
        auto it = placeholders_.find((int)s);
        if (it != placeholders_.end()) target = it->second;
    } break;
    }
    if (target) ShowWindow(target, SW_SHOW);

    if (hwndStatus_)
        SendMessageW(hwndStatus_, SB_SETTEXTW, 0, (LPARAM)sectionTitle(s));
}

} // namespace wid::gui
