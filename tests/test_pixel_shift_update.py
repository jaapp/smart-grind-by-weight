from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[1]
src=(root/'src/hardware/display_manager.cpp').read_text()
update=src[src.index('void DisplayManager::update()'):src.index('// Update the refresh area')]
code=r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include "pixel_shift.h"
uint32_t now=0;uint32_t millis(){return now;}
int invalidations=0,handlers=0;
void* lv_screen_active(){return nullptr;}void lv_obj_invalidate(void*){++invalidations;}void lv_timer_handler(){++handlers;}
constexpr int RGB565_BLACK=0;
struct TouchData{bool pressed=false;};
struct Touch{TouchData data;uint32_t age=1000;void update(){};TouchData get_touch_data(){return data;}uint32_t get_ms_since_last_touch(){return age;}};
struct Gfx{int borders=0;void fillRect(int x,int y,int w,int h,int color){assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=280&&y+h<=456&&color==0);++borders;}};
struct DisplayManager{bool initialized=true,pixel_shift_allowed_=true;Touch touch_driver;oled::PixelShift pixel_shift_;Gfx* gfx_device;int screen_width=280,screen_height=456;void update();};
'''+update+r'''
int main(){Gfx gfx;DisplayManager d;d.gfx_device=&gfx;d.pixel_shift_.reset(0);
 now=59999;d.update();assert(invalidations==0&&handlers==1);
 now=60000;d.update();assert(invalidations==1&&gfx.borders==1&&d.pixel_shift_.offset().x==2);
 now=120000;d.touch_driver.data.pressed=true;d.update();assert(invalidations==1);
 d.touch_driver.data.pressed=false;d.touch_driver.age=999;d.update();assert(invalidations==1);
 d.touch_driver.age=1000;d.pixel_shift_allowed_=false;d.update();assert(invalidations==1);
 d.pixel_shift_allowed_=true;d.update();assert(invalidations==2&&gfx.borders==3&&d.pixel_shift_.offset().y==2);
 d.update();assert(invalidations==2); // No replay of missed time intervals.
 d.initialized=false;const auto old=handlers;now+=60000;d.update();assert(invalidations==2&&handlers==old);
 std::cout<<"PASS: actual display update timing, busy/touch freeze, border clearing and complete invalidation\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'update.cpp';p.write_text(code);exe=Path(d)/'update'
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'src/hardware'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
