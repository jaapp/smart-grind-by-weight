from pathlib import Path
import tempfile,subprocess
base=Path(__file__).resolve().parents[1]
src=(base/'src/ui/ui_manager.cpp').read_text()
methods=src[src.index('bool UIManager::can_shift_display('):src.index('void UIManager::switch_to_state(')]
states=(base/'src/system/state_machine.h').read_text().replace('#pragma once','')
phases=(base/'src/controllers/grind_controller.h').read_text();phases=phases[phases.index('enum class GrindPhase {'):phases.index('\n};',phases.index('enum class GrindPhase {'))+3]
header=(base/'src/controllers/grind_controller.h').read_text()
start=header.index('    bool allows_idle_ui_updates() const {')
idle_method=header[start:header.index('\n    }',start)+6]
code=r'''
#include <cassert>
#include <iostream>
''' + states+phases+r'''
struct Touch{bool is_pressed(){return false;}};
struct Display{Touch touch;int suppressions=0;Touch* get_touch_driver(){return &touch;}void suppress_current_touch(){++suppressions;}};
struct Motor{bool running=false;bool is_grinding(){return running;}};
struct Hardware{Display display;Motor motor;Display* get_display(){return &display;}Motor* get_grinder(){return &motor;}};
struct Profile{int slot=1,option=-1;int get_current_profile(){return slot;}bool select_auto_profile(){if(option<0)return false;slot=option;return true;}};
struct Grind{GrindPhase phase=GrindPhase::IDLE;int acknowledgements=0;GrindPhase get_phase(){return phase;}
''' +idle_method+r'''
 void return_to_idle(){phase=GrindPhase::IDLE;++acknowledgements;}
};
struct Bluetooth{bool updating=false;bool is_updating(){return updating;}};
struct UIManager{Hardware* hardware_manager;StateMachine* state_machine;Profile* profile_controller;Grind* grind_controller;Bluetooth* bluetooth_manager;int current_tab=1,navigations=0;
bool can_shift_display()const;
};
''' +(base/'src/system/state_machine.cpp').read_text().replace('#include "state_machine.h"','').replace('#include <Arduino.h>','')+methods+r'''
int main(){Hardware hw;StateMachine state;Profile profile;Grind grind;Bluetooth ble;UIManager ui{&hw,&state,&profile,&grind,&ble};
for(int s=0;s<=static_cast<int>(UIState::OTA_UPDATE_FAILED);++s)for(int p=0;p<=static_cast<int>(GrindPhase::PURGE_CONFIRM);++p)for(int motor=0;motor<2;++motor)for(int ota=0;ota<2;++ota){
 const auto screen=static_cast<UIState>(s);state.init(screen);grind.phase=static_cast<GrindPhase>(p);hw.motor.running=motor;ble.updating=ota;
 const bool idle_screen=screen==UIState::READY||screen==UIState::MENU||screen==UIState::GRIND_COMPLETE||screen==UIState::GRIND_TIMEOUT;
 const bool safe=idle_screen&&grind.allows_idle_ui_updates()&&!motor&&!ota;
assert(ui.can_shift_display()==safe);
}
 std::cout<<"PASS: actual UI state/phase guard across all screens, phases, motor and OTA states\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'policy.cpp';p.write_text(code);exe=Path(d)/'policy'
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
