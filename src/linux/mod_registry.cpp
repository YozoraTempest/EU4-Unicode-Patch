#include "linux/native.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

namespace eu4unicode::linux_native {
namespace {
struct Candidate {std::string name,path;};
struct Scan {
    void* manager;
    std::vector<Candidate> candidates;
    const Candidate* replacement=nullptr;
};
thread_local Scan* current=nullptr;
thread_local Scan* initial=nullptr;
SafetyHookInline* initialize_hook=nullptr;
SafetyHookInline* reload_hook=nullptr;
template<class T> T member(const void* object,std::size_t offset) {
    T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(T));return value;
}
const String& string(const void* object,std::size_t offset) {
    return *reinterpret_cast<const String*>(static_cast<const char*>(object)+offset);
}
struct Capture {
    Scan* previous;
    explicit Capture(Scan& scan):previous(current) {current=&scan;}
    ~Capture() {current=previous;}
};
struct NativeString {
    alignas(8) unsigned char data[32];
    explicit NativeString(const char* bytes) {
        reinterpret_cast<void(*)(void*,const char*)>(0x254b1c2)(data,bytes);
    }
    ~NativeString() {reinterpret_cast<void(*)(void*)>(0xd95742)(data);}
};
struct NativeParts {
    const String* begin=nullptr;
    const String* end=nullptr;
    const String* capacity=nullptr;
    NativeParts(const String& text,char separator) {
        reinterpret_cast<void(*)(void*,const String*,char)>(0x254c9b6)(this,&text,separator);
    }
    ~NativeParts() {reinterpret_cast<void(*)(void*)>(0xd965e6)(this);}
    std::size_t size() const {return begin?static_cast<std::size_t>(end-begin):0;}
};
static_assert(sizeof(NativeParts)==24);
// GetActiveModNames splits on backslash first and slash second, then compares
// the complete final two components to descriptor+0x90 by length and bytes.
// Invoke the engine Split implementation so empty-component handling is exact.
std::string enabled_path(const String& text) {
    NativeParts backslash(text,'\\');
    if(backslash.size()>1)
        return std::string(backslash.end[-2].view())+"/"+std::string(backslash.end[-1].view());
    NativeParts slash(text,'/');
    if(slash.size()<2) return {};
    return std::string(slash.end[-2].view())+"/"+std::string(slash.end[-1].view());
}
void* registered(void* manager,const String& name) {
    return reinterpret_cast<void*(*)(void*,const String*)>(0x1fddea0)(manager,&name);
}
void capture_candidate(safetyhook::Context& c) {
    if(!current||reinterpret_cast<void*>(c.r15)!=current->manager) return;
    const void* descriptor=reinterpret_cast<void*>(c.rsp+0x28);
    const auto& name=string(descriptor,0x10);
    const auto& path=string(descriptor,0x90);
    if(current->replacement) {
        const auto& replacement=*current->replacement;
        if(name.view()==replacement.name&&path.view()==replacement.path) {
            if(auto existing=registered(current->manager,name)) {
                // Engine assignment copies every string, vector and scalar
                // field. The tree key and node address remain unchanged.
                reinterpret_cast<void(*)(void*,const void*)>(0x1fe5468)(existing,descriptor);
            }
        }
        return;
    }
    try {
        if(std::none_of(current->candidates.begin(),current->candidates.end(),
           [&](const auto& value) {return value.name==name.view()&&value.path==path.view();}))
            current->candidates.push_back({std::string(name.view()),std::string(path.view())});
    } catch(...) {
        log("Mod scan capture failed; refusing incomplete collision selection");
        std::terminate();
    }
}
void repair(Scan& scan) {
    if(scan.candidates.empty()) return;
    std::vector<std::string> enabled;
    const auto count=member<int>(scan.manager,0x9c);
    const auto entries=member<const String*>(scan.manager,0x90);
    if(count<0||count>100000||(!entries&&count)) throw std::runtime_error("Invalid native enabled-mod array");
    for(int i=0;i<count;++i) enabled.push_back(enabled_path(entries[i]));
    std::map<std::string,std::vector<const Candidate*>> groups;
    for(const auto& candidate:scan.candidates) groups[candidate.name].push_back(&candidate);
    for(const auto& [name,candidates]:groups) {
        if(candidates.size()<2) continue;
        const Candidate* selected=nullptr;
        unsigned int active=0;
        for(const auto candidate:candidates)
            if(std::find(enabled.begin(),enabled.end(),candidate->path)!=enabled.end()) {
                selected=candidate;++active;
            }
        const auto choice=active==1?selected:candidates.front();
        log("Mod collision name=\"%s\" candidates=%zu enabled=%u action=%s choice=\"%s\"",
            name.c_str(),candidates.size(),active,
            active==1?"unique-enabled":active?"native-multiple-enabled":"native-none-enabled",
            choice->path.c_str());
        for(const auto candidate:candidates) log("Mod candidate name=\"%s\" descriptor=\"%s\" enabled=%d",
            name.c_str(),candidate->path.c_str(),
            std::find(enabled.begin(),enabled.end(),candidate->path)!=enabled.end());
        if(active!=1||selected==candidates.front()) continue;
        Capture capture(scan);
        scan.replacement=selected;
        NativeString path(selected->path.c_str());
        reinterpret_cast<void(*)(void*,const void*)>(0x1fe03a0)(scan.manager,path.data);
        scan.replacement=nullptr;
        NativeString key(name.c_str());
        const auto result=registered(scan.manager,*reinterpret_cast<String*>(key.data));
        if(!result||string(result,0x90).view()!=selected->path)
            throw std::runtime_error("Native mod replacement failed");
        log("Mod registration replaced name=\"%s\" descriptor=\"%s\"",name.c_str(),selected->path.c_str());
    }
}
void reload(void* manager) {
    Scan scan{manager,{},nullptr};
    {Capture capture(scan);reload_hook->call<void>(manager);}
    if(initial&&initial->manager==manager) initial->candidates=std::move(scan.candidates);
    else repair(scan);
}
void initialize(void* manager,const void* settings) {
    Scan scan{manager,{},nullptr};
    const auto previous=initial;
    initial=&scan;
    try {initialize_hook->call<void>(manager,settings);}
    catch(...) {initial=previous;throw;}
    initial=previous;
    // Initialize has now inserted dlc_load.json into manager+0x88. No mod
    // pointers are retained across initialization; only copied name/path bytes.
    repair(scan);
}
}
void prepare_mod_registry(Hooks& hooks) {
    for(const auto address:{0x254b1c2,0xd95742,0x254c9b6,0xd965e6,0x1fddea0,0x1fe5468,0x1fe03a0})
        verify_site(address);
    initialize_hook=&hooks.add_inline(0x1fdee14,reinterpret_cast<void*>(initialize));
    reload_hook=&hooks.add_inline(0x1fdf3ca,reinterpret_cast<void*>(reload));
    // Confined to AddModFromFile after validation, before pair construction.
    // The generic RB-tree insertion helper, DLC and other trees are untouched.
    hooks.add_mid(0x1fe054a,capture_candidate);
}
}
