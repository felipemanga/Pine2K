#include "Pokitto.h"
#include <LibAudio>
#include <MemOps>
#include <LibTrig>
#include "compiler/SimplePine.h"
#include "assets.h"
#include "miloslav.h"
#include "TextMode.h"

using PD = Pokitto::Display;
using PS = Pokitto::Sound;
using PC = Pokitto::Core;
using PB = Pokitto::Buttons;
using u32 = pine::u32;
using s32 = pine::s32;
using u16 = pine::u16;

#include "hardware.h"
#include "common.h"

extern unsigned int __allocated_memory__;


static constexpr int screenWidth = PROJ_LCDWIDTH;
static constexpr int screenHeight = PROJ_LCDHEIGHT;
extern constexpr uint32_t tileW = POK_TILE_W;
extern constexpr uint32_t tileH = POK_TILE_H;
extern constexpr uint32_t mapW = screenWidth / tileW + 2;
extern constexpr uint32_t mapH = screenHeight / tileH + 2;
extern const uint8_t *tilemap[mapH * mapW];
struct Sprite;
using draw_t = void (*)(uint8_t *line, Sprite &sprite, int y);
struct Sprite {
    int16_t x, y;
    const void *data;
    draw_t draw;
    uint8_t maxY;
    uint8_t b1, b2, b3;
    uint32_t cclass = 0;
};
extern Sprite spriteBuffer[];

void run(const char *path){
    cleanup();
    resTable.setCache(reinterpret_cast<u32*>(tilemap), sizeof(tilemap));
    auto symTable = new (reinterpret_cast<void*>(spriteBuffer)) ia::InfiniteArray<pine::Sym, 60>("pine-2k/symbols.tmp");
    pine::SimplePine pine(path, resTable, *symTable);

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
    pine.setConstant("file", readFile, true, true);
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
    state += PC::getTime();
    // LOG("DYNMEM: ", __allocated_memory__, "\n");
    Audio::setVolume(0);
    if(pine.compile(devmode)){
        if( s32(0x800 - pine::globalCount * 4) > 0 ){
            resTable.setCache(
                reinterpret_cast<u32*>(0x20004000 + pine::globalCount * 4),
                0x800 - pine::globalCount * 4
                );
            fillTiles(0);
        }
        Audio::setVolume(PS::globalVolume);
        onUpdate = nullptr;
        PD::collisionCallback = +[](uint32_t, uint32_t){};
        pine.run();
        if(!onUpdate)
            onUpdate = pine.getCall<void()>("update");

        extern char   _pvHeapStart; /* Set by linker.  */
        u32 total = (reinterpret_cast<u32>(&_pvHeapStart) - 0x10000000) +  __allocated_memory__;
        LOG("DATAMEM: ", total, " bytes (", total * 100 / (32*1024), "%) used.\n");
    } else {
        editProject(~u32{}, pine.getLine(), pine.getError());
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
        if(s32(firstLine) < 0) firstLine = 0;
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
        int c = fmt + 8;
        const char *line = mode.src + lineBuffer[i];
        if(i == limit - 1 && mode.msg){
            c = 170;
            line = mode.msg;
        }
        if(i == (mode.highlight - 1) || c == 170){
            for( u32 j = 0; j<27; ++j )
                PD::drawColorTile(j, i - firstLine, c);
            PD::drawColorTile(27, i - firstLine, 174);
        }

        textFiller->setCursor(0, i - firstLine);
        u32 maxj = firstColumn + (220 / 6);
        for( u32 j = firstColumn; j < maxj; ++j ){
            char c = line[j];
            if(c == 13 || c == 10 || c == 0)
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

void editProject(u32 crashLocation, u32 errLine, const char *msg){
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
            errLine = 0;
            for(; s32(i) > 0; i -= 2){
                a2l.seek(i);
                errLine = a2l.read<u16>();
                if(errLine){
                    errLine--;
                    break;
                }
            }
        }
    }

    if(errLine != ~u32{}){
        mode.highlight = errLine;
        mode.firstLine = mode.highlight - 10;
        if(s32(mode.firstLine) < 0) mode.firstLine = 0;
    }

    mode.msg = msg;
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

    bool updateTheme = false;
    if(PB::bBtn()){
        draw = true;
        if(PB::cBtn()){
            devmode = !devmode;
        }else{
            PD::bgcolor = xorshift32(0, 255);
            PD::color = xorshift32(0, 255);
        }
        prevselection = -1;
        updateTheme = true;
    }

    if(updateTheme){
        File theme;
        theme.openRW("pine-2k/theme", true, false);
        theme << PD::bgcolor << PD::color << devmode;
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
            editProject(~u32{}, ~u32{});
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
        if(devmode){
            PD::directcolor = PD::palette[PD::bgcolor + 1];
            PD::setCursor(80, 0);
            PD::print("DEV MODE");
        }
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
    // LOG("SP: ", sizeof(ia::InfiniteArray<pine::Sym, 60>), "\n");
    PD::adjustCharStep = 0;
    loadMenuColors();
    PD::invisiblecolor = 0;
    if(crashed && devmode){
        setProjectName( ((char*)magicPtr) + 12 );
        editProject(crashLocation, ~u32{}, "Halt");
    }
    else exitPine();
}

void update(){
    cclass = 0;
    cmask = 0;
    onUpdate();
}
