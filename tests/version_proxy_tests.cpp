#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=2) throw std::invalid_argument("Expected version proxy path");
        const auto proxy=LoadLibraryW(argv[1]);
        if(!proxy) throw std::runtime_error("Cannot load version proxy");
        wchar_t directory[32768]{};
        if(!GetSystemDirectoryW(directory,32768)) throw std::runtime_error("No system directory");
        const auto filename=std::filesystem::path(directory)/L"kernel32.dll";
        const auto system=LoadLibraryW((std::filesystem::path(directory)/L"version.dll").c_str());
        using Size=DWORD(WINAPI*)(LPCWSTR,LPDWORD);
        using Info=BOOL(WINAPI*)(LPCWSTR,DWORD,DWORD,LPVOID);
        using InfoEx=BOOL(WINAPI*)(DWORD,LPCWSTR,DWORD,DWORD,LPVOID);
        using Query=BOOL(WINAPI*)(LPCVOID,LPCWSTR,LPVOID*,PUINT);
        const auto size=reinterpret_cast<Size>(GetProcAddress(proxy,"GetFileVersionInfoSizeW"))(filename.c_str(),nullptr);
        if(!size||size!=reinterpret_cast<Size>(GetProcAddress(system,"GetFileVersionInfoSizeW"))(filename.c_str(),nullptr))
            throw std::runtime_error("Version size forwarding failed");
        std::vector<unsigned char> actual(size),expected(size);
        if(!reinterpret_cast<Info>(GetProcAddress(proxy,"GetFileVersionInfoW"))(filename.c_str(),0,size,actual.data())||
           !reinterpret_cast<Info>(GetProcAddress(system,"GetFileVersionInfoW"))(filename.c_str(),0,size,expected.data())||actual!=expected)
            throw std::runtime_error("Version data forwarding failed");
        void* fixed=nullptr;UINT length=0;
        if(!reinterpret_cast<Query>(GetProcAddress(proxy,"VerQueryValueW"))(actual.data(),L"\\",&fixed,&length)||
           length<sizeof(VS_FIXEDFILEINFO)||static_cast<VS_FIXEDFILEINFO*>(fixed)->dwSignature!=0xfeef04bd)
            throw std::runtime_error("Version query output forwarding failed");
        if(!reinterpret_cast<InfoEx>(GetProcAddress(proxy,"GetFileVersionInfoExW"))(FILE_VER_GET_NEUTRAL,filename.c_str(),0,size,actual.data()))
            throw std::runtime_error("Five-argument version forwarding failed");
        wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
        if(_wcsicmp(std::filesystem::path(self).filename().c_str(),L"eu4.exe")==0) {
            if(GetModuleHandleW(L"plugin64.dll")||GetModuleHandleW(L"eu4_unicode_probe.dll")||
               !GetModuleHandleW(L"eu4_menu_patch.dll")||!GetModuleHandleW(L"eu4_unicode_patch.dll"))
                throw std::runtime_error("Player loader conflict/neighbor contract failed");
        }
        std::cout<<"PASS: system version data, output pointers and stack arguments survive the x64 proxy.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
