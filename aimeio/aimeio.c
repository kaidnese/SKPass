#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include "cards_json.h"
#include "spad.h"
#include "windows_hid.h"

#define VID 0x1CCF
#define PID 0x5252
#define FRAME_MAX 600

static CRITICAL_SECTION g_lock;
static HANDLE g_thread, g_stop;
static HANDLE g_port = INVALID_HANDLE_VALUE;
static skpass_hid_handle_t g_hid;
static skpass_hid_assembly_t g_hid_assembly;
static DWORD g_hid_hello, g_last_heartbeat;
static bool g_hid_ack, g_confirmed;
static bool g_classic, g_felica;
static uint8_t g_aime[10], g_idm[8];
static DWORD g_last_card;
static bool g_initialized;
static wchar_t g_json_path[32768];
static bool g_virtual_valid,g_held,g_selected_classic,g_selected_felica;
static uint8_t g_virtual_code[10],g_held_code[10],g_selected_code[10],g_selected_idm[8];
static unsigned g_key=VK_RETURN;
static DWORD g_json_check;
static wchar_t g_dll_dir[32768];

/* The provider directory is the game root. Allow its foreground executables
   (including subdirectories), regardless of game or loader executable name. */
static bool game_directory_allowed(const wchar_t *foreground,const wchar_t *directory) {
    wchar_t root[32768],image[32768];
    if(!foreground || !*foreground || !directory || !*directory)return false;
    DWORD n=GetFullPathNameW(directory,32768,root,NULL);
    DWORD m=GetFullPathNameW(foreground,32768,image,NULL);
    if(!n || n>=32767 || !m || m>=32768)return false;
    if(root[n-1]!=L'\\') {root[n++]=L'\\';root[n]=0;}
    return m>n && !_wcsnicmp(root,image,n);
}
static bool foreground_allowed(void) {
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    if(!pid)return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return false;
    wchar_t foreground[32768];DWORD size=32768;
    BOOL ok=QueryFullProcessImageNameW(process,0,foreground,&size);CloseHandle(process);
    return ok && game_directory_allowed(foreground,g_dll_dir);
}

static void reload_virtual(void) {
    DWORD now=GetTickCount();if(g_json_check && now-g_json_check<250)return;g_json_check=now;
    g_virtual_valid=false;
    HANDLE f=CreateFileW(g_json_path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    if(f==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER size;char text[65537];DWORD got;
    if(GetFileSizeEx(f,&size) && size.QuadPart>0 && size.QuadPart<=65536 && ReadFile(f,text,(DWORD)size.QuadPart,&got,NULL) && got==(DWORD)size.QuadPart) {
        g_virtual_valid=cards_json_parse(text,got,g_virtual_code,&g_key);
    }CloseHandle(f);
}

static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c=0xFFFF;
    while(n--) { c ^= (uint16_t)*p++ << 8; for(int i=0;i<8;i++) c=(c&0x8000)?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1); }
    return c;
}

static void clear_card(void) {
    EnterCriticalSection(&g_lock); g_classic=false; g_felica=false; LeaveCriticalSection(&g_lock);
}

static void accept_frame(const uint8_t *f, size_t n) {
    if(n<8 || f[0]!=0xCA || f[1]!=0x5A || f[2]!=1) return;
    uint16_t len=(uint16_t)f[4]|((uint16_t)f[5]<<8);
    if(len>512 || n != (size_t)len+8) return;
    uint16_t got=(uint16_t)f[n-2]|((uint16_t)f[n-1]<<8);
    if(crc16(f+2,4+len)!=got) return;
    const uint8_t *p=f+6;
    if(f[3]==SKPASS_HID_HELLO_REPLY && g_hid.handle) {
        uint32_t session=len==5 ? (uint32_t)p[1]|((uint32_t)p[2]<<8)|((uint32_t)p[3]<<16)|((uint32_t)p[4]<<24) : 0;
        g_hid_ack=len==5 && p[0]==1 && session==g_hid.session;return;
    }
    if(f[3]==1) {
        if((len>=7 && !memcmp(p,"SKPass-",7)) || (len>=13 && !memcmp(p,"pn7150_cardio",13))) {
            if(!g_hid.handle || g_hid_ack){g_confirmed=true;g_last_heartbeat=GetTickCount();}
        }
        return;
    }
    if((g_hid.handle || g_port!=INVALID_HANDLE_VALUE) && !g_confirmed)return;
    if(f[3]==0x11) { clear_card(); return; }
    if(f[3]!=0x10 || len<20) return;
    uint8_t type=p[0], status=p[1];
    unsigned spad=p[18];
    if((spad!=0 && spad!=16) || len<20+spad) return;
    unsigned mf=p[19+spad];
    if((mf!=0 && mf!=48) || len!=20+spad+mf) return;
    EnterCriticalSection(&g_lock);
    g_classic=false; g_felica=false;
    if(type==0x10 && status==0 && mf==48) {
        const uint8_t *m=p+20+spad;
        bool ok=true; for(int i=0;i<10;i++) if((m[38+i]>>4)>9 || (m[38+i]&15)>9) ok=false;
        if(ok && memcmp(m+38,"\0\0\0\0\0\0\0\0\0\0",10)!=0) { memcpy(g_aime,m+38,10); g_classic=true; }
    } else if(type==0x20) {
        /* Prefer the same printed access code decoded by Qt. Present it via
           the Aime API, exactly like a virtual card; retain IDm fallback. */
        if(status==0 && spad==16 && spad_access_code(p+19,g_aime))g_classic=true;
        else {memcpy(g_idm,p+2,8);g_felica=true;}
    }
    if(g_classic || g_felica) g_last_card=GetTickCount();
    LeaveCriticalSection(&g_lock);
}

static void parse_bytes(uint8_t *buf, size_t *used) {
    for(;;) {
        size_t i=0; while(i+1<*used && (buf[i]!=0xCA || buf[i+1]!=0x5A)) i++;
        if(i) { memmove(buf,buf+i,*used-i); *used-=i; }
        if(*used<6) return;
        uint16_t len=(uint16_t)buf[4]|((uint16_t)buf[5]<<8);
        if(buf[2]!=1 || len>512) { memmove(buf,buf+1,*used-1); --*used; continue; }
        size_t total=(size_t)len+8; if(*used<total) return;
        uint16_t expected=(uint16_t)buf[total-2]|((uint16_t)buf[total-1]<<8);
        if(crc16(buf+2,4+len)!=expected) {memmove(buf,buf+1,*used-1);--*used;continue;}
        accept_frame(buf,total); memmove(buf,buf+total,*used-total); *used-=total;
    }
}

static bool open_cdc_device(void) {
    HDEVINFO info=SetupDiGetClassDevsA(NULL,"USB",NULL,DIGCF_PRESENT|DIGCF_ALLCLASSES);
    if(info==INVALID_HANDLE_VALUE) return false;
    SP_DEVINFO_DATA d={.cbSize=sizeof(d)}; char hw[2048];
    bool found=false;
    for(DWORD i=0;SetupDiEnumDeviceInfo(info,i,&d);i++) {
        if(!SetupDiGetDeviceRegistryPropertyA(info,&d,SPDRP_HARDWAREID,NULL,(BYTE*)hw,sizeof(hw),NULL)) continue;
        if(!strstr(hw,"VID_1CCF&PID_5252")) continue;
        HKEY k=SetupDiOpenDevRegKey(info,&d,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_READ); if(k==INVALID_HANDLE_VALUE) continue;
        char name[64]; DWORD type=0,n=sizeof(name); LONG rc=RegQueryValueExA(k,"PortName",NULL,&type,(BYTE*)name,&n); RegCloseKey(k);
        if(rc!=ERROR_SUCCESS || type!=REG_SZ) continue;
        char path[80]; snprintf(path,sizeof(path),"\\\\.\\%s",name);
        HANDLE h=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
        if(h==INVALID_HANDLE_VALUE) continue;
        DCB c={.DCBlength=sizeof(c)};
        if(!GetCommState(h,&c)) {CloseHandle(h);continue;}
        c.BaudRate=115200;c.ByteSize=8;c.Parity=NOPARITY;c.StopBits=ONESTOPBIT;c.fBinary=TRUE;c.fParity=FALSE;c.fOutxCtsFlow=FALSE;c.fOutxDsrFlow=FALSE;c.fOutX=FALSE;c.fInX=FALSE;c.fDtrControl=DTR_CONTROL_ENABLE;c.fRtsControl=RTS_CONTROL_DISABLE;
        COMMTIMEOUTS t={.ReadIntervalTimeout=50,.ReadTotalTimeoutConstant=50,.ReadTotalTimeoutMultiplier=0};
        if(!SetCommState(h,&c) || !SetCommTimeouts(h,&t)) {CloseHandle(h);continue;}
        g_port=h; found=true; break;
    }
    SetupDiDestroyDeviceInfoList(info); return found;
}

static bool hid_command(uint8_t msg) {
    uint8_t f[12]={0xCA,0x5A,1,msg,4,0};
    for(unsigned i=0;i<4;++i)f[6+i]=(uint8_t)(g_hid.session>>(8*i));
    uint16_t crc=crc16(f+2,8);f[10]=(uint8_t)crc;f[11]=(uint8_t)(crc>>8);
    return skpass_hid_send(&g_hid,f,sizeof(f),250)!=0;
}
static void disconnect_device(void) {
    if(g_hid.handle){hid_command(SKPASS_HID_CLOSE);skpass_hid_close(&g_hid);}
    if(g_port!=INVALID_HANDLE_VALUE){CloseHandle(g_port);g_port=INVALID_HANDLE_VALUE;}
    memset(&g_hid_assembly,0,sizeof(g_hid_assembly));
    g_hid_ack=false;g_confirmed=false;g_last_heartbeat=0;clear_card();
}
static bool open_device(void) {
    int result=skpass_hid_open(&g_hid,NULL,NULL);
    if(result==1) {
        g_hid_ack=false;g_confirmed=false;g_last_heartbeat=GetTickCount();
        if(!hid_command(SKPASS_HID_HELLO)){disconnect_device();return false;}
        g_hid_hello=GetTickCount();
        OutputDebugStringA("SKPass AimeIO: opened data HID; awaiting firmware handshake\n");
        return true;
    }
    if(result<0)return false; // Busy HID must never be bypassed via CDC.
    if(!open_cdc_device())return false;
    g_confirmed=false;g_last_heartbeat=GetTickCount();
    OutputDebugStringA("SKPass AimeIO: legacy CDC fallback\n");return true;
}
static DWORD WINAPI reader_thread(void *arg) {
    (void)arg; uint8_t b[1024], frame[FRAME_MAX]; size_t used=0;DWORD probe=0;
    while(WaitForSingleObject(g_stop,10)==WAIT_TIMEOUT) {
        DWORD now=GetTickCount();
        if(!g_hid.handle && g_port==INVALID_HANDLE_VALUE) {
            if(!probe || now-probe>=1000){probe=now;open_device();}
        }
        bool failed=false;
        if(g_hid.handle) {
            for(unsigned i=0;i<32;++i) {
                uint8_t report[64];int result=skpass_hid_read(&g_hid,report);
                if(result<0){failed=true;break;}if(!result)break;
                if(skpass_hid_receive(&g_hid_assembly,report,64)==1)
                    accept_frame(g_hid_assembly.data,g_hid_assembly.size);
            }
            if(!failed && now-g_hid_hello>=1000){failed=!hid_command(SKPASS_HID_HELLO);g_hid_hello=now;}
        } else if(g_port!=INVALID_HANDLE_VALUE) {
            DWORD got=0;failed=!ReadFile(g_port,b,sizeof(b),&got,NULL);
            for(DWORD i=0;i<got && !failed;i++) {
                if(used==sizeof(frame))used=0;frame[used++]=b[i];parse_bytes(frame,&used);
            }
        }
        if((g_hid.handle || g_port!=INVALID_HANDLE_VALUE) && GetTickCount()-g_last_heartbeat>5000)failed=true;
        if(failed){disconnect_device();used=0;}
        EnterCriticalSection(&g_lock);
        if((g_classic||g_felica) && GetTickCount()-g_last_card>5000){g_classic=false;g_felica=false;}
        LeaveCriticalSection(&g_lock);
    }
    disconnect_device();return 0;
}

__declspec(dllexport) uint16_t aime_io_get_api_version(void) { return 0x0100; }
__declspec(dllexport) HRESULT aime_io_init(void) {
    if(g_initialized) return S_OK;
    /* AimeIO 1.0 has no shutdown callback. Keep code mapped until process exit
       so an unloaded provider cannot leave a thread executing freed code. */
    HMODULE module;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCSTR)&aime_io_init,&module)) return E_FAIL;
    wchar_t dll_path[32768];DWORD path_len=GetModuleFileNameW(module,dll_path,32768);
    if(!path_len || path_len>=32768)return E_FAIL;
    wchar_t *slash=wcsrchr(dll_path,L'\\');if(slash)slash[1]=0;
    wcscpy(g_dll_dir,dll_path);
    wchar_t configured[32768],ini[32768];swprintf(ini,32768,L"%lssegatools.ini",dll_path);
    GetPrivateProfileStringW(L"aimeio",L"cardsPath",L"",configured,32768,ini);
    if(!configured[0])GetPrivateProfileStringW(L"aimeio",L"cardsPath",L"",configured,32768,L".\\segatools.ini");
    if(configured[0]) {
        if(configured[0]==L'\\' || (wcslen(configured)>1 && configured[1]==L':'))swprintf(g_json_path,32768,L"%ls",configured);
        else swprintf(g_json_path,32768,L"%ls%ls",dll_path,configured);
    }else swprintf(g_json_path,32768,L"%lscards.json",dll_path);
    InitializeCriticalSection(&g_lock); g_stop=CreateEventA(NULL,TRUE,FALSE,NULL); if(!g_stop) {DeleteCriticalSection(&g_lock);return E_FAIL;}
    g_thread=CreateThread(NULL,0,reader_thread,NULL,0,NULL); if(!g_thread) {CloseHandle(g_stop);DeleteCriticalSection(&g_lock);return E_FAIL;} g_initialized=true; return S_OK;
}
static void select_card(bool pressed) {
    if(!pressed || !g_virtual_valid)g_held=false;
    else if(!g_held){memcpy(g_held_code,g_virtual_code,10);g_held=true;}
    bool fresh=GetTickCount()-g_last_card<=5000;
    g_selected_classic=g_classic && fresh;g_selected_felica=g_felica && fresh;
    memcpy(g_selected_code,g_aime,10);memcpy(g_selected_idm,g_idm,8);
    if(!g_selected_classic && !g_selected_felica && g_held) {g_selected_classic=true;memcpy(g_selected_code,g_held_code,10);}
}
__declspec(dllexport) HRESULT aime_io_nfc_poll(uint8_t unit_no) {
    if(unit_no)return E_INVALIDARG;if(!g_initialized)return E_FAIL;
    EnterCriticalSection(&g_lock);
    reload_virtual();
    select_card(foreground_allowed() && (GetAsyncKeyState((int)g_key)&0x8000)!=0);
    LeaveCriticalSection(&g_lock);return S_OK;
}
__declspec(dllexport) HRESULT aime_io_nfc_get_aime_id(uint8_t unit_no,uint8_t *luid,size_t n) {
    if(unit_no || !luid || n!=10) return E_INVALIDARG; if(!g_initialized)return S_FALSE; EnterCriticalSection(&g_lock); bool ok=g_selected_classic; if(ok) memcpy(luid,g_selected_code,10); LeaveCriticalSection(&g_lock); return ok?S_OK:S_FALSE;
}
__declspec(dllexport) HRESULT aime_io_nfc_get_felica_id(uint8_t unit_no,uint64_t *idm) {
    if(unit_no || !idm) return E_INVALIDARG; if(!g_initialized)return S_FALSE; EnterCriticalSection(&g_lock); bool ok=g_selected_felica; uint64_t v=0; for(int i=0;i<8;i++)v=(v<<8)|g_selected_idm[i]; if(ok)*idm=v; LeaveCriticalSection(&g_lock); return ok?S_OK:S_FALSE;
}
__declspec(dllexport) void aime_io_led_set_color(uint8_t unit_no,uint8_t r,uint8_t g,uint8_t b) {(void)unit_no;(void)r;(void)g;(void)b;}

/* This game's hook binds an additional optional 32-byte MIFARE data API.
   Classic identity is provided by get_aime_id; no raw block is fabricated. */
__declspec(dllexport) HRESULT aime_io_nfc_get_mifare_block(uint8_t unit_no,uint8_t *block,size_t size) {
    if(unit_no || !block || size!=32)return E_INVALIDARG;
    return S_FALSE;
}
