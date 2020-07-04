#pragma once

#include "pinesUtils.h"

namespace pines {

    class ResTable {
        File file;
        u32 resCount = 0;
        u32 capacity;
        u32 fileSize = 0;

    public:
        ResTable(u32 maxResCount) : capacity(maxResCount) {
            file.openRW("pines/resources.tmp", true, false);
        }

        void reset(){
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
                file.seek(i * 8);
                u32 otherKey = file.read<u32>();
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

            file.seek(resCount++ * 8);
            file << pk << po;
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
            for(u32 i = 0; i < resCount; ++i){
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
