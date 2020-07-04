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

void nopUpdate(){}
void (*onUpdate)() = nopUpdate;

pines::ResTable resTable(1024);
char projectName[16];

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

void pathFromHash(pines::u32 hash, char *str, pines::u32 max){
    const char *src = "pines/";
    while(*src){ *str++ = *src++; max--; }
    src = projectName;
    while(*src){ *str++ = *src++; max--; }
    *str++ = '/'; max--;
    stringFromHash(hash, str, max);
}

void projectFilePath(const char *file, char *path){
    char *out = path;
    const char *str = "pines/";
    while(*str) *out++ = *str++;
    str = projectName;
    while(*str) *out++ = *str++;
    *out++ = '/';
    str = file;
    while(*str) *out++ = *str++;
    *out++ = 0;
}

void music(pines::u32 hash){
    char path[64];
    pathFromHash(hash, path, sizeof(path));
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
    if(!ptr)
        return;
    auto iptr = reinterpret_cast<uintptr_t>(ptr);
    if( iptr > 0x10000000 && iptr < 0x10008000 && (iptr & 0x3) == 0){
        auto len = (reinterpret_cast<uint32_t*>(iptr)[-1]&0xFFFF) * 4;
        auto recolor = ptr[0];
        auto bpp = ptr[1];
        auto s = ptr[2] * ptr[3];
        if(bpp == 8);
        else if(bpp == 4) s >>= 1;
        else if(bpp == 1) s >>= 3;
        else goto defaultDraw;
        if( (s + 4) != len) goto defaultDraw;
        PD::setColorDepth(bpp);
        PD::drawSprite(x, y, ptr + 2, flip, mirror, PD::color + recolor);
        PD::setColorDepth(8);
        return;
    }
defaultDraw:
    PD::drawSprite(x, y, ptr, flip, mirror, PD::color);
}

pines::u32 projectScore;
void highscore(pines::u32 score){
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

void setWindow(int x, int w, int y, int h){
    write_command(0x38); write_data(x + w - 1);
    write_command(0x39); write_data(x);
    write_command(0x36); write_data(y + h - 1);
    write_command(0x37); write_data(y);
    write_command(0x20); write_data(y);
    write_command(0x21); write_data(x);
}

void fillRect(int x, int w, int y, int h, int color){
    pines::u16 out[220];
    setWindow(x, w, y, h);
    write_command(0x22);
    CLR_CS_SET_CD_RD_WR;
    SET_MASK_P2;
    for(int i=0; i<220; ++i) out[i] = color;
    for(int i=0; i<h; ++i)
        flush(out, w);
}

bool drawFile(int cx, int cy, const char *file){
    pines::u16 out[220];
    File f;
    if(!f.openRO(file)){
        return false;
    }

    pines::s32 w = f.read<pines::u16>();
    pines::s32 h = f.read<pines::u16>();
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
        flush(out, w);
    }

    return true;
}

void splash(int cx, int cy, pines::u32 hash){
    char path[64];
    pathFromHash(hash, path, sizeof(path));
    if(!drawFile(cx, cy, path))
        LOG("Could not open:\n[", (const char*) path, "]\n");
}

uint32_t indexSize;
File *resourceFile = nullptr;
struct {
    pines::u32 hash;
    pines::u32 offset;
} hashCache[16];

pines::u32 loadRes(pines::u32 nameHash){
    char path[128];
    pathFromHash(nameHash, path, 128);
    if(!resourceFile)
        resourceFile = new File();
    if(!resourceFile->openRO(path)){
        return 0;
    }
    indexSize = resourceFile->read<uint32_t>();
    return 1;
}

pines::u32 readResource(pines::u32 hash, char *ptr){
    using namespace pines;

    if(!resourceFile || !hash)
        return 0;

    auto& file = *resourceFile;
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

    auto len = file.read<pines::u32>();
    bool created = false;
    if(!ptr){
        created = true;
        u32 size = len;
        if(size & 3) size += 4;
        ptr = reinterpret_cast<char*>(pines::arrayCtr(size >> 2));
    }

    // u32 read =
        file.read(ptr, len);
    // LOG("Read resource ", hash, " to ", (void*)ptr, " ", len, " ", read, "\n");
    return reinterpret_cast<u32>(ptr);
}

pines::u32 readFile(pines::u32 nameHash, char *ptr){
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
    default: break;
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

void fillTiles(pines::u32 color){
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

const char *builtin(pines::u32 hash, pines::u32 index){
    auto pos = reinterpret_cast<const pines::u32*>(&assets);
    if(uintptr_t(pos) & 0x3){
        LOG("Unaligned data\n");
        while(true);
    }
    auto count = *pos++;
    if( hash == pines::hash("\"INDEX") ){
        LOG("Hash lookup ", index, "\n");
        while(count--){
            auto candidate = *pos++;
            auto b = reinterpret_cast<const char *>(pos) + 2;
            if(index-- == 0)
                return b;
            pos += 1 + (int(b[0]) * int(b[1]) >> 2);
        }
    } else {
        while(count--){
            auto candidate = *pos++;
            auto b = reinterpret_cast<const char *>(pos) + 2;
            if(candidate == hash)
                return b;
            pos += 1 + (int(b[0]) * int(b[1]) >> 2);
        }
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
}

void run(const char *);
pines::u32 execHash;
void exec(){
    char path[64];
    pathFromHash(execHash, path, sizeof(path));
    onUpdate = updateMenu;
    pines::deleteArrays();
    resTable.reset();
    run(path);
}


void run(const char *path){
    if(resourceFile){
        delete resourceFile;
        resourceFile = nullptr;
    }

    pines::SimplePine pine(path, resTable);
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
    pine.setConstant("cursor", PD::setCursor);
    pine.setConstant("color", setColor);
    pine.setConstant("background", setBackground);
    pine.setConstant("pressed", pressed);
    pine.setConstant("justPressed", justPressed);
    pine.setConstant("time", PC::getTime);
    pine.setConstant("io", read);
    pine.setConstant("file", readFile, true);
    pine.setConstant("music", music);
    pine.setConstant("highscore", highscore);
    pine.setConstant("exit", exitPine);
    pine.setConstant("exec", +[](pines::u32 hash){
                                  execHash = hash;
                                  onUpdate = exec;
                              });
    if(pine.compile()){
        pine.run();
        onUpdate = pine.getCall<void()>("update");
    }
    if(!onUpdate)
        onUpdate = nopUpdate;
}

void pickProject();
int projectCount;
int selection;

int countDigits(uint32_t x){
    int d = 0;
    if(!x) return 1;
    while(x){
        x /= 10;
        d++;
    }
    return d;
}

void updateMenu(){
    auto prevselection = selection;
    auto projectNames = reinterpret_cast<char*>(0x20000000);
    bool draw = false;
    if(!projectCount){
        pines::deleteArrays();
        resTable.reset();
        prevselection = -1;
        draw = true;
        PD::bgcolor = 209;
        PD::color = 88;
        PD::enableDirectPrinting(true);

        Directory directory;
        directory.open("pines");
        while(
            auto info = directory.read(
            [](FileInfo &info){
                if(!(info.fattrib & AM_DIR))
                    return false;
                setProjectName(info.name());
                char srcpath[64];
                projectFilePath("src.pines", srcpath);
                return !!File{}.openRO(srcpath);
            })
            ){
            strcpy(projectNames + projectCount * 16, info->name());
            projectCount++;
        }

        while(PB::aBtn());
    }

    if(PB::upBtn()){
        selection--;
        if(selection < 0) selection = projectCount - 1;
        draw = true;
    }

    if(PB::downBtn()){
        selection++;
        if(selection == projectCount) selection = 0;
        draw = true;
    }

    if(PB::bBtn()){
        draw = true;
        PD::bgcolor = xorshift32(0, 255);
        PD::color = xorshift32(0, 255);
    }

    if(PB::aBtn()){
        projectCount = 0;
        for(int y = 0; y < 23; ++y){
            for(int x = 0; x < 28; ++x){
                PD::drawColorTile(x, y, 0);
            }
        }
        setProjectName(projectNames + selection * 16);
        pickProject();
        return;
    }

    if(!draw)
        return;

    if((prevselection >> 2) != (selection >> 2)){
        fillRect(41, 220 - 82, 0, 176, 0);
        for(int i = 0; i < 4; ++i){
            auto dirOffset = selection >> 2 << 2;
            auto projectNumber = dirOffset + i;
            if(projectNumber < projectCount){
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
                drawFile(42, 8 + i * 41, "pines/empty.565");
            }
        }
    }

    auto tile = reinterpret_cast<const uint8_t*>(builtin(pines::hash("\"sBtn"), 0)) + 2;
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
    pines::deleteArrays();
    resTable.reset();
    loadProjectHighscore();
    PD::color = 0;
    PD::bgcolor = 0;
    PD::enableDirectPrinting(false);
    PD::setTASWindow(0, 0, 220, 176);

    char path[64];
    projectFilePath("splash.565", path);
    drawFile(0, 0, path);

    projectFilePath("src.pines", path);
    run(path);
}

void init(){
    PD::adjustCharStep = 0;
    PD::bgcolor = 0;
    PD::invisiblecolor = 0;
    state += PC::getTime();
    PD::loadRGBPalette(miloslav);
    exitPine();
}

void update(){
    onUpdate();
}
