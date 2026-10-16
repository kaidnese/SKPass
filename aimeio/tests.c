#include "aimeio.c"
#undef NDEBUG
#include <assert.h>
static size_t make_frame(uint8_t msg,const uint8_t *payload,size_t n,uint8_t *out) {
    out[0]=0xCA;out[1]=0x5A;out[2]=1;out[3]=msg;out[4]=(uint8_t)n;out[5]=(uint8_t)(n>>8);
    if(n)memcpy(out+6,payload,n);
    uint16_t crc=crc16(out+2,n+4);out[n+6]=(uint8_t)crc;out[n+7]=(uint8_t)(crc>>8);return n+8;
}
int main(int argc,char **argv) {
    assert(game_directory_allowed(L"E:\\game\\sinmai.exe",L"E:\\game\\"));
    assert(game_directory_allowed(L"E:\\game\\unknown_game.exe",L"E:\\game"));
    assert(game_directory_allowed(L"E:\\GAME\\bin\\Game.exe",L"E:\\game\\"));
    assert(!game_directory_allowed(L"E:\\other\\sinmai.exe",L"E:\\game\\"));
    assert(!game_directory_allowed(L"E:\\game-other\\game.exe",L"E:\\game"));
    assert(!game_directory_allowed(L"E:\\game\\..\\other\\game.exe",L"E:\\game"));
    assert(!game_directory_allowed(L"",L"E:\\game"));
    assert(!game_directory_allowed(L"E:\\game\\game.exe",L""));
    InitializeCriticalSection(&g_lock);g_initialized=true;
    assert(crc16((const uint8_t*)"123456789",9)==0x29B1);
    uint8_t p[68]={0x10},f[100],b[600],out[10];size_t used=0;
    p[19]=48;for(int i=0;i<10;i++)p[58+i]=(uint8_t)(i*0x11);
    size_t n=make_frame(0x10,p,sizeof(p),f);
    memcpy(b,f,4);used=4;parse_bytes(b,&used);assert(!g_classic);
    memcpy(b+used,f+4,n-4);used+=n-4;parse_bytes(b,&used);
    aime_io_nfc_poll(0);assert(aime_io_nfc_get_aime_id(0,out,10)==S_OK && !memcmp(out,p+58,10));
    clear_card();f[n-1]^=1;accept_frame(f,n);assert(!g_classic);
    f[n-1]^=1;p[58]=0xFA;n=make_frame(0x10,p,sizeof(p),f);accept_frame(f,n);assert(!g_classic);
    uint8_t fp[20]={0x20,0x11,1,2,3,4,5,6,7,8};n=make_frame(0x10,fp,20,f);accept_frame(f,n);
    uint64_t idm;aime_io_nfc_poll(0);assert(aime_io_nfc_get_felica_id(0,&idm)==S_OK && idm==UINT64_C(0x0102030405060708));
    n=make_frame(0x11,NULL,0,f);accept_frame(f,n);aime_io_nfc_poll(0);assert(aime_io_nfc_get_felica_id(0,&idm)==S_FALSE);
    n=make_frame(0x10,fp,20,f);accept_frame(f,n);g_last_card=GetTickCount()-5001;aime_io_nfc_poll(0);assert(aime_io_nfc_get_felica_id(0,&idm)==S_FALSE);
    /* Independent fixed vector already used by the Qt decoder. */
    const uint8_t cipher[16]={0xd2,0x76,0x51,0x9c,0xb5,0x2c,0,0x8b,2,0x27,0x15,0x5e,0xd7,0xd2,0xbe,0xd9};
    const uint8_t expected[10]={1,3,0x56,0x40,0x26,0x80,0x54,9,0x87,7};
    uint8_t sf[36]={0x20,0,1,2,3,4,5,6,7,8};sf[18]=16;memcpy(sf+19,cipher,16);
    n=make_frame(0x10,sf,sizeof(sf),f);accept_frame(f,n);select_card(false);
    assert(aime_io_nfc_get_aime_id(0,out,10)==S_OK && !memcmp(out,expected,10));
    assert(aime_io_nfc_get_felica_id(0,&idm)==S_FALSE);
    memset(sf+19,0,16);n=make_frame(0x10,sf,sizeof(sf),f);accept_frame(f,n);select_card(false);
    assert(aime_io_nfc_get_aime_id(0,out,10)==S_FALSE);
    assert(aime_io_nfc_get_felica_id(0,&idm)==S_OK && idm==UINT64_C(0x0102030405060708));
    memcpy(sf+19,cipher,16);sf[1]=0x14;n=make_frame(0x10,sf,sizeof(sf),f);accept_frame(f,n);select_card(false);
    assert(aime_io_nfc_get_aime_id(0,out,10)==S_FALSE && aime_io_nfc_get_felica_id(0,&idm)==S_OK);
    n=make_frame(0x11,NULL,0,f);accept_frame(f,n);select_card(false);
    assert(aime_io_nfc_get_aime_id(0,out,10)==S_FALSE && aime_io_nfc_get_felica_id(0,&idm)==S_FALSE);
    /* A physical HID session requires its own nonce ACK and a heartbeat. */
    g_hid.handle=(HANDLE)1;g_hid.session=0x12345678;g_confirmed=false;g_hid_ack=false;
    sf[1]=0;memcpy(sf+19,cipher,16);n=make_frame(0x10,sf,sizeof(sf),f);accept_frame(f,n);
    assert(!g_classic && !g_felica);
    uint8_t ack[5]={1,0x78,0x56,0x34,0x13};n=make_frame(0xB2,ack,5,f);accept_frame(f,n);assert(!g_hid_ack);
    const char *identity="SKPass-2.6.0-retry-uf2-write";
    n=make_frame(1,(const uint8_t *)identity,strlen(identity),f);accept_frame(f,n);assert(!g_confirmed);
    ack[4]=0x12;n=make_frame(0xB2,ack,5,f);accept_frame(f,n);assert(g_hid_ack);
    n=make_frame(1,(const uint8_t *)identity,strlen(identity),f);accept_frame(f,n);assert(g_confirmed);
    n=make_frame(0x10,sf,sizeof(sf),f);
    skpass_hid_assembly_t assembly={0};uint8_t report[64];
    for(size_t i=0;i<skpass_hid_fragments(n);++i) {
        assert(skpass_hid_fragment(f,n,9,(uint8_t)i,report));
        if(skpass_hid_receive(&assembly,report,64)==1)accept_frame(assembly.data,assembly.size);
    }
    select_card(false);assert(aime_io_nfc_get_aime_id(0,out,10)==S_OK && !memcmp(out,expected,10));
    g_hid.handle=NULL;g_hid_ack=false;g_confirmed=false;clear_card();
    const char *json="{\"version\":1,\"activeCode\":\"01036261387195961452\",\"keyVk\":13,\"cards\":[{\"type\":\"classic\",\"accessCode\":\"01036261387195961452\"}]}";
    unsigned key=0;assert(cards_json_parse(json,strlen(json),out,&key) && key==13 && out[0]==1 && out[9]==0x52);
    assert(!cards_json_parse(json,strlen(json)-1,out,&key));
    const char *unlisted="{\"version\":1,\"activeCode\":\"01036261387195961452\",\"keyVk\":13,\"cards\":[]}";
    assert(!cards_json_parse(unlisted,strlen(unlisted),out,&key));
    clear_card();g_virtual_valid=true;memcpy(g_virtual_code,out,10);g_held=false;
    select_card(false);assert(aime_io_nfc_get_aime_id(0,out,10)==S_FALSE);
    select_card(true);assert(aime_io_nfc_get_aime_id(0,out,10)==S_OK);uint8_t held[10];memcpy(held,out,10);
    g_virtual_code[9]^=1;select_card(true);assert(aime_io_nfc_get_aime_id(0,out,10)==S_OK && !memcmp(out,held,10));
    select_card(false);assert(aime_io_nfc_get_aime_id(0,out,10)==S_FALSE);
    select_card(true);assert(aime_io_nfc_get_aime_id(0,out,10)==S_OK && memcmp(out,held,10));
    g_virtual_valid=false;select_card(true);assert(aime_io_nfc_get_aime_id(0,out,10)==S_FALSE);
    uint8_t raw_block[32];assert(aime_io_nfc_get_mifare_block(0,raw_block,32)==S_FALSE);
    assert(aime_io_nfc_get_mifare_block(0,raw_block,16)==E_INVALIDARG);
    if(argc>1){
        assert(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,g_json_path,32768)>0);
        g_json_check=0;reload_virtual();assert(g_virtual_valid);
        assert(g_key>0);select_card(false);select_card(true);
        assert(aime_io_nfc_get_aime_id(0,out,10)==S_OK && !memcmp(out,g_virtual_code,10));
        select_card(false);assert(aime_io_nfc_get_aime_id(0,out,10)==S_FALSE);
        printf("Actual JSON loaded: keyVk=%u; hold/read/release passed.\n",g_key);
    }
    DeleteCriticalSection(&g_lock);puts("CRC, fragmented frames, BCD, IDm, removal and expiry passed");return 0;
}
