#pragma once
#include "engine_string.hpp"
#include <cstddef>

namespace eu4unicode {
// Views of engine-owned objects. Do not copy their owning members in C++.
struct NativeModView {
    std::byte prefix[0x10];
    EngineString name;
    std::byte members[0x60];
    EngineString descriptor;
};
struct NativeModEntry {
    EngineString key;
    NativeModView mod;
};
struct NativeModInsertResult {
    void* node;
    bool inserted;
};
static_assert(offsetof(NativeModView,name)==0x10);
static_assert(offsetof(NativeModView,descriptor)==0x90);
static_assert(offsetof(NativeModEntry,mod)==0x20);
static_assert(offsetof(NativeModInsertResult,inserted)==8);

using NativeModInitialize=void(*)(void*,const void*);
using NativeModScan=void(*)(void*);
using NativeModInsert=NativeModInsertResult*(*)(void*,NativeModInsertResult*,NativeModEntry*);
using NativeModRegister=void(*)(void*,const EngineString*);
using NativeModAssign=void*(*)(void*,const void*);
using NativeModEnabled=int(*)(const void*,const EngineString*);

extern NativeModInitialize original_mod_initialize;
extern NativeModScan original_mod_scan;
extern NativeModInsert original_mod_insert;
extern NativeModRegister native_mod_register;
extern NativeModAssign native_mod_assign;
extern NativeModEnabled native_mod_enabled;
extern void(*native_mod_log)(const char*);

void initialize_mod_registry(void* manager,const void* configuration);
void scan_mod_registry(void* manager);
NativeModInsertResult* insert_mod_registry(void* tree,NativeModInsertResult* result,NativeModEntry* entry);
}
