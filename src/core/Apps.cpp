#include "core/Apps.h"

#include <algorithm>

namespace wid::core {

const std::vector<AppEntry>& builtinAppCatalog() {
    static const std::vector<AppEntry> cat = {
        // URLs / silent switches are best-effort defaults — the user may
        // override per entry in the GUI. Vendor sites change; treat these
        // as starting points, not contracts.
        { L"app.antigravity", L"Google Antigravity", L"Google",
          L"", L"/S", L"", true },

        { L"app.brave", L"Brave Browser", L"Brave Software",
          L"https://laptop-updates.brave.com/latest/winx64",
          L"/silent /install", L"", true },

        { L"app.edge", L"Microsoft Edge", L"Microsoft",
          L"https://go.microsoft.com/fwlink/?linkid=2108834",
          L"/silent /install", L"", true },

        { L"app.vscode", L"Visual Studio Code", L"Microsoft",
          L"https://update.code.visualstudio.com/latest/win32-x64/stable",
          L"/VERYSILENT /MERGETASKS=!runcode", L"", true },

        { L"app.windsurf", L"Windsurf", L"Codeium",
          L"", L"/S", L"", true },

        { L"app.cursor", L"Cursor", L"Anysphere",
          L"", L"/S", L"", true },

        { L"app.claude", L"Claude", L"Anthropic",
          L"", L"/S", L"", true },
    };
    return cat;
}

const AppEntry* findApp(const std::vector<AppEntry>& catalog, const std::wstring& id) {
    auto it = std::find_if(catalog.begin(), catalog.end(),
                           [&](const AppEntry& a){ return a.id == id; });
    return it == catalog.end() ? nullptr : &*it;
}

} // namespace wid::core
