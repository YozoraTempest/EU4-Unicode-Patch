#include <cstdio>
#include <cstdlib>

// EU4 exports legacy OpenSSL symbols. A preload must hash the executable
// without binding to these incompatible context functions.
extern "C" {
void* EVP_MD_CTX_new() { std::abort(); }
void EVP_MD_CTX_free(void*) { std::abort(); }
const void* EVP_sha256() { std::abort(); }
int EVP_DigestInit_ex(void*,const void*,void*) { std::abort(); }
int EVP_DigestUpdate(void*,const void*,std::size_t) { std::abort(); }
int EVP_DigestFinal_ex(void*,unsigned char*,unsigned int*) { std::abort(); }
}
int main() {
    std::puts("fixture main called");
    return 42;
}
