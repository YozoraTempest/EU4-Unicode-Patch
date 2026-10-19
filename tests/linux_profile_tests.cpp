#include "linux/native.hpp"
#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
namespace eu4unicode::linux_native {void log(const char*,...) noexcept {}}
namespace {
__attribute__((noinline)) int probe(int value) {volatile int result=value+31;return result;}
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
}
int main() {
    using namespace eu4unicode::linux_native;
    const auto address=reinterpret_cast<std::uintptr_t>(probe);
    std::array<unsigned char,16> bytes{};std::memcpy(bytes.data(),reinterpret_cast<const void*>(address),bytes.size());
    verify_native_instructions(address,bytes);
    auto changed=bytes;changed[7]^=1;
    bool rejected=false;
    try {verify_native_instructions(address,changed);} catch(const std::runtime_error&) {rejected=true;}
    check(rejected,"changed native instruction must be refused");
    for(const auto other:{std::uintptr_t{1},reinterpret_cast<std::uintptr_t>(bytes.data())}) {
        rejected=false;try {verify_native_instructions(other,bytes);} catch(const std::runtime_error&) {rejected=true;}
        check(rejected,"unmapped and non-executable addresses must be refused before reading");
    }
    const auto page=static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    void* foreign=mmap(nullptr,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    check(foreign!=MAP_FAILED,"allocate foreign mapping");std::memcpy(foreign,bytes.data(),bytes.size());
    check(!mprotect(foreign,page,PROT_READ|PROT_EXEC),"protect foreign mapping");
    rejected=false;try {verify_native_instructions(reinterpret_cast<std::uintptr_t>(foreign),bytes);} catch(const std::runtime_error&) {rejected=true;}
    munmap(foreign,page);check(rejected,"matching bytes outside the main executable are insufficient");
    check(probe(11)==42,"validation leaves native instructions unchanged");
    std::cout<<"Native image mapping and instruction validation passed\n";
}
