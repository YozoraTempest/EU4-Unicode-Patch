#include "legacy_text.hpp"
#include "unicode_text.hpp"
#include <array>
#include <cstdint>

namespace eu4unicode {
namespace {
constexpr std::array<std::uint32_t,32> cp1252={
    0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,
    0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
    0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
    0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
bool payload_byte(std::uint32_t scalar,unsigned& byte) {
    if(scalar<0x80||(scalar>=0xa0&&scalar<=0xff)) {byte=scalar;return true;}
    for(unsigned i=0;i<cp1252.size();++i) if(cp1252[i]==scalar) {byte=0x80+i;return true;}
    return false;
}
}
bool contains_legacy_escape(std::string_view text) noexcept {
    return text.find_first_of("\x10\x11\x12\x13")!=std::string_view::npos;
}
LegacyText decode_legacy_text(std::string_view text,LegacyPayload payload) {
    LegacyText result;
    if(!contains_legacy_escape(text)&&payload==LegacyPayload::raw_bytes) {
        result.text.assign(text);return result;
    }
    result.text.reserve(text.size());
    std::uint32_t high_surrogate=0;
    std::size_t surrogate_offset=0;
    const auto fail=[&](LegacyError error,std::size_t offset) {
        result.error=error;result.error_offset=offset;result.text.clear();
    };
    std::size_t i=0;
    while(i<text.size()) {
        const auto start=i;
        const auto marker=static_cast<unsigned char>(text[i]);
        if(marker<0x10||marker>0x13) {
            if(high_surrogate) {fail(LegacyError::unpaired_surrogate,surrogate_offset);return result;}
            const auto scalar=decode(text.substr(i));
            if(scalar.valid) {
                result.text.append(text.substr(i,scalar.bytes));i+=scalar.bytes;
            } else if(payload==LegacyPayload::raw_bytes) {
                // Invalid single-byte text is CP1252 only within a value that
                // contains the explicitly recognized legacy protocol.
                result.text+=encode(marker>=0x80&&marker<0xa0?cp1252[marker-0x80]:marker);++i;
            } else {fail(LegacyError::invalid_utf8,i);return result;}
            continue;
        }
        ++i;
        unsigned bytes[2]{};
        for(auto& byte:bytes) {
            if(i==text.size()) {fail(LegacyError::truncated_escape,start);return result;}
            if(payload==LegacyPayload::raw_bytes) byte=static_cast<unsigned char>(text[i++]);
            else {
                const auto scalar=decode(text.substr(i));
                if(!scalar.valid) {fail(LegacyError::invalid_utf8,i);return result;}
                if(!payload_byte(scalar.value,byte)) {fail(LegacyError::invalid_payload,i);return result;}
                i+=scalar.bytes;
            }
        }
        constexpr int shifts[]={0,-0xe,0x900,0x8f2};
        const int unit=static_cast<int>((bytes[1]<<8)+bytes[0])+shifts[marker-0x10];
        if(unit<0x100||unit>0xffff) {fail(LegacyError::invalid_unit,start);return result;}
        auto scalar=static_cast<std::uint32_t>(unit);
        ++result.sequences;
        if(scalar>0xe100&&scalar<0xea00) {scalar-=0xe000;++result.relocated;}
        if(high_surrogate) {
            if(scalar<0xdc00||scalar>0xdfff) {fail(LegacyError::unpaired_surrogate,surrogate_offset);return result;}
            result.text+=encode(0x10000+((high_surrogate-0xd800)<<10)+scalar-0xdc00);
            high_surrogate=0;
        } else if(scalar>=0xd800&&scalar<=0xdbff) {
            high_surrogate=scalar;surrogate_offset=start;
        } else if(scalar>=0xdc00&&scalar<=0xdfff) {
            fail(LegacyError::unpaired_surrogate,start);return result;
        } else result.text+=encode(scalar);
    }
    if(high_surrogate) fail(LegacyError::unpaired_surrogate,surrogate_offset);
    return result;
}
const char* legacy_error_name(LegacyError error) noexcept {
    switch(error) {
    case LegacyError::none:return "none";
    case LegacyError::invalid_utf8:return "invalid UTF-8";
    case LegacyError::truncated_escape:return "truncated legacy escape";
    case LegacyError::invalid_payload:return "invalid CP1252 payload character";
    case LegacyError::invalid_unit:return "invalid legacy UTF-16 unit";
    case LegacyError::unpaired_surrogate:return "unpaired legacy surrogate";
    }
    return "unknown legacy decoding error";
}
}
