#include "unicode_layout.hpp"
#include "unicode_text.hpp"
#include "unicode_services.hpp"
#include <windows.h>
#include <dwrite_3.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace eu4unicode {
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
class RunCollector final : public IDWriteTextRenderer {
public:
    std::vector<GlyphRun> runs;
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
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,FLOAT,FLOAT,DWRITE_MEASURING_MODE,
        const DWRITE_GLYPH_RUN* run,const DWRITE_GLYPH_RUN_DESCRIPTION* description,IUnknown*) override {
        try {
            const auto start=byte_positions.at(description->textPosition);
            const auto end=byte_positions.at(description->textPosition+description->stringLength);
            runs.push_back({family_name(run->fontFace),run->bidiLevel,start,end-start,
                {run->glyphIndices,run->glyphIndices+run->glyphCount},
                {run->glyphAdvances,run->glyphAdvances+run->glyphCount}});
            return S_OK;
        } catch(...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*,FLOAT,FLOAT,const DWRITE_UNDERLINE*,IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,FLOAT,FLOAT,const DWRITE_STRIKETHROUGH*,IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,FLOAT,FLOAT,IDWriteInlineObject*,BOOL,BOOL,IUnknown*) override { return E_NOTIMPL; }
};
struct ComApartment {
    HRESULT status=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ComApartment() { if(FAILED(status)&&status!=RPC_E_CHANGED_MODE) checked(status); }
    ~ComApartment() { if(SUCCEEDED(status)) CoUninitialize(); }
};
}
struct TextLayout::Impl {
    std::string text;
    std::wstring wide;
    std::vector<std::size_t> byte_positions;
    ComPtr<IDWriteFactory2> factory;
    ComPtr<IDWriteTextLayout2> layout;
    Impl(std::string_view value,float size,float width,float height,std::wstring_view family):text(value),wide(to_wide(value)) {
        if(!std::isfinite(size)||!std::isfinite(width)||!std::isfinite(height)||size<=0||width<=0||height<=0)
            throw std::invalid_argument("Layout dimensions must be positive finite values");
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
        const std::wstring name(family);
        checked(factory->CreateTextFormat(name.c_str(),nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&format));
        ComPtr<IDWriteTextLayout> base;
        checked(factory->CreateTextLayout(wide.data(),static_cast<UINT32>(wide.size()),format.Get(),width,height,&base));
        checked(base.As(&layout));
        ComPtr<IDWriteFontFallback> fallback;
        checked(factory->GetSystemFontFallback(&fallback));
        checked(layout->SetFontFallback(fallback.Get()));
    }
};
TextLayout::TextLayout(std::string_view text,float size,float width,float height,std::wstring_view family):impl_(std::make_unique<Impl>(text,size,width,height,family)) {}
TextLayout::~TextLayout()=default;
TextLayout::TextLayout(TextLayout&&) noexcept=default;
TextLayout& TextLayout::operator=(TextLayout&&) noexcept=default;
LayoutMetrics TextLayout::metrics() const {
    DWRITE_TEXT_METRICS value{};
    checked(impl_->layout->GetMetrics(&value));
    return {value.widthIncludingTrailingWhitespace,value.height,value.lineCount};
}
std::vector<GlyphRun> TextLayout::glyph_runs() const {
    RunCollector collector(impl_->byte_positions);
    checked(impl_->layout->Draw(nullptr,&collector,0,0));
    return std::move(collector.runs);
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
RasterImage TextLayout::rasterize() const {
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
