#include "spad.h"
#include "../desktop/src/SpadTables.h"
#include <cstring>

// Keep the Qt reader's validated SPAD algorithm and exact substitution tables.
extern "C" bool spad_access_code(const uint8_t cipher[16], uint8_t code[10]) {
    uint8_t inverse[9][256];
    for (int t=0;t<9;++t) {
        bool seen[256]={};
        for (int i=0;i<256;++i) {
            uint8_t v=SpadTables[t][i];
            if(seen[v])return false;
            seen[v]=true;inverse[t][v]=uint8_t(i);
        }
    }
    uint8_t d[16];
    for(int i=0;i<16;++i)d[i]=inverse[8][cipher[i]];
    int count=(d[15]>>4)+7,table=d[15]+5*count;
    for(int k=0;k<count;++k) {
        table-=5;uint8_t prior=d[14];
        for(int i=0;i<15;++i) {
            uint8_t old=d[i];d[i]=uint8_t((old>>5)|((prior&31)<<3));prior=old;
        }
        for(int i=0;i<15;++i)d[i]=inverse[table%8][d[i]];
    }
    if((d[0]&15)!=0)return false;
    bool nonzero=false;
    for(int i=6;i<16;++i) {
        if((d[i]>>4)>9 || (d[i]&15)>9)return false;
        nonzero|=d[i]!=0;
    }
    if(!nonzero)return false;
    std::memcpy(code,d+6,10);return true;
}
