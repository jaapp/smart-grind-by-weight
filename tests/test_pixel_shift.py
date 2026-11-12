from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1]
source=(base/'src/hardware/display_manager.cpp').read_text()
flush=source[source.index('void DisplayManager::display_flush_cb('):source.index('void DisplayManager::touchpad_read_cb(')]
touch_read=source[source.index('void DisplayManager::touchpad_read_cb('):source.index('uint32_t DisplayManager::millis_cb()')]
code=r'''
#include <cassert>
#include <vector>
#include <cstring>
#include <iostream>
#include "pixel_shift.h"
#define LV_COLOR_16_SWAP 0
struct lv_display_t{};
struct lv_indev_t{};
constexpr int LV_INDEV_STATE_RELEASED=0,LV_INDEV_STATE_PRESSED=1;
struct lv_indev_data_t{int state;struct {int x,y;} point;};
struct TouchData{int x,y;bool pressed;};
struct Touch{TouchData data{100,100,false};TouchData get_touch_data(){return data;}};
struct lv_area_t{int x1,y1,x2,y2;};
int lv_area_get_width(const lv_area_t* a){return a->x2-a->x1+1;}
int lv_area_get_height(const lv_area_t* a){return a->y2-a->y1+1;}
int completed=0;
void lv_display_flush_ready(lv_display_t*){completed++;}
struct Gfx{
 std::vector<uint16_t> frame=std::vector<uint16_t>(280*456,0);
 void draw16bitRGBBitmap(int x,int y,uint16_t* data,int w,int h){
  assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=280&&y+h<=456);
  for(int j=0;j<h;j++)for(int i=0;i<w;i++)frame[(y+j)*280+x+i]=data[j*w+i];
 }
 void draw16bitBeRGBBitmap(int x,int y,uint16_t* d,int w,int h){draw16bitRGBBitmap(x,y,d,w,h);}
};
struct DisplayManager{
 Gfx* gfx_device;uint16_t* dma_staging_buffer;int dma_staging_rows=16,screen_width=280,screen_height=456;
 oled::PixelShift pixel_shift_;
 Touch touch_driver;
 static void display_flush_cb(lv_display_t*,const lv_area_t*,uint8_t*);
 static void touchpad_read_cb(lv_indev_t*,lv_indev_data_t*);
};
DisplayManager* g_display_manager;
'''+flush+touch_read+r'''
int main(){
 using namespace oled;
 PixelShift timer;timer.reset(100);assert(!timer.update(60099,true));assert(timer.update(60100,true));
 assert(timer.offset().x==2);assert(!timer.update(200000,false));assert(timer.update(200000,true));
 timer.reset(UINT32_MAX-100);assert(!timer.update(59898,true));assert(timer.update(59899,true));
 Gfx gfx;std::vector<uint16_t> staging(280*16,0);DisplayManager display{&gfx,staging.data()};g_display_manager=&display;
 std::vector<uint16_t> pixels(280*456);
 for(int y=0;y<456;y++)for(int x=0;x<280;x++)pixels[y*280+x]=1+(x+y*31)%65534;
 for(int step=0;step<9;step++){
  if(step)assert(display.pixel_shift_.update(step*60000,true));
  const auto shift=display.pixel_shift_.offset();assert(std::abs(shift.x)<=2&&std::abs(shift.y)<=2);
  std::fill(gfx.frame.begin(),gfx.frame.end(),0);
  lv_area_t area{0,0,279,455};DisplayManager::display_flush_cb(nullptr,&area,reinterpret_cast<uint8_t*>(pixels.data()));
  for(int y=0;y<456;y++)for(int x=0;x<280;x++){
   Point logical;bool inside=logical_touch(x,y,shift,280,456,logical);
   assert(gfx.frame[y*280+x]==(inside?pixels[logical.y*280+logical.x]:0));
  }
  for(int y:{0,1,220,454,455})for(int x:{0,1,130,278,279}){
   lv_indev_data_t event{};display.touch_driver.data={x,y,true};
   DisplayManager::touchpad_read_cb(nullptr,&event);Point logical;
   const bool inside=logical_touch(x,y,shift,280,456,logical);
   assert(event.state==(inside?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED));
   if(inside)assert(event.point.x==logical.x&&event.point.y==logical.y);
  }
  for(int y:{0,1,220,454,455})for(int x:{0,1,130,278,279})for(int w:{1,2,280-x}){
   int h=std::min(17,456-y);lv_area_t part{x,y,x+w-1,y+h-1};std::vector<uint16_t> input(w*h,321);
   DisplayManager::display_flush_cb(nullptr,&part,reinterpret_cast<uint8_t*>(input.data()));
  }
 }
 assert(completed>9);
 lv_indev_data_t input{};display.touch_driver.data={100,100,true};
 DisplayManager::touchpad_read_cb(nullptr,&input);assert(input.state==LV_INDEV_STATE_PRESSED);
 const auto offset=display.pixel_shift_.offset();assert(input.point.x==100-offset.x && input.point.y==100-offset.y);
 std::cout<<"PASS: actual display flush for all nine offsets; full/partial clipping, touch mapping, freeze, interval and timer wrap\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'shift.cpp';p.write_text(code);exe=Path(tmp)/'shift'
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(base/'src/hardware'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
