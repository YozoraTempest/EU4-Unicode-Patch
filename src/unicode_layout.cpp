#include "unicode_layout.hpp"
#include "unicode_text.hpp"
#include "unicode_services.hpp"
#include <windows.h>
#include <dwrite_3.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <icu.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace eu4unicode {
class GlyphFace {
public:
    Microsoft::WRL::ComPtr<IDWriteFontFace> native;
    explicit GlyphFace(IDWriteFontFace* value):native(value) {}
};
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT status) {
    if(FAILED(status)) throw std::runtime_error("Windows text API failed: "+std::to_string(static_cast<unsigned long>(status)));
}
std::wstring to_wide(std::string_view value) {
    if(value.empty()) return {};
    if(value.size()>static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::length_error("UTF-8 input exceeds Windows conversion range");
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!size) throw std::invalid_argument("Invalid UTF-8 text");
    std::wstring result(static_cast<std::size_t>(size),L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size))
        throw std::runtime_error("UTF-8 conversion failed");
    return result;
}
std::string to_utf8(std::wstring_view value) {
    if(value.empty()) return {};
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!size) throw std::runtime_error("Invalid UTF-16 from Windows text API");
    std::string result(static_cast<std::size_t>(size),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr))
        throw std::runtime_error("UTF-16 conversion failed");
    return result;
}
std::string family_name(IDWriteFontFace* face) {
    ComPtr<IDWriteFontFace3> face3;
    if(FAILED(face->QueryInterface(IID_PPV_ARGS(&face3)))) return {};
    ComPtr<IDWriteLocalizedStrings> names;
    checked(face3->GetFamilyNames(&names));
    UINT32 index=0,length=0; BOOL exists=FALSE;
    checked(names->FindLocaleName(L"en-us",&index,&exists));
    if(!exists) index=0;
    checked(names->GetStringLength(index,&length));
    std::wstring name(length+1,L'\0');
    checked(names->GetString(index,name.data(),length+1));
    name.resize(length);
    return to_utf8(name);
}
class StyleEffect final : public IUnknown {
    std::atomic<ULONG> references_{1};
public:
    std::uint32_t style;
    explicit StyleEffect(std::uint32_t value):style(value) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** value) override {
        if(!value) return E_POINTER;
        *value=nullptr;
        if(id!=__uuidof(IUnknown)) return E_NOINTERFACE;
        *value=static_cast<IUnknown*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count=--references_;if(!count) delete this;return count; }
};
class InlineObject final : public IDWriteInlineObject {
    std::atomic<ULONG> references_{1};
public:
    TextInlineObject value;
    explicit InlineObject(TextInlineObject object):value(object) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** result) override {
        if(!result) return E_POINTER;
        *result=nullptr;
        if(id!=__uuidof(IUnknown)&&id!=__uuidof(IDWriteInlineObject)) return E_NOINTERFACE;
        *result=static_cast<IDWriteInlineObject*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count=--references_;if(!count) delete this;return count; }
    HRESULT STDMETHODCALLTYPE Draw(void*,IDWriteTextRenderer*,FLOAT,FLOAT,BOOL,BOOL,IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override {
        if(!metrics) return E_POINTER;
        *metrics={value.width,value.height,value.baseline,FALSE};return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS* metrics) override {
        if(!metrics) return E_POINTER;
        *metrics={};return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBreakConditions(DWRITE_BREAK_CONDITION* before,DWRITE_BREAK_CONDITION* after) override {
        if(!before||!after) return E_POINTER;
        *before=*after=DWRITE_BREAK_CONDITION_NEUTRAL;return S_OK;
    }
};
std::uint32_t drawing_style(IUnknown* effect) { return effect?static_cast<StyleEffect*>(effect)->style:0; }
class RunCollector final : public IDWriteTextRenderer {
public:
    std::vector<GlyphRun> runs;
    std::vector<InlinePlacement> objects;
    const std::vector<std::size_t>& byte_positions;
    explicit RunCollector(const std::vector<std::size_t>& positions):byte_positions(positions) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** value) override {
        if(!value) return E_POINTER;
        *value=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDWriteTextRenderer)||id==__uuidof(IDWritePixelSnapping)) {
            *value=static_cast<IDWriteTextRenderer*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,BOOL* value) override { *value=TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,DWRITE_MATRIX* value) override { *value={1,0,0,1,0,0}; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*,FLOAT* value) override { *value=1; return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,FLOAT x,FLOAT y,DWRITE_MEASURING_MODE measuring,
        const DWRITE_GLYPH_RUN* run,const DWRITE_GLYPH_RUN_DESCRIPTION* description,IUnknown* effect) override {
        try {
            const auto start=byte_positions.at(description->textPosition);
            const auto end=byte_positions.at(description->textPosition+description->stringLength);
            GlyphRun result{};
            result.font_family=family_name(run->fontFace);
            result.bidi_level=run->bidiLevel;result.text_start=start;result.text_length=end-start;
            result.glyphs.assign(run->glyphIndices,run->glyphIndices+run->glyphCount);
            result.advances.assign(run->glyphAdvances,run->glyphAdvances+run->glyphCount);
            result.baseline_x=x;result.baseline_y=y;result.em_size=run->fontEmSize;
            result.sideways=run->isSideways!=FALSE;
            result.face=std::make_shared<GlyphFace>(run->fontFace);
            result.measuring=static_cast<GlyphMeasure>(measuring);
            result.style=drawing_style(effect);
            for(UINT32 index=0;index<run->glyphCount;++index) {
                const auto offset=run->glyphOffsets?run->glyphOffsets[index]:DWRITE_GLYPH_OFFSET{};
                result.offsets.push_back({offset.advanceOffset,offset.ascenderOffset});
            }
            std::vector<UINT32> glyph_starts;
            for(UINT32 index=0;index<description->stringLength;++index)
                glyph_starts.push_back(description->clusterMap[index]);
            glyph_starts.push_back(run->glyphCount);
            std::sort(glyph_starts.begin(),glyph_starts.end());
            glyph_starts.erase(std::unique(glyph_starts.begin(),glyph_starts.end()),glyph_starts.end());
            for(UINT32 index=0;index<description->stringLength;) {
                const auto first=description->clusterMap[index];
                auto next=index+1;
                while(next<description->stringLength&&description->clusterMap[next]==first) ++next;
                const auto byte_start=byte_positions.at(description->textPosition+index);
                const auto byte_end=byte_positions.at(description->textPosition+next);
                const auto last=std::upper_bound(glyph_starts.begin(),glyph_starts.end(),first);
                if(first>=run->glyphCount||last==glyph_starts.end()||byte_end<=byte_start)
                    throw std::runtime_error("Invalid DirectWrite cluster mapping");
                result.clusters.push_back({byte_start,byte_end-byte_start,first,*last-first});
                index=next;
            }
            runs.push_back(std::move(result));
            return S_OK;
        } catch(...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*,FLOAT,FLOAT,const DWRITE_UNDERLINE*,IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,FLOAT,FLOAT,const DWRITE_STRIKETHROUGH*,IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,FLOAT x,FLOAT y,IDWriteInlineObject* object,BOOL sideways,BOOL rtl,IUnknown* effect) override {
        try {
            if(sideways||!object) return E_INVALIDARG;
            const auto& value=static_cast<InlineObject*>(object)->value;
            objects.push_back({value.text_start,value.text_length,x,y,value.width,value.height,value.id,drawing_style(effect),rtl!=FALSE});
            return S_OK;
        } catch(...) { return E_FAIL; }
    }
};
struct ComApartment {
    HRESULT status=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ComApartment() { if(FAILED(status)&&status!=RPC_E_CHANGED_MODE) checked(status); }
    ~ComApartment() { if(SUCCEEDED(status)) CoUninitialize(); }
};
}
struct TextFonts::Impl {
    ComPtr<IDWriteFontCollection1> collection;
    ComPtr<IDWriteFontFallback> fallback;
    std::vector<std::wstring> names;
    bool system_first;
    explicit Impl(const std::vector<std::filesystem::path>& files,bool prefer_system):system_first(prefer_system) {
        if(files.empty()) throw std::invalid_argument("Font collection requires files");
        ComPtr<IDWriteFactory3> factory;
        checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory3),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
        ComPtr<IDWriteFontSetBuilder> builder;
        checked(factory->CreateFontSetBuilder(&builder));
        for(const auto& file:files) {
            ComPtr<IDWriteFontFile> native;
            checked(factory->CreateFontFileReference(std::filesystem::absolute(file).c_str(),nullptr,&native));
            BOOL supported=FALSE; DWRITE_FONT_FILE_TYPE type{}; DWRITE_FONT_FACE_TYPE face{}; UINT32 count=0;
            checked(native->Analyze(&supported,&type,&face,&count));
            if(!supported||!count) throw std::invalid_argument("Unsupported font file");
            for(UINT32 index=0;index<count;++index) {
                ComPtr<IDWriteFontFaceReference> reference;
                checked(factory->CreateFontFaceReference(native.Get(),index,DWRITE_FONT_SIMULATIONS_NONE,&reference));
                checked(builder->AddFontFaceReference(reference.Get()));
            }
        }
        ComPtr<IDWriteFontSet> set;
        checked(builder->CreateFontSet(&set));
        checked(factory->CreateFontCollectionFromFontSet(set.Get(),&collection));
        // Preserve caller file order for overlapping CJK coverage.
        for(const auto& file:files) {
            ComPtr<IDWriteFontFile> native;
            checked(factory->CreateFontFileReference(std::filesystem::absolute(file).c_str(),nullptr,&native));
            BOOL supported=FALSE; DWRITE_FONT_FILE_TYPE type{}; DWRITE_FONT_FACE_TYPE face{}; UINT32 count=0;
            checked(native->Analyze(&supported,&type,&face,&count));
            for(UINT32 index=0;index<count;++index) {
                ComPtr<IDWriteFontFace> font;
                IDWriteFontFile* list[]={native.Get()};
                checked(factory->CreateFontFace(face,1,list,index,DWRITE_FONT_SIMULATIONS_NONE,&font));
                const auto name=to_wide(family_name(font.Get()));
                if(std::find(names.begin(),names.end(),name)==names.end()) names.push_back(name);
            }
        }
        ComPtr<IDWriteFontFallbackBuilder> fallback_builder;
        checked(factory->CreateFontFallbackBuilder(&fallback_builder));
        ComPtr<IDWriteFontFallback> system;
        checked(factory->GetSystemFontFallback(&system));
        if(system_first) checked(fallback_builder->AddMappings(system.Get()));
        const DWRITE_UNICODE_RANGE range{0,0x10ffff};
        std::vector<const wchar_t*> targets;
        for(const auto& name:names) targets.push_back(name.c_str());
        checked(fallback_builder->AddMapping(&range,1,targets.data(),static_cast<UINT32>(targets.size()),collection.Get()));
        if(!system_first) checked(fallback_builder->AddMappings(system.Get()));
        checked(fallback_builder->CreateFontFallback(&fallback));
    }
};
TextFonts::TextFonts(const std::vector<std::filesystem::path>& files,bool system_first):impl_(std::make_unique<Impl>(files,system_first)) {}
TextFonts::~TextFonts()=default;
std::vector<std::string> TextFonts::families() const {
    std::vector<std::string> result;
    for(const auto& name:impl_->names) result.push_back(to_utf8(name));
    return result;
}
struct TextLayout::Impl {
    std::string text;
    std::wstring wide;
    std::vector<std::size_t> byte_positions;
    ComPtr<IDWriteFactory2> factory;
    ComPtr<IDWriteTextLayout2> layout;
    std::shared_ptr<const TextFonts> fonts;
    bool application_drawing=false;
    Impl(std::string_view value,float size,float width,float height,std::wstring_view family,
         std::shared_ptr<const TextFonts> custom,TextLayoutOptions options):text(value),wide(to_wide(value)),fonts(std::move(custom)) {
        if(!std::isfinite(size)||!std::isfinite(width)||!std::isfinite(height)||size<=0||width<=0||height<=0)
            throw std::invalid_argument("Layout dimensions must be positive finite values");
        if(!std::isfinite(options.line_height)||options.line_height<0)
            throw std::invalid_argument("Line spacing must be finite and nonnegative");
        if(wide.size()>INT32_MAX) throw std::length_error("Text exceeds paragraph analysis range");
        auto remaining=value;
        std::size_t offset=0;
        while(!remaining.empty()) {
            const auto scalar=decode(remaining);
            byte_positions.push_back(offset);
            if(scalar.value>0xffff) byte_positions.push_back(offset);
            offset+=scalar.bytes;
            remaining.remove_prefix(scalar.bytes);
        }
        byte_positions.push_back(offset);
        checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory2),reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
        ComPtr<IDWriteTextFormat> format;
        const bool file_first=fonts&&!fonts->impl_->system_first;
        const std::wstring name=file_first?fonts->impl_->names.front():std::wstring(family);
        checked(factory->CreateTextFormat(name.c_str(),file_first?fonts->impl_->collection.Get():nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&format));
        const bool rtl=options.direction==TextDirection::RightToLeft||
            (options.direction==TextDirection::Automatic&&ubidi_getBaseDirection(
                reinterpret_cast<const UChar*>(wide.data()),static_cast<int32_t>(wide.size()))==UBIDI_RTL);
        checked(format->SetReadingDirection(rtl?DWRITE_READING_DIRECTION_RIGHT_TO_LEFT:DWRITE_READING_DIRECTION_LEFT_TO_RIGHT));
        // Keep a physical left origin. The native UI applies its own alignment
        // to the measured paragraph box after DirectWrite orders its glyphs.
        checked(format->SetTextAlignment(rtl?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING));
        checked(format->SetWordWrapping(options.wrap?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP));
        if(options.line_height>0)
            checked(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,options.line_height,size*.8f));
        ComPtr<IDWriteTextLayout> base;
        checked(factory->CreateTextLayout(wide.data(),static_cast<UINT32>(wide.size()),format.Get(),width,height,&base));
        checked(base.As(&layout));
        ComPtr<IDWriteFontFallback> fallback;
        checked(factory->GetSystemFontFallback(&fallback));
        checked(layout->SetFontFallback(fonts?fonts->impl_->fallback.Get():fallback.Get()));
        auto range=[this](std::size_t start,std::size_t length) {
            if(start>text.size()||length>text.size()-start) throw std::invalid_argument("Layout range exceeds text");
            const auto first=std::lower_bound(byte_positions.begin(),byte_positions.end(),start);
            const auto last=std::lower_bound(byte_positions.begin(),byte_positions.end(),start+length);
            if(first==byte_positions.end()||last==byte_positions.end()||*first!=start||*last!=start+length)
                throw std::invalid_argument("Layout range splits a UTF-8 scalar");
            return DWRITE_TEXT_RANGE{static_cast<UINT32>(first-byte_positions.begin()),static_cast<UINT32>(last-first)};
        };
        for(const auto& style:options.styles) if(style.style) {
            ComPtr<IUnknown> effect;effect.Attach(new StyleEffect(style.style));
            checked(layout->SetDrawingEffect(effect.Get(),range(style.text_start,style.text_length)));
            application_drawing=true;
        }
        for(const auto& value:options.objects) {
            if(!std::isfinite(value.width)||!std::isfinite(value.height)||!std::isfinite(value.baseline)||
               value.width<0||value.height<=0||value.baseline<0||value.baseline>value.height)
                throw std::invalid_argument("Invalid inline object metrics");
            ComPtr<IDWriteInlineObject> object;object.Attach(new InlineObject(value));
            checked(layout->SetInlineObject(object.Get(),range(value.text_start,value.text_length)));
            application_drawing=true;
        }
    }
};
TextLayout::TextLayout(std::string_view text,float size,float width,float height,std::wstring_view family,
    std::shared_ptr<const TextFonts> fonts,TextLayoutOptions options):impl_(std::make_unique<Impl>(text,size,width,height,family,std::move(fonts),options)) {}
TextLayout::~TextLayout()=default;
TextLayout::TextLayout(TextLayout&&) noexcept=default;
TextLayout& TextLayout::operator=(TextLayout&&) noexcept=default;
LayoutMetrics TextLayout::metrics() const {
    DWRITE_TEXT_METRICS value{};
    checked(impl_->layout->GetMetrics(&value));
    return {value.widthIncludingTrailingWhitespace,value.height,value.lineCount};
}
std::vector<LayoutLine> TextLayout::lines() const {
    std::vector<DWRITE_LINE_METRICS> native(metrics().lines);
    UINT32 count=0;
    checked(impl_->layout->GetLineMetrics(native.data(),static_cast<UINT32>(native.size()),&count));
    std::vector<LayoutLine> result;
    UINT32 start=0;float top=0;
    for(UINT32 index=0;index<count;++index) {
        const auto& line=native[index];
        const auto byte_start=impl_->byte_positions.at(start);
        const auto byte_end=impl_->byte_positions.at(start+line.length);
        const auto content_end=impl_->byte_positions.at(start+line.length-line.newlineLength);
        float width=0;
        if(line.length>line.newlineLength) {
            UINT32 boxes=0;
            const auto status=impl_->layout->HitTestTextRange(start,line.length-line.newlineLength,0,0,nullptr,0,&boxes);
            if(status!=E_NOT_SUFFICIENT_BUFFER) checked(status);
            std::vector<DWRITE_HIT_TEST_METRICS> hits(boxes);
            if(boxes) checked(impl_->layout->HitTestTextRange(start,line.length-line.newlineLength,0,0,hits.data(),boxes,&boxes));
            float left=(std::numeric_limits<float>::max)(),right=-(std::numeric_limits<float>::max)();
            for(const auto& hit:hits) { left=(std::min)(left,hit.left);right=(std::max)(right,hit.left+hit.width); }
            if(boxes) width=right-left;
        }
        result.push_back({byte_start,byte_end-byte_start,byte_end-content_end,width,top,line.height,line.baseline});
        top+=line.height;start+=line.length;
    }
    return result;
}
std::vector<GlyphRun> TextLayout::glyph_runs() const {
    return drawing().runs;
}
LayoutDrawing TextLayout::drawing() const {
    RunCollector collector(impl_->byte_positions);
    checked(impl_->layout->Draw(nullptr,&collector,0,0));
    return {std::move(collector.runs),std::move(collector.objects)};
}
HitPosition TextLayout::hit_test(float x,float y) const {
    BOOL trailing=FALSE,inside=FALSE;
    DWRITE_HIT_TEST_METRICS hit{};
    checked(impl_->layout->HitTestPoint(x,y,&trailing,&inside,&hit));
    const auto index=hit.textPosition+(trailing?hit.length:0);
    auto offset=impl_->byte_positions.at(index);
    const auto boundaries=grapheme_boundaries(impl_->text);
    auto position=std::lower_bound(boundaries.begin(),boundaries.end(),offset);
    if(position!=boundaries.end()&&*position!=offset) {
        if(trailing) offset=*position;
        else if(position!=boundaries.begin()) offset=*--position;
    }
    return {offset,inside!=FALSE};
}
GlyphBitmap rasterize_glyph_run(const GlyphRun& run) {
    if(!run.face||!std::isfinite(run.em_size)||run.em_size<=0||
       !std::isfinite(run.baseline_x)||!std::isfinite(run.baseline_y)||
       static_cast<std::uint32_t>(run.measuring)>2||
       run.glyphs.size()>UINT32_MAX||run.advances.size()!=run.glyphs.size()||
       run.offsets.size()!=run.glyphs.size())
        throw std::invalid_argument("Incomplete shaped glyph run");
    if(run.em_size>16384) throw std::length_error("Shaped glyph size exceeds raster budget");
    std::vector<DWRITE_GLYPH_OFFSET> offsets;
    offsets.reserve(run.offsets.size());
    for(std::size_t index=0;index<run.glyphs.size();++index) {
        const auto offset=run.offsets[index];
        if(!std::isfinite(run.advances[index])||!std::isfinite(offset.advance)||!std::isfinite(offset.ascender))
            throw std::invalid_argument("Glyph geometry must be finite");
        offsets.push_back({offset.advance,offset.ascender});
    }
    if(run.glyphs.empty()) return {};
    DWRITE_GLYPH_RUN native{run.face->native.Get(),run.em_size,static_cast<UINT32>(run.glyphs.size()),
        run.glyphs.data(),run.advances.data(),offsets.data(),run.sideways?TRUE:FALSE,run.bidi_level};
    ComPtr<IDWriteFactory2> factory;
    checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory2),
        reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
    ComPtr<IDWriteGlyphRunAnalysis> analysis;
    checked(factory->CreateGlyphRunAnalysis(&native,nullptr,DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
        static_cast<DWRITE_MEASURING_MODE>(run.measuring),DWRITE_GRID_FIT_MODE_DISABLED,DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
        run.baseline_x-std::floor(run.baseline_x),run.baseline_y-std::floor(run.baseline_y),&analysis));
    RECT bounds{};
    checked(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1,&bounds));
    const auto width=static_cast<std::int64_t>(bounds.right)-bounds.left;
    const auto height=static_cast<std::int64_t>(bounds.bottom)-bounds.top;
    if(width<0||height<0||width>16384||height>16384||width*height>64*1024*1024)
        throw std::length_error("Shaped glyph bitmap exceeds raster budget");
    GlyphBitmap result{static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),bounds.left,bounds.top,{}};
    result.alpha.resize(static_cast<std::size_t>(width*height));
    if(!result.alpha.empty())
        checked(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1,&bounds,result.alpha.data(),
            static_cast<UINT32>(result.alpha.size())));
    return result;
}
RasterImage TextLayout::rasterize() const {
    if(impl_->application_drawing) throw std::logic_error("Application text effects require the custom drawing renderer");
    ComApartment apartment;
    const auto size=metrics();
    if(size.width>16320 || size.height>16320) throw std::length_error("Glyph bitmap exceeds dimension limit");
    const auto width=static_cast<UINT>(std::ceil((std::max)(size.width,1.0f)))+32;
    const auto height=static_cast<UINT>(std::ceil((std::max)(size.height,1.0f)))+32;
    ComPtr<IWICImagingFactory> wic;
    checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;
    checked(wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    ComPtr<ID2D1Factory> factory;
    checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));
    ComPtr<ID2D1RenderTarget> target;
    checked(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target));
    ComPtr<ID2D1SolidColorBrush> brush;
    checked(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White),&brush));
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0,0,0,0));
    target->DrawTextLayout(D2D1::Point2F(16,16),impl_->layout.Get(),brush.Get());
    checked(target->EndDraw());
    std::vector<DWRITE_LINE_METRICS> lines(metrics().lines);
    UINT32 actual=0;
    checked(impl_->layout->GetLineMetrics(lines.data(),static_cast<UINT32>(lines.size()),&actual));
    RasterImage result{width,height,actual?lines[0].baseline:0,{}};
    result.pixels.resize(static_cast<std::size_t>(width)*height*4);
    checked(bitmap->CopyPixels(nullptr,width*4,static_cast<UINT>(result.pixels.size()),result.pixels.data()));
    return result;
}
void TextLayout::render_png(const std::filesystem::path& path) const {
    if(impl_->application_drawing) throw std::logic_error("Application text effects require the custom drawing renderer");
    ComApartment apartment;
    const auto size=metrics();
    if(size.width>16320 || size.height>16320) throw std::length_error("Diagnostic bitmap exceeds dimension limit");
    const auto width=static_cast<UINT>(std::ceil((std::max)(size.width,1.0f)))+32;
    const auto height=static_cast<UINT>(std::ceil((std::max)(size.height,1.0f)))+32;
    ComPtr<IWICImagingFactory> wic;
    checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IWICBitmap> bitmap;
    checked(wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    ComPtr<ID2D1Factory> factory;
    checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));
    ComPtr<ID2D1RenderTarget> target;
    checked(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target));
    ComPtr<ID2D1SolidColorBrush> brush;
    checked(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black),&brush));
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->BeginDraw();
    target->Clear(D2D1::ColorF(D2D1::ColorF::White));
    target->DrawTextLayout(D2D1::Point2F(16,16),impl_->layout.Get(),brush.Get());
    checked(target->EndDraw());
    ComPtr<IWICStream> stream;
    checked(wic->CreateStream(&stream));
    checked(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    checked(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
    checked(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    checked(encoder->CreateNewFrame(&frame,nullptr));
    checked(frame->Initialize(nullptr));
    checked(frame->SetSize(width,height));
    auto format=GUID_WICPixelFormat32bppBGRA;
    checked(frame->SetPixelFormat(&format));
    checked(frame->WriteSource(bitmap.Get(),nullptr));
    checked(frame->Commit());
    checked(encoder->Commit());
}
}
