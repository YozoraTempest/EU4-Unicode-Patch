// Version proxy and plugins-directory loading follow the Matanki EU4dll
// loader contract. See third-party/EU4dll-LICENSE.txt for its MIT notice.
// Forwarders use x64 tail jumps to preserve every argument and return value.
#include <windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

extern "C" {
FARPROC version_exports[17]{};
}

namespace {
constexpr std::array<const char*,17> export_names{{
    "GetFileVersionInfoA","GetFileVersionInfoByHandle",
    "GetFileVersionInfoExA","GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA","GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW","GetFileVersionInfoSizeW",
    "GetFileVersionInfoW","VerFindFileA","VerFindFileW",
    "VerInstallFileA","VerInstallFileW","VerLanguageNameA",
    "VerLanguageNameW","VerQueryValueA","VerQueryValueW"
}};
bool load_system_version() {
    wchar_t directory[32768]{};
    const auto length=GetSystemDirectoryW(directory,32768);
    if(!length||length>=32768) return false;
    const auto path=std::filesystem::path(directory)/L"version.dll";
    const auto system=LoadLibraryW(path.c_str());
    if(!system) return false;
    for(std::size_t index=0;index<export_names.size();++index) {
        version_exports[index]=GetProcAddress(system,export_names[index]);
        if(!version_exports[index]) return false;
    }
    // Keep the system module loaded for the lifetime of the proxy.
    return true;
}
void load_plugins(HMODULE module) {
    wchar_t executable[32768]{},self[32768]{};
    GetModuleFileNameW(nullptr,executable,32768);
    if(_wcsicmp(std::filesystem::path(executable).filename().c_str(),L"eu4.exe")!=0) return;
    GetModuleFileNameW(module,self,32768);
    const auto directory=std::filesystem::path(self).parent_path()/L"plugins";
    if(!std::filesystem::is_directory(directory)) return;
    const auto unicode=directory/L"eu4_unicode_patch.dll";
    const bool unicode_present=std::filesystem::is_regular_file(unicode);
    if(unicode_present) LoadLibraryW(unicode.c_str());
    std::vector<std::filesystem::path> plugins;
    for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        if(!entry.is_regular_file()||_wcsicmp(entry.path().extension().c_str(),L".dll")!=0) continue;
        const auto name=entry.path().filename().wstring();
        if(_wcsicmp(name.c_str(),L"eu4_unicode_patch.dll")==0) continue;
        // Legacy code overlaps the Unicode hooks. Leave the old files intact
        // so restoring the original loader also restores the former setup.
        if(unicode_present&&(_wcsicmp(name.c_str(),L"plugin64.dll")==0||
                             _wcsicmp(name.c_str(),L"eu4_unicode_probe.dll")==0)) continue;
        plugins.push_back(entry.path());
    }
    std::sort(plugins.begin(),plugins.end());
    for(const auto& plugin:plugins) LoadLibraryW(plugin.c_str());
    // Do not execute the old double-byte patch's autoupdate64.bat. This loader
    // never starts a downloader or updates another plugin's files.
}
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        try {
            if(!load_system_version()) return FALSE;
            load_plugins(module);
        } catch(...) { return FALSE; }
    }
    return TRUE;
}
