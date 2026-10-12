#include "native_mod_registry.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace eu4unicode;
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
    bool foreign_tree=false;
    void* tree() { return manager.data()+0xd0; }
} fixture;
NativeModView& mod(Node& node) { return *reinterpret_cast<NativeModView*>(node.bytes.data()+0x40); }
NativeModInsertResult* native_insert(void* tree,NativeModInsertResult* result,NativeModEntry* entry) {
    auto& nodes=tree==fixture.tree()?fixture.nodes:fixture.other_nodes;
    const auto name=text(entry->key);
    auto found=nodes.find(name);
    result->inserted=found==nodes.end();
    if(result->inserted) {
        auto node=std::make_unique<Node>();
        std::memcpy(node->bytes.data()+0x40,&entry->mod,0x338);
        found=nodes.emplace(name,std::move(node)).first;
        // The real insertion consumes the temporary. Metadata must be copied
        // before forwarding to this call, not from the moved-from argument.
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
}
void native_register(void* manager,const EngineString* descriptor) {
    check(manager==fixture.manager.data(),"reparse keeps the owning manager");
    ++fixture.registrations;
    for(const auto& record:fixture.records) if(record.path==text(*descriptor)) {
        register_record(record,fixture.tree());return;
    }
    check(false,"reparse uses the complete original descriptor path");
}
void* native_assign(void* destination,const void* source) {
    ++fixture.assignments;
    std::memcpy(destination,source,0x338);
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
    fixture.enabled=fixture.configured;
}
void log(const char* value) { fixture.logs.emplace_back(value); }
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
    std::cout<<"Native mod registry hook lifecycle and ABI checks passed.\n";
}
