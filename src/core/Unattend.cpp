#include "core/Unattend.h"

#include <fstream>
#include <sstream>

namespace wid::core {

namespace {

std::wstring xmlEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
            case L'&':  out += L"&amp;";  break;
            case L'<':  out += L"&lt;";   break;
            case L'>':  out += L"&gt;";   break;
            case L'"':  out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default:    out.push_back(c); break;
        }
    }
    return out;
}

} // namespace

bool writeUnattendXml(const fs::path& isoSourcesDir, const UnattendOptions& opts) {
    std::error_code ec;
    fs::create_directories(isoSourcesDir, ec);
    fs::path out = isoSourcesDir.parent_path() / L"autounattend.xml";

    std::wofstream f(out);
    if (!f) return false;

    f << L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
    f << L"<unattend xmlns=\"urn:schemas-microsoft-com:unattend\">\r\n";

    // specialize: locale + timezone + computer name
    f << L"  <settings pass=\"specialize\">\r\n";
    f << L"    <component name=\"Microsoft-Windows-Shell-Setup\" "
         L"processorArchitecture=\"amd64\" publicKeyToken=\"31bf3856ad364e35\" "
         L"language=\"neutral\" versionScope=\"nonSxS\">\r\n";
    if (!opts.computerName.empty())
        f << L"      <ComputerName>" << xmlEscape(opts.computerName) << L"</ComputerName>\r\n";
    f << L"      <TimeZone>" << xmlEscape(opts.timezone) << L"</TimeZone>\r\n";
    f << L"    </component>\r\n";
    f << L"  </settings>\r\n";

    // oobeSystem: skip MS account, EULA, first-logon commands, auto-logon
    f << L"  <settings pass=\"oobeSystem\">\r\n";
    f << L"    <component name=\"Microsoft-Windows-Shell-Setup\" "
         L"processorArchitecture=\"amd64\" publicKeyToken=\"31bf3856ad364e35\" "
         L"language=\"neutral\" versionScope=\"nonSxS\">\r\n";
    f << L"      <OOBE>\r\n";
    f << L"        <HideEULAPage>" << (opts.acceptEula ? L"true" : L"false") << L"</HideEULAPage>\r\n";
    f << L"        <ProtectYourPC>3</ProtectYourPC>\r\n";
    if (opts.skipMicrosoftAccount)
        f << L"        <HideOnlineAccountScreens>true</HideOnlineAccountScreens>\r\n";
    f << L"      </OOBE>\r\n";

    if (opts.autoLogon && !opts.autoLogonUser.empty()) {
        f << L"      <AutoLogon>\r\n";
        f << L"        <Username>" << xmlEscape(opts.autoLogonUser) << L"</Username>\r\n";
        f << L"        <Enabled>true</Enabled>\r\n";
        f << L"        <LogonCount>1</LogonCount>\r\n";
        f << L"        <Password><Value>"
          << xmlEscape(opts.autoLogonPassword) << L"</Value><PlainText>true</PlainText></Password>\r\n";
        f << L"      </AutoLogon>\r\n";
    }

    if (!opts.firstLogonCommands.empty()) {
        f << L"      <FirstLogonCommands>\r\n";
        int order = 1;
        for (const auto& c : opts.firstLogonCommands) {
            f << L"        <SynchronousCommand wcm:action=\"add\">\r\n";
            f << L"          <Order>" << order++ << L"</Order>\r\n";
            f << L"          <CommandLine>" << xmlEscape(c.commandLine) << L"</CommandLine>\r\n";
            if (!c.description.empty())
                f << L"          <Description>" << xmlEscape(c.description) << L"</Description>\r\n";
            f << L"          <RequiresUserInput>false</RequiresUserInput>\r\n";
            f << L"        </SynchronousCommand>\r\n";
        }
        f << L"      </FirstLogonCommands>\r\n";
    }

    f << L"      <UserAccounts>\r\n";
    if (!opts.adminPassword.empty()) {
        f << L"        <AdministratorPassword>\r\n";
        f << L"          <Value>" << xmlEscape(opts.adminPassword) << L"</Value>\r\n";
        f << L"          <PlainText>true</PlainText>\r\n";
        f << L"        </AdministratorPassword>\r\n";
    }
    f << L"      </UserAccounts>\r\n";

    f << L"      <UILanguage>" << xmlEscape(opts.locale) << L"</UILanguage>\r\n";
    f << L"    </component>\r\n";
    f << L"  </settings>\r\n";

    f << L"</unattend>\r\n";
    return f.good();
}

} // namespace wid::core
