#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
typedef HRESULT (*Init)(void);
typedef HRESULT (*Poll)(uint8_t);
typedef HRESULT (*Read)(uint8_t,uint8_t*,size_t);
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    HMODULE dll=LoadLibraryA(argv[1]);if(!dll){printf("LoadLibrary failed: %lu\n",GetLastError());return 1;}
    const char *names[]={"aime_io_get_api_version","aime_io_init","aime_io_nfc_poll","aime_io_nfc_get_aime_id","aime_io_nfc_get_felica_id","aime_io_led_set_color","aime_io_nfc_get_mifare_block"};
    for(int i=0;i<7;i++)if(!GetProcAddress(dll,names[i])){printf("Missing: %s\n",names[i]);return 1;}
    Init init=(Init)GetProcAddress(dll,"aime_io_init");Poll poll=(Poll)GetProcAddress(dll,"aime_io_nfc_poll");Read read=(Read)GetProcAddress(dll,"aime_io_nfc_get_aime_id");
    if(init()!=S_OK || poll(0)!=S_OK)return 1;
    uint8_t code[10];HRESULT result=read(0,code,10);if(result!=S_FALSE){puts("Unexpected card without key");return 1;}
    puts("Actual DLL loaded; seven exports, initialization, poll and no-key removal passed.");return 0;
}
