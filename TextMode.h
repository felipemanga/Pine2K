#pragma once

#include <MemOps>

template<std::uint32_t fontWidth, std::uint32_t fontHeight, bool rotate>
class TextMode {
    static inline TextMode *instance;
    static constexpr std::uint32_t invFontHeight = (1 << 24) / (fontHeight + 1);
    static constexpr std::uint32_t invFontWidth = (1 << 24) / fontWidth;

public:
    static constexpr std::uint32_t columns = rotate ? POK_LCD_H / fontWidth : POK_LCD_W / (fontWidth + 1);
    static constexpr std::uint32_t rows = rotate ? POK_LCD_W / fontHeight : POK_LCD_H / (fontHeight + 1);
    static constexpr std::uint32_t hbytes = ((fontHeight>>3) + ((fontHeight != 8) && (fontHeight != 16)));
    static constexpr bool isTall = hbytes == 2;


    unsigned int x = 0;
    unsigned int y = 0;

    TextMode(){
        instance = this;
        clear();
    }

    void clear(){
        x = y = 0;
        MemOps::set(buffer, 0, sizeof(buffer));
    }

    void clear(std::uint32_t start, std::uint32_t end = ~0){
        if(start > rows){
            return;
        }
        if(end >= rows){
            end = rows;
        }
        if(start >= end){
            return;
        }
        auto count = end - start;
        MemOps::set(buffer + start * columns, 0, count * columns);
    }

    void setPosition(int x, int y){
        this->x = x;
        this->y = y;
    }

    void print(std::uint32_t x, std::uint32_t y, const char *text){
        auto oldX = this->x;
        auto oldY = this->y;
        this->y = y;
        this->x = x;
        print(text);
        this->x = oldX;
        this->y = oldY;
    }

    void print(const char *text){
        if(!text)
            return;
        while(*text){
            print(*text);
            text++;
        }
    }

    void print(char ch){
        std::int32_t c = ch;

        if(c == '\n'){
            newline();
            return;
        }

        if(x >= columns || y >= rows){
            newline();
        }

        if(Pokitto::Display::font[3] && c >= 'a' && c <= 'z'){
            c = (c - 'a') + 'A';
        }

        c -= Pokitto::Display::font[2];

        if(c < 0) c = 0;

        buffer[y * columns + x++] = c;
    }

    void printNumber(std::int32_t number){
        if(number < 0){
            print('-');
            number = -number;
        }

        if(number == 0){
            print('0');
            return;
        }

        char tmp[11];
        char *cursor = tmp + sizeof(tmp);
        *--cursor = 0;

        while(number && cursor > tmp){
            *--cursor = '0' + number % 10;
            number /= 10;
        }
        print(cursor);
    }

    void newline(){
        x = 0;
        y++;
        if(y >= rows){
            y = rows - 1;
            scrollUp();
        }
    }

    void scrollUp(){
        MemOps::copy(buffer, buffer + columns, (rows - 1) * columns);
        MemOps::set(buffer + (rows - 1) * columns, 0, columns);
    }

    void fillLandscape(std::uint8_t *line, std::uint32_t y, bool skip){
        std::uint32_t row = (y * invFontHeight + (1 << 20)) >> 24;
        std::uint32_t shift = y - row * (fontHeight + 1);
        if(row >= rows) return;
        const char *text = buffer + row * columns;
        auto font = Pokitto::Display::font + 4;
        for(std::uint32_t column = 0; column < columns; ++column){
            std::int32_t index = *text++;
            if(index == 0){
                line += fontWidth;
                continue;
            }

            auto bitmap = font + index * (fontWidth * hbytes + 1);
            std::uint32_t gliphWidth = *bitmap++;
            for(std::uint32_t localX = 0; localX < gliphWidth; ++localX, ++line){
                std::uint32_t bitcolumn = *bitmap++;
                if(isTall){
                    bitcolumn |= (*bitmap++) << 8;
                }
                auto color = (bitcolumn >> shift) & 1 ? Pokitto::Display::color : Pokitto::Display::bgcolor;
                if(color){
                    *line = color;
                }
            }
            line += fontWidth - gliphWidth + 1;
        }
    }

    void fillPortrait(std::uint8_t *line, std::uint32_t ix, bool skip){
        std::uint32_t x = POK_LCD_H - ix - 1;
        std::uint32_t column = (x * invFontWidth + (1 << 20)) >> 24;
        if(column >= columns) return;

        std::uint32_t localX = x - column * fontWidth;
        const char *text = buffer + column;
        for(std::uint32_t row = 0; row < rows; ++row){
            std::int32_t index = *text;
            text += columns;
            if(index == 0){
                line += fontHeight;
                continue;
            }

            auto bitmap = Pokitto::Display::font + 4 + index * (fontWidth * hbytes + 1);
            std::uint32_t gliphWidth = *bitmap++;
            if(localX >= gliphWidth){
                line += fontHeight;
                continue;
            }

            bitmap += localX * hbytes;

            std::uint32_t bitcolumn = *bitmap++;
            if(isTall){
                bitcolumn |= (*bitmap++) << 8;
            }

            for(std::uint32_t localY = 0; localY < fontHeight; ++localY, ++line){
                auto color = (bitcolumn >> localY) & 1 ? Pokitto::Display::color : Pokitto::Display::bgcolor;
                if(color){
                    *line = color;
                }
            }
        }
    }

    void activate(int fillerNumber){
        auto filler = +[](std::uint8_t *line, std::uint32_t y, bool skip){
            if( rotate )
                instance->fillPortrait(line, y, skip);
            else
                instance->fillLandscape(line, y, skip);
        };
        for(auto i=0; i<sizeof(Pokitto::Display::lineFillers) / sizeof(Pokitto::Display::lineFillers[0]); ++i){
            if(Pokitto::Display::lineFillers[i] == filler)
                Pokitto::Display::lineFillers[i] = TAS::NOPFiller;
        }
        Pokitto::Display::lineFillers[fillerNumber] = filler;
    }

private:
    char buffer[columns * rows];
};

template<const unsigned char *font> struct Text {
    using Landscape = TextMode<font[0], font[1], false>;
    using Portrait = TextMode<font[0], font[1], true>;
};
