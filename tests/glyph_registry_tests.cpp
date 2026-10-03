#include "glyph_registry.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
int main() {
    using namespace eu4unicode;
    std::array<void*,256> first{},alias{},second{};
    int anchor1=0,anchor2=0;
    first[0x41]=alias[0x41]=&anchor1;
    second[0x41]=&anchor2;
    auto han=allocate_unicode_glyph(first.data(),0x20000);
    auto emoji=allocate_unicode_glyph(first.data(),0x1f600);
    check(han&&emoji,"supplementary Han and emoji allocation");
    han->advance=18;
    check(!allocate_unicode_glyph(first.data(),0x20000),"duplicate glyph is rejected");
    check(find_unicode_glyph(alias.data(),0x20000)==han&&han->advance==18,"font aliases share a stable atlas binding");
    check(find_unicode_glyph(first.data(),0x1f600)==emoji,"full 32-bit scalar key");
    check(!find_unicode_glyph(second.data(),0x20000),"different fonts stay separate");
    auto other=allocate_unicode_glyph(second.data(),0x20000);
    check(other&&other!=han&&find_unicode_glyph(second.data(),0x20000)==other,
        "same scalar can have independent font metrics");
    auto low=allocate_unicode_glyph(first.data(),0x100);
    auto pua=allocate_unicode_glyph(first.data(),0xe100);
    check(low&&pua&&low!=pua,"Unicode no longer collides with font fields or relocated PUA slots");
    check(!allocate_unicode_glyph(first.data(),0xff)&&!allocate_unicode_glyph(first.data(),0xd800)&&!allocate_unicode_glyph(first.data(),0x110000),
        "native ASCII, surrogate and out-of-range keys cannot enter the registry");
    std::array<void*,256> reordered{},reordered_alias{};
    int late_anchor=0;
    auto early=allocate_unicode_glyph(reordered.data(),0x20000);
    check(early&&find_unicode_glyph(reordered.data(),0x20000)==early,"Unicode may load before ASCII A");
    reordered[0x41]=reordered_alias[0x41]=&late_anchor;
    check(bind_unicode_font(reordered.data())&&find_unicode_glyph(reordered_alias.data(),0x20000)==early,
        "late atlas binding retains the original pointer and supports font aliases");
    const auto before=unicode_glyph_usage();
    release_unicode_font(first.data());
    check(!find_unicode_glyph(alias.data(),0x20000)&&!find_unicode_glyph(first.data(),0x1f600),
        "destroying the owning native font invalidates all atlas aliases");
    check(find_unicode_glyph(second.data(),0x20000)==other,"destroying a font preserves independent fonts");
    const auto after=unicode_glyph_usage();
    check(after.fonts+1==before.fonts&&after.glyphs+4==before.glyphs,"font destruction releases every sparse glyph record");
    auto reused=allocate_unicode_glyph(first.data(),0x20000);
    check(reused&&reused->advance==0,"reused atlas identity starts with fresh metrics");
    reused->advance=31;
    check(find_unicode_glyph(alias.data(),0x20000)==reused&&reused->advance==31,"aliases bind to the new atlas generation");
    release_unicode_font(first.data());
    release_unicode_font(second.data());
    release_unicode_font(reordered.data());
    release_unicode_font(nullptr);
    std::array<void*,256> pending{};
    check(allocate_unicode_glyph(pending.data(),0x20000)!=nullptr,"unbound font can allocate a pending record");
    release_unicode_font(pending.data());
    check(!find_unicode_glyph(pending.data(),0x20000),"unbound font destruction releases pending records");
    check(unicode_glyph_usage().fonts==0&&unicode_glyph_usage().glyphs==0,"complete font lifecycle leaves no registry records");
    std::cout<<"Sparse Unicode glyph and font-alias checks passed.\n";
}
