#include "Pokitto.h"
#include <LibAudio>
#include <MemOps>
#include "compiler/SimplePine.h"
#include "assets.h"
#include "miloslav.h"

using PD = Pokitto::Display;
using PC = Pokitto::Core;
using PB = Pokitto::Buttons;

Audio::Sink<4, PROJ_AUD_FREQ> audio;

void (*onUpdate)() = +[](){};
pines::ResTable resTable(1024);

void stringFromHash(pines::u32 hash, char *str, pines::u32 max){
    auto pos = resTable.find(hash);
    if(!pos){
        str[0] = 0;
        return;
    }
    auto& file = resTable.at(pos);
    int i = 0;
    for(; i < max - 1; ++i){
        auto ch = str[i] = file.read<char>();
        if(!ch) break;
    }
    str[i] = 0;
}

void music(pines::u32 hash){
    char path[128];
    stringFromHash(hash, path, sizeof(path));
    Audio::play((const char*)path);
}

void printNumber(pines::s32 value){
    PD::print(value);
    PD::print(" ");
}

void print(pines::u32 value){
    auto pos = resTable.find(value);
    if(pos){
        auto& file = resTable.at(pos);
        while(true){
            auto ch = file.read<char>();
            if(!ch) break;
            PD::print(ch);
        }
    }else{
        printNumber(value);
    }
}

bool pressed(pines::u32 id){
    switch(id){
    case pines::hash("\"A"): return PC::aBtn();
    case pines::hash("\"B"): return PC::bBtn();
    case pines::hash("\"C"): return PC::cBtn();
    case pines::hash("\"UP"): return PC::upBtn();
    case pines::hash("\"DOWN"): return PC::downBtn();
    case pines::hash("\"LEFT"): return PC::leftBtn();
    case pines::hash("\"RIGHT"): return PC::rightBtn();
    }
    return false;
}

bool justPressed(pines::u32 id){
    switch(id){
    case pines::hash("\"A"): return PB::pressed(BTN_A);
    case pines::hash("\"B"): return PB::pressed(BTN_B);
    case pines::hash("\"C"): return PB::pressed(BTN_C);
    case pines::hash("\"UP"): return PB::pressed(BTN_UP);
    case pines::hash("\"DOWN"): return PB::pressed(BTN_DOWN);
    case pines::hash("\"LEFT"): return PB::pressed(BTN_LEFT);
    case pines::hash("\"RIGHT"): return PB::pressed(BTN_RIGHT);
    }
    return false;
}

void setColor(int c){ PD::color = c; }
void setBackground(int c){ PD::bgcolor = c; }

bool mirror;
bool flip;

void setMirror(int m){ mirror = m; }
void setFlip(int f){ flip = f; }

void sprite(int x, int y, const uint8_t *ptr){
    PD::drawSprite(x, y, ptr, flip, mirror, PD::color);
}

void tile(pines::u32 x, pines::u32 y, pines::u32 t){
    if(t > 256){
        auto ptr = reinterpret_cast<const pines::u8 *>(t);
        auto w = *ptr++;
        auto h = *ptr++;
        PD::drawTile(x, y, ptr, PD::color, w == PROJ_TILE_W * 2);
    }else{
        PD::drawColorTile(x, y, t);
    }
}

const char *builtin(pines::u32 hash){
    auto pos = reinterpret_cast<const pines::u32*>(&assets);
    if(uintptr_t(pos) & 0x3){
        LOG("Unaligned data\n");
        while(true);
    }
    auto count = *pos++;
    while(count--){
        // LOG((void*)pos, " ", *pos, "\n");
        auto candidate = *pos++;
        auto b = reinterpret_cast<const char *>(pos) + 2;
        if(candidate == hash)
            return b;
        pos += 1 + (int(b[0]) * int(b[1]) >> 2);
    }
    LOG("Resource not found!\n", hash, "\n");
    return 0;
}

pines::u32 state = 0xDEADBEEF;
pines::s32 xorshift32(pines::s32 min, pines::s32 max){
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (((max - min) > 0) ? (state % (max - min)) : 0) + min;
}

void init(){
    PD::adjustCharStep = 0;
    PD::bgcolor = 0;
    PD::invisiblecolor = 0;
    state += PC::getTime();
    PD::loadRGBPalette(miloslav);
    resTable.reset();
    pines::SimplePine pine("src.pines", resTable);
    pine.setConstant("print", print);
    pine.setConstant("printNumber", printNumber);
    pine.setConstant("tile", tile);
    pine.setConstant("sprite", sprite);
    pine.setConstant("mirror", setMirror);
    pine.setConstant("flip", setFlip);
    pine.setConstant("window", PD::setTASWindow);
    pine.setConstant("random", xorshift32);
    pine.setConstant("builtin", builtin);
    pine.setConstant("cursor", PD::setCursor);
    pine.setConstant("color", setColor);
    pine.setConstant("background", setBackground);
    pine.setConstant("pressed", pressed);
    pine.setConstant("justPressed", justPressed);
    pine.setConstant("time", PC::getTime);
    pine.setConstant("music", music);
    if(pine.compile()){
        pine.run();
        onUpdate = pine.getCall<void()>("update") ?: onUpdate;
    }
}

void update(){
    onUpdate();
}
