#pragma once

#include <stdexcept>

#include <File>
#include <LibLog>

#define CRASH(msg) while(true)
// #define CRASH(msg) throw std::runtime_error(msg)

#define ASMFUNC(RETTYPE, NAME, ARGS, x...) RETTYPE NAME ARGS __attribute__((naked)) { __asm__ volatile ( ".syntax unified\n" #x ); }

#if !__cpp_consteval
#define consteval constexpr
#endif

namespace cg {

    using u32 = uint32_t;
    using u16 = uint16_t;

    enum ConditionCode : u32 {
        EQ = 0,
        NE,
        CS,
        CC,
        MI,
        PL,
        VS,
        VC,
        HI,
        LS,
        GE,
        LT,
        GT,
        LE,
        HS = CS,
        LO = CC
    };

    enum RSYS : u32 {
        APSR    = 0b00000,
        IAPSR   = 0b00001,
        EAPSR   = 0b00010,
        XPSR    = 0b00011,
        IPSR    = 0b00101,
        EPSR    = 0b00110,
        IEPSR   = 0b00111,
        MSP     = 0b01000,
        PSP     = 0b01001,
        PRIMASK = 0b10000,
        CONTROL = 0b10100
    };

    class Label {
    public:
        const u32 value;
        constexpr Label(u32 hash) : value( hash ) {}
    };

    constexpr Label operator "" _lbl(const char *str, std::size_t len){
        u32 v = 5381;
        while(len--){
            v = (v*31) + *str++;
        }
        return Label(v);
    }

    class Reg {
    public:
        const u32 value;
        explicit constexpr Reg(u32 v) : value(v) {}
    };

    class RegLow : public Reg {
    public:
        explicit constexpr RegLow(u32 v) : Reg(v) {}
    } constexpr R0(0),
        R1(1),
        R2(2),
        R3(3),
        R4(4),
        R5(5),
        R6(6),
        R7(7);

    class RegHigh : public Reg {
    public:
        constexpr RegHigh(u32 v) : Reg(v) {}
    } constexpr R8(8),
        R9(9),
        R10(10),
        R11(11),
        R12(12),
        R13(13);

    template <u32 V>
    class RegX : public RegHigh {
    public:
        constexpr RegX() : RegHigh(V) {};
    };

    using SP_t = RegX<13>;
    SP_t SP;

    using LR_t = RegX<14>;
    LR_t LR;

    using PC_t = RegX<15>;
    PC_t PC;

    template<u32 bits, u32 shift = 0>
    class Imm {
    public:
        const u32 value;

        consteval Imm(u32 v) : value((v >> shift) & (~u32{} >> (32 - bits))) {
            if((value << shift) != v){
                CRASH("Can't encode " + std::to_string(v));
            }
        }

        template<u32 ob, u32 os>
            consteval Imm(Imm<ob, os> o) : value(((o.value << os) >> shift) & (~u32{} >> (32 - bits))) {
            auto v = o.value << os;
            if((value << shift) != v){
                CRASH("Can't encode " + std::to_string(v));
            }
        }
    };

    using I3 = Imm<3, 0>;
    using I8 = Imm<8, 0>;

    namespace detail {
        template <std::size_t N>
        constexpr int to_number(const int (&a)[N])
        {
            int res = 0;
            for (auto e : a) {
                res *= 10;
                res += e;
            }
            return res;
        }

        constexpr u32 bits_in_number(const u32 value){
            u32 bits = 0;
            for(u32 i=0; i<32; ++i){
                if(value >> i) bits++;
                else break;
            }
            return bits;
        }
    }

    template <char ... Ns>
    constexpr auto operator ""_imm ()
    {
        constexpr u32 value = detail::to_number({(Ns - '0')...});
        constexpr u32 bits = detail::bits_in_number(value);
        return Imm<bits, 0>(value);
    }

    class FileWriter {
        File file;
        u32 offset;
    public:
        FileWriter(const char *name) : offset(0){
            file.openRW(name, true, false);
        }

        FileWriter(File&& file) : file(std::move(file)) {
            offset = this->file.tell();
        }

        void newChunk(){
            offset = file.tell();
        }

        template<typename T>
        FileWriter& operator << (T opcode){
            file << opcode;
            return *this;
        }

        u32 tell(bool abs = false){
            return abs ? (file.tell() >> 1) : ((file.tell() - offset) >> 1);
        }

        void seek(u32 pos, bool abs = false){
            file.seek((pos << 1) + (abs ? 0 : offset));
        }

        u16 read(){
            return file.read<u16>();
        }

        bool full(){
            return false;
        }
    };

    template<u32 BufferSize>
    class BufferWriter {
        u16 *buffer;
        u32 offset = 0;
        u32 pos = 0;
        u32 max = 0;

        static void *allocStatic(){
            static u16 b[BufferSize >> 1];
            return b;
        }

    public:
        BufferWriter(void *buffer = allocStatic()) : buffer((u16*) buffer) {}

        template <typename Type>
        Type* function() {
            return reinterpret_cast<Type*>(reinterpret_cast<uintptr_t>(buffer) | 1);
        }

        template<typename T>
        BufferWriter& operator << (T opcode){
            if((pos + offset) >= (BufferSize >> 1)){
                pos++;
                pos += sizeof(T) == 4;
            }else{
                buffer[offset + pos++] = opcode;
                if(sizeof(T) == 4){
                    buffer[offset + pos++] = opcode >> 16;
                }
            }
            if(pos > max) max = pos;
            return *this;
        }

        void newChunk(){
            offset += pos;
            pos = 0;
        }

        bool full(){
            return (offset + pos) >= BufferSize;
        }

        u16 *getBuffer(){
            return buffer;
        }

        u32 size(){
            return max;
        }

        u32 tell(bool abs = false){
            return abs ? pos + offset : pos;
        }

        void seek(u32 pos, bool abs = false){
            if(abs) offset = 0;
            this->pos = pos;
        }

        u16 read(){
            if((pos + offset) >= (BufferSize >> 1)){
                return 0;
            }
            return buffer[offset + pos++];
        }
    };

    template <typename CodeWriter, u32 symTableCapacity = 256, u32 constPoolCapacity = 128, bool verbose = true>
    class CodeGen {
        CodeWriter& writer;
        const char *error = nullptr;
        u32 symCount = 0;
        u32 constCount = 0;
        u32 dataStart = ~u32{};

        struct Sym {
            u32 hash;
            u32 address;
        } symTable[symTableCapacity];

        u32 constPool[constPoolCapacity];

        constexpr u32 s(ConditionCode c, u32 p){
            return static_cast<u32>(c) << p;
        }

        constexpr u32 s(RSYS r, u32 p){
            return static_cast<u32>(r) << p;
        }

        constexpr u32 s(Reg r, u32 p){
            return r.value << p;
        }

        constexpr u32 s(RegLow r, u32 p){
            if(r.value > 7){
                error = "Forbidden High Reg";
                while(true);
            }
            return r.value << p;
        }

        constexpr u32 sw(RegLow r, u32 p){
            clearConst(r);
            return s(r, p);
        }

        template<typename ImmX>
        constexpr u32 s(ImmX i, u32 p){
            return i.value << p;
        }

        constexpr u32 s(Reg r, u32 h, u32 l){
            auto u = r.value;
            return (((u>>3)&1) << h) | ((u & 7) << l);
        }

        constexpr u32 sw(Reg r, u32 h, u32 l){
            clearConst(RL(r.value));
            auto u = r.value;
            return (((u>>3)&1) << h) | ((u & 7) << l);
        }

        template<typename OpType>
        void symRef(Label lbl, OpType& op){
            for(u32 i=0; i<symCount; ++i){
                if( symTable[i].hash == lbl.value ){
                    op |= i;
                    return;
                }
            }
            op |= symCount;
            symTable[symCount++] = { lbl.value, ~u32{} };
        }

        u32 regHasConst = 0;
        u32 regConst[15];

    public:
        CodeGen(CodeWriter& writer) : writer(writer) {}

        operator bool () {
            return !error;
        }

        const char *getError(){
            return error;
        }

        CodeWriter& getWriter(){ return writer; }

        u32 tell(){
            return writer.tell(true) << 1;
        }

        void link(){
            if(error){
                LOG(error, "\n");
                while(true);
                return;
            }
            regHasConst = 0;
            LOGD("Linking\n");
            u32 end = writer.tell();
            writer.seek(0);
            for(u32 i = 0; i < end && i < dataStart; ++i){
                u16 op = writer.read();
                u32 bits = 0;
                u16 low = 0;

                LOGD(i << 1, " ", op, "\n");

                if( (op & 0xFF00) == 0b1101'1110'0000'0000 ){
                    u32 skip = op & 0xFF;
                    op = 0b1110'0000'0000'0000 | skip;
                    writer.seek(i);
                    writer << u16(op);
                    i += skip + 1;
                    writer.seek(i+1);
                    LOGD("Found UDF ", skip, "\n");
                    continue;
                } else if( (op & 0xF800) == 0b0100'1000'0000'0000 ){
                    u32 skip = (((dataStart) - (i + 1)) >> 1) + (op & 0xFF);
                    if(skip > 0xFF){
                        error = "Out of range";
                        return;
                    }
                    op = (op & 0xFF00) | skip;
                    writer.seek(i);
                    writer << u16(op);
                    LOGD("Found const ref\n");
                    continue;
                } else if( (op & 0xF000) == 0b1101'0000'0000'0000 ) bits = 8;
                else if( (op & 0xF800) == 0b1110'0000'0000'0000 ) bits = 11;
                else if( (op & 0xF800) == 0b1111'0000'0000'0000 ){
                    low = writer.read();
                    if( (low & 0xD000) == 0b1101'0000'0000'0000 ){
                        bits = 10;
                    }else continue;
                }
                else continue;

                u32 addrMask = ~u32{} >> (32 - bits);
                u32 symId = op & addrMask;
                op &= ~addrMask;
                u32 address = symTable[symId].address;
                if( address == ~u32{} ){
                    error = "Unresolved Symbol";
                    return;
                }

                address = ((address >> 1) - (i + 2));
                writer.seek(i);
                if(low){
                    u32 imm11 = address & 0x7FF;
                    u32 imm10 = (address >> 10) & 0x3FF;
                    u32 I2 = (address >> 22) & 1;
                    u32 I1 = (address >> 23) & 1;
                    u32 S = address < 0;
                    u32 J2 = 1 ^ I2 ^ S;
                    u32 J1 = 1 ^ I1 ^ S;
                    writer << (0xF000 | (S << 10) | imm10);
                    writer << (0xD000 | (J1 << 13) | (J2 << 11) | imm11);
                    ++i;
                }else{
                    address = address & addrMask;
                    writer << u16(op | address);
                }
            }

            writer.seek(end);
            writer.newChunk();
            constCount = 0;
            symCount = 0;
        }

#define OP16(NAME, ARGS, OP)                                    \
        void NAME ARGS {                                        \
            if(error) return;                                   \
            if(verbose) LOGD( reinterpret_cast<void*>(uintptr_t(writer.tell(true) << 1)), ": ", #NAME, " (L", __LINE__, ")\n" ); \
            if(writer.full()) error = "Writer Full";            \
            else writer << u16(OP);                             \
        }

#define OP16L(NAME, ARGS, SYMBOL, OP)                           \
        void NAME ARGS {                                        \
            if(error) return;                                   \
            if(verbose) LOGD( reinterpret_cast<void*>(uintptr_t(writer.tell(true) << 1)), ": ", #NAME, " (L", __LINE__, ")\n" ); \
            auto op = u16(OP);                                  \
            symRef(SYMBOL, op);                                 \
            if(writer.full()) error = "Writer Full";            \
            else writer << op;                                  \
        }

#define OP32(NAME, ARGS, OP)                                    \
        void NAME ARGS {                                        \
            if(error) return;                                   \
            if(verbose) LOGD( reinterpret_cast<void*>(uintptr_t(writer.tell(true) << 1)), ": ", #NAME, " (L", __LINE__, ")\n" ); \
            if(writer.full()) error = "Writer Full";            \
            else writer << u32(OP);                             \
        }

#define OP32L(NAME, ARGS, SYMBOL, OP)                           \
        void NAME ARGS {                                        \
            if(error) return;                                   \
            if(verbose) LOGD( reinterpret_cast<void*>(uintptr_t(writer.tell(true) << 1)), ": ", #NAME, " (L", __LINE__, ")\n" ); \
            auto op = u32(OP);                                  \
            symRef(SYMBOL, op);                                 \
            if(writer.full()) error = "Writer Full";            \
            else writer << op;                                  \
        }

        using RL = RegLow;
        using RX = Reg;
        using RH = RegHigh;

        void label(Label lbl) {
            regHasConst = 0;
            for(u32 i=0; i<symCount; ++i){
                if(symTable[i].hash == lbl.value){
                    if(symTable[i].address != ~u32{}){
                        error = "Symbol redeclared";
                        return;
                    }
                    symTable[i].address = writer.tell() << 1;
                    return;
                }
            }
            symTable[symCount++] = { lbl.value, writer.tell() << 1 };
        }

        CodeGen& operator [] (Label lbl){
            label(lbl);
            return *this;
        }

        void POOL(){
            if(error) return;
            dataStart = writer.tell();
            if(writer.tell(true) & 1){
                NOP();
                dataStart++;
            }
            // LOGD("POOL ", reinterpret_cast<void*>(dataStart << 1), "\n");
            for(u32 i=0; i<constCount; ++i){
                // LOGD("CONST ", i, " ", reinterpret_cast<void*>(constPool[i]), "\n");
                writer << constPool[i];
            }
            constCount = 0;
        }

        void U32(u32 v){
            if(error) return;
            if(verbose) LOGD( reinterpret_cast<void*>(uintptr_t(writer.tell(true) << 1)), ": U32 (L", __LINE__, ")\n" );

            if(dataStart == ~u32{}) dataStart = writer.tell();
            if(writer.full()) error = "Writer Full";
            else writer << v;
        }

        void STRING(const char *str){
            if(error) return;
            if(verbose) LOGD( __LINE__, ": STRING\n");
            if(dataStart == ~u32{}) dataStart = writer.tell();
            if(writer.full()){
                error = "Writer Full";
                return;
            }
            while(true){
                u16 l = str[0];
                u16 h = l ? str[1] : 0;
                writer << u16((h << 8) | l);
                if(!h) break;
                str += 2;
            }
        }

        OP16(ADCS, (RL rdn, RL rm), (0b0100'0001'0100'0000 | s(rm, 3) | sw(rdn, 0)))

        OP16(ADDS, (RL rd, RL rn, I3 i), (0b0001'1100'0000'0000 | s(i, 6) | s(rn, 3) | sw(rd, 0)))

        OP16(ADDS, (RL rdn, I8 i), (0b0011'0000'0000'0000 | sw(rdn, 8) | s(i, 0)))

        OP16(ADDS, (RL rd, RL rn, RL rm), (0b0001'1000'0000'0000 | s(rm, 6) | s(rn, 3) | sw(rd, 0)))

        void ADDS(RL rnd, RL rm){
            ADDS(rnd, rnd, rm);
        }

        OP16(ADD, (RX rdn, RX rm), (0b0100'0100'0000'0000 | s(rm, 3) | sw(rdn, 7, 0)))

        OP16(ADD, (RL rd, SP_t, Imm<8, 2> i), (0b1010'1000'0000'0000 | sw(rd, 8) | s(i, 0)))

        OP16(ADD, (SP_t, Imm<7, 2> i), (0b1011'0000'0000'0000 | s(i, 0)))

        OP16(ADD, (RX rdm, SP_t), (0b0100'0100'1101'0000 | sw(rdm, 7, 0)))

        OP16(ADD, (SP_t, RX rm), (0b0100'0100'1000'0101 | s(rm, 3)))

        OP16(ADR, (RL rd, Imm<8, 2> imm), (0b1010'0000'0000'0000 | sw(rd, 8) | s(imm, 0)))

        OP16(ANDS, (RL rdn, RL rm), (0b0100'0000'0000'0000 | s(rm, 3) | sw(rdn, 0)))

        OP16(ASRS, (RL rd, RL rm, Imm<5, 0> imm), (0b0001'0000'0000'0000 | s(imm, 6) | s(rm, 3) | sw(rd, 0)))

        void ASRS(RL rd, I8 imm){
            ASRS(rd, rd, imm);
        }

        OP16(ASRS, (RL rdn, RL rm), (0b0100'0001'0000'0000 | s(rm, 3) | sw(rdn, 0)))

        OP16L(B, (ConditionCode cc, Label label), label, (0b1101'0000'0000'0000 | s(cc, 8)))

        OP16L(B, (Label label), label, (0b1110'0000'0000'0000))

        OP16(BIC, (RL rdn, RL rm), (0b0100'0011'1000'0000 | s(rm, 3) | sw(rdn, 0)))

        OP16(BKPT, (I8 i), (0b1011'1110'0000'0000 | s(i, 0)))

        OP32L(BL, (Label label), label, (0b1111'0000'0000'0000'1101'0000'0000'0000))

        OP16(BLX, (RL rm), (0b0100'0111'1000'0000 | s(rm, 3)))

        OP16(BX, (RX rm), (0b0100'0111'0000'0000 | s(rm, 3)))

        OP16(CMN, (RL rn, RL rm), (0b0100'0010'1100'0000 | s(rm, 3) | s(rn, 0)))

        OP16(CMP, (RL rn, I8 imm), (0b0010'1000'0000'0000 | s(rn, 8) | s(imm, 0)))

        OP16(CMP, (RL rn, RL rm), (0b0100'0010'1000'0000 | s(rm, 3) | s(rn, 0)))

        OP16(CMP, (RX rn, RX rm), (0b0100'0101'0000'0000 | s(rm, 3) | s(rn, 7, 0)))

        OP16(EORS, (RL rdn, RL rm), (0b0100'0000'0100'0000 | s(rm, 3) | sw(rdn, 0)))

        template<typename ... Args>
            OP16(LDM, (RL rn, Args ... rl), (0b1100'1000'0000'0000 | sw(rn, 8) | (... | (1<<rl.value))));

        void setConst(RL rl, u32 imm){
            regHasConst |= 1 << rl.value;
            regConst[rl.value] = imm;
        }

        bool hasConst(RL rl){
            return regHasConst & (1 << rl.value);
        }

        void clearConst(RL rl){
            regHasConst &= ~(1 << rl.value);
        }

        bool tryLDRS(RL rt, u32 imm){
            if(hasConst(rt) && regConst[rt.value] == imm)
                return true;
            if((imm & ~0xFF) == 0){
                MOVS(rt, imm);
                setConst(rt, imm);
                return true;
            // }else{
            //     u32 b = imm;
            //     u32 tz = 0;
            //     while(!(b & 1)){
            //         tz++;
            //         b >>= 1;
            //     }
            //     if((b & ~0xFF) == 0){
            //         MOVS(rt, b);
            //         LSLS(rt, rt, tz);
            //         return true;
            //     }
            }
            return false;
        }

        void LDR(RL rt, u32 imm){
            if(hasConst(rt) && regConst[rt.value] == imm)
                return;
            u32 offset = constCount;
            for(u32 i=0; i<constCount; ++i){
                if(constPool[i] == imm){
                    offset = i;
                    break;
                }
            }
            if(offset == constCount){
                constPool[constCount++] = imm;
            }
            LDR(rt, PC, offset << 2);
        }

        void LDRS(RL rt, u32 imm){
            if(tryLDRS(rt, imm))
                return;
            LDR(rt, imm);
            CMP(rt, 0);
        }

        void LDRI(RL rt, u32 imm, bool preserveStatus = false){
            if(!preserveStatus){
                if(tryLDRS(rt, imm))
                    return;
            }
            LDR(rt, imm);
        }

        OP16(LDR, (RL rt, RL rn, Imm<5, 2> imm = 0), (0b0110'1000'0000'0000 | s(imm, 6) | s(rn, 3) | sw(rt, 0)))

        OP16(LDR, (RL rt, SP_t, Imm<8, 2> imm), (0b1001'1000'0000'0000 | sw(rt, 8) | s(imm, 0)))

        OP16(LDR, (RL rt, PC_t, Imm<8, 2> imm), (0b0100'1000'0000'0000 | sw(rt, 8) | s(imm, 0)))

        OP16(LDR, (RL rt, RL rn, RL rm), (0b0101'1000'0000'0000 | s(rm, 6) | s(rn, 3) | sw(rt, 0)))

        OP16(LDRB, (RL rt, RL rn, Imm<5, 0> imm), (0b0111'1000'0000'0000 | sw(rt, 0) | s(rn, 3) | s(imm, 6)))

        OP16(LDRB, (RL rt, RL rn, RL rm), (0b0101'1100'0000'0000 | s(rm, 6) | s(rn, 3) | sw(rt, 0)))

        OP16(LDRH, (RL rt, RL rn, Imm<5, 1> imm), (0b1000'1000'0000'0000 | sw(rt, 0) | s(rn, 3) | s(imm, 6)))

        OP16(LDRH, (RL rt, RL rn, RL rm), (0b0101'1010'0000'0000 | s(rm, 6) | s(rn, 3) | sw(rt, 0)))

        OP16(LDRSB, (RL rt, RL rn, RL rm), (0b0101'0110'0000'0000 | sw(rt, 0) | s(rn, 3) | s(rm, 6)))

        OP16(LDRSH, (RL rt, RL rn, RL rm), (0b0101'1110'0000'0000 | sw(rt, 0) | s(rn, 3) | s(rm, 6)))

        void MODS(RL rd, I8 imm){
            if(!imm.value){
                MOVS(rd, 0);
            }else{
                LSLS(rd, 32 - imm.value);
                LSRS(rd, 32 - imm.value);
            }
        }

        OP16(LSLS, (RL rd, RL rm, Imm<5, 0> imm), (0b0000'0000'0000'0000 | sw(rd, 0) | s(rm, 3) | s(imm, 6)))

        void LSLS(RL rd, I8 imm){
            LSLS(rd, rd, imm);
        }

        OP16(LSLS, (RL rdn, RL rm), (0b0100'0000'1000'0000 | s(rm, 3) | sw(rdn, 0)))

        OP16(LSRS, (RL rd, RL rm, Imm<5, 0> imm), (0b0000'1000'0000'0000 | sw(rd, 0) | s(rm, 3) | s(imm, 6)))

        void LSRS(RL rd, I8 imm){
            LSRS(rd, rd, imm);
        }

        OP16(LSRS, (RL rdn, RL rm), (0b0100'0000'1100'0000 | sw(rdn, 0) | s(rm, 3)))

        OP16(MOVS, (RL rd, I8 imm), (0b0010'0000'0000'0000 | sw(rd, 8) | s(imm, 0)))

        OP16(MOV, (RX rd, RX rm), (0b0100'0110'0000'0000 | sw(rm, 3) | s(rd, 7, 0)))

        OP16(MOVS, (RL rd, RL rm), (s(rm, 3) | sw(rd, 0)))

        OP32(MRS, (RX rd, RSYS sys), (0b1111'0011'1110'1111'1000'0000'0000'0000 | sw(rd, 8) | s(sys, 0)))

        OP32(MSR, (RSYS sys, RX rd), (0b1111'0011'1000'0000'1000'1000'0000'0000 | sw(rd, 16) | s(sys, 0)))

        OP16(MULS, (RL rdm, RL rn), (0b0100'0011'0100'0000 | s(rn, 3) | sw(rdm, 0)))

        OP16(MVNS, (RL rd, RL rm), (0b0100'0011'1100'0000 | s(rm, 3) | sw(rd, 0)))

        OP16(NOP, (), 0b1011111100000000)

        OP16(ORRS, (RL rdn, RL rm), (0b0100'0011'0000'0000 | sw(rdn, 0) | s(rm, 3)))

        template<typename ... Arg>
        OP16(POP, (Arg ... arg), (0b1011'1100'0000'0000 | (... | (1 << (arg.value == 15 ? 8 : arg.value)))))

        OP16(POP, (u32 map), (0b1011'1100'0000'0000 | map))

        template<typename ... Arg>
        OP16(PUSH, (Arg ... arg), (0b1011'0100'0000'0000 | (... | (1 << (arg.value == 14 ? 8 : arg.value)))))

        OP16(PUSH, (u32 map), (0b1011'0100'0000'0000 | map))

        OP16(REV, (RL rd, RL rm), (0b1011'1010'0000'0000 | s(rm, 3) | sw(rd, 0)))

        OP16(REV16, (RL rd, RL rm), (0b1011'1010'0100'0000 | s(rm, 3) | sw(rd, 0)))

        OP16(REVSH, (RL rd, RL rm), (0b1011'1010'1100'0000 | s(rm, 3) | sw(rd, 0)))

        OP16(RORS, (RL rdn, RL rm), (0b0100'0001'1100'0000 | s(rm, 3) | sw(rdn, 0)))

        OP16(RSBS, (RL rd, RL rn), (0b0100'0010'0100'0000 | s(rn, 3) | sw(rd, 0)))

        OP16(SBCS, (RL rdn, RL rm), (0b0100'0001'1000'0000 | s(rm, 3) | sw(rdn, 0)))

        template<typename ... Arg>
        OP16(STMIA, (RL rn, Arg ... arg), (0b1100'0000'0000'0000 | s(rn, 8) | (... | (1 << arg.value))))

        OP16(STR, (RL rt, RL rn, Imm<5, 2> imm = 0), (0b0110'0000'0000'0000 | s(imm, 6) | s(rn, 3) | s(rt, 0)))

        OP16(STR, (RL rt, SP_t sp, Imm<8, 2> imm), (0b1001'0000'0000'0000 | s(rt, 8) | s(imm, 0)))

        OP16(STR, (RL rt, RL rn, RL rm), (0b0101'0000'0000'0000 | s(rm, 6) | s(rn, 3) | s(rt, 0)))

        OP16(STRB, (RL rt, RL rn, Imm<5, 0> imm), (0b0111'0000'0000'0000 | s(imm, 6) | s(rn, 3) | s(rt, 0)))

        OP16(STRB, (RL rt, RL rn, RL rm), (0b0101'0100'0000'0000 | s(rm, 6) | s(rn, 3) | s(rt, 0)))

        OP16(STRH, (RL rt, RL rn, Imm<5, 1> imm), (0b1000'0000'0000'0000 | s(imm, 6) | s(rn, 3) | s(rt, 0)))

        OP16(STRH, (RL rt, RL rn, RL rm), (0b0101'0010'0000'0000 | s(rm, 6) | s(rn, 3) | s(rt, 0)))

        OP16(SUBS, (RL rd, RL rn, I3 imm), (0b0001'1110'0000'0000 | s(imm, 6) | s(rn, 3) | sw(rd, 0)))

        OP16(SUBS, (RL rdn, I8 imm), (0b0011'1000'0000'0000 | sw(rdn, 8) | s(imm, 0)))

        OP16(SUBS, (RL rd, RL rn, RL rm), (0b0001'1010'0000'0000 | s(rm, 6) | s(rn, 3) | sw(rd, 0)))

        void SUBS(RL rnd, RL rm){
            SUBS(rnd, rnd, rm);
        }

        OP16(SUB, (SP_t, Imm<7, 2> imm), (0b1011'0000'1000'0000 | s(imm, 0)))

        OP16(SVC, (I8 imm), (0b1101'1111'0000'0000 | s(imm, 0)))

        OP16(SXTB, (RL rd, RL rm), (0b1011'0010'0100'0000 | s(rm, 3) | sw(rd, 0)))

        OP16(SXTH, (RL rd, RL rm), (0b1011'0010'0000'0000 | s(rm, 3) | sw(rd, 0)))

        OP16(TST, (RL rn, RL rm), (0b0100'0010'0000'0000 | s(rm, 3) | s(rn, 0)))

        OP16(UDF, (I8 imm), (0b1101'1110'0000'0000 | s(imm, 0)))

        OP16(UXTB, (RL rd, RL rm), (0b1011'0010'1100'0000 | s(rm, 3) | sw(rd, 0)))

        OP16(UXTH, (RL rd, RL rm), (0b1011'0010'1000'0000 | s(rm, 3) | sw(rd, 0)))

#undef OP32
#undef OP32L
#undef OP16
#undef OP16L


    };

}
