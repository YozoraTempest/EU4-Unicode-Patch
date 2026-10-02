#include <windows.h>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    auto module=LoadLibraryW(argv[1]);
    if(!module) return 3;
    FreeLibrary(module);
    return 0;
}
