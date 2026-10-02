#include <windows.h>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    auto module=LoadLibraryW(argv[1]);
    if(!module) return 3;
    auto enabled=reinterpret_cast<int(*)()>(GetProcAddress(module,"Eu4UnicodeProbeEnabled"));
    if(!enabled || enabled()!=0) { FreeLibrary(module); return 4; }
    FreeLibrary(module);
    return 0;
}
