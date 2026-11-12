from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[1];src=(root/'src/hardware/display_manager.cpp').read_text()
read=src[src.index('void DisplayManager::touchpad_read_cb('):src.index('uint32_t DisplayManager::millis_cb()')]
code=r'''
#include <cassert>
#include <iostream>
#include "wake_touch_guard.h"
constexpr int LV_INDEV_STATE_RELEASED=0,LV_INDEV_STATE_PRESSED=1;
struct lv_indev_t{};struct lv_indev_data_t{int state;struct{int x,y;}point;};
struct TouchData{int x=123,y=234;bool pressed=true;};struct Touch{TouchData data;TouchData get_touch_data(){return data;}};
struct DisplayManager{Touch touch_driver;WakeTouchGuard wake_touch_guard_;static void touchpad_read_cb(lv_indev_t*,lv_indev_data_t*);};
DisplayManager* g_display_manager=nullptr;
'''+read+r'''
int main(){DisplayManager d;g_display_manager=&d;lv_indev_data_t event{};
 d.wake_touch_guard_.set_dimmed(true,false);DisplayManager::touchpad_read_cb(nullptr,&event);assert(event.state==1&&event.point.x==123&&event.point.y==234); // Off unchanged.
 d.wake_touch_guard_.set_dimmed(true,true);DisplayManager::touchpad_read_cb(nullptr,&event);assert(event.state==0);
 d.wake_touch_guard_.set_dimmed(false,true);DisplayManager::touchpad_read_cb(nullptr,&event);assert(event.state==0); // Hold cannot activate new UI.
 d.touch_driver.data.pressed=false;DisplayManager::touchpad_read_cb(nullptr,&event);assert(event.state==0);
 d.touch_driver.data.pressed=true;DisplayManager::touchpad_read_cb(nullptr,&event);assert(event.state==1);
 d.wake_touch_guard_.suppress_current_touch();DisplayManager::touchpad_read_cb(nullptr,&event);assert(event.state==0);
 d.touch_driver.data.pressed=false;DisplayManager::touchpad_read_cb(nullptr,&event);assert(event.state==0);
 d.touch_driver.data.pressed=true;DisplayManager::touchpad_read_cb(nullptr,&event);assert(event.state==1);
 std::cout<<"PASS: actual touch callback consumes wake and startup-held gestures through release; Off behavior preserved\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'touch.cpp';p.write_text(code);exe=Path(d)/'touch'
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'src/hardware'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
