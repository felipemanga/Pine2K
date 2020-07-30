#pragma once

#include "pine.h"
#include <MemOps>

namespace pine {
    
    inline u16 arrays = 0;

    struct ArrayHeader {
        u32 *data;
        u16 length;
        u16 next;
        bool mark;
        bool hasPtrs;
        bool isRoot;

        operator u32* (){
            return data;
        }

        explicit ArrayHeader(u16 v){
            uncompress(v);
        }

        void uncompress(u16 v){
            v &= 0x7FFC;
            data = v ? reinterpret_cast<u32*>(0x10000000 + v) : nullptr;
            if(data){
                u32 compressed = data[-1];
                length = static_cast<u16>(compressed);
                next = static_cast<u16>(compressed >> 16) & 0x7FFC;
                mark = compressed >> 31;
                hasPtrs = compressed & (1 << 16);
                isRoot = compressed & (1 << 17);
            } else {
                length = 0;
                next = 0;
                mark = false;
                hasPtrs = false;
                isRoot = false;
            }
        }

        ArrayHeader& operator ++ () {
            uncompress(next);
            return *this;
        }

        u16 *nextPtr(){
            return reinterpret_cast<u16*>(data - 1) + 1;
        }

        void update(){
            u32 compressed = length;
            compressed |= hasPtrs << 16;
            compressed |= isRoot << 17;
            compressed |= u32(next & 0x7FFC) << 16;
            compressed |= u32(mark) << 31;
            data[-1] = compressed;
        }
    };

    inline void deleteArrays(){
        for(ArrayHeader array(arrays); array; ++array){
            delete[] (array.data - 1);
        }
        arrays = 0;
    }

    inline void gc(u32 **stackBottom, u32 **stackTop, u32 **globals, u32 globalCount){
        u32 markCount = 0;
        // LOG("GC\n");
        for(ArrayHeader array(arrays); array; ++array){
            array.mark = array.isRoot;
            array.hasPtrs = false;
            u32 *begin = array.data;
            u32 *end = array.data + array.length;
            // if(array.isRoot) LOG("Skip root ", array.data, "\n");
            if(!array.mark){
                for(auto stack = stackBottom; stack < stackTop;  ++stack){
                    if(*stack >= begin && *stack < end){
                        // LOG("Stack Hit ", stack - stackBottom,  array.mark, " ", begin, ">=", *stack, "<", end, "\n");
                        array.mark = true;
                        break;
                    }
                }
            }
            if(!array.mark){
                for(u32 global = 0; global < globalCount; ++global){
                    auto value = globals[global];
                    if(value >= begin && value < end){
                        array.mark = true;
                        // LOG("Static Hit ", stackTop,  array.mark, " ", begin, " ", end, "\n");
                        break;
                    }
                }
            }
            for(auto i=0; i<array.length; ++i){
                if(array.data[i] >= 0x10000000 && array.data[i] < 0x10008000){
                    array.hasPtrs = true;
                    break;
                }
            }
            markCount += array.mark;
            array.update();
        }

        // LOG(markCount, "\n");

        while(markCount){
            markCount = 0;
            for(ArrayHeader array(arrays); array; ++array){
                if(array.mark)
                    continue;
                u32 *begin = array.data;
                u32 *end = array.data + array.length;
                for(ArrayHeader other(arrays); other.data; ++other){
                    if(!other.mark || !other.hasPtrs)
                        continue;
                    for(u32 i=0; i<other.length; ++i){
                        auto e = reinterpret_cast<u32*>(other.data[i]);
                        if(e >= begin && e < end){
                            array.mark = true;
                            array.update();
                            markCount += array.hasPtrs;
                            goto safe;
                        }
                    }
                }
            safe:;
            }
        }

        u32 cc = 0, kc = 0;
        u16 *prev = &arrays;
        for(ArrayHeader array(arrays); array; ++array){
            if(array.mark){
                prev = array.nextPtr();
            }else{
                cc++;
                *prev = (*prev & 2) | (array.next & 0x7FFC);
                // LOG("Collect ", array.data - 1, "\n");
                delete[] (array.data - 1);
            }
        }
    }

    inline u32 gcLockCount = 0;
    inline u32 globalCount = 0;
    inline u32* arrayCtr(u32 size){
        auto stackTop = reinterpret_cast<u32 **>(0x10008000);
        u32** stackBottom;
        __asm__ volatile("mov %[stackBottom], SP":[stackBottom] "+l" (stackBottom)::"r4", "r5", "r6", "r7");
        auto globals = reinterpret_cast<u32 **>(0x20004000);
        if(!gcLockCount)
            gc(stackBottom, stackTop, globals, globalCount);

        auto array = new u32[size + 1];
        if(!array){
            LOG("Out of Memory ", size, "\n");
            while(true);
        }

        // LOG("Alloc ", size, " @ ", array, "\n");

        array[0] = size | (u32(arrays) << 16);
        arrays = reinterpret_cast<uintptr_t>(array + 1) - 0x10000000;
        for(u32 i=1; i<=size; ++i)
            array[i] = 0;
        return array + 1;
    }

    class SimplePine {
        static constexpr const u32 dataSection = 0x20004000;
        static constexpr const u32 codeSection = 0x20000000;
        Tokenizer tok;
        /* * /
           cg::FileWriter writer;
           /*/
        cg::BufferWriter<2048> writer;
        /* */
        cg::CodeGen<decltype(writer)> cg;
        ia::InfiniteArray<Sym, 32> symTable;
        ResTable& resTable;
        Pine<decltype(cg), decltype(symTable)> pine;
        bool wasInit = false;

    public:
        SimplePine(const char *file, ResTable &resTable) :
            tok(file),
            writer /* * / ("pine.bin"), /*/ (reinterpret_cast<void*>(codeSection)) /* */,
            cg(writer),
            symTable("pine-2k/symbols.tmp"),
            resTable(resTable),
            pine(tok, cg, symTable, resTable, dataSection)
            {
                gcLockCount = 0;
                pine.setGCLock([](bool locked){
                                   if(locked) gcLockCount++;
                                   else if(gcLockCount) gcLockCount--;
                               });
                pine.setAllocator([](u32 size) -> void * {
                                       u32 *array = arrayCtr(size >> 2);
                                       array[-1] |= 1 << 17;
                                       return array;
                                   });
                setConstant("Array", arrayCtr, true);
                MemOps::set(reinterpret_cast<void*>(codeSection), 0, 0x800);
            }

        template<typename type>
        void setConstant(const char *name, type&& value, bool isConstexpr = false){
            auto &sym = pine.createGlobal(name);
            sym.setConstant(value);
            if(isConstexpr)
                sym.setConstexpr();
        }

        bool compile(){
            using namespace cg;
            // cg.LDR(R0, 0xCCBBDDEE);
            // cg.LDR(R1, 1);
            // cg[Label(u32(-1))].CMP(R1, 0);
            // cg.B(EQ, Label(u32(-2)));
            // cg.LDM(R0, R2, R3);
            // cg.SUBS(R1, 1);
            // cg.STR(R3, R2, 0);
            // cg.B(Label(u32(-1)));
            // cg.POOL();
            // cg[Label(u32(-2))];
            // cg.NOP();
            // cg.link();

            pine.parseGlobal(codeSection);
            if(pine.getError())
                return false;

            u32 uncompiled = ~u32{};

            do {
                uncompiled = 0;
                for(auto &sym : pine.symbols()){
                    if(sym.type == Sym::UNCOMPILED){
                        break;
                    }
                    uncompiled++;
                }
                if(uncompiled < pine.symbols().size()){
                    pine.parseFunction(codeSection, uncompiled);
                }else{
                    break;
                }
            } while(!pine.getError());

            if(pine.getError())
                return false;

            MemOps::set(reinterpret_cast<void*>(dataSection), 0, 0x800);

            u32 size = cg.tell();
            if(size > 2048){
                LOG("PROGMEM OVERFLOWED BY ", size - 2048, " BYTES\n");
                return false;
            }

            auto undefinedFunc = reinterpret_cast<u32>(+[](){
                                                            LOG("ERROR: Undefined function call\n");
                                                            while(true);
                                                        });

            u32 id = 0, len = 0;
            for(auto &sym : pine.symbols()){
                // if(sym.type == Sym::Type::FUNCTION){
                //     LOG("Func @ 0x", (void*) sym.kctv, "\n");
                // }
                if(sym.address != 0xFFFF){
                    auto address = reinterpret_cast<u32*>( dataSection + (sym.address << 2) );
                    // LOG("MemInit ", id, " ", (void*) address, " ", (void *) sym.init, "\n");
                    if(!sym.memInit() && sym.isCalled())
                        sym.setMemInit(undefinedFunc);
                    if(sym.memInit() && sym.init){
                        *address = sym.init;
                        len++;
                    }
                }
                id++;
            }

            LOG("PROGMEM: ", cg.tell(), " bytes (", (cg.tell() * 100) / 2048, "%) used.\n");

            // writer.seek(0x10 >> 1, true);
            // writer << u16(init)
            //        << u16(init>>16)
            //        << u16(len)
            //        << u16(len>>16);

            globalCount = pine.getGlobalScopeSize();

            return true;
        }

        template <typename func_t>
        func_t* getCall(const char *func){
            u32 h = hash(func);
            u32 id = 0;
            for(auto &sym : pine.symbols()){
                if(sym.scopeId == 0 && sym.hash == h){
                    auto f = reinterpret_cast<func_t*>(uintptr_t(sym.init));
                    // LOGD("Found sym ", id, " at ", f, "\n");
                    return f;
                }
                id++;
            }
            return nullptr;
        }

        void run(){
            if(wasInit) return;
            wasInit = true;
            auto func = writer.function<void()>();
            func();
        }
    };

}
