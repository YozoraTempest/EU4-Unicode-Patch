#include "native_mod_registry.hpp"
#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace eu4unicode {
NativeModInitialize original_mod_initialize=nullptr;
NativeModScan original_mod_scan=nullptr;
NativeModInsert original_mod_insert=nullptr;
NativeModRegister native_mod_register=nullptr;
NativeModAssign native_mod_assign=nullptr;
NativeModEnabled native_mod_enabled=nullptr;
void(*native_mod_log)(const char*)=nullptr;

namespace {
constexpr std::size_t mod_tree_offset=0xd0,enabled_offset=0x88,node_value_offset=0x40;
std::string_view text(const EngineString& value) {
    return {value.data(),static_cast<std::size_t>(value.size)};
}
EngineString borrow(const std::string& value) {
    // Both native readers leave this input alone; they copy into engine owners.
    EngineString result{};
    result.storage.pointer=value.c_str();result.size=value.size();result.capacity=16;
    return result;
}
NativeModView& node_mod(void* node) {
    return *reinterpret_cast<NativeModView*>(static_cast<std::byte*>(node)+node_value_offset);
}
struct Candidate { std::string descriptor; };
struct Group { void* node=nullptr;std::vector<Candidate> candidates; };
struct Replay {
    const std::string& name;
    const std::string& descriptor;
    void* node;
    bool assigned=false;
};
struct Scan {
    void* manager;
    bool initializing=false,scanning=false,failed=false;
    Replay* replay=nullptr;
    std::map<std::string,Group,std::less<>> groups;
};
// Each scan/initialization owns its metadata. No manager, node, or temporary
// descriptor pointer survives the call, including nested or subsequent scans.
thread_local Scan* current=nullptr;
struct Scope {
    Scan* previous;
    explicit Scope(Scan& scan):previous(current) { current=&scan; }
    ~Scope() { current=previous; }
};
struct Scanning {
    Scan& scan;
    bool previous;
    explicit Scanning(Scan& value):scan(value),previous(value.scanning) { scan.scanning=true; }
    ~Scanning() { scan.scanning=previous; }
};
void log(const char* value) noexcept { if(native_mod_log) native_mod_log(value); }
bool enabled(const Scan& scan,const std::string& descriptor) {
    const auto path=borrow(descriptor);
    // Same native exact-length/byte comparison as the enabled collection loop.
    return native_mod_enabled(static_cast<std::byte*>(scan.manager)+enabled_offset,&path)!=-1;
}
void resolve(Scan& scan) noexcept {
    if(scan.failed) return;
    try {
        for(auto& [name,group]:scan.groups) {
            if(group.candidates.size()<2) continue;
            std::vector<bool> active;
            std::size_t count=0,selected=0;
            for(std::size_t i=0;i<group.candidates.size();++i) {
                const bool value=enabled(scan,group.candidates[i].descriptor);
                active.push_back(value);
                if(value) { ++count;selected=i; }
            }
            bool replaced=false;
            if(count==1&&text(node_mod(group.node).descriptor)!=group.candidates[selected].descriptor) {
                // Reparse only the discarded enabled descriptor. The engine
                // owns its temporary members and destroys them normally.
                Replay replay{name,group.candidates[selected].descriptor,group.node};
                const auto path=borrow(replay.descriptor);
                scan.replay=&replay;
                try { native_mod_register(scan.manager,&path); }
                catch(...) { scan.replay=nullptr;throw; }
                scan.replay=nullptr;
                replaced=replay.assigned;
                if(!replaced) log("Mod collision repair could not register the selected descriptor.");
            }
            std::ostringstream message;
            message<<"Mod name collision: name="<<std::quoted(name)<<" candidates=[";
            for(std::size_t i=0;i<group.candidates.size();++i) {
                if(i) message<<", ";
                message<<std::quoted(group.candidates[i].descriptor)<<(active[i]?" (enabled)":" (disabled)");
            }
            message<<"] rule="<<(count==1?"unique-enabled":count==0?"native-none-enabled":"native-multiple-enabled")
                   <<" selected="<<std::quoted(std::string(text(node_mod(group.node).descriptor)))
                   <<" replaced="<<(replaced?"yes":"no");
            log(message.str().c_str());
        }
    } catch(...) { log("Mod collision resolution failed."); }
}
}

void initialize_mod_registry(void* manager,const void* configuration) {
    Scan scan{manager};scan.initializing=true;
    Scope scope(scan);
    // Native initialization scans first, then reads dlc_load.json and copies its
    // enabled_mods to manager+0x88. Resolve only after that copy is complete.
    original_mod_initialize(manager,configuration);
    resolve(scan);
}
void scan_mod_registry(void* manager) {
    if(current&&current->manager==manager&&current->initializing&&!current->scanning) {
        Scanning scanning(*current);
        original_mod_scan(manager);
        return;
    }
    Scan scan{manager};
    Scope scope(scan);
    { Scanning scanning(scan);original_mod_scan(manager); }
    // A standalone rescan already has its manager's current enabled list.
    resolve(scan);
}
NativeModInsertResult* insert_mod_registry(void* tree,NativeModInsertResult* result,NativeModEntry* entry) {
    auto* scan=current;
    if(!scan||tree!=static_cast<std::byte*>(scan->manager)+mod_tree_offset||
       (!scan->scanning&&!scan->replay)) return original_mod_insert(tree,result,entry);
    if(scan->replay) {
        const auto& replay=*scan->replay;
        const bool matches=text(entry->key)==replay.name&&text(entry->mod.descriptor)==replay.descriptor;
        auto* returned=original_mod_insert(tree,result,entry);
        if(matches&&!returned->inserted&&returned->node==replay.node) {
            // Duplicate insertion does not move from entry. Use the native
            // deep assignment for every string, vector, and scalar member;
            // neither the tree key nor the original name is changed.
            native_mod_assign(&node_mod(returned->node),&entry->mod);
            scan->replay->assigned=true;
        }
        return returned;
    }
    Group* group=nullptr;
    if(!scan->failed) {
        try {
            // First insertion moves from entry, so record bytes beforehand.
            auto& found=scan->groups[std::string(text(entry->key))];
            const auto path=text(entry->mod.descriptor);
            if(std::none_of(found.candidates.begin(),found.candidates.end(),
                [path](const auto& value) { return value.descriptor==path; }))
                found.candidates.push_back({std::string(path)});
            group=&found;
        } catch(...) {
            scan->failed=true;
            log("Mod collision metadata allocation failed; native registration retained.");
        }
    }
    auto* returned=original_mod_insert(tree,result,entry);
    if(group) group->node=returned->node;
    return returned;
}
}
