#include "Pokitto.h"
#include <LibAudio>
#include <MemOps>
#include <LibTrig>
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

void console(pines::u32 value){
    auto pos = resTable.find(value);
    if(pos){
        auto& file = resTable.at(pos);
        while(true){
            auto ch = file.read<char>();
            if(!ch) break;
            LOG(ch);
        }
    }else{
        LOG(value);
    }
    LOG("\n");
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
    case pines::hash("\"A"):     return PC::aBtn();
    case pines::hash("\"B"):     return PC::bBtn();
    case pines::hash("\"C"):     return PC::cBtn();
    case pines::hash("\"UP"):    return PC::upBtn();
    case pines::hash("\"DOWN"):  return PC::downBtn();
    case pines::hash("\"LEFT"):  return PC::leftBtn();
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

// char projectName[16];

#define ASM(x...) __asm__ volatile (".syntax unified\n" #x)
void write_command_16(uint16_t data);
void write_data_16(uint16_t data);

void __attribute__((naked)) flush(pines::u16 *data, pines::u32 len){
    ASM(
        push {r4, r5, lr}       \n
        ldr r2, =0xA0002188     \n
        ldr r3, =1<<12          \n
        movs r4, 252            \n
        1:                      \n
        ldrh r5, [r0];          \n
        lsls r5, 3              \n
        str r5, [r2];           \n
        str r3, [r2, r4];       \n
        adds r0, 2              \n
        subs r1, 1              \n
        str r3, [r2, 124];      \n
        bne 1b                  \n
        pop {r4, r5, pc}        \n
        );
}

void splash(int cx, int cy, pines::u32 hash){
    pines::u16 out[220];
    auto chout = reinterpret_cast<char*>(out);

    {
        char fileName[32];
        stringFromHash(hash, fileName, sizeof(fileName));
        // snprintf(chout, sizeof(out), "pines/%s/%s.565", projectName, fileName);
        snprintf(chout, sizeof(out), "pines/%s.565", fileName);
    }

    File f;
    if(!f.openRO(chout)){
        LOG("Could not open splash:\n[", (const char*) chout, "]\n");
        return;
    }

    pines::s32 w = f.read<pines::u16>();
    pines::s32 h = f.read<pines::u16>();
    auto stride = w * 2;

    if(h + cy >= 176)
        h = 176 - cy;

    if(w + cx >= 220)
        w = 220 - cx;

    if(w <= 0 || h <= 0)
        return;

    write_command(0x38); write_data(cx + w - 1);
    write_command(0x39); write_data(cx);
    write_command(0x36); write_data(cy + h - 1);
    write_command(0x37); write_data(cy);
    write_command(0x20); write_data(cy);
    write_command(0x21); write_data(cx);
    write_command(0x22);
    CLR_CS_SET_CD_RD_WR;
    SET_MASK_P2;

    for(int y = 0; y < h; y++){
        f.read((void*) out, stride);
        flush(out, w);
    }
}

pines::u32 readFile(pines::u32 nameHash, char *ptr){
    char path[128];
    stringFromHash(nameHash, path, 128);
    File file;
    if(!file.openRO(path)){
        return 0;
    }
    if(!ptr){
        pines::u32 size = file.size();
        if(size & 3) size += 4;
        ptr = reinterpret_cast<char*>(pines::arrayCtr(size >> 2));
    }
    file.read(ptr, file.size());
    return reinterpret_cast<pines::u32>(ptr);
}

pines::u32 read(pines::u32 key, pines::u32 a, pines::u32 b, pines::u32 c){
    switch(key){
    case pines::hash("\"TILE"): return reinterpret_cast<pines::u32>(PD::getTile(a, b));
    case pines::hash("\"COLOR"): return PD::getTileColor(a, b);
    case pines::hash("\"PEEK"): return reinterpret_cast<pines::u8*>(a)[b];
    case pines::hash("\"POKE"): return reinterpret_cast<pines::u8*>(a)[b] = c;
    default: return readFile(key, reinterpret_cast<char*>(a));
    }
    LOG("Invalid read\n");
    return 0;
}

void tileshift(pines::u32 x, pines::u32 y){
    PD::shiftTilemap(x, y);
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
    auto r = (((max - min) > 0) ? (state % (max - min)) : 0) + min;
    // LOG("rand ", min, ", ", max, " = ", r, "\n");
    return r;
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
    pine.setConstant("console", console);
    pine.setConstant("printNumber", printNumber);
    pine.setConstant("tile", tile);
    pine.setConstant("tileshift", tileshift);
    pine.setConstant("sprite", sprite);
    pine.setConstant("splash", splash);
    pine.setConstant("mirror", setMirror);
    pine.setConstant("flip", setFlip);
    pine.setConstant("window", PD::setTASWindow);
    pine.setConstant("random", xorshift32);
    pine.setConstant("builtin", builtin, true);
    pine.setConstant("cursor", PD::setCursor);
    pine.setConstant("color", setColor);
    pine.setConstant("background", setBackground);
    pine.setConstant("pressed", pressed);
    pine.setConstant("justPressed", justPressed);
    pine.setConstant("time", PC::getTime);
    pine.setConstant("io", read);
    pine.setConstant("music", music);
    if(pine.compile()){
        pine.run();
        onUpdate = pine.getCall<void()>("update") ?: onUpdate;
    }
}

void update(){
    onUpdate();
}
