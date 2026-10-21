#include "linux/ime_presentation.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace eu4unicode::linux_native {
namespace {
std::mutex mutex;
std::shared_ptr<const ImePresentation> presentation;
std::atomic<std::uint64_t> underlines=0,selections=0;
std::atomic<std::uint64_t> state_checks=0,state_mismatches=0;
// The verified engine already uses compatibility-profile immediate drawing in
// GfxDraw2dLines. Decorations own no texture, native sprite or deferred buffer.
struct Gl {
    void(*get)(unsigned,int*);
    void(*get_float)(unsigned,float*);
    void(*push_attrib)(unsigned);
    void(*pop_attrib)();
    void(*matrix_mode)(unsigned);
    void(*push_matrix)();
    void(*pop_matrix)();
    void(*load_identity)();
    void(*ortho)(double,double,double,double,double,double);
    void(*use_program)(unsigned);
    void(*active_texture)(unsigned);
    void(*disable)(unsigned);
    void(*enable)(unsigned);
    void(*blend_func)(unsigned,unsigned);
    void(*scissor)(int,int,int,int);
    void(*begin)(unsigned);
    void(*end)();
    void(*color)(float,float,float,float);
    void(*vertex)(float,float);
} gl{};
template<class T> void resolve(T& function,const char* name) {
    function=reinterpret_cast<T>(dlsym(RTLD_DEFAULT,name));
    if(!function) throw std::runtime_error(std::string("Linux IME GL API unavailable: ")+name);
}
struct State {
    std::array<int,28> integers{};
    std::array<float,36> floats{};
    std::vector<int> texture_enables;
    State() {
        constexpr std::array<unsigned,16> names{0x8b8d,0x0ba0,0x84e0,0x0be2,
            0x0b71,0x0b44,0x0b50,0x0bc0,0x0b60,0x0bf2,0x0c11,0x0b90,
            0x80c9,0x80c8,0x80cb,0x80ca};
        for(std::size_t i=0;i<names.size();++i) gl.get(names[i],&integers[i]);
        gl.get(0x0ba2,integers.data()+16);gl.get(0x0c10,integers.data()+20);gl.get(0x0c23,integers.data()+24);
        gl.get_float(0x0ba6,floats.data());gl.get_float(0x0ba7,floats.data()+16);gl.get_float(0x0b00,floats.data()+32);
        int units=0;gl.get(0x84e2,&units);
        if(units<1||units>32) throw std::runtime_error("Unexpected native GL texture-unit count");
        texture_enables.resize(units*4);
        for(int unit=0;unit<units;++unit) {
            gl.active_texture(0x84c0+unit);
            const std::array<unsigned,4> targets{0x0de0,0x0de1,0x806f,0x8513};
            for(std::size_t i=0;i<targets.size();++i) gl.get(targets[i],&texture_enables[unit*4+i]);
        }
        gl.active_texture(integers[2]);
    }
    bool same(const State& other) const noexcept {
        return integers==other.integers&&texture_enables==other.texture_enables&&
            !std::memcmp(floats.data(),other.floats.data(),sizeof(floats));
    }
};
struct Paint {
    int program=0,mode=0,active=0;
    explicit Paint(const std::array<int,4>& viewport) {
        gl.get(0x8b8d,&program);gl.get(0x0ba0,&mode);gl.get(0x84e0,&active);
        // Restore current color, all fixed-function texture enables, blending,
        // clipping and transform mode. Each matrix has its own saved stack.
        gl.push_attrib(0x1|0x100|0x1000|0x2000|0x4000|0x40000|0x80000);
        gl.matrix_mode(0x1701);gl.push_matrix();gl.load_identity();
        gl.ortho(viewport[0],viewport[0]+viewport[2],viewport[1]+viewport[3],viewport[1],-1,1);
        gl.matrix_mode(0x1700);gl.push_matrix();gl.load_identity();
        gl.use_program(0);
        int units=0;gl.get(0x84e2,&units);
        for(int unit=0;unit<units;++unit) {
            gl.active_texture(0x84c0+unit);
            for(const auto target:{0x0de0u,0x0de1u,0x806fu,0x8513u}) gl.disable(target);
        }
        for(const auto state:{0x0b50u,0x0b71u,0x0b44u,0x0bc0u,0x0b60u,0x0bf2u}) gl.disable(state);
        gl.enable(0x0be2);gl.blend_func(0x0302,0x0303);
    }
    ~Paint() {
        gl.matrix_mode(0x1700);gl.pop_matrix();
        gl.matrix_mode(0x1701);gl.pop_matrix();
        gl.pop_attrib();gl.matrix_mode(mode);gl.use_program(program);gl.active_texture(active);
    }
};
void rectangle(int left,int top,int right,int bottom,const std::array<float,4>& color) {
    if(left>=right||top>=bottom) return;
    gl.color(color[0],color[1],color[2],color[3]);gl.begin(0x0007);
    gl.vertex(left,top);gl.vertex(right,top);gl.vertex(right,bottom);gl.vertex(left,bottom);gl.end();
}
void paint(void* sprite,std::size_t text_offset,bool background) noexcept {
    try {
        std::shared_ptr<const ImePresentation> current;
        {std::lock_guard lock(mutex);current=presentation;}
        if(!current||current->sprite!=sprite||current->width<=0||current->height<=0) return;
        // A newer main-thread update must not decorate an older native draw.
        // The engine owns this live sprite and source for the entire callback.
        const auto& source=*reinterpret_cast<const String*>(static_cast<char*>(sprite)+text_offset);
        if(source.view()!=current->text) return;
        std::array<int,4> viewport{};gl.get(0x0ba2,viewport.data());
        if(viewport[2]<=0||viewport[3]<=0) return;
        int enabled=0;gl.get(0x0c11,&enabled);
        std::array<int,4> clip{current->x,viewport[1]+viewport[3]-(current->y+current->height),current->width,current->height};
        if(enabled) {
            std::array<int,4> native{};gl.get(0x0c10,native.data());
            const auto right=std::min(clip[0]+clip[2],native[0]+native[2]);
            const auto top=std::min(clip[1]+clip[3],native[1]+native[3]);
            clip[0]=std::max(clip[0],native[0]);clip[1]=std::max(clip[1],native[1]);
            clip[2]=std::max(0,right-clip[0]);clip[3]=std::max(0,top-clip[1]);
        }
        if(!clip[2]||!clip[3]) return;
        // Verify restoration on the first real native draws. This checks the
        // live program, matrices, color, clips, blend factors and every legacy
        // texture unit, instead of treating a surviving UI as sufficient proof.
        const bool checking=state_checks.load(std::memory_order_relaxed)<32;
        const auto before=checking?std::make_unique<State>():nullptr;
        {
            Paint scope(viewport);
            gl.enable(0x0c11);gl.scissor(clip[0],clip[1],clip[2],clip[3]);
            const auto left=current->x,top=current->y;
            if(background) {
                for(const auto& range:current->selections) if(range.end>range.begin) {
                    rectangle(left+range.begin,top,left+range.end,
                        top+current->height,{0.22f,0.42f,0.68f,0.65f});++selections;
                }
            } else for(const auto& range:current->underlines) if(range.end>range.begin) {
                rectangle(left+range.begin,top+current->height-current->thickness,
                    left+range.end,top+current->height,{1,1,1,1});++underlines;
            }
        }
        if(checking) {
            const State after;++state_checks;
            if(!before->same(after)&&state_mismatches.fetch_add(1)==0)
                log("Linux IME decoration GL state restoration mismatch");
        }
    } catch(...) {
        static std::atomic<bool> logged=false;
        if(!logged.exchange(true)) log("Linux IME decoration drawing failed");
    }
}
void instant_before(safetyhook::Context& c) {paint(reinterpret_cast<void*>(c.rbx),0x1a0,true);}
void instant_after(safetyhook::Context& c) {paint(reinterpret_cast<void*>(c.rbx),0x1a0,false);}
void texture_before(safetyhook::Context& c) {paint(reinterpret_cast<void*>(c.r13),0x220,true);}
void texture_after(safetyhook::Context& c) {paint(reinterpret_cast<void*>(c.r13-0x288),0x220,false);}
}
void publish_ime_presentation(ImePresentation value) {
    auto next=std::make_shared<const ImePresentation>(std::move(value));
    std::lock_guard lock(mutex);presentation=std::move(next);
}
void clear_ime_presentation() noexcept {
    try {std::lock_guard lock(mutex);presentation.reset();} catch(...) {}
}
void prepare_ime_presentation(Hooks& hooks) {
    resolve(gl.get,"glGetIntegerv");resolve(gl.push_attrib,"glPushAttrib");resolve(gl.pop_attrib,"glPopAttrib");
    resolve(gl.get_float,"glGetFloatv");
    resolve(gl.matrix_mode,"glMatrixMode");resolve(gl.push_matrix,"glPushMatrix");resolve(gl.pop_matrix,"glPopMatrix");
    resolve(gl.load_identity,"glLoadIdentity");resolve(gl.ortho,"glOrtho");resolve(gl.use_program,"glUseProgram");
    resolve(gl.active_texture,"glActiveTexture");resolve(gl.disable,"glDisable");resolve(gl.enable,"glEnable");
    resolve(gl.blend_func,"glBlendFunc");resolve(gl.scissor,"glScissor");resolve(gl.begin,"glBegin");
    resolve(gl.end,"glEnd");resolve(gl.color,"glColor4f");resolve(gl.vertex,"glVertex2f");
    // Inside the existing native draw/scissor boundaries, on the thread that
    // owns its GL context. No graphics object is created or retained.
    hooks.add_mid(0x208c130,instant_before);hooks.add_mid(0x208c19d,instant_after);
    hooks.add_mid(0x20d9ad4,texture_before);hooks.add_mid(0x20d9ae6,texture_after);
}
void report_ime_presentation_counts() noexcept {
    log("Linux IME decorations underlines=%llu selections=%llu state-checks=%llu state-mismatches=%llu",
        static_cast<unsigned long long>(underlines.load()),static_cast<unsigned long long>(selections.load()),
        static_cast<unsigned long long>(state_checks.load()),static_cast<unsigned long long>(state_mismatches.load()));
}
}
