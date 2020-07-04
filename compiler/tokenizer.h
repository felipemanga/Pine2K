#pragma once

#include "./pinesUtils.h"

namespace pines {

enum class TokenClass {
    Number = 0,
    String = 1,
    Word = 2,
    Operator = 3,
    Special = 4,
    Unknown = 5,
    Eof = 6
};

class Tokenizer {
    static constexpr u32 maxStrToken = 32;
    File source;
    char strToken[maxStrToken];
    TokenClass tokClass;
    u32 numToken;
    u32 strHash = 0;
    u32 line = 0, column = 0;
    u8 next = 0, eof = 0, strDelim;

    bool isWordStart(u8 ch){
        return (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch == '_');
    }

    bool isWordChar(u8 ch){
        return (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            (ch == '_');
    }

    bool isHexChar(u8 ch){
        return isNumericChar(ch) ||
            (ch >= 'a' && ch <= 'f') ||
            (ch >= 'A' && ch <= 'F');
    }

    bool isNumericChar(u8 ch){
        return (ch >= '0' && ch <= '9');
    }

    void read(){
        eof = source.read(&next, 1) != 1;
        if(eof) next = 0;

        if(next == '\n'){
            column = 0;
            line++;
        }else{
            column++;
        }
    }

public:
    Tokenizer(const char *source){
        this->source.openRO(source);
        read();
    }

    u32 getLocation(){ return this->source.tell(); }

    void setLocation(u32 loc, u32 line){
        this->line = line - 1;
        this->column = 0;
        this->source.seek(loc);
        read();
    }

    char *getText(){ return strToken; }

    u32 getLine(){ return line + 1; }

    u32 getColumn(){ return column; }

    TokenClass getClass(){ return tokClass; }

    u32 getNumeric(){ return numToken; }

    bool isNumeric(){ return tokClass == TokenClass::Number; }

    bool isOperator(){ return tokClass == TokenClass::Operator; }

    bool isSpecial(){ return tokClass == TokenClass::Special; }

    bool isString(){ return tokClass == TokenClass::String; }

    u32 get(){
        numToken = 0;
        for(u32 i=0; i<sizeof(strToken); ++i){
            strToken[i] = 0;
        }

        if(tokClass == TokenClass::String){
            if(eof){
                tokClass = TokenClass::Eof;
                return 0;
            }
            if (next != strDelim) {
                strToken[0] = next;
                read();
                return 0;
            } else read();
        }

        tokClass = TokenClass::Unknown;

        bool hasWhitespace;
        do {
            hasWhitespace = false;
            strToken[0] = next;

            // skip whitespace
            while(!eof && next <= 32){
                read();
                hasWhitespace = true;
            }

            // skip comment
            if( next == '/' ){
                read();
                hasWhitespace = true;
                // single-line comment
                if( next == '/' ){
                    do {
                        read();
                    } while(next != '\n' && !eof);
                } else if(next == '*') {
                    do {
                        read();
                        if(next == '*'){
                            read();
                            if(next == '/'){
                                read();
                                break;
                            }
                        }
                    } while(!eof);
                } else if(next == '=') {
                    strToken[1] = next;
                    read();
                    tokClass = TokenClass::Operator;
                    return "/="_token;
                } else {
                    tokClass = TokenClass::Operator;
                    return "/"_token;
                }
            }
        }while(hasWhitespace);

        if(eof){
            tokClass = TokenClass::Eof;
            return 0;
        }

        if(next == '"' || next == '\'' || next == '`'){
            strDelim = next;
            read();
            tokClass = TokenClass::String;
            strToken[0] = next;
            read();
            return 0;
        }

        if(next == '!'){
            tokClass = TokenClass::Operator;
            read();
            if(next == '='){
                strToken[1] = next;
                read();
                return "!="_token;
            }
            return "!"_token;
        }

        if(next == '~'){
            tokClass = TokenClass::Operator;
            read();
            return "~"_token;
        }

        if(next == '-'){
            tokClass = TokenClass::Operator;
            read();
            if(next == '='){
                strToken[1] = next;
                read();
                return "-="_token;
            }
            // if(next == '>'){
            //     strToken[1] = next;
            //     read();
            //     return "->"_token;
            // }
            if(next == '-'){
                strToken[1] = next;
                read();
                return "--"_token;
            }
            return "-"_token;
        }

        for(const char *s = "(){}[];,."; *s; ++s){
            if(next == *s){
                tokClass = TokenClass::Special;
                read();
                return (5381 * 31) + *s;
            }
        }

        for(const char *s = "+=*^%<>&|"; *s; ++s){
            if( next == *s ){
                tokClass = TokenClass::Operator;
                read();
                if(next == '='){
                    strToken[1] = next;
                    read();
                    return ((5381 * 31) + *s) * 31 + '=';
                }
                if(next == *s){
                    strToken[1] = next;
                    read();
                    if(next == '='){
                        strToken[2] = next;
                        read();
                        return (((5381 * 31) + *s) * 31 + *s) * 31 + '=';
                    }
                    return ((5381 * 31) + *s) * 31 + *s;
                }
                return (5381 * 31) + *s;
            }
        }

        if( isWordStart(next) ){
            tokClass = TokenClass::Word;
            strHash = 5381;
            u32 pos = 0;
            do {
                strToken[pos++] = next;
                strHash = (strHash * 31) + next;
                read();
            }while( (pos+1) < maxStrToken && !eof && isWordChar(next) );
            return strHash;
        }

        if( next == '#' )
            return 0;

        if( next == '0' ){
            read();
            strHash = 5381 * 31 + '#';
            numToken = 0;
            tokClass = TokenClass::Number;
            if(next == 'x' || next == 'X'){
                u32 pos = 1;
                strToken[pos++] = next;
                do {
                    numToken <<= 4;
                    if(next >= '0' && next <= '9')
                        numToken += next - '0';
                    else if(next >= 'A' && next <= 'F')
                        numToken += 10 + next - 'A';
                    else if(next >= 'a' && next <= 'f')
                        numToken += 10 + next - 'a';
                    strToken[pos++] = next;
                    read();
                }while( (pos+1) < maxStrToken && !eof && isHexChar(next) );
            }else if(next == 'b' || next == 'B'){
                u32 pos = 1;
                strToken[pos++] = next;
                do {
                    numToken <<= 1;
                    numToken += next == '1';
                    strToken[pos++] = next;
                    read();
                }while( (pos+1) < maxStrToken && !eof && (next == '0' || next == '1') );
            }
            return strHash;
        }

        if( isNumericChar(next) ){
            tokClass = TokenClass::Number;
            strHash = 5381 * 31 + '#';
            numToken = 0;
            u32 pos = 1;
            do {
                numToken *= 10;
                numToken += next - '0';
                strToken[pos++] = next;
                read();
            }while( (pos+1) < maxStrToken && !eof && isNumericChar(next) );
            return strHash;
        }

        if(!eof){
            u32 ret = (5381 * 31) + next;
            read();
            return ret;
        }

        return 0;
    }
};

}
