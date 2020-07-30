#include "Pokitto.h"
#include <LibAudio>
#include <MemOps>
#include <LibTrig>
#include "compiler/SimplePine.h"
#include "assets.h"
#include "miloslav.h"
#include "TextMode.h"

using PD = Pokitto::Display;
using PC = Pokitto::Core;
using PB = Pokitto::Buttons;
using u32 = pine::u32;
using s32 = pine::s32;
using u16 = pine::u16;

const auto magic = 0xFEEDBEEF;
const auto magicPtr = reinterpret_cast<volatile u32*>(0x20004000);
const auto reset = 0x05FA0004;
const auto resetPtr = reinterpret_cast<volatile u32*>(0xE000ED0C);

namespace FMT {
    enum _FMT {
        CL=1, NL=2, SL=4,
        CS=8, NS=16, SS=32
    };
}

union {
    struct {
        int projectCount;
    };
    struct {
        char *src;
        u32 firstLine;
        u16 lineCount;
        u16 highlight;
    };
} mode;
int selection;

extern const unsigned char font5x7[];
extern const unsigned char fontC64[];
extern const unsigned char fontZXSpec[];

bool mirror;
bool flip;
u32 fmt;
u32 numPad = 0;
Audio::Sink<2, PROJ_AUD_FREQ> audio;
Audio::Note note;
constexpr auto version = 1;

uint32_t indexSize;
File *musicFile = nullptr;
File *resourceFile = nullptr;
struct {
    u32 hash;
    u32 offset;
} hashCache[16];

void nopUpdate(){}
void (*onUpdate)() = nopUpdate;

pine::ResTable resTable(1024);
char projectName[16];

class TextFiller {
    using F88 = TextMode<8, 8, false>;
    using F57 = TextMode<5, 7, false>;
    F88 *f88 = nullptr;
    F57 *f57 = nullptr;

public:
    TextFiller(){
        if(PD::font[0] == 8) f88 = new F88();
        else if(PD::font[0] == 5) f57 = new F57();
    }

    ~TextFiller(){
        if(f88) delete f88;
        if(f57) delete f57;
    }

    void activate(u32 n){
        if(f88) f88->activate(n);
        if(f57) f57->activate(n);
    }

    void clear(){
        if(f88) f88->clear();
        if(f57) f57->clear();
    }

    void setCursor(int x, int y){
        if(f88) f88->setPosition(x, y);
        if(f57) f57->setPosition(x, y);
    }

    void print(char ch){
        if(f88) f88->print(ch);
        if(f57) f57->print(ch);
    }

    void printNumber(std::int32_t n){
        if(f88) f88->printNumber(n);
        if(f57) f57->printNumber(n);
    }
};

TextFiller *textFiller = nullptr;
u32 cmask = 0;
u32 cclass = 0;

void cleanup(){
    PD::fontSize = 1;
    PD::setFont(fontC64);
    fmt = FMT::NS | FMT::CL | FMT::SS;
    numPad = 0;
    mirror = false;
    flip = false;
    pine::deleteArrays();
    resTable.reset();
    if(textFiller){
        delete textFiller;
        textFiller = nullptr;
    }
    if(resourceFile){
        delete resourceFile;
        resourceFile = nullptr;
    }
    if(musicFile){
        delete musicFile;
        musicFile = nullptr;
    }
    Audio::stop<0>();
    Audio::stop<1>();
    note = Audio::Note().duration(500).wave(1).loop(false);
    PD::lineFillers[0] = TAS::BGTileFiller;
    PD::lineFillers[1] = TAS::SpriteFiller;
    PD::lineFillers[2] = TAS::NOPFiller;
    PD::lineFillers[3] = TAS::NOPFiller;
    PD::loadRGBPalette(miloslav);
}

void loadMenuColors(){
    File theme;
    if(theme.openRO("pine-2k/theme")){
        theme >> PD::bgcolor >> PD::color;
    }else{
        PD::bgcolor = 209;
        PD::color = 88;
    }
}



void stringFromHash(u32 hash, char *str, u32 max){
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

void pathFromHash(u32 hash, char *str, u32 max){
    const char *src = "pine-2k/";
    while(*src){ *str++ = *src++; max--; }
    src = projectName;
    while(*src){ *str++ = *src++; max--; }
    *str++ = '/'; max--;
    stringFromHash(hash, str, max);
}

void projectFilePath(const char *file, char *path){
    char *out = path;
    const char *str = "pine-2k/";
    while(*str) *out++ = *str++;
    str = projectName;
    while(*str) *out++ = *str++;
    *out++ = '/';
    str = file;
    while(*str) *out++ = *str++;
    *out++ = 0;
}

void printNumber(pine::s32 value){
    if(numPad && value >= 0){
        u32 v = 9;
        u32 dc = 1;
        while(value > v){
            v *= 10;
            v += 9;
            dc++;
        }
        for(;dc < numPad;++dc){
            if(textFiller) textFiller->print('0');
            else PD::print('0');
        }
    }
    if(textFiller){
        textFiller->printNumber(value);
        if(fmt & FMT::NS)
            textFiller->print(' ');
        if(fmt & FMT::NL)
            textFiller->print('\n');
    } else {
        PD::print(value);
        if(fmt & FMT::NS)
            PD::print(" ");
        if(fmt & FMT::NL)
            PD::print("\n");
    }
}

void console(u32 value){
    auto pos = resTable.find(value);
    if(pos){
        auto& file = resTable.at(pos);
        while(true){
            auto ch = file.read<char>();
            if(!ch) break;
            LOG(ch);
        }
    }else{
        LOG(s32(value));
    }
    if(fmt & FMT::CS)
        LOG(" ");
    if(fmt & FMT::CL)
        LOG("\n");
}

void print(u32 value){
    auto pos = resTable.find(value);
    if(pos){
        auto& file = resTable.at(pos);
        while(true){
            auto ch = file.read<char>();
            if(!ch) break;
            if(textFiller) textFiller->print(ch);
            else PD::print(ch);
        }

        if(textFiller){
            if(fmt & FMT::SS)
                textFiller->print(' ');
            if(fmt & FMT::SL)
                textFiller->print('\n');
        }else{
            if(fmt & FMT::SS)
                PD::print(" ");
            if(fmt & FMT::SL)
                PD::print("\n");
        }
    }else{
        printNumber(value);
    }
}

bool pressed(u32 id){
    switch(id){
    case pine::hash("\"A"):     return PC::aBtn();
    case pine::hash("\"B"):     return PC::bBtn();
    case pine::hash("\"C"):     return PC::cBtn();
    case pine::hash("\"UP"):    return PC::upBtn();
    case pine::hash("\"DOWN"):  return PC::downBtn();
    case pine::hash("\"LEFT"):  return PC::leftBtn();
    case pine::hash("\"RIGHT"): return PC::rightBtn();
    }
    return false;
}

bool justPressed(u32 id){
    switch(id){
    case pine::hash("\"A"): return PB::pressed(BTN_A);
    case pine::hash("\"B"): return PB::pressed(BTN_B);
    case pine::hash("\"C"): return PB::pressed(BTN_C);
    case pine::hash("\"UP"): return PB::pressed(BTN_UP);
    case pine::hash("\"DOWN"): return PB::pressed(BTN_DOWN);
    case pine::hash("\"LEFT"): return PB::pressed(BTN_LEFT);
    case pine::hash("\"RIGHT"): return PB::pressed(BTN_RIGHT);
    }
    return false;
}

void setColor(int c){ PD::color = c; }
void setBackground(int c){ PD::bgcolor = c; }

void setCursor(int x, int y){
    if(textFiller){
        textFiller->setCursor(x, y);
    }
    PD::setCursor(x, y);
}

void setMirror(int m){ mirror = m; }
void setFlip(int f){ flip = f; }

void sprite(int x, int y, const uint8_t *ptr){
    if(ptr){
        auto iptr = reinterpret_cast<uintptr_t>(ptr);
        if( iptr > 0x10000000 && iptr < 0x10008000 && (iptr & 0x3) == 0){
            PD::m_colordepth = ptr[1]; // PD::setColorDepth(ptr[1]);
            PD::drawSprite(
                x, y,
                ptr + 2,
                flip, mirror,
                PD::color + ptr[0],
                cmask, cclass);
            PD::setColorDepth(8);
        } else {
            PD::drawSprite(
                x, y,
                ptr,
                flip, mirror,
                PD::color,
                cmask, cclass);
        }
    }
    cmask = 0;
    cclass = 0;
}

u32 projectScore;
void highscore(u32 score){
    if(score <= projectScore)
        return;
    projectScore = score;
    char path[64];
    projectFilePath("highscore", path);
    File file;
    if(file.openRW(path, true, false)){
        file << projectScore;
    }
}

#define ASM(x...) __asm__ volatile (".syntax unified\n" #x)
void write_command_16(uint16_t data);
void write_data_16(uint16_t data);

void __attribute__((naked)) flush(pine::u16 *data, u32 len){
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

void setWindow(int x, int w, int y, int h){
    write_command(0x38); write_data(x + w - 1);
    write_command(0x39); write_data(x);
    write_command(0x36); write_data(y + h - 1);
    write_command(0x37); write_data(y);
    write_command(0x20); write_data(y);
    write_command(0x21); write_data(x);
}

void fillRect(int x, int w, int y, int h, int color){
    pine::u16 out[220];
    setWindow(x, w, y, h);
    write_command(0x22);
    CLR_CS_SET_CD_RD_WR;
    SET_MASK_P2;
    for(int i=0; i<220; ++i) out[i] = color;
    for(int i=0; i<h; ++i)
        flush(out, w);
}

bool drawFile(int cx, int cy, const char *file){
    pine::u16 out[220];
    File f;
    if(!f.openRO(file)){
        return false;
    }

    pine::s32 w = f.read<pine::u16>();
    pine::s32 h = f.read<pine::u16>();
    auto stride = w * 2;

    if(h + cy >= 176)
        h = 176 - cy;

    if(w + cx >= 220)
        w = 220 - cx;

    if(w <= 0 || h <= 0)
        return true;

    setWindow(cx, w, cy, h);
    write_command(0x22);
    CLR_CS_SET_CD_RD_WR;
    SET_MASK_P2;

    for(int y = 0; y < h; y++){
        f.read((void*) out, stride);
        for(int x = 0; x < w; ++x){
            if(out[x] == 0xf81f) out[x] = PD::palette[PD::bgcolor];
        }
        flush(out, w);
    }

    return true;
}

void splash(int cx, int cy, u32 hash){
    char path[64];
    pathFromHash(hash, path, sizeof(path));
    if(!drawFile(cx, cy, path))
        LOG("Could not open:\n[", (const char*) path, "]\n");
}

void clearHashCache(){
    for(auto i=0; i<16; ++i){
        hashCache[i].hash = 0;
        hashCache[i].offset = 0;
    }
}

u32 loadRes(u32 nameHash){
    char path[128];
    pathFromHash(nameHash, path, 128);
    if(!resourceFile)
        resourceFile = new File();
    if(!resourceFile->openRO(path))
        return 0;
    indexSize = resourceFile->read<uint32_t>();
    clearHashCache();
    return 1;
}

u32 readResource(u32 hash, char *ptr){
    using namespace pine;

    if(!resourceFile || !hash)
        return 0;

    auto& file = *resourceFile;
    if(!file) return 0;
    
    auto& cache = hashCache[hash & 0xF];
    if(cache.hash == hash){
        file.seek(cache.offset);
    } else {
        u32 c = 0;
        u32 low = 0;
        u32 hi = indexSize - 1;
        while(low <= hi){
            u32 mid = (hi + low) >> 1;
            u32 pivot = file.seek(4 + mid * 8).read<u32>();
            c++;
            // if(c > 50);
            if(pivot == hash){
                u32 offset = file.read<u32>();
                file.seek(offset);
                cache.hash = hash;
                cache.offset = offset;
                break;
            }else if(low == hi){
                char buf[33];
                stringFromHash(hash, buf, 32);
                LOG("Missing resource ", (const char*) buf, "\n");
                return 0;
            }
            if(pivot < hash){
                low = mid + 1;
            }else{
                hi = mid - 1;
            }
        }
    }

    auto len = file.read<u32>();
    bool created = false;
    if(!ptr){
        created = true;
        u32 size = len;
        if(size & 3) size += 4;
        ptr = reinterpret_cast<char*>(pine::arrayCtr(size >> 2));
    }

    // u32 read =
        file.read(ptr, len);
    // LOG("Read resource ", hash, " to ", (void*)ptr, " ", len, " ", read, "\n");
    return reinterpret_cast<u32>(ptr);
}

bool writeFile(u32 nameHash, u32 *ptr){
    if(!ptr) return false;
    char path[128];
    pathFromHash(nameHash, path, 128);
    File file;
    if(!file.openRW(path, true, false))
        return false;
    auto len = ptr[-1] & 0xFFFF;
    file.write(reinterpret_cast<void*>(ptr), len * 4);
    return true;
}

char *readFileEx(char *path, char *ptr){
    File file;
    if(!file.openRO(path)){
        return 0;
    }
    if(!ptr){
        u32 size = file.size();
        if(size & 3) size += 4;
        ptr = reinterpret_cast<char*>(pine::arrayCtr(size >> 2));
    }
    file.read(ptr, file.size());
    return ptr;
}

u32 readFile(u32 nameHash, char *ptr){
    if(resourceFile){
        auto ret = readResource(nameHash, ptr);
        if(ret)
            return ret;
    }

    char path[128];
    pathFromHash(nameHash, path, 128);

    auto len = strlen(path);
    if(strcmp(path + len - 3, "res") == 0)
        return loadRes(nameHash);

    return reinterpret_cast<u32>(readFileEx(path, ptr));
}

void music(u32 hash){
    char path[64];
    pathFromHash(hash, path, sizeof(path));
    if(!musicFile) musicFile = new File();
    if(!musicFile->openRO(path)) return;
    Audio::play<0>(*musicFile).setLoop(note._loop);
}

void sound(u32 num, u32 osc){
    note.noteNumber(num);
    note.play<1>(osc & 3);
}

u32 read(u32 key, u32 a, u32 b, u32 c){
    switch(key){
    case pine::hash("\"HOUR"):
        return ((uint32_t*)0x40024000)[2]/(60*60)%24;
    case pine::hash("\"MINUTE"):
        return ((uint32_t*)0x40024000)[2]/60%60;
    case pine::hash("\"SECOND"):
        return ((uint32_t*)0x40024000)[2]%60;
    case pine::hash("\"TIMESTAMP"):
        return ((uint32_t*)0x40024000)[2];
    case pine::hash("\"FORMAT"):
        fmt = a;
        numPad = b < 15 ? b : 0;
        return 0;
    case pine::hash("\"COLLISION"): {
        cclass = a;
        if(!c) b = 0;
        if(!b) c = 0;
        cmask = b;
        PD::collisionCallback = reinterpret_cast<void(*)(u32, u32)>(c);
        return 0;
    }
    case pine::hash("\"FILLER"): {
        if(a >= 4) return 0;
        if(b == pine::hash("\"TILES")){
            PD::lineFillers[a] = TAS::BGTileFiller;
        } else if(b == pine::hash("\"SPRITES")) {
            PD::lineFillers[a] = TAS::SpriteFiller;
        } else if(b == pine::hash("\"TEXT")) {
            if(!textFiller){
                switch(c){
                case pine::hash("\"C64"): PD::setFont(fontC64); break;
                case pine::hash("\"ZX"): PD::setFont(fontZXSpec); break;
                case pine::hash("\"5x7"): PD::setFont(font5x7); break;
                }
                textFiller = new TextFiller();
            }
            textFiller->activate(a);
        } else {
            PD::lineFillers[a] = reinterpret_cast<TAS::LineFiller>(b);
        }
        return 0;
    }
    case pine::hash("\"CLEARTEXT"): if(textFiller) textFiller->clear(); return 0;
    case pine::hash("\"SCALE"): PD::fontSize = a; return a;
    case pine::hash("\"VERSION"): return version;
    case pine::hash("\"TILE"): return reinterpret_cast<u32>(PD::getTile(a, b));
    case pine::hash("\"COLOR"): return PD::getTileColor(a, b);
    case pine::hash("\"VOLUME"): if(a < 255) note.volume(a); return note._volume;
    case pine::hash("\"OVERDRIVE"): if(a < 2) note.overdrive(a); return note._overdrive;
    case pine::hash("\"LOOP"): if(a < 2) note.loop(a); return note._loop;
    case pine::hash("\"ECHO"): if(a < 2) note.echo(a); return note._echo;
    case pine::hash("\"ATTACK"): if(a <= 0xFFFF) note.attack(a); return note._attack;
    case pine::hash("\"DECAY"): if(a <= 0xFFFF) note.decay(a); return note._decay;
    case pine::hash("\"SUSTAIN"): if(a <= 0xFFFF) note.sustain(a); return note._sustain;
    case pine::hash("\"RELEASE"): if(a <= 0xFFFF) note.release(a); return note._release;
    case pine::hash("\"MAXBEND"): if(int(a) <= 0xFFFF) note.maxbend(a); return note._maxbend;
    case pine::hash("\"BENDRATE"): if(int(a) <= 0xFFFF) note.release(a); return note._release;
    case pine::hash("\"DURATION"): if(a) note.duration(a << 3); return note._duration >> 3;
    case pine::hash("\"WAVE"):
        switch(a){
        case pine::hash("\"SQUARE"): note.wave(1); break;
        case pine::hash("\"SAW"): note.wave(2); break;
        case pine::hash("\"TRIANGLE"): note.wave(3); break;
        case pine::hash("\"NOISE"): note.wave(4); break;
        }
        return note._wave;
    default: 
        LOG("Invalid io\n");
        break;
    }
    return 0;
}

void tileshift(u32 x, u32 y){
    PD::shiftTilemap(x, y);
}

void tile(u32 x, u32 y, u32 t){
    if(t > 256){
        auto ptr = reinterpret_cast<const pine::u8 *>(t);
        auto w = *ptr++;
        auto h = *ptr++;
        PD::drawTile(x, y, ptr, PD::color, w == PROJ_TILE_W * 2);
    }else{
        PD::drawColorTile(x, y, t);
    }
}

void fillTiles(u32 color){
    static constexpr int screenWidth = PROJ_LCDWIDTH;
    static constexpr int screenHeight = PROJ_LCDHEIGHT;
    constexpr uint32_t tileW = POK_TILE_W;
    constexpr uint32_t tileH = POK_TILE_H;
    constexpr uint32_t mapW = screenWidth / tileW + 2;
    constexpr uint32_t mapH = screenHeight / tileH + 2;
    for(auto y=0; y<mapH; ++y){
        for(auto x=0; x<mapW; ++x){
            PD::drawColorTile(x, y, color);
        }
    }
}

const char *builtin(u32 hash, u32 index){
    auto pos = reinterpret_cast<const u32*>(&assets);
    if(uintptr_t(pos) & 0x3){
        LOG("Unaligned data\n");
        while(true);
    }
    auto count = *pos++;
    if( hash == pine::hash("\"INDEX") ){
        LOG("Hash lookup ", index, "\n");
        while(count--){
            auto candidate = *pos++;
            auto b = reinterpret_cast<const char *>(pos) + 2;
            if(index-- == 0)
                return b;
            u32 s = int(b[0]) * int(b[1]);
            if(s&3) s += 4;
            pos += 1 + (s >> 2);
        }
    } else {
        while(count--){
            auto candidate = *pos++;
            auto b = reinterpret_cast<const char *>(pos) + 2;
            if(candidate == hash)
                return b;
            u32 s = int(b[0]) * int(b[1]);
            if(s&3) s += 4;
            pos += 1 + (s >> 2);
        }
    }
    LOG("Resource not found!\n", hash, "\n");
    return 0;
}

u32 state = 0xDEADBEEF;
pine::s32 xorshift32(pine::s32 min, pine::s32 max){
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    auto r = (((max - min) > 0) ? (state % (max - min)) : 0) + min;
    // LOG("rand ", min, ", ", max, " = ", r, "\n");
    return r;
}


void setProjectName(const char *name){
    int p = 0;
    for(; p < 15 && name[p]; p++) projectName[p] = name[p];
    projectName[p] = 0;
}

void loadProjectHighscore(){
    char path[64];
    projectFilePath("highscore", path);
    File file;
    if(file.openRO(path)){
        file >> projectScore;
    }else{
        projectScore = 0;
    }
}

void updateMenu();
void exitPine(){
    onUpdate = updateMenu;
    mode.projectCount = 0;
}

void run(const char *);
u32 execHash;
void exec(){
    char path[64];
    pathFromHash(execHash, path, sizeof(path));
    onUpdate = updateMenu;
    pine::deleteArrays();
    resTable.reset();
    run(path);
}

void run(const char *path){
    cleanup();

    pine::SimplePine pine(path, resTable);
    pine.setConstant("print", print);
    pine.setConstant("console", console);
    pine.setConstant("printNumber", printNumber);
    pine.setConstant("tile", tile);
    pine.setConstant("fill", fillTiles);
    pine.setConstant("tileshift", tileshift);
    pine.setConstant("sprite", sprite);
    pine.setConstant("splash", splash);
    pine.setConstant("mirror", setMirror);
    pine.setConstant("flip", setFlip);
    pine.setConstant("window", PD::setTASWindow);
    pine.setConstant("random", xorshift32);
    pine.setConstant("builtin", builtin, true);
    pine.setConstant("cursor", setCursor);
    pine.setConstant("color", setColor);
    pine.setConstant("background", setBackground);
    pine.setConstant("pressed", pressed);
    pine.setConstant("justPressed", justPressed);
    pine.setConstant("time", PC::getTime);
    pine.setConstant("io", read);
    pine.setConstant("file", readFile, true);
    pine.setConstant("save", writeFile);
    pine.setConstant("music", music);
    pine.setConstant("sound", sound);
    pine.setConstant("highscore", highscore);
    pine.setConstant("exit", exitPine);
    pine.setConstant("sin", trig::sin, true);
    pine.setConstant("cos", trig::cos, true);
    pine.setConstant("min", +[](int a, int b){ return a < b ? a : b; }, true);
    pine.setConstant("max", +[](int a, int b){ return a > b ? a : b; }, true);
    pine.setConstant("exec", +[](u32 hash){
                                  execHash = hash;
                                  onUpdate = exec;
                              });
    if(pine.compile()){
        if( s32(0x800 - pine::globalCount * 4) > 0 ){
            resTable.setCache(
                reinterpret_cast<u32*>(0x20004000 + pine::globalCount * 4),
                0x800 - pine::globalCount * 4
                );
        }
        onUpdate = nullptr;
        PD::collisionCallback = +[](uint32_t, uint32_t){};
        pine.run();
        if(!onUpdate)
            onUpdate = pine.getCall<void()>("update");
    }
    if(!onUpdate)
        onUpdate = nopUpdate;
}

void pickProject();

int countDigits(uint32_t x){
    int d = 0;
    if(!x) return 1;
    while(x){
        x /= 10;
        d++;
    }
    return d;
}

void updateEditor(){
    auto lineBuffer = reinterpret_cast<u16*>(0x20004000);
    u32 firstLine = mode.firstLine;
    u32 firstColumn = 0;
    bool dirty = numPad == 0;
    u32 length = reinterpret_cast<u32*>(mode.src)[-1] & 0xFFFF;

    numPad = 1;

    if(PB::pressed(BTN_DOWN)){
        firstLine++;
        dirty = true;
    } else if(PB::pressed(BTN_UP)){
        firstLine--;
        dirty = true;
    } else if(PB::pressed(BTN_C)){
        exitPine();
        return;
    }

    mode.firstLine = firstLine;

    if(!dirty)
        return;

    fillTiles(fmt);

    textFiller->clear();
    u32 limit = std::min<u32>(firstLine + (176 / 8), mode.lineCount - 1);
    for( u32 i = firstLine; i < limit; ++i ){
        if(i == (mode.highlight - 1)){
            for( u32 j = 0; j<27; ++j )
                PD::drawColorTile(j, i - firstLine, fmt + 8);
            PD::drawColorTile(27, i - firstLine, 174);
        }

        char *line = mode.src + lineBuffer[i];
        textFiller->setCursor(0, i - firstLine);
        u32 maxj = firstColumn + (220 / 6);
        for( u32 j = firstColumn; j < maxj; ++j ){
            char c = line[j];
            if(c == 13 || c == 10)
                break;
            if(c == '\t'){
                c = ' ';
                textFiller->print(' ');
                maxj--;
            }
            textFiller->print(c);
        }
    }
}

void editProject(u32 crashLocation){
    cleanup();
    loadMenuColors();
    fmt = PD::bgcolor;
    numPad = 0;
    PD::bgcolor = 0;
    PD::color += 7;
    char path[64];
    projectFilePath("src.js", path);
    mode.highlight = ~u16{};
    mode.src = readFileEx(path, nullptr);
    mode.firstLine = 0;
    PD::setFont(font5x7);
    read(pine::hash("\"FILLER"), 1, pine::hash("\"TEXT"), 0);
    onUpdate = updateEditor;
    u32 line = 0;
    u32 length = (reinterpret_cast<u32*>(mode.src)[-1] & 0xFFFF) << 2;
    auto lineBuffer = reinterpret_cast<u16*>(0x20004000);
    lineBuffer[0] = 0;
    for(u32 offset = 0; offset < length; ++offset){
        char c = mode.src[offset];
        if(c == '\n'){
            line++;
            lineBuffer[line] = offset + 1;
        }
    }
    mode.lineCount = line + 1;
    for(;line < 0x400; ++line)
        lineBuffer[line] = 0;

    if(crashLocation >= 0x20000000 && crashLocation < 0x20000800){
        File a2l;
        if( a2l.openRO("pine-2k/a2l") ){
            u32 i = (crashLocation - 0x20000000) & ~1;
            mode.highlight = 0;
            for(; i > 0 && mode.highlight == 0; i -= 2){
                a2l.seek(i);
                mode.highlight = a2l.read<u16>();
            }
            mode.firstLine = mode.highlight - 10;
            if(s32(mode.firstLine) < 0) mode.firstLine = 0;
        }
    }
}

void updateMenu(){
    auto prevselection = selection;
    auto projectNames = reinterpret_cast<char*>(0x20000000);
    bool draw = false;
    if(!mode.projectCount){
        cleanup();
        prevselection = -1;
        draw = true;
        loadMenuColors();

        PD::enableDirectPrinting(true);

        {
            Directory directory;
            directory.open("pine-2k");
            while(
                auto info = directory.read(
                    [](FileInfo &info){
                        if(!(info.fattrib & AM_DIR))
                            return false;
                        if(strlen(info.name()) > 15)
                            return false;
                        setProjectName(info.name());
                        char srcpath[64];
                        projectFilePath("src.js", srcpath);
                        return !!File{}.openRO(srcpath);
                    })
                ){
                setProjectName(info->name());
                for(u32 i=0; i<mode.projectCount; ++i){
                    if(strcmp(projectNames + i * 16, projectName) > 0){
                        char tmp[16];
                        strcpy(tmp, projectNames + i * 16);
                        strcpy(projectNames + i * 16, projectName);
                        strcpy(projectName, tmp);
                    }
                }
                strcpy(projectNames + mode.projectCount * 16, projectName);
                mode.projectCount++;
            }
        }

        while(PB::aBtn());
    }

    if(PB::upBtn()){
        selection--;
        if(selection < 0) selection = mode.projectCount - 1;
        draw = true;
    }

    if(PB::downBtn()){
        selection++;
        if(selection == mode.projectCount) selection = 0;
        draw = true;
    }

    if(PB::bBtn()){
        draw = true;
        PD::bgcolor = xorshift32(0, 255);
        PD::color = xorshift32(0, 255);
        File theme;
        theme.openRW("pine-2k/theme", true, false);
        theme << PD::bgcolor << PD::color;
        prevselection = -1;
    }

    if(PB::aBtn()){
        mode.projectCount = 0;
        for(int y = 0; y < 23; ++y){
            for(int x = 0; x < 28; ++x){
                PD::drawColorTile(x, y, 0);
            }
        }
        setProjectName(projectNames + selection * 16);

        PD::enableDirectPrinting(false);
        PD::setTASWindow(0, 0, 220, 176);

        if(PB::cBtn()){
            PD::color += 7;
            editProject(~u32{});
        }else{
            PD::color = 7;
            PD::bgcolor = 0;
            pickProject();
        }
        return;
    }

    if(!draw)
        return;

    if((prevselection >> 2) != (selection >> 2)){
        fillRect(40, 220 - 80, 0, 176, PD::palette[PD::bgcolor]);
        for(int i = 0; i < 4; ++i){
            auto dirOffset = selection >> 2 << 2;
            auto projectNumber = dirOffset + i;
            if(projectNumber < mode.projectCount){
                setProjectName(projectNames + projectNumber * 16);
                loadProjectHighscore();
                char srcpath[64];
                projectFilePath("title.565", srcpath);
                int y = 8 + i * 41;
                if(!drawFile(42, y, srcpath)){
                    fillRect(42, 136, y, 41, 0x2984);
                }
                PD::setCursor(42 + 57, y + 28);
                PD::directcolor = 0xFFFF;
                for(int x=countDigits(projectScore); x < 8; ++x){
                    PD::print('0');
                }
                PD::print(projectScore);
            }else{
                drawFile(42, 8 + i * 41, "pine-2k/empty.565");
            }
        }
    }

    auto tile = reinterpret_cast<const uint8_t*>(builtin(pine::hash("\"sBtn"), 0)) + 2;
    for(int y = 0; y < 23; ++y){
        for(int x = 0; x < 28; ++x){
            PD::drawTile(x, y, tile, PD::bgcolor, false);
        }
    }
    for(int y = 0; y < 3; ++y){
        for(int x = 0; x <= y; ++x){
            PD::drawTile(
                x + 1,
                y + (selection & 3) * 5 + 1,
                tile,
                PD::color,
                false
                );
            PD::drawTile(
                x + 1,
                (4 - y) + (selection & 3) * 5 + 1,
                tile,
                PD::color,
                false
                );

            PD::drawTile(
                27 - (x + 1),
                y + (selection & 3) * 5 + 1,
                tile,
                PD::color,
                false
                );
            PD::drawTile(
                27 - (x + 1),
                (4 - y) + (selection & 3) * 5 + 1,
                tile,
                PD::color,
                false
                );
        }
    }

    PD::shiftTilemap(0, 0);
    PD::setTASWindow(0, 0, 40, 176);
    PD::update();

    PD::shiftTilemap(3, 0);
    PD::setTASWindow(220 - 40, 0, 220, 176);
    PD::update();
    
    wait_ms(60);
}

void pickProject(){
    pine::deleteArrays();
    resTable.reset();
    loadProjectHighscore();

    char path[64];
    projectFilePath("splash.565", path);
    drawFile(0, 0, path);

    projectFilePath("src.js", path);
    run(path);
}

void init(bool crashed, u32 crashLocation){
    PD::adjustCharStep = 0;
    PD::bgcolor = 0;
    PD::invisiblecolor = 0;
    state += PC::getTime();
    #ifdef PROJ_DEVELOPER_MODE
    PD::update();
    #endif
    if(crashed){
        setProjectName( ((char*)magicPtr) + 12 );
        editProject(crashLocation);
    }
    else exitPine();
}

void update(){
    cclass = 0;
    cmask = 0;
    onUpdate();
}

extern "C" {
    void __attribute__((naked)) HardFault_Handler(void) {

        ASM(
            ldr r0, =0xE000ED00 \n
            ldr r0, [r0]        \n
            ldr r1, =1947       \n
            cmp r0, r1          \n
            bne 1f              \n
            bx lr               \n
            1:                  \n
            ldr r0, =0x20004000 \n
            ldr r1, =0xFEEDBEEF \n
            ldr r2, [sp, 0x18]  \n
            ldr r3, [sp, 0x14]  \n
            stm r0!, {r1-r3}    \n
            ldr r6, =projectName \n
            ldm r6!, {r1-r4}     \n
            stm r0!, {r1-r5}    \n
            ldr r1, =0x05FA0004 \n
            ldr r0, =0xE000ED0C \n
            str r1, [r0]        \n
            );
    }
}

int main(){
    {
        bool crashed = *magicPtr == magic;
        u32 crashLocation = magicPtr[1] >= 0x20000000 && magicPtr[1] < 0x20000800 ? magicPtr[1] : magicPtr[2];
        if( !crashed && ((volatile uint32_t *) 0xE000ED00)[0] != 1947 ){
            PC::begin();
        } else {
            PC::init();
            PD::begin();
        }

        if(crashed && crashLocation < 0x20000000 ) crashed = false;
        init(crashed, crashLocation);
    }
    while(true){
        if(PC::update())
            update();
    }
}
