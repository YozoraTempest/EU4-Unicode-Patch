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
    result.storage.pointer=value.c_str();result.size=value.size();
    result.capacity=std::max<std::size_t>(16,value.size());
    return result;
}
NativeModView& node_mod(void* node) {
    return *reinterpret_cast<NativeModView*>(static_cast<std::byte*>(node)+node_value_offset);
}
struct Candidate { std::string descriptor;bool active=false; };
struct Group { void* node=nullptr;std::vector<Candidate> candidates; };
enum class ReplayStatus { not_registered,assigned,rejected,assignment_failed,parse_failed };
struct Replay {
    const std::string& name;
    const std::string& descriptor;
    void* node;
    ReplayStatus status=ReplayStatus::not_registered;
};
struct Scan {
    void* manager;
    bool initializing=false,scanning=false,failed=false;
    Replay* replay=nullptr;
    std::map<std::string,Group,std::less<>> groups;
};
// Each scan/initialization owns its metadata; game pointers are not cached
// across calls. The verified initializer invokes one nonrecursive scan.
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
struct Replaying {
    Scan& scan;
    explicit Replaying(Scan& value,Replay& replay):scan(value) { scan.replay=&replay; }
    ~Replaying() { scan.replay=nullptr; }
};
void log(const char* value) noexcept {
    try { if(native_mod_log) native_mod_log(value); }
    catch(...) { /* Diagnostics must not interrupt registration or cleanup. */ }
}
const char* status_name(ReplayStatus status) {
    switch(status) {
    case ReplayStatus::assigned:return "assigned";
    case ReplayStatus::rejected:return "rejected";
    case ReplayStatus::assignment_failed:return "assignment-failed-state-unknown";
    case ReplayStatus::parse_failed:return "parse-failed";
    default:return "not-registered";
    }
}
void report_rejection(const Replay& replay,const NativeModEntry& entry) noexcept {
    try {
        std::ostringstream message;
        message.exceptions(std::ios::badbit|std::ios::failbit);
        message<<"Mod collision replay rejected: expected_name="<<std::quoted(replay.name)
               <<" actual_key="<<std::quoted(std::string(text(entry.key)))
               <<" actual_name="<<std::quoted(std::string(text(entry.mod.name)))
               <<" expected_descriptor="<<std::quoted(replay.descriptor)
               <<" actual_descriptor="<<std::quoted(std::string(text(entry.mod.descriptor)));
        log(message.str().c_str());
    } catch(...) { log("Mod collision replay rejected; diagnostic allocation failed."); }
}
void report_collision(const std::string& name,const Group& group,std::size_t count,
                      const Candidate* selected,const Replay* replay) noexcept {
    try {
        std::ostringstream message;
        message.exceptions(std::ios::badbit|std::ios::failbit);
        message<<"Mod name collision: name="<<std::quoted(name)<<" candidates=[";
        for(std::size_t i=0;i<group.candidates.size();++i) {
            if(i) message<<", ";
            message<<std::quoted(group.candidates[i].descriptor)
                   <<(group.candidates[i].active?" (enabled)":" (disabled)");
        }
        const bool assigned=replay&&replay->status==ReplayStatus::assigned;
        message<<"] rule="<<(count==1?"unique-enabled":count==0?"native-none-enabled":"native-multiple-enabled")
               <<" requested="<<std::quoted(selected?selected->descriptor:std::string{})
               <<" selected=";
        // An interrupted native assignment has no verified rollback contract.
        // Report owned metadata instead of reading possibly partial members.
        if(replay&&(replay->status==ReplayStatus::assignment_failed||replay->status==ReplayStatus::parse_failed))
            message<<"unknown";
        else message<<std::quoted(assigned?replay->descriptor:group.candidates.front().descriptor);
        message<<" replaced="<<(assigned?"yes":"no")
               <<" status="<<(replay?status_name(replay->status):"native-retained");
        log(message.str().c_str());
    } catch(...) { log("Mod collision diagnostic allocation failed; registration processing continues."); }
}
bool enabled(const Scan& scan,const std::string& descriptor) {
    const auto path=borrow(descriptor);
    // Same native exact-length/byte comparison as the enabled collection loop.
    return native_mod_enabled(static_cast<std::byte*>(scan.manager)+enabled_offset,&path)!=-1;
}
void resolve(Scan& scan) noexcept {
    if(scan.failed) return;
    for(auto& [name,group]:scan.groups) {
        try {
            std::size_t count=0;
            Candidate* selected=nullptr;
            for(auto& candidate:group.candidates) {
                candidate.active=enabled(scan,candidate.descriptor);
                if(candidate.active) { ++count;selected=&candidate; }
            }
            if(count==1&&group.candidates.front().descriptor!=selected->descriptor) {
                // Reparse only the discarded enabled descriptor. The engine
                // owns its temporary members and destroys them normally.
                Replay replay{name,selected->descriptor,group.node};
                const auto path=borrow(replay.descriptor);
                {
                    Replaying context(scan,replay);
                    try { native_mod_register(scan.manager,&path); }
                    catch(...) {
                        replay.status=ReplayStatus::parse_failed;
                        scan.failed=true;
                    }
                }
                report_collision(name,group,count,selected,&replay);
                if(scan.failed) {
                    log("Mod collision native operation failed; further repairs stopped.");
                    return;
                }
            } else {
                report_collision(name,group,count,count==1?selected:nullptr,nullptr);
            }
        } catch(...) { log("Mod collision enabled lookup failed; further repairs stopped.");return; }
    }
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
        current->groups.clear();
        current->failed=false;
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
        auto& replay=*scan->replay;
        // The verified parser ignores this duplicate-insertion result and
        // destroys the unconsumed entry. Replays may only update this node;
        // forwarding a changed name to insertion could create a new node.
        result->node=replay.node;result->inserted=false;
        if(replay.status!=ReplayStatus::not_registered) return result;
        if(text(entry->key)!=replay.name||text(entry->mod.name)!=replay.name||
           text(entry->mod.descriptor)!=replay.descriptor) {
            replay.status=ReplayStatus::rejected;
            report_rejection(replay,*entry);
            return result;
        }
        try {
            native_mod_assign(&node_mod(replay.node),&entry->mod);
            replay.status=ReplayStatus::assigned;
        } catch(...) {
            replay.status=ReplayStatus::assignment_failed;
            scan->failed=true;
        }
        return result;
    }
    auto* returned=original_mod_insert(tree,result,entry);
    // Successful insertion moves entry into the returned node. On duplicate
    // insertion entry is untouched, so capture metadata only for collisions.
    if(returned->inserted) return returned;
    if(!scan->failed) {
        try {
            const auto path=text(entry->mod.descriptor);
            const auto first=text(node_mod(returned->node).descriptor);
            if(path==first) return returned;
            const auto name=text(entry->key);
            auto found=scan->groups.find(name);
            if(found==scan->groups.end()) {
                Group group{returned->node};
                group.candidates.reserve(2);
                group.candidates.push_back({std::string(first)});
                found=scan->groups.emplace(std::string(name),std::move(group)).first;
            }
            auto& candidates=found->second.candidates;
            if(std::none_of(candidates.begin(),candidates.end(),
                [path](const auto& value) { return value.descriptor==path; }))
                candidates.push_back({std::string(path)});
        } catch(...) {
            scan->failed=true;
            log("Mod collision metadata allocation failed; native registration retained.");
        }
    }
    return returned;
}
}
