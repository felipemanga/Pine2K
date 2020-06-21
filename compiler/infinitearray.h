#pragma once

#include <cstdint>

namespace ia {

    using u32 = std::uint32_t;
    using u16 = std::uint16_t;

    template<typename Type, u32 cacheLineCount = 8>
    class InfiniteArray {
        // static constexpr cacheLineCount = 8;
        File file;
        u32 count = 0;
        bool itIsDirty;

        u32 maxAge = 0;
        struct {
            u32 age;
            u32 offset;
            Type data;
        } cache[ cacheLineCount ];


    public:
        InfiniteArray(const char *swap){
            file.openRW(swap, true, false);
            for(u32 i=0; i<cacheLineCount; ++i){
                cache[i].offset = ~u32{};
                cache[i].age = 0;
            }
        }

        struct iterator {
            InfiniteArray *container;
            u32 index;
            u32 tmpIndex = ~u32{};
            Type tmp;

            iterator& operator++() {
                ++index;
                return *this;
            }

            bool operator != (u32 end) { return index != end; }

            Type &operator *() {
                flush();
                for(u32 i=0; i<cacheLineCount; ++i){
                    auto& cl = container->cache[i];
                    if(cl.offset == index){
                        tmpIndex = ~u32{};
                        return cl.data;
                    }
                }
                tmpIndex = index;
                container->file.seek(index * sizeof(Type));
                container->file.read(&tmp, sizeof(Type));
                return tmp;
            }

            ~iterator(){
                flush();
            }

            void flush(){
                if(tmpIndex >= container->count || !container->itIsDirty)
                    return;
                container->itIsDirty = false;
                container->file.seek(tmpIndex * sizeof(Type));
                container->file.write(&tmp, sizeof(Type));
                tmpIndex = ~u32{};
            }
        };

        void dirtyIterator(){
            itIsDirty = true;
        }

        iterator begin(){
            return {this, 0};
        }

        u32 end(){
            return size();
        }

        bool empty(){
            return count == 0;
        }

        u32 size(){
            return count;
        }

        Type& operator [] (u32 offset){
            if(offset > count){
                CRASH("Invalid offset");
            }
            u32 pickAge = cache[0].age;
            u32 pickNum = 0;
            if(cache[0].offset == offset){
                cache[0].age = ++maxAge;
                return cache[0].data;
            }

            for(u32 i=1; i<cacheLineCount; ++i){
                auto& cl = cache[i];
                if(cl.offset == offset){
                    cl.age = ++maxAge;
                    return cl.data;
                }
                if(cl.age < pickAge){
                    pickAge = cl.age;
                    pickNum = i;
                }
            }

            auto& pick = cache[pickNum];
            if(pick.offset != ~u32{}){
                file.seek(pick.offset * sizeof(Type));
                file.write(&pick.data, sizeof(Type));
            }

            if(offset < count){
                file.seek(offset * sizeof(Type));
                if( file.read(&pick.data, sizeof(Type)) != sizeof(Type) ){
                    pick.data = Type{};
                // }else{
                //     LOG("Cache miss ", offset, "\n");
                }
            }else{
                pick.data = Type{};
                count = offset + 1;
                file.seek(offset * sizeof(Type));
                file.write(&pick.data, sizeof(Type));
            }
            pick.offset = offset;
            pick.age = maxAge++;
            return pick.data;
        }
    };
}
