#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <fstream>
#include "u8g2.h"
#include "companion_ui.h"
#include "mcp_protocol.h"
#include "button_gesture.h"

// Actual U8g2 software rasterizer, same fonts and bit layout as the OLED.
struct Canvas {
  u8g2_t u{};
  u8x8_display_info_t info{};
  std::array<uint8_t,1088> memory;
  Canvas() {
    memory.fill(0xa5);
    info.tile_width = 16; info.tile_height = 8;
    info.pixel_width = 128; info.pixel_height = 64;
    u.u8x8.display_info = &info;
    u8g2_SetupBuffer(&u, memory.data()+32, 8, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
    clearBuffer();
  }
  void guard() {
    for (int i=0;i<32;i++) assert(memory[i]==0xa5 && memory[1056+i]==0xa5);
  }
  void clearBuffer() { std::memset(memory.data()+32,0,1024); u8g2_SetDrawColor(&u,1); }
  void setDrawColor(int c) {u8g2_SetDrawColor(&u,c);}
  void setFont(const uint8_t *f) {u8g2_SetFont(&u,f);}
  int getStrWidth(const char *s) {return u8g2_GetStrWidth(&u,s);}
  void drawStr(int x,int y,const char *s) {
    assert(x>=0 && x+getStrWidth(s)<=128);
    assert(y-u8g2_GetAscent(&u)>=0 && y-u8g2_GetDescent(&u)<64);
    u8g2_DrawStr(&u,x,y,s);
  }
  void drawPixel(int x,int y) {u8g2_DrawPixel(&u,x,y);}
  void drawLine(int a,int b,int c,int d) {u8g2_DrawLine(&u,a,b,c,d);}
  void drawHLine(int x,int y,int w) {assert(w>=0);u8g2_DrawHLine(&u,x,y,w);}
  void drawVLine(int x,int y,int h) {assert(h>=0);u8g2_DrawVLine(&u,x,y,h);}
  void drawBox(int x,int y,int w,int h) {assert(w>=0&&h>=0);u8g2_DrawBox(&u,x,y,w,h);}
  void drawRFrame(int x,int y,int w,int h,int r) {assert(2*r<=w&&2*r<=h);u8g2_DrawRFrame(&u,x,y,w,h,r);}
  void drawRBox(int x,int y,int w,int h,int r) {assert(2*r<=w&&2*r<=h);u8g2_DrawRBox(&u,x,y,w,h,r);}
  void drawDisc(int x,int y,int r) {u8g2_DrawDisc(&u,x,y,r,U8G2_DRAW_ALL);}
  void drawCircle(int x,int y,int r) {u8g2_DrawCircle(&u,x,y,r,U8G2_DRAW_ALL);}
  void drawEllipse(int x,int y,int rx,int ry) {u8g2_DrawEllipse(&u,x,y,rx,ry,U8G2_DRAW_ALL);}
  void drawTriangle(int a,int b,int c,int d,int e,int f) {u8g2_DrawTriangle(&u,a,b,c,d,e,f);}
  uint64_t hash() {
    uint64_t h=1469598103934665603ULL;
    for(int i=32;i<1056;i++) {h^=memory[i];h*=1099511628211ULL;}
    return h;
  }
  void dump(const char *name) {
    guard();
    std::ofstream f(std::string(name)+".pbm",std::ios::binary);
    f<<"P4\n128 64\n";
    for(int y=0;y<64;y++) for(int x=0;x<128;x+=8) {
      uint8_t b=0;
      for(int bit=0;bit<8;bit++) if((memory[32+(y/8)*128+x+bit]>>(y%8))&1) b|=128>>bit;
      f.put(static_cast<char>(~b));
    }
  }
};

void testMcp() {
  DynamicJsonDocument in(2048),out(4096);
  auto run=[&](const char *json) {
    in.clear();out.clear();assert(!deserializeJson(in,json));
    return lucy_mcp::respond(in.as<JsonVariantConst>(),out,"test status");
  };
  assert(run(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})")==200);
  assert(std::string(out["result"]["protocolVersion"].as<const char*>())=="2025-06-18");
  assert(run(R"({"jsonrpc":"2.0","method":"notifications/initialized"})")==202);
  run(R"({"jsonrpc":"2.0","id":"a","method":"tools/list"})");
  assert(out["result"]["tools"].size()==2);
  assert(std::string(out["id"].as<const char*>())=="a");
  run(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"lucy_status","arguments":{}}})");
  assert(std::string(out["result"]["content"][0]["text"].as<const char*>())=="test status");
  run(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"lucy_animations"}})");
  char overwrite[1200];std::memset(overwrite,'x',sizeof(overwrite));
  std::string catalog=out["result"]["content"][0]["text"].as<const char*>();
  assert(catalog.find("Making biscuits")!=std::string::npos && catalog.find("Hello paw")!=std::string::npos);
  assert(!out.overflowed());
  run(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"lucy_status","arguments":{"secret":"x"}}})");
  assert(out["error"]["code"]==-32602);
  run(R"({"jsonrpc":"2.0","id":5,"method":"bad"})");assert(out["error"]["code"]==-32601);
  run(R"([{"jsonrpc":"2.0","id":6,"method":"ping"}])");assert(out["error"]["code"]==-32600);
  run(R"({"id":6,"method":"ping"})");assert(out["error"]["code"]==-32600);
}

int main() {
  assert(buttonGesture(80,true)==Gesture::SELECT);
  assert(buttonGesture(649,false)==Gesture::NEXT);
  assert(buttonGesture(650,false)==Gesture::SELECT);
  assert(buttonGesture(1799,false)==Gesture::SELECT);
  assert(buttonGesture(1800,false)==Gesture::BACK);
  testMcp();
  cat::Player player;player.begin(11,0xfffff000);
  uint32_t now=player.started;
  std::set<int> visited;
  for(int i=0;i<cat::CLIPS;i++) {
    assert(visited.insert(player.clip).second);
    now+=cat::duration(player.clip);
    assert(player.tick(now));
  }
  assert(player.clip==11 && visited.size()==120);
  player.select(119,now);assert(!player.tick(now+99999));
  Canvas d;
  std::set<uint64_t> clips;
  for(int id=0;id<cat::CLIPS;id++) {
    uint64_t signature=0;
    std::set<uint64_t> frames;
    for(int t=0;t<3000;t+=80) {
      companion_ui::home(d,id,t,-1,false,0,true);
      d.guard(); frames.insert(d.hash());
      signature=signature*1099511628211ULL+d.hash();
    }
    assert(frames.size()>1); // every clip actually moves
    assert(clips.insert(signature).second); // no duplicate motion performances
    if(id%5==0) {char name[32];std::snprintf(name,sizeof(name),"cat-%02d",id/5);d.dump(name);}
  }
  for(int i=0;i<9;i++) {
    companion_ui::settings(d,i,u8g2_font_helvR08_tf,u8g2_font_helvB08_tf,true);
    char name[32];std::snprintf(name,sizeof(name),"settings-%d",i);d.dump(name);
  }
  for(int i=0;i<=5;i++) companion_ui::boot(d,i,800,u8g2_font_helvR08_tf);
  d.dump("boot");
  for(int percent : {-1,0,1,50,100}) {
    companion_ui::home(d,15,1000,percent,true,-63,false);d.guard();
  }
  d.dump("home");
  const int preview[] = {cat::WAVE,cat::KNEAD,cat::SLOW_BLINK,cat::ZOOM,cat::PURR,cat::BELLY};
  for(int frame=0;frame<120;frame++) {
    companion_ui::home(d,preview[frame/20]*5+2,(frame%20)*120,75,true,-63,false);
    char name[32];std::snprintf(name,sizeof(name),"motion-%03d",frame);d.dump(name);
  }
  std::puts("PASS: 120 distinct moving clips, 120-clip cycle + rollover, framebuffer guards, text bounds, one-button gestures and MCP protocol.");
}
