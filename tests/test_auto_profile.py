"""Exercise actual preference, timeout, UI-selection and touch-guard code with host I/O."""
from pathlib import Path
import subprocess
import tempfile

base = Path(__file__).resolve().parents[1]

def run(name, code, headers=None):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for path, content in (headers or {}).items():
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        source = root / f'{name}.cpp'
        source.write_text(code)
        exe = root / name
        subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                        '-I'+str(root), '-I'+str(base/'src/hardware'),
                        '-I'+str(base/'src/bluetooth'), str(source), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

profile_header = (base/'src/controllers/profile_controller.h').read_text().replace(
    '#include "../config/constants.h"', '#include "constants.h"')
preferences = r'''
#pragma once
#include <map>
#include <string>
struct Preferences {
 std::map<std::string,int> ints;
 std::map<std::string,float> floats;
 int int_writes=0, float_writes=0;
 int getInt(const char* k,int d){auto p=ints.find(k);return p==ints.end()?d:p->second;}
 float getFloat(const char* k,float d){auto p=floats.find(k);return p==floats.end()?d:p->second;}
 void putInt(const char* k,int v){ints[k]=v;++int_writes;}
 void putFloat(const char* k,float v){floats[k]=v;++float_writes;}
};
'''
constants = r'''
#pragma once
constexpr int USER_PROFILE_COUNT=3, USER_PROFILE_NAME_MAX_LENGTH=16;
constexpr float USER_SINGLE_ESPRESSO_WEIGHT_G=12.5f, USER_DOUBLE_ESPRESSO_WEIGHT_G=18,
 USER_CUSTOM_PROFILE_WEIGHT_G=20, USER_SINGLE_ESPRESSO_TIME_S=5,
 USER_DOUBLE_ESPRESSO_TIME_S=6, USER_CUSTOM_PROFILE_TIME_S=7,
 USER_MIN_TARGET_WEIGHT_G=5, USER_MAX_TARGET_WEIGHT_G=1000,
 USER_MIN_TARGET_TIME_S=1, USER_MAX_TARGET_TIME_S=60;
'''
profile_source = (base/'src/controllers/profile_controller.cpp').read_text()
run('preferences', '#include <cassert>\n#include <iostream>\n'+profile_source+r'''
int main(){
 for(int option:{-1,0,1,2,3,-2}) {
  Preferences prefs;prefs.ints={{"profile",2},{"auto_profile",option},{"grind_mode",1}};
  prefs.floats={{"weight0",12.4f},{"weight1",18.2f},{"weight2",21.7f},{"time2",8.5f},{"hx_cal",-5166.4f}};
  const auto before=prefs.floats;
  ProfileController p;p.init(&prefs);
  const int expected=option>=0&&option<3?option:2;
  assert(p.get_current_profile()==expected);
  assert(p.get_auto_select_profile()==(option>=-1&&option<3?option:-1));
  assert(prefs.floats==before && prefs.float_writes==0 && p.get_grind_mode()==GrindMode::TIME);
  const auto saved=prefs.int_writes;
  if(option>=0&&option<3)assert(p.select_auto_profile());else assert(!p.select_auto_profile());
  assert(prefs.int_writes==saved); // Already selected: no extra flash write.
 }
 Preferences prefs;prefs.ints["profile"]=1;
 ProfileController p;p.init(&prefs);
 assert(p.get_auto_select_profile()==-1 && p.get_current_profile()==1); // Upgrade is Off.
 p.set_auto_select_profile(0);assert(p.get_current_profile()==1 && prefs.ints["auto_profile"]==0);
 p.set_auto_select_profile(0);p.set_auto_select_profile(3);p.set_auto_select_profile(-2);
 assert(prefs.int_writes==1);
 assert(p.select_auto_profile() && p.get_current_profile()==0);
 p.set_current_profile(2);assert(p.get_auto_select_profile()==0);
 assert(p.select_auto_profile() && p.get_current_profile()==0); // Manual choice lasts until next wake.
 assert(p.get_profile_weight(0)==12.5f && p.get_profile_weight(1)==18 && p.get_profile_time(2)==7);
 p.set_auto_select_profile(-1);assert(!p.select_auto_profile() && p.get_current_profile()==0);
 std::cout<<"PASS: boot/wake default, Off/invalid preferences, manual selection, saved targets/times/calibration and bounded NVS writes\n";
}
''', {'profile_controller.h':profile_header, 'Preferences.h':preferences,
      'constants.h':constants, 'Arduino.h':'#pragma once\n',
      'grind_mode.h':(base/'src/controllers/grind_mode.h').read_text()})

timeout_source = (base/'src/ui/controllers/screen_timeout_controller.cpp').read_text()
timeout_source = '\n'.join(line for line in timeout_source.splitlines() if not line.startswith('#include'))
timeout_header = (base/'src/ui/controllers/screen_timeout_controller.h').read_text().replace('#pragma once', '')
run('timeout', r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include "wake_touch_guard.h"
uint32_t now=0;
uint32_t millis(){return now;}
constexpr uint32_t USER_SCREEN_AUTO_DIM_TIMEOUT_MS=300000;
constexpr float USER_WEIGHT_ACTIVITY_THRESHOLD_G=2, USER_SCREEN_BRIGHTNESS_NORMAL=1,
 USER_SCREEN_BRIGHTNESS_DIMMED=.35f;
struct Touch {uint32_t age=300000;uint32_t get_ms_since_last_touch(){return age;}};
struct Display {
 Touch touch;WakeTouchGuard guard;float brightness=1;int changes=0;
 Touch* get_touch_driver(){return &touch;}
 void set_dimmed(bool d,bool p){guard.set_dimmed(d,p);}
 void set_brightness(float b){brightness=b;++changes;}
};
struct Sensor {bool activity=false;bool weight_range_exceeds(uint32_t,float){return activity;}};
struct Hardware {Display display;Sensor sensor;Display* get_display(){return &display;}Sensor* get_weight_sensor(){return &sensor;}};
enum class UIState {READY,GRINDING};
struct State {UIState state=UIState::READY;bool is_state(UIState s){return s==state;}};
struct Profile {int option=0;int get_auto_select_profile(){return option;}};
struct Menu {float get_normal_brightness(){return .8f;}float get_screensaver_brightness(){return .2f;}};
struct UIManager {Hardware* hardware_manager;State* state_machine;Profile* profile_controller;Menu* menu_controller_;};
'''+timeout_header+timeout_source+r'''
int main(){
 Hardware hw;State state;Profile profile;Menu menu;UIManager ui{&hw,&state,&profile,&menu};
 ScreenTimeoutController timer(&ui);auto& d=hw.display;
 assert(!timer.update() && d.brightness==.2f);
 assert(!timer.update() && d.changes==1); // Dim once.
 assert(!d.guard.allow(true)); // First wake press never reaches LVGL.
 d.touch.age=0;assert(timer.update() && d.brightness==.8f);
 assert(!d.guard.allow(true));assert(!d.guard.allow(false));assert(d.guard.allow(true));
 assert(!timer.update());
 d.touch.age=300000;assert(!timer.update());
 hw.sensor.activity=true;assert(timer.update() && d.brightness==.8f); // Cup/weight wakes too.
 hw.sensor.activity=false;assert(!timer.update());
 state.state=UIState::GRINDING;assert(!timer.update() && d.brightness==.8f); // Never report a profile wake while grinding.
 state.state=UIState::READY;assert(!timer.update());
 profile.option=-1;assert(!timer.update());assert(d.guard.allow(true)); // Off preserves original wake input.
 WakeTouchGuard forced;forced.suppress_current_touch();assert(!forced.allow(true));
 forced.set_dimmed(false,false);assert(!forced.allow(true));assert(!forced.allow(false));assert(forced.allow(true));
 WakeTouchGuard no_touch;no_touch.suppress_current_touch();assert(!no_touch.allow(false));assert(no_touch.allow(true));
 ScreenTimeoutController absent(nullptr);assert(!absent.update());
 std::cout<<"PASS: touch/weight wake, consumed wake gesture, busy restoration, Off behavior and timer rollover\n";
}
''')
