#define U_DISABLE_RENAMING 1

#include "icu_runtime.hpp"
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>
#include <unicode/utrans.h>
#include <unicode/uscript.h>
#include <dlfcn.h>
#include <array>
#include <cstdlib>
#include <glob.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace eu4unicode::icu_runtime {
namespace {

struct LibraryPair {
    void* uc=nullptr;
    void* i18n=nullptr;
    int major=0;
};

struct Api {
    decltype(&u_charDirection) fn_u_charDirection=nullptr;
    decltype(&u_charType) fn_u_charType=nullptr;
    decltype(&u_errorName) fn_u_errorName=nullptr;
    decltype(&u_getIntPropertyValue) fn_u_getIntPropertyValue=nullptr;
    decltype(&u_hasBinaryProperty) fn_u_hasBinaryProperty=nullptr;
    decltype(&u_isalpha) fn_u_isalpha=nullptr;
    decltype(&u_strFromUTF8) fn_u_strFromUTF8=nullptr;
    decltype(&u_strToUTF8) fn_u_strToUTF8=nullptr;
    decltype(&utext_close) fn_utext_close=nullptr;
    decltype(&utext_openUTF8) fn_utext_openUTF8=nullptr;
    decltype(&uscript_getScript) fn_uscript_getScript=nullptr;
    decltype(&ubrk_close) fn_ubrk_close=nullptr;
    decltype(&ubrk_first) fn_ubrk_first=nullptr;
    decltype(&ubrk_next) fn_ubrk_next=nullptr;
    decltype(&ubrk_open) fn_ubrk_open=nullptr;
    decltype(&ubrk_setText) fn_ubrk_setText=nullptr;
    decltype(&ubrk_setUText) fn_ubrk_setUText=nullptr;
    decltype(&unorm2_getNFCInstance) fn_unorm2_getNFCInstance=nullptr;
    decltype(&unorm2_getNFDInstance) fn_unorm2_getNFDInstance=nullptr;
    decltype(&unorm2_getNFKCCasefoldInstance) fn_unorm2_getNFKCCasefoldInstance=nullptr;
    decltype(&unorm2_normalize) fn_unorm2_normalize=nullptr;
    decltype(&utrans_close) fn_utrans_close=nullptr;
    decltype(&utrans_openU) fn_utrans_openU=nullptr;
    decltype(&utrans_transUChars) fn_utrans_transUChars=nullptr;
};

std::string loader_error(std::string_view action) {
    const auto* error=dlerror();
    return std::string(action)+(error ? std::string(": ")+error : std::string("."));
}

LibraryPair open_icu() {
    // ICU's public C ABI uses a major-version suffix on ELF symbols and
    // libraries. Try the installed SONAMEs instead of assuming the build
    // host's ICU major. The range covers the current and recent distro ABIs.
    for(int major=40;major<=99;++major) {
        const auto suffix="."+std::to_string(major);
        void* uc=dlopen(("libicuuc.so"+suffix).c_str(),RTLD_NOW|RTLD_LOCAL);
        if(!uc) continue;
        void* i18n=dlopen(("libicui18n.so"+suffix).c_str(),RTLD_NOW|RTLD_LOCAL);
        if(i18n) return {uc,i18n,major};
        dlclose(uc);
    }

    // Debian-family runtime packages commonly install only the fully
    // versioned file (for example libicuuc.so.74.2), while Arch exposes the
    // SONAME symlink (libicuuc.so.78). Search standard multi-arch paths and
    // LD_LIBRARY_PATH without requiring the development package's linker
    // symlinks.
    std::vector<std::string> directories;
    if(const auto* value=std::getenv("LD_LIBRARY_PATH")) {
        std::string paths(value);
        std::size_t begin=0;
        while(begin<=paths.size()) {
            const auto end=paths.find(':',begin);
            const auto directory=paths.substr(begin,end==std::string::npos ? end : end-begin);
            if(!directory.empty()) directories.push_back(directory);
            if(end==std::string::npos) break;
            begin=end+1;
        }
    }
    for(const auto* directory:{"/lib/x86_64-linux-gnu","/usr/lib/x86_64-linux-gnu",
        "/lib64","/usr/lib64","/lib","/usr/lib"}) directories.emplace_back(directory);

    auto candidates=[&](std::string_view name) {
        std::vector<std::string> result;
        for(const auto& directory:directories) {
            glob_t matches{};
            const auto pattern=directory+"/"+std::string(name)+".*";
            if(glob(pattern.c_str(),GLOB_NOSORT,nullptr,&matches)==0) {
                for(std::size_t i=0;i<matches.gl_pathc;++i) result.emplace_back(matches.gl_pathv[i]);
            }
            globfree(&matches);
        }
        return result;
    };
    const auto uc_paths=candidates("libicuuc.so");
    const auto i18n_paths=candidates("libicui18n.so");
    for(const auto& uc_path:uc_paths) {
        const auto marker=uc_path.rfind("libicuuc.so.");
        if(marker==std::string::npos) continue;
        const auto suffix=uc_path.substr(marker+sizeof("libicuuc.so.")-1);
        const auto separator=suffix.find('.');
        const auto major_text=suffix.substr(0,separator);
        int major=0;
        try { major=std::stoi(major_text); } catch(const std::exception&) { continue; }
        if(major<1||major>99) continue;
        for(const auto& i18n_path:i18n_paths) {
            const auto i18n_marker=i18n_path.rfind("libicui18n.so.");
            if(i18n_marker==std::string::npos) continue;
            const auto i18n_suffix=i18n_path.substr(i18n_marker+sizeof("libicui18n.so.")-1);
            if(i18n_suffix.substr(0,i18n_suffix.find('.'))!=major_text) continue;
            void* uc=dlopen(uc_path.c_str(),RTLD_NOW|RTLD_LOCAL);
            if(!uc) continue;
            void* i18n=dlopen(i18n_path.c_str(),RTLD_NOW|RTLD_LOCAL);
            if(i18n) return {uc,i18n,major};
            dlclose(uc);
        }
    }

    // Development packages sometimes expose unversioned linker names. They
    // are useful in custom runtimes even when the SONAME symlink is absent.
    void* uc=dlopen("libicuuc.so",RTLD_NOW|RTLD_LOCAL);
    void* i18n=dlopen("libicui18n.so",RTLD_NOW|RTLD_LOCAL);
    if(uc&&i18n) return {uc,i18n,0};
    if(uc) dlclose(uc);
    if(i18n) dlclose(i18n);
    throw std::runtime_error(loader_error("Cannot find a matching libicuuc/libicui18n pair"));
}

template<typename Function>
Function resolve(void* handle,std::string_view base,int major) {
    std::array<std::string,2> names{};
    names[0]=std::string(base)+(major ? "_"+std::to_string(major) : "");
    names[1]=std::string(base);
    for(const auto& name:names) {
        if(name.empty()) continue;
        dlerror();
        void* address=dlsym(handle,name.c_str());
        if(!dlerror()&&address) return reinterpret_cast<Function>(address);
    }
    throw std::runtime_error(loader_error("Cannot resolve ICU symbol "+std::string(base)));
}

Api load_api() {
    const auto libraries=open_icu();
    Api api;
#define RESOLVE_UC(name) api.fn_##name=resolve<decltype(api.fn_##name)>(libraries.uc,#name,libraries.major)
#define RESOLVE_I18N(name) api.fn_##name=resolve<decltype(api.fn_##name)>(libraries.i18n,#name,libraries.major)
    RESOLVE_UC(u_charDirection);
    RESOLVE_UC(u_charType);
    RESOLVE_UC(u_errorName);
    RESOLVE_UC(u_getIntPropertyValue);
    RESOLVE_UC(u_hasBinaryProperty);
    RESOLVE_UC(u_isalpha);
    RESOLVE_UC(u_strFromUTF8);
    RESOLVE_UC(u_strToUTF8);
    RESOLVE_UC(utext_close);
    RESOLVE_UC(utext_openUTF8);
    RESOLVE_UC(uscript_getScript);
    RESOLVE_UC(ubrk_close);
    RESOLVE_UC(ubrk_first);
    RESOLVE_UC(ubrk_next);
    RESOLVE_UC(ubrk_open);
    RESOLVE_UC(ubrk_setText);
    RESOLVE_UC(ubrk_setUText);
    RESOLVE_UC(unorm2_getNFCInstance);
    RESOLVE_UC(unorm2_getNFDInstance);
    RESOLVE_UC(unorm2_getNFKCCasefoldInstance);
    RESOLVE_UC(unorm2_normalize);
    RESOLVE_I18N(utrans_close);
    RESOLVE_I18N(utrans_openU);
    RESOLVE_I18N(utrans_transUChars);
#undef RESOLVE_UC
#undef RESOLVE_I18N
    return api;
}

Api& api() {
    static Api value;
    static std::once_flag flag;
    std::call_once(flag,[] { value=load_api(); });
    return value;
}

}

void ensure_loaded() { (void)api(); }

}

extern "C" UCharDirection u_charDirection(UChar32 c) {
    return eu4unicode::icu_runtime::api().fn_u_charDirection(c);
}
extern "C" int8_t u_charType(UChar32 c) {
    return eu4unicode::icu_runtime::api().fn_u_charType(c);
}
extern "C" const char* u_errorName(UErrorCode code) {
    return eu4unicode::icu_runtime::api().fn_u_errorName(code);
}
extern "C" int32_t u_getIntPropertyValue(UChar32 c,UProperty property) {
    return eu4unicode::icu_runtime::api().fn_u_getIntPropertyValue(c,property);
}
extern "C" UBool u_hasBinaryProperty(UChar32 c,UProperty property) {
    return eu4unicode::icu_runtime::api().fn_u_hasBinaryProperty(c,property);
}
extern "C" UBool u_isalpha(UChar32 c) {
    return eu4unicode::icu_runtime::api().fn_u_isalpha(c);
}
extern "C" char* u_strToUTF8(char* dest,int32_t capacity,int32_t* length,
    const UChar* source,int32_t sourceLength,UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_u_strToUTF8(dest,capacity,length,source,sourceLength,status);
}
extern "C" UChar* u_strFromUTF8(UChar* dest,int32_t capacity,int32_t* length,
    const char* source,int32_t sourceLength,UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_u_strFromUTF8(dest,capacity,length,source,sourceLength,status);
}
extern "C" UText* utext_close(UText* text) {
    return eu4unicode::icu_runtime::api().fn_utext_close(text);
}
extern "C" UText* utext_openUTF8(UText* text,const char* source,int64_t length,
    UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_utext_openUTF8(text,source,length,status);
}
extern "C" UScriptCode uscript_getScript(UChar32 codePoint,UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_uscript_getScript(codePoint,status);
}
extern "C" void ubrk_close(UBreakIterator* iterator) {
    eu4unicode::icu_runtime::api().fn_ubrk_close(iterator);
}
extern "C" int32_t ubrk_first(UBreakIterator* iterator) {
    return eu4unicode::icu_runtime::api().fn_ubrk_first(iterator);
}
extern "C" int32_t ubrk_next(UBreakIterator* iterator) {
    return eu4unicode::icu_runtime::api().fn_ubrk_next(iterator);
}
extern "C" UBreakIterator* ubrk_open(UBreakIteratorType type,const char* locale,
    const UChar* text,int32_t textLength,UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_ubrk_open(type,locale,text,textLength,status);
}
extern "C" void ubrk_setText(UBreakIterator* iterator,const UChar* text,
    int32_t textLength,UErrorCode* status) {
    eu4unicode::icu_runtime::api().fn_ubrk_setText(iterator,text,textLength,status);
}
extern "C" void ubrk_setUText(UBreakIterator* iterator,UText* text,UErrorCode* status) {
    eu4unicode::icu_runtime::api().fn_ubrk_setUText(iterator,text,status);
}
extern "C" const UNormalizer2* unorm2_getNFCInstance(UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_unorm2_getNFCInstance(status);
}
extern "C" const UNormalizer2* unorm2_getNFDInstance(UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_unorm2_getNFDInstance(status);
}
extern "C" const UNormalizer2* unorm2_getNFKCCasefoldInstance(UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_unorm2_getNFKCCasefoldInstance(status);
}
extern "C" int32_t unorm2_normalize(const UNormalizer2* normalizer,const UChar* source,
    int32_t length,UChar* dest,int32_t capacity,UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_unorm2_normalize(normalizer,source,length,dest,capacity,status);
}
extern "C" void utrans_close(UTransliterator* transliterator) {
    eu4unicode::icu_runtime::api().fn_utrans_close(transliterator);
}
extern "C" UTransliterator* utrans_openU(const UChar* id,int32_t idLength,UTransDirection direction,
    const UChar* rules,int32_t rulesLength,UParseError* parseError,UErrorCode* status) {
    return eu4unicode::icu_runtime::api().fn_utrans_openU(id,idLength,direction,rules,rulesLength,parseError,status);
}
extern "C" void utrans_transUChars(const UTransliterator* transliterator,UChar* text,
    int32_t* textLength,int32_t textCapacity,int32_t start,int32_t* limit,UErrorCode* status) {
    eu4unicode::icu_runtime::api().fn_utrans_transUChars(transliterator,text,textLength,textCapacity,start,limit,status);
}
