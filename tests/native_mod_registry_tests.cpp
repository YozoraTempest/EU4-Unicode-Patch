#include "native_mod_registry.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace eu4unicode;
namespace allocation_probe {
bool counting=false,fail_next=false;
std::size_t count=0;
struct Pause {
    bool previous=counting;
    Pause() { counting=false; }
    ~Pause() { counting=previous; }
};
}
void* operator new(std::size_t size) {
    if(allocation_probe::fail_next) { allocation_probe::fail_next=false;throw std::bad_alloc(); }
    if(allocation_probe::counting) ++allocation_probe::count;
    if(void* value=std::malloc(size?size:1)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value,std::size_t) noexcept { std::free(value); }
namespace {
void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); }
}
EngineString view(const std::string& value) {
    EngineString result{};result.storage.pointer=value.c_str();result.size=value.size();result.capacity=16;
    return result;
}
std::string text(const EngineString& value) { return {value.data(),static_cast<std::size_t>(value.size)}; }
struct Record { std::string name,path;unsigned char marker; };
struct Node { alignas(8) std::array<std::byte,0x378> bytes{}; };
struct Fixture {
    alignas(8) std::array<std::byte,0x108> manager{};
    std::vector<Record> records;
    std::vector<std::string> enabled,configured;
    std::map<std::string,std::unique_ptr<Node>> nodes,other_nodes;
    std::vector<std::string> logs;
    int assignments=0,registrations=0,scans=0;
    int insertions=0,cleanups=0,log_calls=0;
    bool foreign_tree=false,override_replay=false,missing_replay=false,throw_parse=false,throw_assign=false;
    bool fail_diagnostic_allocation=false,throw_first_log=false;
    Record replay_record;
    void* tree() { return manager.data()+0xd0; }
} fixture;
NativeModView& mod(Node& node) { return *reinterpret_cast<NativeModView*>(node.bytes.data()+0x40); }
NativeModInsertResult* native_insert(void* tree,NativeModInsertResult* result,NativeModEntry* entry) {
    allocation_probe::Pause native_allocation;
    ++fixture.insertions;
    auto& nodes=tree==fixture.tree()?fixture.nodes:fixture.other_nodes;
    const auto name=text(entry->key);
    auto found=nodes.find(name);
    result->inserted=found==nodes.end();
    if(result->inserted) {
        auto node=std::make_unique<Node>();
        std::memcpy(node->bytes.data()+0x40,&entry->mod,0x338);
        found=nodes.emplace(name,std::move(node)).first;
        // Successful native insertion consumes the temporary. Collision
        // metadata must use the returned node for the first descriptor.
        std::memset(entry,0,0x358);
    }
    result->node=found->second->bytes.data();return result;
}
void register_record(const Record& record,void* tree) {
    alignas(8) std::array<std::byte,0x358> bytes{};
    auto& entry=*reinterpret_cast<NativeModEntry*>(bytes.data());
    entry.key=view(record.name);entry.mod.name=view(record.name);entry.mod.descriptor=view(record.path);
    bytes[0x20+0x337]=static_cast<std::byte>(record.marker);
    NativeModInsertResult result{};
    insert_mod_registry(tree,&result,&entry);
    ++fixture.cleanups;
}
void native_register(void* manager,const EngineString* descriptor) {
    check(manager==fixture.manager.data(),"reparse keeps the owning manager");
    ++fixture.registrations;
    if(fixture.throw_parse) throw std::bad_alloc();
    if(fixture.missing_replay) return;
    if(fixture.override_replay) { register_record(fixture.replay_record,fixture.tree());return; }
    for(const auto& record:fixture.records) if(record.path==text(*descriptor)) {
        register_record(record,fixture.tree());return;
    }
    check(false,"reparse uses the complete original descriptor path");
}
void* native_assign(void* destination,const void* source) {
    ++fixture.assignments;
    if(fixture.throw_assign) throw std::bad_alloc();
    std::memcpy(destination,source,0x338);
    if(fixture.fail_diagnostic_allocation) {
        fixture.fail_diagnostic_allocation=false;
        allocation_probe::fail_next=true;
    }
    return destination;
}
int native_enabled(const void* list,const EngineString* descriptor) {
    check(list==fixture.manager.data()+0x88,"query the native enabled list");
    const auto value=text(*descriptor);
    for(std::size_t i=0;i<fixture.enabled.size();++i)
        if(fixture.enabled[i]==value) return static_cast<int>(i);
    return -1;
}
void native_scan(void* manager) {
    check(manager==fixture.manager.data(),"scan forwards manager");
    ++fixture.scans;fixture.nodes.clear();fixture.other_nodes.clear();
    for(const auto& record:fixture.records) register_record(record,fixture.tree());
    if(fixture.foreign_tree) {
        for(const auto& record:fixture.records) register_record(record,fixture.manager.data()+0xc0);
    }
}
void native_initialize(void* manager,const void* configuration) {
    check(configuration==&fixture.configured,"initializer forwards configuration");
    check(fixture.enabled.empty(),"enabled list is not yet populated during initial scan");
    scan_mod_registry(manager);
    check(fixture.assignments==0,"initial scan cannot use the unread enabled list");
    { allocation_probe::Pause native_allocation;fixture.enabled=fixture.configured; }
}
void log(const char* value) {
    if(++fixture.log_calls==1&&fixture.throw_first_log) throw std::bad_alloc();
    fixture.logs.emplace_back(value);
}
bool has_log(const std::string& text) {
    for(const auto& value:fixture.logs) if(value.find(text)!=std::string::npos) return true;
    return false;
}
void two_groups() {
    fixture=Fixture{};
    fixture.records={{"A","mod/a-old.mod",1},{"A","mod/a-active.mod",2},
                     {"B","mod/b-old.mod",3},{"B","mod/b-active.mod",4}};
    fixture.configured={"mod/a-active.mod","mod/b-active.mod"};
}
void initialize() { initialize_mod_registry(fixture.manager.data(),&fixture.configured); }
void check_both_groups() {
    check(fixture.assignments==2,"diagnostic failure must not skip another conflict group");
    check(text(mod(*fixture.nodes.at("A")).descriptor)=="mod/a-active.mod","first group repaired");
    check(text(mod(*fixture.nodes.at("B")).descriptor)=="mod/b-active.mod","second group repaired");
    check(fixture.insertions==4,"replays cannot insert additional native tree entries");
    check(fixture.cleanups==6,"all native scan and replay temporaries reach cleanup");
}
std::size_t unique_allocations(std::size_t count) {
    fixture=Fixture{};
    for(std::size_t i=0;i<count;++i)
        fixture.records.push_back({"Unique mod name "+std::to_string(i),"mod/unique-"+std::to_string(i)+".mod",1});
    allocation_probe::count=0;allocation_probe::counting=true;
    initialize();
    allocation_probe::counting=false;
    check(fixture.nodes.size()==count&&fixture.logs.empty(),"unique names follow native insertion without diagnostics");
    return allocation_probe::count;
}
void run(std::vector<Record> records,std::vector<std::string> enabled,const std::string& selected,int assignments,
         const std::string& rule) {
    fixture=Fixture{};fixture.records=std::move(records);fixture.configured=std::move(enabled);
    initialize_mod_registry(fixture.manager.data(),&fixture.configured);
    check(fixture.nodes.size()==1,"retain one native tree entry per original name");
    auto& node=*fixture.nodes.begin()->second;
    check(text(mod(node).name)==fixture.records.front().name,"keep the original descriptor name");
    check(text(mod(node).descriptor)==selected,"selected descriptor");
    check(fixture.assignments==assignments,"only discarded unique enabled entries are deep-assigned");
    check(fixture.registrations==assignments,"only selected discarded entries are reparsed");
    if(rule.empty()) check(fixture.logs.empty(),"unique names do not produce conflict logs");
    else check(fixture.logs.size()==1&&fixture.logs.front().find(rule)!=std::string::npos,"one explicit conflict log");
    for(const auto& record:fixture.records) if(record.path==selected)
        check(std::to_integer<unsigned char>(node.bytes[0x40+0x337])==record.marker,"assignment includes the native object's tail");
}
}
int main() {
    original_mod_initialize=native_initialize;original_mod_scan=native_scan;original_mod_insert=native_insert;
    native_mod_register=native_register;native_mod_assign=native_assign;native_mod_enabled=native_enabled;native_mod_log=log;
    const Record old{"Same name","mod/old.mod",1},active{"Same name","mod/active.mod",2};
    run({active},{active.path},active.path,0,"");
    run({active,active},{active.path},active.path,0,"");
    run({old,active},{active.path},active.path,1,"unique-enabled");
    run({active,old},{active.path},active.path,0,"unique-enabled");
    run({old,active},{},old.path,0,"native-none-enabled");
    run({old,active},{old.path,active.path},old.path,0,"native-multiple-enabled");
    run({old,active},{"mod/active.mod.extra"},old.path,0,"native-none-enabled");
    run({old,active},{"MOD/active.mod"},old.path,0,"native-none-enabled");
    run({old,active},{active.path},active.path,1,"unique-enabled");
    fixture.enabled={old.path};fixture.foreign_tree=true;
    scan_mod_registry(fixture.manager.data());
    check(text(mod(*fixture.nodes.begin()->second).descriptor)==old.path,"rescan discards previous nodes and uses the current list");
    check(text(mod(*fixture.other_nodes.begin()->second).descriptor)==old.path,"foreign tree keeps native insertion");
    fixture.enabled={active.path};
    scan_mod_registry(fixture.manager.data());
    check(text(mod(*fixture.nodes.begin()->second).descriptor)==active.path,"a second standalone rescan resolves again");
    check(text(mod(*fixture.other_nodes.begin()->second).descriptor)==old.path,"DLC/other trees are not repaired");
    fixture=Fixture{};fixture.records={old,active};fixture.enabled={active.path};
    for(const auto& record:fixture.records) register_record(record,fixture.tree());
    check(text(mod(*fixture.nodes.begin()->second).descriptor)==old.path&&fixture.assignments==0,
          "calls outside a mod scan are forwarded without retaining metadata");
    two_groups();fixture.throw_first_log=true;initialize();check_both_groups();
    two_groups();fixture.fail_diagnostic_allocation=true;initialize();check_both_groups();
    check(has_log("diagnostic allocation failed"),"allocation failure has a nonallocating diagnostic fallback");
    for(bool change_path:{false,true}) {
        fixture=Fixture{};fixture.records={old,active};fixture.configured={active.path};
        fixture.override_replay=true;
        fixture.replay_record={change_path?active.name:"Changed name",change_path?"mod/changed.mod":active.path,9};
        initialize();
        check(fixture.nodes.size()==1&&fixture.assignments==0&&fixture.insertions==2,
              "changed replay identity must not mutate or extend the native tree");
        check(text(mod(*fixture.nodes.begin()->second).descriptor)==old.path,"rejected replay retains the original node");
        check(fixture.cleanups==3&&has_log("status=rejected"),"rejection keeps cleanup and an explicit conflict result");
        check(has_log("expected_name=")&&has_log("actual_descriptor="),"rejection identifies both expected and actual candidates");
    }
    two_groups();fixture.missing_replay=true;initialize();
    check(fixture.registrations==2&&fixture.assignments==0&&has_log("status=not-registered"),
          "a descriptor removed before replay is reported for each affected group");
    two_groups();fixture.throw_assign=true;initialize();
    check(fixture.assignments==1&&fixture.registrations==1&&fixture.cleanups==5,
          "native assignment failure stops further repairs and leaves temporary cleanup to the parser");
    check(has_log("selected=unknown")&&has_log("assignment-failed-state-unknown"),
          "assignment failure must not claim the original object was retained");
    two_groups();fixture.throw_parse=true;initialize();
    check(fixture.assignments==0&&fixture.registrations==1&&has_log("status=parse-failed"),
          "native parser failure stops further repairs");
    fixture.throw_parse=false;fixture.enabled=fixture.configured;
    scan_mod_registry(fixture.manager.data());
    check(fixture.assignments==2,"a later scan does not retain failed replay state");
    const auto one=unique_allocations(1),many=unique_allocations(512);
    check(one==many,"patch metadata allocations must not grow with unique mod count");
    std::cout<<"Unique-name metadata allocations: 1 mod="<<one<<", 512 mods="<<many<<".\n";
    std::cout<<"Native mod registry hook lifecycle and ABI checks passed.\n";
}
