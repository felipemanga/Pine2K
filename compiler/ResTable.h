#pragma once

#include "pineUtils.h"

namespace pine {

    class ResTable {
        File file;
        u32 resCount = 0;
        u32 capacity;
        u32 fileSize = 0;
        u32 *cache;
        u32 cacheSize = 0;

    public:
        ResTable(u32 maxResCount) : capacity(maxResCount) {
            file.openRW("pine-2k/resources.tmp", true, false);
        }

        void setCache(u32 *ptr, u32 size){
            if(cacheSize){
                file.seek(0);
                for(u32 i = 0; i<cacheSize && i < resCount; ++i){
                    file << cache[i*2] << cache[i*2+1];
                }
            }
            cache = ptr;
            cacheSize = size >> 3;
            file.seek(0);
            for(u32 i = 0; i<cacheSize; ++i){
                file >> cache[i*2] >> cache[i*2+1];
            }
        }

        void reset(){
            cache = nullptr;
            cacheSize = 0;
            resCount = 0;
            fileSize = 0;
            file.seek(0);
        }

        File *write(u32 key){
            if(file.tell() > fileSize)
                fileSize = file.tell();
            u32 pos = resCount;
            u32 offset = std::max(capacity * 8, fileSize);
            for(u32 i = 0; i < resCount; ++i){
                u32 otherKey;

                if(i < cacheSize){
                    otherKey = cache[i*2];
                }else{
                    file.seek(i * 8);
                    otherKey = file.read<u32>();
                    // file.read<u32>();
                }

                if(otherKey == key){
                    // u32 oldPos = file.read<u32>();
                    // return at(oldPos);
                    return nullptr;
                }
                // if(otherKey > key){
                //     pos = i;
                //     break;
                // }
            }
            
            u32 pk = key;
            u32 po = offset;
            // for(u32 i = pos; i < resCount; ++i){
            //     file.seek(i * 8);
            //     u32 ok = file.read<u32>();
            //     u32 oo = file.read<u32>();
            //     file.seek(i * 8);
            //     file << pk << po;
            //     pk = ok;
            //     po = oo;
            // }

            if(resCount < cacheSize){
                cache[resCount * 2] = pk;
                cache[resCount * 2 + 1] = po;
            } else {
                file.seek(resCount * 8);
                file << pk << po;
            }
            resCount++;

            file.seek(offset);
            return &file;
        }

        File& at(u32 offset){
            if(file.tell() > fileSize)
                fileSize = file.tell();
            file.seek(offset);
            return file;
        }

        u32 find(u32 key){
            if(file.tell() > fileSize)
                fileSize = file.tell();
            u32 i = 0;
            for(; i < cacheSize; ++i ){
                if( cache[i<<1] == key )
                    return cache[(i<<1)+1];
            }
            for(; i < resCount; ++i){
                file.seek(i * 8);
                u32 otherKey = file.read<u32>();
                if(otherKey == key){
                    return file.read<u32>();
                }
            }
            return 0;
        }

        File &read(u32 key){
            return at(find(key));
        }
    };
    
}
