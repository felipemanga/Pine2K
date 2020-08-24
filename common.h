
constexpr auto version = 10400;

void editProject(u32 crashLocation, u32 errLine, const char *msg = nullptr);


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
        const char *msg;
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
        devmode = theme.read<char>();
    }else{
        PD::bgcolor = 209;
        PD::color = 88;
    }
}

void stringFromHash(u32 hash, char *str, u32 max){
    int i = 0;
    auto pos = resTable.find(hash);
    if(!pos){
        auto ptr = reinterpret_cast<char *>(pine::arrayFromPtr(hash));
        if(ptr){
            for(; i < max - 1; ++i){
                auto ch = str[i] = ptr[i];
                if(!ch) break;
            }
        }
    }else{
        auto& file = resTable.at(pos);
        for(; i < max - 1; ++i){
            auto ch = str[i] = file.read<char>();
            if(!ch) break;
        }
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
    }else if(auto ptr = pine::arrayFromPtr(value)){
        auto len = (ptr[-1] & 0xFFFF) << 2;
        auto cptr = reinterpret_cast<char*>(ptr);
        for(int i = 0; i<len; ++i){
            auto ch = cptr[i];
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
        return;
    }

    auto ptr = pine::arrayFromPtr(value);
    if(ptr){
        auto len = (ptr[-1] & 0xFFFF) << 2;
        auto cptr = reinterpret_cast<char*>(ptr);
        for(int i = 0; i<len; ++i){
            auto ch = cptr[i];
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
        return;
    }

    printNumber(value);
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
    // LOG("Loading res ", (const char*) path, "\n");
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
                // char buf[33];
                // stringFromHash(hash, buf, 32);
                // LOG("Missing resource ", (const char*) buf, " i=", low, " pivot=", (void*)pivot, " hash=", (void*)hash, "\n");
                return 0;
            }
            if(pivot < hash){
                low = mid + 1;
            }else{
                hi = mid - 1;
            }
        }
        if(low > hi) return 0;
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
    // LOG("Read resource ", hash, " to ", (void*)ptr, " ", len, " \n");
    return reinterpret_cast<u32>(ptr);
}

bool writeFile(u32 nameHash, u32 val){
    if(!val) return false;
    char path[128];
    pathFromHash(nameHash, path, 128);
    File file;
    if(!file.openRW(path, true, false))
        return false;
    auto ptr = pine::arrayFromPtr(val);
    if(ptr){
        auto len = ptr[-1] & 0xFFFF;
        LOG("Writing array ", ptr, " len ", len, " to ", path, " [0]=", ptr[0], "\n");
        file.write(reinterpret_cast<void*>(ptr), len * 4);
        return true;
    }

    auto pos = resTable.find(val);
    if(pos){
        auto& strfile = resTable.at(pos);
        char ch;
        do{
            ch = strfile.read<char>();
            file << ch;
        }while(ch);
        return true;
    }

    file << val;
    return true;
}

char *readFileEx(char *path, char *ptr){
    File file;
    LOG("Reading ", (const char*) path, "\n");
    if(!file.openRO(path)){
        return 0;
    }
    if(!ptr){
        u32 size = file.size();
        if(size & 3) size += 4;
        ptr = reinterpret_cast<char*>(pine::arrayCtr(size >> 2));
    // }else{
    //     LOG("Load to ptr\n");
    }

    file.read(ptr, file.size());
    // u32 hash = pine::hash(ptr, file.size());
    // if(resTable.find(hash)){
        //     ptr = reinterpret_cast<char*>(hash);
        //     LOG("String found\n");
        // }else{
    LOG("Read array ", ptr, "[0]=", ((uint32_t*)ptr)[0], "\n");
        // }

    return ptr;
}

u32 readFile(u32 nameHash, char *ptr){
    auto array = pine::arrayFromPtr(nameHash);
    if(array){
        u32 len = array[-1] & 0xFFFF;
        for(u32 i=0; i<len; ++i){
            array[i] = readFile(array[i], nullptr);
            // if(array[i])
            //     LOG("Read ", i, " s:", reinterpret_cast<u32*>(array[i])[-1]&0xFFFF, "\n");
        }
        return nameHash;
    }

    if(resourceFile){
        auto ret = readResource(nameHash, ptr);
        if(ret)
            return ret;
    }

    char path[128];
    pathFromHash(nameHash, path, 128);

    auto len = strlen(path);
    if(strcmp(path + len - 4, ".res") == 0)
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
    case pine::hash("\"TILE"):{
        u32 tile = reinterpret_cast<u32>(PD::getTile(a, b));
        if(tile >=0x10000000 && tile < 0x10008000)
            tile -= 2;
        return tile;
    }
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
        ptr += (t >= 0x10000000) ? 2 : 0;
        // LOG(ptr, "\n");
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
        // LOG("Hash lookup ", index, "\n");
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
    LOG("exec: \"", (const char*) path, "\"\n");
    onUpdate = updateMenu;
    pine::deleteArrays();
    resTable.reset();
    run(path);
}
