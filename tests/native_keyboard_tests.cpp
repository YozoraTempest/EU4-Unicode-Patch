#include "native_keyboard.hpp"
#include "native_ime.hpp"
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <stdexcept>
#include <vector>

namespace {
std::array<std::uint8_t,512> keys{};
std::vector<int> releases;
bool left_down=false,right_down=false,pumped=false,missing_state=false;
int held_modifier=0;
int key_count=512,pumps=0;
decltype(&GetKeyState) original_state=nullptr;
void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
SHORT WINAPI physical_state(int key) {
    if(key==VK_LWIN) return left_down?static_cast<SHORT>(0x8000):0;
    if(key==VK_RWIN) return right_down?static_cast<SHORT>(0x8000):0;
    if(key==VK_CONTROL||key==VK_MENU) return key==held_modifier?static_cast<SHORT>(0x8000):0;
    return original_state(key);
}
void pump(void*) { pumped=true;++pumps; }
void text_input() {}
std::uint8_t text_event_state(std::uint32_t,int) { return 1; }
const std::uint8_t* read_keys(int* count) {
    require(pumped,"Keyboard repair must run after the native event pump");
    *count=key_count;return missing_state?nullptr:keys.data();
}
int key_event(std::uint8_t pressed,int code) {
    require(pressed==0,"Keyboard repair must only release keys");
    releases.push_back(code);keys.at(code)=0;return 1;
}
struct Fixture {
    eu4unicode::NativeKeyboardPump saved_pump=eu4unicode::original_keyboard_pump;
    eu4unicode::NativeKeyboardState saved_state=eu4unicode::native_keyboard_state;
    eu4unicode::NativeKeyboardKey saved_key=eu4unicode::native_keyboard_key;
    void* target=nullptr;
    Fixture() {
        require(MH_Initialize()==MH_OK,"Cannot initialize keyboard state fixture");
        target=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetKeyState"));
        require(target&&MH_CreateHook(target,reinterpret_cast<void*>(physical_state),
            reinterpret_cast<void**>(&original_state))==MH_OK,"Cannot hook keyboard state reader");
        require(MH_EnableHook(target)==MH_OK,"Cannot enable keyboard state reader");
        eu4unicode::original_keyboard_pump=pump;
        eu4unicode::native_keyboard_state=read_keys;
        eu4unicode::native_keyboard_key=key_event;
    }
    ~Fixture() {
        MH_DisableHook(target);MH_RemoveHook(target);MH_Uninitialize();
        eu4unicode::original_keyboard_pump=saved_pump;
        eu4unicode::native_keyboard_state=saved_state;
        eu4unicode::native_keyboard_key=saved_key;
    }
    void tick() {
        pumped=false;const auto before=pumps;
        eu4unicode::pump_native_keyboard(nullptr);
        require(pumped&&pumps==before+1,"Native event pump must execute exactly once");
    }
};
}
void verify_native_keyboard() {
    Fixture fixture;keys.fill(0);releases.clear();fixture.tick();
    require(releases.empty(),"No key release is needed when Win is already up");
    keys[227]=1;left_down=true;fixture.tick();
    require(releases.empty()&&keys[227]==1,"A physically held Win key must remain pressed");
    keys[224]=keys[225]=keys[226]=keys[228]=keys[229]=keys[230]=keys[57]=keys[83]=1;
    left_down=false;fixture.tick();
    require(releases==std::vector<int>{227}&&keys[227]==0,"Missing left Win key-up must enter the native key queue");
    fixture.tick();require(releases.size()==1,"A repaired key must not produce repeated release events");
    for(const auto code:{224,225,226,228,229,230,57,83})
        require(keys[code]==1,"Repair must preserve other modifiers and lock keys");
    releases.clear();keys[227]=keys[231]=1;left_down=true;right_down=false;fixture.tick();
    require(releases==std::vector<int>{231}&&keys[227]==1,"Left and right Win keys must be repaired independently");
    left_down=false;fixture.tick();require(releases==std::vector<int>({231,227}),"The remaining Win key must release when physically up");
    releases.clear();keys[227]=keys[231]=1;fixture.tick();
    require(releases==std::vector<int>({227,231}),"Both missing Win key-ups must be repaired");
    releases.clear();keys[227]=keys[231]=1;key_count=231;fixture.tick();
    require(releases.empty(),"A short keyboard-state buffer must not be indexed");
    key_count=512;missing_state=true;fixture.tick();require(releases.empty(),"A missing state buffer must not be indexed");
    left_down=right_down=false;
    require(eu4unicode::native_speed_increase('='),"Plain equals must increase campaign speed");
    require(eu4unicode::native_speed_increase('+'),"Native plus must remain a speed shortcut");
    for(const auto character:{'-','a','\0'})
        require(!eu4unicode::native_speed_increase(character),"Other text must not increase speed");
    for(const auto modifier:{VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN}) {
        held_modifier=modifier;left_down=modifier==VK_LWIN;right_down=modifier==VK_RWIN;
        require(!eu4unicode::native_speed_increase('='),"Modified equals must not become a game shortcut");
        require(eu4unicode::native_speed_increase('+'),"Existing plus handling must remain native");
    }
    held_modifier=0;left_down=right_down=false;
    const auto start=eu4unicode::start_native_text_input,stop=eu4unicode::stop_native_text_input;
    const auto state=eu4unicode::native_text_event_state;
    eu4unicode::start_native_text_input=eu4unicode::stop_native_text_input=text_input;
    eu4unicode::native_text_event_state=text_event_state;
    int editor=0;
    eu4unicode::focus_native_editor(&editor);
    const auto editor_equals=eu4unicode::native_speed_increase('=');
    eu4unicode::blur_native_editor(&editor);
    eu4unicode::start_native_text_input=start;eu4unicode::stop_native_text_input=stop;
    eu4unicode::native_text_event_state=state;
    require(!editor_equals,"Equals must remain literal while an editor owns input");
    require(eu4unicode::native_speed_increase('='),"Speed shortcut must resume after editor blur");
}
