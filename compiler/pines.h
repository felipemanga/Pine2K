#pragma once

#include "codegen.h"
#include "tokenizer.h"

#include "infinitearray.h"
#include "ResTable.h"

extern "C" {
    struct _div {int n, d; };
    _div __aeabi_idiv(int, int);
    _div __aeabi_uidivmod(int, int);
}

namespace pines {

struct Node {
    enum Type {
        Program
    } type;

    union {
        struct {
            u32 x;
        } program;
    };
};

struct Sym {
    u32 hash = 0;
    u32 kctv = 0;
    u32 init = 0;
    u16 address = 0xFFFF;
    u16 scopeId = 0;
    u16 line = 0;
    u8 reg = 0xFF;
    u8 flags = 0;
    enum Type {
        U32,
        S32,
        BOOL,
        UNCOMPILED,
        FUNCTION,
        CAST_EQ,
        CAST_NE,
        CAST_LT,
        CAST_LE,
        CAST_GT,
        CAST_GE,
    } type = U32;

    bool isInStack(){
        return (scopeId != 0) || isTemp();
    }
    bool memInit(){
        return (scopeId == 0) && (flags & (1 << 5));
    }
    void setMemInit(u32 v){
        flags |= 1 << 5;
        init = v;
    }
    void setMemInit(){
        if(scopeId != 0) return;
        if(!hasKCTV()) return;
        if(kctv == 0) clearDirty();
        setMemInit(kctv);
    }
    bool equals(u32 v){
        return hasKCTV() && (kctv == v);
    }
    bool isInRange(u32 min, u32 max){
        return hasKCTV() && (kctv >= min) && (kctv <= max);
    }
    bool isDeref(){
        return flags & (1 << 4);
    }
    void clearDeref(){
        flags &= ~(1 << 4);
    }
    void setDeref(){
        flags |= 1 << 4;
    }
    bool isTemp(){
        return hash == 0;
    }
    Sym& unhitTemp(){
        if(!hash)
            flags &= ~(1 << 1);
        return *this;
    }
    Sym& hitTemp(){
        if(!hash)
            flags |= (1 << 1);
        return *this;
    }
    bool wasHit(){
        return isTemp() && (flags & (1 << 1));
    }
    bool isDirty(){
        return !wasHit() && (flags & (1 << 2));
    }
    void setDirty(){
        if(!isConstant())
            flags |= 1 << 2;
    }
    void clearDirty(){
        flags &= ~(1 << 2);
    }
    bool hasKCTV(){
        return !isDeref() && (flags & (1 << 3));
    }
    bool canDeref(){
        return isDeref() && (flags & (1 << 3));
    }
    void setKCTV(u32 v){
        setDirty();
        reg = 0xFF;
        kctv = v;
        flags |= 1 << 3;
    }
    bool isConstant(){
        return flags & (1 << 6);
    }
    void setConstant(u32 v){
        clearDirty();
        reg = 0xFF;
        kctv = v;
        flags |= 1 << 3;
        flags |= 1 << 6;
    }
    void setConstexpr(){
        flags |= 1 << 7;
    }
    bool isConstexpr(){
        return flags & (1 << 7);
    }
    template <typename T>
    void setConstant(T v){
        setConstant(reinterpret_cast<u32>(v));
    }
    bool clearKCTV(){
        if( flags & (1 << 6) )
            return false;
        flags &= ~(1 << 3);
        return true;
    }
};

class RegAlloc {
public:
    static constexpr u32 maxReg = 7;
    struct RegSym {
        u32 age;
        u32 sym;
        u32 hold;
    } reg[maxReg];
    u32 maxAge = 1;
    u32 useMap = 0;

    using Spill_t = void (*)(void *data, u32 sym);
    Spill_t spill;
    void *data;

    bool isValid(u32 reg){
        return reg > 0 && reg <= maxReg;
    }

    template<typename Type>
    void init(Type *data, void (*spill)(Type *data, u32 sym)){
        this->data = reinterpret_cast<void*>(data);
        this->spill = reinterpret_cast<Spill_t>(spill);
        for(u32 i=0; i<maxReg; ++i){
            reg[i].hold = 0;
            reg[i].age = 0;
            reg[i].sym = ~u32{};
        }
    }

    void invalidate(cg::RegLow r){
        auto &c = reg[r.value - 1];
        if(c.hold){
            LOG("Invalidating held register\n");
        }
        c.sym = ~u32{};
        c.age = 0;
        c.hold = 0;
    }

    bool assign(u32 symId, cg::RegLow r, bool hold = false){
        if(!isValid(r.value)){
            for(u32 i=0; i<maxReg; ++i){
                if(reg[i].sym == symId){
                    invalidate(cg::RegLow(i+1));
                }
            }
            return false;
        }else{
            auto &c = reg[r.value - 1];
            c.sym = symId;
            c.age = maxAge++;
            c.hold = hold;
            useMap |= 1 << r.value;
            return true;
        }
    }

    u32 operator [] (cg::RegLow r){
        return isValid(r.value) ? reg[r.value - 1].sym : ~u32{};
    }

    cg::RegLow operator [] (u32 symId){
        u32 p = 0;
        for(; p<maxReg - 1; ++p){
            if(reg[p].sym == symId){
                reg[p].age = maxAge++;
                return cg::RegLow(p + 1);
            }
            if(!reg[p].hold) break;
        }
        u32 age = reg[p].age;
        for(u32 i = p + 1; i < maxReg; ++i){
        // auto sp = p;
        // for(u32 i = maxReg - 1; i > sp; --i){
            auto& r = reg[i];
            if(r.sym == symId){
                r.age = maxAge++;
                return cg::RegLow(i+1);
            }
            if(!r.hold && r.age < age){
                age = r.age;
                p = i;
            }
        }

        // LOGD("Allocate R", p + 1, " to ", symId, "\n");
        if(reg[p].sym != ~u32{}){
            // LOGD("Spilling ", reg[p].sym, "\n");
            spill(data, reg[p].sym);
        }
        reg[p].hold = 0;
        reg[p].sym = symId;
        reg[p].age = maxAge++;
        useMap |= 1 << (p + 1);
        return cg::RegLow(p+1);
    }

    bool isLocked(cg::RegLow r){
        return reg[r.value - 1].hold;
    }

    void hold(cg::RegLow r){
        if(isValid(r.value))
            reg[r.value - 1].hold = 1;
    }

    void release(cg::RegLow r){
        if(isValid(r.value) && reg[r.value - 1].hold)
            reg[r.value - 1].hold = 0;
    }

    u32 getUseMap(){
        return useMap;
    }

    void clearUseMap(){
        useMap = 0;
    }
};

template <typename CodeGen, typename SymTable = ia::InfiniteArray<Sym, 16>>
class Pines {
    static constexpr const u32 invalidSym = ~u32{};
    static constexpr const u16 invalidAddress = 0xFFFF;
    static constexpr const u8 invalidReg = 0xFF;
    const u32 dataSection;
    Tokenizer& tok;
    const char *error = nullptr;
    u32 isConstexpr;
    u32 line = 0, column = 0;
    u32 scopeId = 0, globalScopeSize = 0, scopeSize = 0, maxScope = 0;
    u32 token, symId = invalidSym;
    RegAlloc regAlloc;
    CodeGen& codegen;
    u32 nextLabel = 1, returnLabel = 0;
    u32 preserveFlags = 0;
    SymTable& symTable;
    ResTable& resTable;

    void toBranch(u32 label){
        auto& sym = symTable[symId].hitTemp();
        if(sym.type < Sym::CAST_EQ){
            commitAll();
            load(symId);
            codegen.CMP(cg::RegLow(sym.reg), 0);
            codegen.B(cg::EQ, label);
            invalidateRegisters();
        } else {
            preserveFlags++;
            commitAll();
            preserveFlags--;
            auto code = cg::NE;
            switch(sym.type){
            case Sym::CAST_EQ: code = cg::EQ; break;
            case Sym::CAST_NE: code = cg::NE; break;
            case Sym::CAST_LT: code = cg::LT; break;
            case Sym::CAST_LE: code = cg::LE; break;
            case Sym::CAST_GT: code = cg::GT; break;
            case Sym::CAST_GE: code = cg::GE; break;
            }
            codegen.B(code, label);
        }
    }

    void setError(const char *msg){
        if(error) return;
        error = msg;
        line = tok.getLine();
        column = tok.getColumn();
        LOG(error, " line: ", line, " column:", column, "\n");
        LOG("Text: ", (const char *) tok.getText(), "\n");
        while(true);
    }

    bool isKeyword(){
        const u32 keywords[] = {
            "var"_token,
            "function"_token,
            "return"_token,
            "if"_token,
            "else"_token,
            "while"_token,
            "const"_token,
            "for"_token
        };
        for(u32 i=0; i<sizeof(keywords)/sizeof(keywords[0]); ++i){
            if(keywords[i] == token)
                return true;
        }
        return false;
    }

    bool isName(){
        return !(isKeyword() || tok.isOperator() || tok.isSpecial() ||tok.getClass() == TokenClass::Unknown);
    }

    bool isUnaryOperator(){
        return (token == "+"_token) ||
            (token == "++"_token) ||
            (token == "-"_token) ||
            (token == "--"_token) ||
            (token == "!"_token) ||
            (token == "~"_token);
    }

    bool isPostfixOperator(){
        return (token == "--"_token) ||
            (token == "++"_token) ||
            (token == "("_token) ||
            (token == "["_token);
    }

    void accept(){
        if(!error){
            token = tok.get();
            // LOGD("T:", tok.getText()[0], "\n");
        }
    }

    bool accept(u32 hash){
        if(error)
            return false;
        if(token == hash){
            token = tok.get();
            // LOGD("T:", tok.getText()[0], "\n");
            return true;
        }
        return false;
    }

    void boolCast(Sym& sym){
        if(!regAlloc.isValid(sym.reg))
            return;
        LOGD("boolCast\n");
        cg::RegLow reg(sym.reg);
        switch(sym.type){
        case Sym::CAST_EQ:
            codegen.SUBS(cg::R0, reg, 1);
            codegen.SBCS(reg, cg::R0);
            break;

        case Sym::CAST_NE:
            codegen.RSBS(cg::R0, reg);
            codegen.ADCS(reg, cg::R0);
            break;


        case Sym::CAST_LE:
            codegen.SUBS(reg, 1);
        case Sym::CAST_LT:
            codegen.MVNS(reg, reg);
            codegen.LSRS(reg, 31);
            break;

        case Sym::CAST_GT:
            codegen.SUBS(reg, 1);
            codegen.LSRS(reg, 31);
            break;
        case Sym::CAST_GE:
            codegen.LSRS(reg, 31);
            break;

        default:
            if(sym.type > Sym::CAST_EQ){
                setError("CAST BUG");
            }
            return;
        }
        sym.type = Sym::BOOL;
    }

    u32 commit(Sym& sym, u32 symId){
        if(!sym.isDirty() || (sym.isTemp() && sym.wasHit())){
            // LOGD("Skip Commit ", symId, "\n");
            return invalidReg;
        }
        LOGD("COMMIT ", symId, " reg:", sym.reg, " hit:", sym.wasHit(), " kctv:", sym.kctv, "\n");
        sym.clearDirty();
        if(sym.hasKCTV() && !sym.memInit()){
            sym.setMemInit(sym.kctv);
        }
        if(!regAlloc.isValid(sym.reg)){
            if(!sym.hasKCTV()){
                LOGD("Sym has no reg and no kctv\n");
                return false;
            }
            auto reg = regAlloc[symId];
            sym.reg = reg.value;
            codegen.LDRI(reg, sym.kctv, preserveFlags);
        }
        boolCast(sym);
        cg::RegLow reg(sym.reg);
        if(sym.address == invalidAddress){
            if(!sym.isInStack()) sym.address = globalScopeSize++;
            else sym.address = scopeSize++;
        }
        LOGD("COMMIT ", symId, "\n");
        if(!sym.isInStack()){
            codegen.LDRI(cg::R0, dataSection + (sym.address << 2), preserveFlags);
            codegen.STR(reg, cg::R0, 0);
        }else{
            codegen.STR(reg, cg::SP, sym.address << 2);
        }
        return sym.reg;
    }

    bool spill(cg::RegLow reg){
        u32 symId = regAlloc[reg];
        if(symId != invalidSym){
            return spill(symTable[symId], symId);
        }
        return false;
    }

    bool spill(Sym& sym, u32 symId){
        commit(sym, symId);
        if(regAlloc.isValid(sym.reg)){
            regAlloc.invalidate(cg::RegLow(sym.reg));
            sym.reg = invalidReg;
            return true;
        }
        return false;
    }

    void spillAll(){
        LOGD("Spill all\n");
        for(u32 i = 1; i<RegAlloc::maxReg; ++i){
            u32 symId = regAlloc[cg::RegLow(i)];
            if(symId == invalidSym) continue;
            spill(symTable[symId], symId);
        }
    }

    bool isScratchReg(u32 reg){
        return reg < 4;
    }

    void invalidateRegisters(bool all = true){
        u32 id = 0;
        LOGD("Invalidating registers\n");

        if(all){
            for(u32 i = 1; i<RegAlloc::maxReg; ++i){
                auto symId = regAlloc[cg::RegLow(i)];
                if(symId != invalidSym){
                    symTable[symId].reg = invalidReg;
                    regAlloc.invalidate(cg::RegLow(i));
                }
            }
        } else {
            for(u32 i = 1; isScratchReg(i); ++i){
                auto symId = regAlloc[cg::RegLow(i)];
                if(symId != invalidSym){
                    symTable[symId].reg = invalidReg;
                    regAlloc.invalidate(cg::RegLow(i));
                }
            }
        }

        // for(auto& sym : symTable){
        //     if(regAlloc.isValid(sym.reg)){
        //         if(all || isScratchReg(sym.reg)){
        //             regAlloc.invalidate(cg::RegLow(sym.reg));
        //             sym.reg = invalidReg;
        //             symTable.dirtyIterator();
        //         }
        //     }
        //     id++;
        // }
    }

    void commitAll(){
        u32 id = 0;
        LOGD("commit all\n");
        for(auto& sym : symTable){
            if(commit(sym, id) != invalidReg){
                symTable.dirtyIterator();
            }
            id++;
        }
    }

    void commitScratch(){
        u32 id = 0;
        LOGD("commit scratch\n");
        for(auto& sym : symTable){
            if(isScratchReg(sym.reg) || !regAlloc.isValid(sym.reg) || sym.scopeId == 0){
                if(commit(sym, id) != invalidReg)
                    symTable.dirtyIterator();
            }
            id++;
        }
    }

    void flush(){
        u32 id = 0;
        // LOGD("Flushing\n");
        for(auto& sym : symTable){
            if(sym.scopeId == scopeId || sym.scopeId == 0){
                spill(sym, id);
                sym.clearKCTV();
                symTable.dirtyIterator();
            // }else{
            //     LOGD("Not flushing ", id, " ", sym.scopeId, " != ", scopeId, "\n");
            }
            id++;
        }
    }

    void clearAllKCTV(){
        u32 id = 0;
        for(auto& sym : symTable){
            if(sym.hasKCTV()){
                sym.clearKCTV();
                symTable.dirtyIterator();
            }
            id++;
        }
    }


    Sym &load(u32 symId){
        loadToRegister(symId, regAlloc[symId].value);
        return symTable[symId];
    }

    void * (*allocator)(u32 size);

public:
    Pines(Tokenizer& tok, CodeGen& cg, SymTable& symTable, ResTable& resTable, u32 dataSection) :
        tok(tok),
        symTable(symTable),
        resTable(resTable),
        codegen(cg),
        dataSection(dataSection) {
        auto onSpillCallback =
            +[](Pines *p, u32 sym){
                 p->spill(p->symTable[sym], sym);
             };
        regAlloc.init(this, onSpillCallback);
        clearHashCache();
    }

    void setAllocator(void *(*allocator)(u32)){
        this->allocator = allocator;
    }

    const char *varName(){
        if(!isName()){
            setError("Expected variable name");
            return nullptr;
        }
        return tok.getText();
    }

    void prenExpression(){
        if(!accept("("_token)){
            setError("0 Unexpected token");
            return;
        }
        do{
            simpleExpression();
            if(error) return;
        }while(accept(","_token));
        if(!accept(")"_token)){
            setError("Expected \")\"");
            return;
        }
    }

    void unaryExpression(){
        auto op = token;
        bool isUnary = isUnaryOperator();
        if(isUnary) accept();
        if(isUnaryOperator()) unaryExpression();
        else value();
        if(!isUnary) return;
        auto &sym = symTable[symId];
        if(sym.hasKCTV()){
            switch(op){
            case "-"_token: sym.setKCTV(-sym.kctv); break;
            case "+"_token: break;
            case "!"_token: sym.setKCTV(!sym.kctv); break;
            case "~"_token: sym.setKCTV(~sym.kctv); break;
            case "++"_token: sym.setKCTV(sym.kctv+1); break;
            case "--"_token: sym.setKCTV(sym.kctv-1); break;
            default: setError("Unexpected operator"); break;
            }
        } else {
            switch(op){
            case "-"_token:
            {
                u32 tmpId = createTmpSymbol();
                auto &tmp = symTable[tmpId];
                auto &sym = load(symId);
                auto reg = regAlloc[tmpId];
                tmp.reg = reg.value;
                tmp.clearKCTV();
                codegen.RSBS(reg, cg::RegLow(sym.reg));
                symId = tmpId;
                break;
            }
            case "+"_token:
                break;
            case "~"_token:
            {
                u32 tmpId = createTmpSymbol();
                auto &tmp = symTable[tmpId];
                auto &sym = load(symId);
                auto reg = regAlloc[tmpId];
                tmp.reg = reg.value;
                tmp.clearKCTV();
                codegen.MVNS(reg, cg::RegLow(sym.reg));
                symId = tmpId;
                break;
            }
            case "++"_token:
                load(symId);
                codegen.ADDS(cg::RegLow{sym.reg}, 1);
                sym.setDirty();
                break;
            case "--"_token:
                load(symId);
                codegen.SUBS(cg::RegLow{sym.reg}, 1);
                sym.setDirty();
                break;
            case "!"_token:
            {
                u32 tmpId = createTmpSymbol();
                assign(tmpId);
                symTable[tmpId].type = Sym::CAST_NE;
                break;
            }
            default:
                setError("unaryExpression not implemented");
                break;
            }
        }
    }

    void postfixOperator(){
        if(symId == invalidSym){
            setError("Invalid operation");
            return;
        }
        switch(token){
        case "--"_token:
        case "++"_token: {
            auto token = this->token;
            accept();
            auto &sym = symTable[symId];
            if(sym.hasKCTV()){
                auto symId = this->symId;
                auto kctv = sym.kctv;
                sym.setKCTV(token == "++"_token ? (kctv + 1) : (kctv - 1));
                u32 tmpId = createTmpSymbol();
                this->symId = tmpId;
                auto &tmp = symTable[tmpId];
                tmp.setKCTV(kctv);
                LOGD("inc kctv: ", kctv, " sym:", symId, "\n");
            }else{
                auto symId = this->symId;
                load(symId);
                u32 tmpId = createTmpSymbol();
                auto &tmp = symTable[tmpId];
                this->symId = tmpId;
                auto reg = regAlloc[tmpId];
                if(token == "++"_token){
                    codegen.ADDS(reg, cg::RegLow(sym.reg), 1);
                }else{
                    codegen.SUBS(reg, cg::RegLow(sym.reg), 1);
                }
                regAlloc.assign(tmpId, cg::RegLow(sym.reg));
                tmp.reg = sym.reg;
                regAlloc.assign(symId, reg);
                sym.reg = reg.value;
                sym.setDirty();
                tmp.clearKCTV();
            }
            break;
        }
        case "["_token: {
            accept();
            auto baseId = symId;
            expression();
            if(!accept("]"_token)){
                setError("Unexpected token (not ])");
                return;
            }
            u32 tmpId = createTmpSymbol();
            auto &sym = symTable[symId].hitTemp();
            auto &base = symTable[baseId].hitTemp();
            auto &tmp = symTable[tmpId];
            if(base.hasKCTV() && sym.hasKCTV()){
                tmp.setKCTV(base.kctv + sym.kctv * 4);
            }else{
                load(baseId);
                load(symId);
                auto reg = cg::RegLow(sym.reg);
                spill(sym, symId);
                regAlloc.assign(tmpId, cg::RegLow(reg));
                codegen.LSLS(cg::R0, reg, 2);
                codegen.ADDS(reg, cg::RegLow(base.reg), cg::R0);
                tmp.reg = reg.value;
                tmp.clearKCTV();
            }
            tmp.setDeref();
            symId = tmpId;
            break;
        }
        case "("_token:{
            u32 fncId = symId;
            u32 argc, argv[7];
            callArgs(argc, argv);
            writeCall(fncId, argc, argv);
            break;
        }

        default:
            setError("postfixOperator: Unexpected token");
        }
    }

    void hitTmpSym(u32 symId){
        if(symId == invalidSym)
            return;
        symTable[symId].hitTemp();
    }

    void binaryExpression(){
        auto lsymId = symId;
        auto op = token;
        accept();

        if( op == "="_token ){
            simpleExpression();
            assign(lsymId);
            return;
        }
        bool doAssign = true;

        switch( op ){
        case "==="_token:    op = "=="_token; doAssign = false; break;
        case "!=="_token:    op = "!="_token; doAssign = false; break;
        case "*="_token:     op = "*"_token;    break;
        case "/="_token:     op = "/"_token;    break;
        case "+="_token:     op = "+"_token;    break;
        case "-="_token:     op = "-"_token;    break;
        case "<<="_token:    op = "<<"_token;   break;
        case ">>="_token:    op = ">>"_token;   break;
        case ">>>="_token:   op = ">>>"_token;  break;
        case "&="_token:     op = "&"_token;    break;
        case "&&="_token:    op = "&&"_token;   break;
        case "|="_token:     op = "|"_token;    break;
        case "||="_token:    op = "||"_token;   break;
        case "^="_token:     op = "^"_token;    break;
        case "%="_token:     op = "%"_token;    break;
        default:             doAssign = false;  break;
        }

        if(doAssign)
            simpleExpression();
        else
            unaryExpression();

        if(error)
            return;

        auto &lsym = symTable[lsymId];
        auto &rsym = symTable[symId];

        bool bothKnown = lsym.hasKCTV() && rsym.hasKCTV();

        if(bothKnown){
            bool matched = true;
            Sym::Type type = lsym.type;
            u32 kctv = rsym.kctv;
            u32 lkctv = lsym.kctv;
            switch(op){
            case "+"_token:     kctv = lkctv + kctv;           type = Sym::S32;         break;
            case "*"_token:     kctv = lkctv * s32(kctv);      type = Sym::S32;         break;
            case "/"_token:     kctv = lkctv / kctv;           type = Sym::S32;         break;
            case "%"_token:     kctv = lkctv % kctv;           type = Sym::S32;         break;
            case "-"_token:     kctv = lkctv - kctv;           type = Sym::S32;         break;
            case "<<"_token:    kctv = lkctv << kctv;          type = Sym::S32;         break;
            case ">>"_token:    kctv = s32(lkctv) >> kctv;     type = Sym::S32;         break;
            case ">>>"_token:   kctv = lkctv >> kctv;          type = Sym::U32;         break;
            case "&"_token:     kctv = lkctv & kctv;           type = Sym::S32;         break;
            case "&&"_token:    kctv = lkctv && kctv;          type = Sym::S32;         break;
            case "|"_token:     kctv = lkctv | kctv;           type = Sym::S32;         break;
            case "||"_token:    kctv = lkctv || kctv;          type = Sym::S32;         break;
            case "^"_token:     kctv = lkctv ^ kctv;           type = Sym::S32;         break;
            case "<"_token:     kctv = lkctv < kctv;           type = Sym::BOOL;        break;
            case ">"_token:     kctv = lkctv > kctv;           type = Sym::BOOL;        break;
            case "<="_token:    kctv = lkctv <= kctv;          type = Sym::BOOL;        break;
            case ">="_token:    kctv = lkctv >= kctv;          type = Sym::BOOL;        break;
            case "!="_token:    kctv = lkctv != kctv;          type = Sym::BOOL;        break;
            case "=="_token:    kctv = lkctv == kctv;          type = Sym::BOOL;        break;
            default:            matched = false;               break;
            }
            if(matched){
                symId = doAssign ? lsymId : createTmpSymbol();
                auto &sym = symTable[symId];
                sym.type = type;
                sym.setKCTV(kctv);
                lsym.hitTemp();
                rsym.hitTemp();
                return;
            }
            setError("binaryExpression: Unexpected operator");
        }

        u32 tmpId = doAssign ? invalidSym : createTmpSymbol();
        u32 lhasKCTV = lsym.hasKCTV();
        u32 rhasKCTV = rsym.hasKCTV();
        u32 renameSym = invalidSym;

        decltype(&CodeGen::MULS) noImmFunc = nullptr;
        void (CodeGen::*rightImmFunc)(cg::RegLow, cg::I8 imm) = nullptr;
        u32 rkctv = rsym.kctv;

        switch( op ){
        case "^"_token: noImmFunc = &CodeGen::EORS; break;
        case "||"_token:
        case "|"_token: noImmFunc = &CodeGen::ORRS; break;
        case "&"_token: noImmFunc = &CodeGen::ANDS; break;
        case "&&"_token:
        case "*"_token: noImmFunc = &CodeGen::MULS; break;
        case "%"_token:
        case "/"_token:
        {
            if(rsym.hasKCTV()){
                u32 shifts = 0;
                bool isPOT = false;
                renameSym = lsymId;

                if(rkctv){
                    for(;shifts < 32 && rkctv > (1 << shifts); shifts++);
                    isPOT = rkctv == (1 << shifts);
                }

                if(!rkctv){
                    setError("Division by zero");
                    return;
                }

                if(op == "%"_token){
                    if(rkctv == 1){
                        rightImmFunc = &CodeGen::MOVS;
                        rkctv = 0;
                        break;
                    }else if(isPOT){
                        rightImmFunc = &CodeGen::MODS;
                        rkctv = shifts;
                        break;
                    }
                }else{
                    if(rkctv == 1){
                        break;
                    }else if(isPOT){
                        rightImmFunc = &CodeGen::ASRS;
                        rkctv = shifts;
                        break;
                    }
                }
            }

            commitScratch();
            spill(cg::R2);
            if(op == "%"_token){
                codegen.LDRI(cg::R2, reinterpret_cast<uintptr_t>(__aeabi_uidivmod));
            }else{
                codegen.LDRI(cg::R2, reinterpret_cast<uintptr_t>(__aeabi_idiv));
            }
            regAlloc.hold(cg::R2);
            loadToRegister(symId, 1);
            loadToRegister(lsymId, 0);
            codegen.BLX(cg::R2);
            regAlloc.release(cg::R2);
            invalidateRegisters(false);
            if(doAssign) tmpId = lsymId;
            auto reg = regAlloc[tmpId];
            if(op == "%"_token){
                if(reg.value != 1)
                    codegen.MOVS(reg, cg::R1);
            }else codegen.MOVS(reg, cg::R0);
            auto &tmp = symTable[tmpId];
            tmp.clearKCTV();
            tmp.reg = reg.value;
            symId = tmpId;
            return;
        }

        case "+"_token:
        {
            if(lsym.isInRange(0, 0xFF)){
                lsym.hitTemp();
                load(symId);
                commit(load(symId), symId);
                codegen.ADDS(cg::RegLow(rsym.reg), lsym.kctv);
                renameSym = symId;
            } else if (rsym.isInRange(0, 0xFF)) {
                rightImmFunc = &CodeGen::ADDS;
            } else {
                noImmFunc = &CodeGen::ADDS;
            }
            break;
        }

        case "<<"_token:
            if (rsym.isInRange(0, 0x1F)) rightImmFunc = &CodeGen::LSLS;
            else noImmFunc = &CodeGen::LSLS;
            break;

        case ">>>"_token:
            if (rsym.isInRange(0, 0x1F)) rightImmFunc = &CodeGen::LSRS;
            else noImmFunc = &CodeGen::LSRS;
            break;

        case ">>"_token:
            if (rsym.isInRange(0, 0x1F)) rightImmFunc = &CodeGen::ASRS;
            else noImmFunc = &CodeGen::ASRS;
            break;

        case "-"_token:
            if (rsym.isInRange(0, 0xFF)) rightImmFunc = &CodeGen::SUBS;
            else noImmFunc = &CodeGen::SUBS;
            break;

        case ">"_token:
        case ">="_token:
        case "<"_token:
        case "<="_token:
        case "=="_token:
        case "!="_token:
        {
            // LOGD("Operator ", (op == ">"_token ? ">" : "!="), "\n");
            renameSym = lsymId;
            if(rsym.isInRange(0, 255)){
                commit(load(lsymId), lsymId);
                rsym.hitTemp();
                codegen.SUBS(cg::RegLow(lsym.reg), rsym.kctv);
            }else if(lsym.isInRange(0, 255)){
                lsym.hitTemp();
                commit(load(symId), symId);
                codegen.SUBS(cg::RegLow(rsym.reg), lsym.kctv);
                renameSym = symId;
            }else{
                load(symId);
                commit(load(lsymId), lsymId);
                codegen.SUBS(cg::RegLow(lsym.reg), cg::RegLow(rsym.reg));
            }
            symId = renameSym;
            assign(tmpId);
            switch(op){
            case "=="_token: symTable[tmpId].type = Sym::CAST_NE; break;
            case "!="_token: symTable[tmpId].type = Sym::CAST_EQ; break;
            case ">="_token: symTable[tmpId].type = Sym::CAST_LT; break;
            case "<="_token: symTable[tmpId].type = Sym::CAST_GT; break;
            case ">"_token:  symTable[tmpId].type = Sym::CAST_LE; break;
            case "<"_token:  symTable[tmpId].type = Sym::CAST_GE; break;
            }
            return;
        }

        default:
            LOGD("BOOP ", lhasKCTV, " ", rhasKCTV, "\n");
            setError("Operator not implemented");
            break;
        }

        if(rightImmFunc){
            rsym.hitTemp();
            auto &lsym = load(lsymId);
            if(!doAssign) commit(lsym, lsymId);
            else lsym.setDirty();
            (codegen.*rightImmFunc)(cg::RegLow(lsym.reg), rkctv);
            renameSym = lsymId;
        }

        if(noImmFunc){
            load(symId);
            auto& lsym = load(lsymId);
            if(!doAssign) commit(lsym, lsymId);
            else lsym.setDirty();
            (codegen.*noImmFunc)(cg::RegLow(lsym.reg), cg::RegLow(rsym.reg));
            renameSym = lsymId;
        }

        symId = renameSym;

        if(doAssign){
            assign(lsymId);
        } else {
            auto &tmp = symTable[tmpId];
            auto &src = symTable[symId];
            regAlloc.assign(tmpId, cg::RegLow(src.reg));
            tmp.reg = src.reg;
            src.reg = invalidReg;
            tmp.clearKCTV();
            symId = tmpId;
        }
    }

    void purgeTemps(){
        clearHashCache();
        for(auto& sym : symTable){
            if(!sym.isTemp() && sym.scopeId){
                sym.hash = 0;
            }
            if(sym.isTemp() && !sym.wasHit()){
                sym.hitTemp();
                sym.clearDirty();
                symTable.dirtyIterator();
            }
        }
    }

    u32 createTmpSymbol(){
        if(error) return invalidSym;
        u32 id = 0;
        for(auto& sym : symTable){
            if(sym.wasHit()){
                LOGD("Tmp Recycle ", id, "\n");
                break;
            }
            id++;
        }
        if(id >= symTable.size()){
            LOGD("Creating Tmp ", id, "\n");
        }
        auto& sym = symTable[id];
        sym.flags = 0;
        sym.hash = 0;
        sym.scopeId = scopeId;
        sym.address = invalidAddress;
        sym.reg = invalidReg;
        sym.type = Sym::S32;
        sym.setDirty();
        sym.setKCTV(0);
        regAlloc.assign(id, cg::R0);
        return id;
    }

    u32 createSymbol(const char *name, u32 scopeId){
        u32 token = hash(name);
        u32 id = 0;
        u32 evict = invalidSym;
        for(auto &sym : symTable){
            if(token == sym.hash && sym.scopeId == scopeId){
                LOGD("redeclared variable ", name, " id:", id, "\n");
                return id;
            }
            if(sym.isTemp() && sym.wasHit()){
                evict = id;
            // } else
            //     if(sym.scopeId && sym.scopeId != scopeId){
            //     evict = id;
            }
            id++;
        }

        if(evict < id){
            id = evict;
        }

        hashCache[token % hashCacheSize] = id;
        auto& sym = symTable[id];
        sym.hash = token;
        sym.scopeId = scopeId;
        sym.address = invalidAddress;
        sym.reg = invalidReg;
        sym.flags = 0;
        sym.setKCTV(0);
        sym.clearDirty();
        LOGD("declared variable ", name, " ", id, " hash:", sym.hash, "\n");
        return id;
    }

    u32 createSymbol(u32 scopeId){
        const char *name = varName();
        if(error) return invalidSym;
        u32 id = createSymbol(name, scopeId);
        accept();
        return id;
    }

    Sym &createGlobal(const char *name){
        return symTable[createSymbol(name, 0)];
    }

    u32 findOrCreateSymbol(u32 scopeId=0){
        u32 symId = findSymbol(token, scopeId);
        if(symId == invalidSym){
            symId = createSymbol(0);
            symTable[symId].clearKCTV();
        }else{
            accept();
        }
        return symId;
    }

    static constexpr u32 hashCacheSize = 128;
    u32 hashCache[hashCacheSize];
    void clearHashCache(){
        for(u32 i=0; i<hashCacheSize; i++){
            hashCache[i] = invalidSym;
        }
    }
    u32 findSymbol(u32 hash, u32 scopeId){
        u32 bestMatch = hashCache[hash % hashCacheSize];
        if(bestMatch < symTable.end()){
            auto &sym = symTable[bestMatch];
            if(sym.hash == hash)
                return bestMatch;
        }
        bestMatch = invalidSym;

        // LOGD("Looking for ", hash, " in ", scopeId, "\n");
        u32 exactMatch = symTable.find(
            [&](const Sym &sym, u32 id){
                if(sym.hash != hash)
                    return false;
                if(sym.scopeId == 0)
                    bestMatch = id;
                return sym.scopeId == scopeId;
            });
        if(exactMatch != symTable.end())
            return hashCache[hash % hashCacheSize] = exactMatch;
        if(bestMatch != invalidSym){
            hashCache[hash % hashCacheSize] = bestMatch;
            auto &sym = symTable[bestMatch];
            if(!sym.hasKCTV())
                isConstexpr = false;
        }
        return bestMatch;
    }

    void value(){
        bool requireCall = accept("new"_token);
        if(token == "["_token){
            arrayLiteral();
        } else if(token == "("_token){
            prenExpression();
        } else if(accept("true"_token)){
            symId = createTmpSymbol();
            auto &sym = symTable[symId];
            sym.setKCTV(1);
        } else if(accept("false"_token) || accept("null"_token) || accept("undefined"_token)){
            symId = createTmpSymbol();
            auto &sym = symTable[symId];
            sym.setKCTV(0);
        } else if(tok.isNumeric()){
            symId = createTmpSymbol();
            auto &sym = symTable[symId];
            sym.setKCTV(tok.getNumeric());
            accept();
        } else if(tok.isString()){
            stringLiteral();
        } else if(isName()) {
            symId = findOrCreateSymbol(scopeId);
        } else {
            setError("value: Unexpected token");
        }
        if(requireCall && token != "("_token){
            writeCall(symId, 0, nullptr);
        }
        while(!error && isPostfixOperator()){
            postfixOperator();
        }
    }

    u32 arrayId = 0;
    void arrayLiteral(){
        if(!accept("["_token)){
            setError("Unexpected token (not [)");
            return;
        }
        auto ptr = reinterpret_cast<u32*>(0x20004000 + arrayId * 4);
        u32 start = arrayId;
        while(!error && tok.getClass() != TokenClass::Eof){
            if(arrayId * 4 >= 0x800){
                setError("Array too big");
                return;
            }
            simpleExpression();
            auto &sym = symTable[symId];
            if(!sym.hasKCTV()){
                setError("Invalid Array literal value");
                return;
            }
            ptr[arrayId++] = sym.kctv;
            sym.hitTemp();
            if(!accept(","_token))
                break;
        }
        if(!accept("]"_token)){
            setError("Unexpected token (not ])");
            return;
        }
        u32 byteSize = (arrayId - start) << 2;
        // LOG("Creating array of size ", byteSize, "\n");
        void *out = allocator(byteSize);
        memcpy(out, ptr, byteSize);
        arrayId = start;
        u32 tmpId = createTmpSymbol();
        symTable[tmpId].setKCTV(reinterpret_cast<uintptr_t>(out));
        symId = tmpId;
    }

    void stringLiteral(){
        u32 line = tok.getLine();
        u32 start = tok.getLocation();
        u32 len = 0;
        u32 hash = 5381 * 31 + '"';
        while(tok.isString()){
            len++;
            hash = hash * 31 + tok.getText()[0];
            accept();
        }
        File *file = resTable.write(hash);
        if( file ){
            tok.setLocation(start - 3, line);
            accept();
            for(u32 i = 0; i < len; ++i){
                (*file) << tok.getText()[0];
                accept();
            }
            (*file) << '\0';
        }
        symId = createTmpSymbol();
        auto &sym = symTable[symId];
        sym.setKCTV(hash);
    }

    void callArgs(u32 &argc, u32 *argv){
        argc = 0;
        if(!accept("("_token)){
            setError("callArgs: Unexpected token (not ()");
            return;
        }
        if(accept(")"_token))
            return;
        do{
            if(argc == 8){
                setError("Too many arguments");
                return;
            }
            simpleExpression();
            argv[argc++] = symId;
        }while(accept(","_token));
        if(error)
            return;
        if(!accept(")"_token)){
            setError("callArgs 2: Unexpected token (not ))");
            return;
        }
    }

    void callPeek(u32 argc, u32 *argv){
        if(argc < 1 || argc > 2){
            setError("Peek expects one or two arguments");
            return;
        }
        symId = createTmpSymbol();
        auto reg = regAlloc[symId];
        auto &sym = symTable[symId];
        sym.reg = reg.value;
        sym.clearKCTV();
        regAlloc.hold(reg);
        auto &ptr = load(argv[0]);
        regAlloc.hold(cg::RegLow(ptr.reg));
        if(argc == 1){
            codegen.LDRB(reg, cg::RegLow(ptr.reg), 0);
        } else if(argc == 2){
            auto &off = symTable[argv[1]];
            if(off.isInRange(0, (1<<5) - 1)){
                off.hitTemp();
                codegen.LDRB(reg, cg::RegLow(ptr.reg), off.kctv);
            } else {
                load(argv[1]);
                codegen.LDRB(reg, cg::RegLow(ptr.reg), cg::RegLow(off.reg));
            }
        }
        regAlloc.release(cg::RegLow(ptr.reg));
        regAlloc.release(reg);
    }

    void callPoke(u32 argc, u32 *argv){
        if(argc < 2 || argc > 3){
            setError("Poke expects two or three arguments");
            return;
        }
        auto &ptr = load(argv[0]);
        auto reg = cg::RegLow(ptr.reg);
        regAlloc.hold(reg);
        if(argc == 2){
            auto &val = load(argv[1]);
            codegen.STRB(cg::RegLow(val.reg), reg, 0);
        } else if(argc == 3){
            auto &off = symTable[argv[1]];
            auto &val = load(argv[2]);
            if(off.isInRange(0, (1<<5) - 1)){
                off.hitTemp();
                codegen.STRB(cg::RegLow(val.reg), reg, off.kctv);
            } else {
                load(argv[1]);
                codegen.STRB(cg::RegLow(val.reg), reg, cg::RegLow(off.reg));
            }
        }
        regAlloc.release(reg);
    }

    void callPressed(u32 argc, u32 *argv){
        auto &sym = symTable[argv[0]];
        u32 off = 0;
        switch(sym.kctv){
        case hash("\"A"):     off = 9;  break;
        case hash("\"B"):     off = 4;  break;
        case hash("\"C"):     off = 10; break;
        case hash("\"UP"):    off = 13; break;
        case hash("\"DOWN"):  off = 3;  break;
        case hash("\"LEFT"):  off = 25; break;
        case hash("\"RIGHT"): off = 7;  break;
        default: break;
        }
        sym.hitTemp();
        codegen.LDRI(cg::R0, 0xA0000020);
        u32 tmpId = createTmpSymbol();
        auto reg = regAlloc[tmpId];
        auto &tmp = symTable[tmpId];
        tmp.clearKCTV();
        tmp.reg = reg.value;
        codegen.LDRB(reg, cg::R0, off);
        symId = tmpId;
    }

    void callLength(u32 argc, u32 *argv){
        if(argc != 1){
            setError("length expects 1 argument");
            return;
        }
        auto tmpId = createTmpSymbol();
        auto reg = regAlloc[tmpId];
        auto &tmp = symTable[tmpId];
        tmp.clearKCTV();
        tmp.reg = reg.value;
        auto &sym = load(argv[0]);
        codegen.SUBS(reg, cg::RegLow(sym.reg), 4);
        codegen.LDRH(reg, reg, 0);
        symId = tmpId;
    }

    bool callIntrinsic(u32 symId, u32 argc, u32* argv){
        auto &sym = symTable[symId];
        switch(sym.hash){
        case "peek"_token: callPeek(argc, argv); return true;
        case "poke"_token: callPoke(argc, argv); return true;
        case "pressed"_token:
            if(argc == 1 && symTable[argv[0]].hasKCTV()){
                callPressed(argc, argv);
                return true;
            }
        case "length"_token: callLength(argc, argv); return true;
        default: return false;
        }
    }

    void writeCall(u32 symId, u32 argc, u32* argv){
        if(error)
            return;
        if(callIntrinsic(symId, argc, argv))
            return;
        LOGD("Write call\n");
        bool isConstexpr;
        {
            auto &call = symTable[symId];
            if(call.hasKCTV() && call.kctv == 0)
                call.clearKCTV();
            isConstexpr = argc <= 4 && call.isConstexpr();
        }

        {
            u32 argkctv[4];
            for(u32 i=0; isConstexpr && i<argc; ++i){
                auto &arg = symTable[argv[i]];
                isConstexpr = arg.hasKCTV();
                argkctv[i] = arg.kctv;
            }
            if(isConstexpr){
                for(u32 i=0; i<argc; ++i){
                    symTable[argv[i]].hitTemp();
                }
                auto &func = symTable[symId].kctv;
                // LOG("Consteval: ", (void*) func, "\n");
                u32 r = 0;
                switch(argc){
                case 0: r = reinterpret_cast<u32(*)()>(func)(); break;
                case 1: r = reinterpret_cast<u32(*)(u32)>(func)(argkctv[0]); break;
                case 2: r = reinterpret_cast<u32(*)(u32, u32)>(func)(argkctv[0], argkctv[1]); break;
                case 3: r = reinterpret_cast<u32(*)(u32, u32, u32)>(func)(argkctv[0], argkctv[1], argkctv[2]); break;
                case 4: r = reinterpret_cast<u32(*)(u32, u32, u32, u32)>(func)(argkctv[0], argkctv[1], argkctv[2], argkctv[3]); break;
                }
                this->symId = symId = createTmpSymbol();
                symTable[symId].setKCTV(r);
                return;
            }else{
                this->isConstexpr = false;
            }
        }

        for(u32 i=1; i<argc; ++i){
            loadToRegister(argv[i], i);
            regAlloc.assign(argv[i], cg::RegLow(i), true);
        }
        if(argc > 0){
            for(u32 i=0; i<argc; ++i){
                if(i && !regAlloc.isLocked(cg::RegLow(i))){
                    LOG("BUG\n");
                }
                symTable[argv[i]].hitTemp();
            }
            commitScratch();
            load(symId);
            loadToRegister(argv[0], 0);
        }else{
            commitScratch();
            load(symId);
        }
        {
            auto &call = symTable[symId];
            if(call.reg < argc){
                setError("REGALLOC BUG\n");
                return;
            }
            codegen.BLX(cg::RegLow(call.reg));
        }
        for(u32 i=1; i<argc; ++i)
            regAlloc.release(cg::RegLow(i));
        invalidateRegisters(false);
        this->symId = symId = createTmpSymbol();
        auto reg = regAlloc[symId];
        auto& sym = symTable[symId];
        sym.reg = reg.value;
        sym.clearKCTV();
        codegen.MOVS(reg, cg::R0);
    }

    void simpleExpression(){
        if(error) return;
        unaryExpression();
        while(!error && tok.isOperator() && token != "="_token)
            binaryExpression();
        if(accept("="_token)){
            auto lsymId = symId;
            if(symId == invalidSym){
                setError("simpleExpression: Invalid assignment");
                return;
            }
            simpleExpression();
            assign(lsymId);
        }
    }

    void expression(){
        if(error) return;
        do {
            simpleExpression();
        }while(accept(","_token));
    }

    void loadToRegister(u32 symId, u32 reg){
        auto& sym = symTable[symId].hitTemp();
        LOGD("LOAD ", symId, " reg:", sym.reg, " into reg ", reg, " type:", sym.type, "\n");
        if(reg == sym.reg){
            boolCast(sym);
            if(sym.isDeref()){
                codegen.LDR(cg::RegLow(reg), cg::RegLow(reg));
                sym.clearDeref();
            }
            LOGD("SKIP LOAD ", symId, "\n");
            return;
        }

        {
            u32 evictId = regAlloc[cg::RegLow(reg)];
            if(evictId != invalidSym && evictId != symId){
                auto &evict = symTable[evictId];
                boolCast(evict);
                commit(evict, evictId);
                evict.reg = invalidReg;
            }
        }
        
        bool deref = sym.isDeref();
        sym.clearDeref();
        if(regAlloc.isValid(sym.reg)){
            if(deref){
                codegen.LDR(cg::RegLow(reg), cg::RegLow(sym.reg), 0);
            }else{
                boolCast(sym);
                codegen.MOVS(cg::RegLow(reg), cg::RegLow(sym.reg));
            }
            if(reg)
                sym.reg = reg;
            return;
        }
        if(reg)
            sym.reg = reg;
        if(sym.hasKCTV()){
            codegen.LDRI(cg::RegLow(reg), sym.kctv, preserveFlags);
        }else{
            if(sym.address == invalidAddress){
                if(!sym.isInStack()) sym.address = globalScopeSize++;
                else sym.address = scopeSize++;
            }
            if(!sym.isInStack()){
                codegen.LDRI(cg::RegLow(reg), dataSection + (sym.address << 2), preserveFlags);
                codegen.LDR(cg::RegLow(reg), cg::RegLow(reg), 0);
            } else {
                codegen.LDR(cg::RegLow(reg), cg::SP, sym.address << 2);
            }
        }
        if(deref){
            LOGD("Deref load ", symId, "[0]\n");
            codegen.LDR(cg::RegLow(reg), cg::RegLow(reg));
            sym.clearKCTV();
        }
    }

    void assign(u32 id){
        if(symId == id)
            return;
        auto& evict = symTable[symId];
        auto& sym = symTable[id];
        if(sym.isDeref()){
            LOGD("Deref store ", id, "[0] = ", symId, "\n");
            sym.clearDeref();
            load(id);
            load(symId);
            codegen.STR(cg::RegLow(evict.reg), cg::RegLow(sym.reg));
            sym.hitTemp();
            return;
        }
        if(evict.hasKCTV() && !evict.isDeref()){
            evict.hitTemp();
            sym.setKCTV(evict.kctv);
            LOGD("assign ", id, " into KCTV ", evict.kctv, " from ", symId, "\n");
        }else{
            LOGD("assign ", id, " into ", symId, "\n");
            sym.clearKCTV();
            load(symId);
            u32 reg = evict.reg;
            spill(evict, symId);
            regAlloc.assign(id, cg::RegLow(reg));
            sym.reg = reg;
            sym.setDirty();
        }
        sym.type = (evict.type != Sym::UNCOMPILED) ? evict.type : Sym::FUNCTION;
        symId = id;
    }

    void constDecl(){
        u32 id;
        do {
            id = createSymbol(scopeId);
            if(error) return;
            if(!accept("="_token)){
                setError("Const without initializer\n");
                return;
            }
            simpleExpression();
            auto &rval = symTable[symId];
            rval.hitTemp();
            // assign(id);
            auto &sym = symTable[id];
            if(!rval.hasKCTV()){
                setError("Const initializer not known in compile-time");
                return;
            }else{
                sym.setConstant(rval.kctv);
            }
        }while(accept(","_token));
        symId = id;
    }

    void varDecl(){
        u32 id;
        do {
            id = createSymbol(scopeId);
            if(error) return;
            if(accept("="_token)){
                simpleExpression();
                assign(id);
                auto &sym = symTable[id];
                if(sym.hasKCTV())
                    sym.setMemInit();
            }else if(scopeId != 0){
                symTable[id].setDirty();
            }
        }while(accept(","_token));
        symId = id;
    }

    void deadBlock(){
        LOGD("Dead block\n");
        if(!accept("{"_token)){
            setError("1 Unexpected token (not {)");
            return;
        }
        u32 depth = 1;
        while(depth && tok.getClass() != TokenClass::Eof && !error){
            if(accept("{"_token)) depth++;
            else if(accept("}"_token)) depth--;
            else accept();
        }
    }

    void ifStatement(){
        u32 endLabel = nextLabel++;
        while(!error) {
            if(!accept("if"_token)){
                setError("2 Unexpected token (not if)");
                return;
            }

            prenExpression();
            if(error)
                return;

            u32 failLabel = nextLabel++;
            // auto &sym = symTable[symId];
            // if(sym.hasKCTV()){
            //     sym.hitTemp();
            //     if(sym.kctv){
            //         statementOrBlock();
            //         return;
            //     }else if (token == "{"_token){
            //         deadBlock();
            //         return;
            //     }
            // }

            toBranch(failLabel);
            statementOrBlock();
            flush();

            if(accept("else"_token)){
                codegen.B(endLabel);
                codegen[failLabel];
                if(token == "if"_token) continue;
                statementOrBlock();
                flush();
                break;
            }else{
                codegen[failLabel];
                break;
            }
        }
        codegen[endLabel];
        // all KCTVs are now invalid
    }

    void doStatement(){
        if(!accept("do"_token)){
            setError("do: Unexpected token (not do)");
            return;
        }

        u32 lblTest = nextLabel++;
        u32 lblBreak = nextLabel++;
        u32 lblNext = nextLabel++;

        flush();

        codegen[lblNext];

        block();

        if(error)
            return;

        if(!accept("while"_token)){
            setError("do: Unexpected token (not while)");
            return;
        }

        codegen[lblTest];
        prenExpression();
        
        if(error)
            return;
        
        toBranch(lblBreak);
        codegen.B(lblNext);
        codegen[lblBreak];

    }

    void whileStatement(){
        if(!accept("while"_token)){
            setError("while: Unexpected token (not while)");
            return;
        }
        u32 lblTest = nextLabel++;
        u32 lblBreak = nextLabel++;
        flush();

        codegen[lblTest];
        prenExpression();
        if(error)
            return;

        auto sym = symTable[symId];
        if(sym.hasKCTV()){
            if(!sym.kctv){
                statementOrBlock();
                return;
            }
        }
        toBranch(lblBreak);
        statementOrBlock();
        flush();
        codegen.B(lblTest);
        codegen[lblBreak];
   }

    void forStatement(){
        if(!accept("for"_token) || !accept("("_token)){
            setError("for: Unexpected token (not for and not ()");
            return;
        }
        LOGD("For Init\n");
        switch(token){
        case ";"_token: break;
        case "var"_token: accept(); varDecl(); break;
        default: expression(); hitTmpSym(symId); break;
        }

        if(accept("of"_token)){
            forOfStatement();
        }else if(accept("in"_token)){
            forInStatement();
        }else if(accept(";"_token)){
            classicFor();
        }else{
            setError("for: Expected ;");
            return;
        }
    }

    void forOfStatement(){
        u32 valueId = symId;

        value();

        if(!accept(")"_token)){
            setError("forOf: Expected )");
            return;
        }

        u32 lblTest = nextLabel++;
        u32 lblContinue = nextLabel++;
        u32 lblEnter = nextLabel++;
        u32 lblBreak = nextLabel++;

        u32 arrId = symId;
        u32 itId = createTmpSymbol();
        u32 maxId = createTmpSymbol();

        {
            load(arrId);
            assign(itId);
            auto &it = symTable[itId];
            auto &max = symTable[maxId];
            max.setKCTV(-4);
            load(maxId);
            max.unhitTemp();
            codegen.LDRH(cg::RegLow(max.reg), cg::RegLow(it.reg), cg::RegLow(max.reg));
            codegen.CMP( cg::RegLow(max.reg), 0 );
            max.setDirty();
            preserveFlags++;
            commitAll();
            invalidateRegisters();
            preserveFlags--;
            codegen.B(cg::EQ, lblBreak);
            codegen[lblTest];
            max.unhitTemp();
        }

        {
            auto valReg = regAlloc[valueId];
            auto &value = symTable[valueId];
            auto &it = load(itId);
            value.reg = valReg.value;
            codegen.LDM(cg::RegLow(it.reg), valReg);
            value.setDirty();
            value.clearKCTV();
            it.setDirty();
            it.unhitTemp();
        }

        
        if(token == "{"_token)
            block();
        else
            statements();
        {
            flush();
            codegen[lblContinue];
            auto &max = load(maxId);
            max.unhitTemp();
            codegen.SUBS( cg::RegLow(max.reg), 1);
            max.setDirty();
            commit(max, maxId);
            codegen.B(cg::NE, lblTest);
            codegen[lblBreak];
        }

        symTable[maxId].hitTemp();
        symTable[itId].hitTemp();
    }

    void forInStatement(){
        u32 valueId = symId;
        value();

        if(!accept(")"_token)){
            setError("forIn: Expected )");
            return;
        }

        u32 lblTest = nextLabel++;
        u32 lblContinue = nextLabel++;
        u32 lblEnter = nextLabel++;
        u32 lblBreak = nextLabel++;

        u32 arrId = symId;
        u32 itId = createTmpSymbol();
        u32 maxId = createTmpSymbol();

        {
            load(arrId);
            assign(itId);
            auto &it = load(itId).unhitTemp();
            auto &max = symTable[maxId];
            max.setKCTV(-4);
            load(maxId);
            max.unhitTemp();
            codegen.LDRH(cg::RegLow(max.reg), cg::RegLow(it.reg), cg::RegLow(max.reg));
            codegen.LDRI(cg::RegLow(it.reg), 0);
            codegen.CMP(cg::RegLow(max.reg), 0);
            max.setDirty();
            it.setDirty();
            preserveFlags++;
            flush();
            preserveFlags--;
            codegen.B(cg::EQ, lblBreak);
            codegen[lblTest];
            max.unhitTemp();
        }

        {
            auto valReg = regAlloc[valueId];
            auto &value = symTable[valueId];
            auto &it = load(itId).unhitTemp();
            codegen.MOVS(valReg, cg::RegLow(it.reg));
            codegen.ADDS(cg::RegLow(it.reg), cg::RegLow(it.reg), 1);
            it.setDirty();
            spill(it, itId);
            value.reg = valReg.value;
            value.setDirty();
            value.clearKCTV();
        }

        if(token == "{"_token)
            block();
        else
            statements();

        {
            flush();
            codegen[lblContinue];
            auto &max = load(maxId);
            max.unhitTemp();
            codegen.SUBS( cg::RegLow(max.reg), 1);
            max.setDirty();
            commit(max, maxId);
            codegen.B(cg::NE, lblTest);
            codegen[lblBreak];
        }

        symTable[maxId].hitTemp();
        symTable[itId].hitTemp();
    }

    void classicFor(){
        u32 lblTest = nextLabel++;
        u32 lblContinue = nextLabel++;
        u32 lblEnter = nextLabel++;
        u32 lblBreak = nextLabel++;

        LOGD("For Condition\n");
        flush();
        if(accept(";"_token)){
            lblTest = lblEnter;
        }else{
            codegen[cg::Label(lblTest)];
            expression();
            toBranch(lblBreak);
            if(!accept(";"_token)){
                setError("for: Expected ;");
                return;
            }
        }
        flush();
        codegen.B(lblEnter);

        LOGD("For Continue\n");
        if(accept(")"_token)){
            lblContinue = lblTest;
        }else{
            codegen[cg::Label(lblContinue)];
            statements();
            if(!accept(")"_token)){
                setError("for: Expected )");
                return;
            }
            flush();
        }
        codegen.B(cg::Label(lblTest));
        codegen[cg::Label(lblEnter)];

        LOGD("For Body\n");
        if(token == "{"_token)
            block();
        else
            statements();

        flush();
        codegen.B(cg::Label(lblContinue));
        codegen[cg::Label(lblBreak)];

        LOGD("For complete\n");
    }

    void returnStatement(){
        if(scopeId == 0){
            setError("Can't return outside function");
            return;
        }
        if(!accept("return"_token)){
            setError("Unexpected token (not return)");
            return;
        }
        if(accept(";"_token) || (token == "}"_token) || isKeyword()){
            flush();
            codegen.LDRI(cg::R0, 0);
        }else{
            expression();
            symTable[symId].hitTemp();
            commitAll();
            loadToRegister(symId, 0);
            invalidateRegisters();
        }
        codegen.B(cg::Label(returnLabel));
    }

    void statementOrBlock(){
        if(token != "{"_token)
            statements();
        else
            block();
    }

    void statements(){
        switch(token){
        case ";"_token: break;
        case "debugger"_token: accept(); codegen.BKPT(0); break;
        case "var"_token: accept(); varDecl(); break;
        case "const"_token: accept(); constDecl(); break;
        case "if"_token: ifStatement(); break;
        case "do"_token: doStatement(); break;
        case "while"_token: whileStatement(); break;
        case "for"_token: forStatement(); break;
        case "return"_token: returnStatement(); break;
        default: expression(); hitTmpSym(symId); break;
        }
        accept(";"_token);
    }

    void block(){
        if(!accept("{"_token)){
            setError("3 Unexpected token (not {)");
            return;
        }

        while(!error && (token != "}"_token)){
            statements();
        }

        if(!accept("}"_token)){
            setError("4 Unexpected token (not })");
            return;
        }
    }

    void declArgs(){
        if(!accept("("_token)){
            setError("declArgs: Unexpected token (not ()");
            return;
        }
        if(accept(")"_token)){
            return;
        }
        u32 argc = 0;
        u32 sym0 = invalidSym;
        do {
            if(argc == 8){
                setError("Too many arguments");
                return;
            }
            u32 id = createSymbol(scopeId);
            if(error) return;
            auto &sym = symTable[id];
            sym.clearKCTV();
            sym.setDirty();
            if(sym0 == invalidSym){
                sym0 = id;
            } else {
                regAlloc.assign(id, cg::RegLow(++argc));
                sym.reg = argc;
            }
        }while(accept(","_token));

        if(sym0 != invalidSym){
            auto reg = regAlloc[sym0];
            symTable[sym0].reg = reg.value;
            codegen.MOVS(reg, cg::R0);
        }

        if(!accept(")"_token)){
            setError("Unexpected token (not ))");
        }
    }

    auto& symbols(){
        return symTable;
    }

    const char *getError(){
        return error;
    }

    u32 initStack;
    void beginFunction(){
        using namespace cg;
        initStack = codegen.getWriter().tell();
        codegen.PUSH(R4, R5, R6, R7, LR);
        codegen.NOP();
        regAlloc.clearUseMap();
    }

    void endFunction(u32 &addr){
        using namespace cg;
        auto& writer = codegen.getWriter();
        u32 end = writer.tell();

        writer.seek(initStack);

        if(!scopeSize)
            codegen.NOP();

        codegen.PUSH((regAlloc.getUseMap() & 0xF0) | 0x100);

        if(scopeSize){
            codegen.SUB(SP, scopeSize << 2);
            writer.seek(end);
            codegen.ADD(SP, scopeSize << 2);
        }else{
            writer.seek(end);
            addr += 2;
        }

        codegen.POP((regAlloc.getUseMap() & 0xF0) | 0x100);
        codegen.POOL();
        codegen.link();
    }

    void declFunction(){
        u32 location = tok.getLocation();
        u32 line = tok.getLine();
        if(!accept("function"_token) || !isName()){
            setError("Unexpected token (not function and not name)");
            return;
        }

        u32 fsymId = findOrCreateSymbol(0);
        auto& sym = symTable[fsymId];
        sym.type = Sym::UNCOMPILED;
        sym.line = line;
        sym.init = location;
        sym.clearKCTV();

        if(!accept("("_token)){
            setError("declFunc: Unexpected token (not ()");
            return;
        }

        u32 depth=1;
        while(tok.getClass() != TokenClass::Eof && !error && depth){
            if(accept("("_token)) depth++;
            else if(accept(")"_token)) depth--;
            else accept();
        }

        deadBlock();
    }

    void parseFunction(u32 baseAddress, u32 symId){
        using namespace cg;
        auto& sym = symTable[symId];
        if(sym.type != Sym::UNCOMPILED){
            setError("Symbol already compiled");
            return;
        }

        isConstexpr = true;
        scopeId = ++maxScope;
        returnLabel = nextLabel++;
        scopeSize = 0;
        u32 addr = baseAddress + codegen.tell() | 1;
        tok.setLocation(sym.init, sym.line);
        sym.setMemInit(addr);
        sym.type = Sym::FUNCTION;
        beginFunction();
        sym.kctv = addr;
        clearAllKCTV();

        accept(); // function

        accept(); // name

        declArgs();

        block();

        if(error){
            LOG("ERROR on line ", line, " column ", column, ": ", error, "\nOn token: [", tok.getText()[0], (tok.getText()[1] ?: '@'), "|", tok.getClass(), "]\n");
            while(true);
        } else {
            flush();
            codegen.LDRI(R0, 0);
            codegen[cg::Label(returnLabel)];

            endFunction(addr);

            auto &sym = symTable[symId];
            sym.setMemInit(addr);
            if(isConstexpr)
                sym.setConstexpr();

            purgeTemps();
        }
    }

    u32 getGlobalScopeSize(){
        return globalScopeSize;
    }

    void parseGlobal(){
        using namespace cg;
        beginFunction();
        scopeSize = 0;
        accept();
        while(tok.getClass() != TokenClass::Eof){
            if(token == "function"_token)
                declFunction();
            else statements();
            if(error) break;
        }

        if(error){
            LOGD("ERROR on line ", line, " column ", column, ": ", error, "\nOn token: [", tok.getText()[0], (tok.getText()[1] ?: '@'), "|", tok.getClass(), "]\n");
            while(true);
        } else {
            flush();
            u32 addr;
            endFunction(addr);
        }
    }
};

}
