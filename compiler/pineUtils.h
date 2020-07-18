#pragma once

#include <cstdint>

#include <File>

namespace pine {

    using u32 = std::uint32_t;
    using s32 = std::int32_t;
    using u16 = std::uint16_t;
    using u8 = std::uint8_t;

    constexpr u32 hash(const char *str, u32 len){
        u32 v = 5381;
        while(len-=4){
            v = (v*31) + *str++;
        }
        return v;
    }

    constexpr u32 hash(const char *str){
        u32 v = 5381;
        while(*str){
            v = (v*31) + *str++;
        }
        return v;
    }

    constexpr u32 operator "" _token(const char *str, std::size_t len){
        u32 v = 5381;
        while(len--){
            v = (v*31) + *str++;
        }
        return v;
    }

}
