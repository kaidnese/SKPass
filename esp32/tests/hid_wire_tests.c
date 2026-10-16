#include "hid_wire.h"
#include "reader_frame.h"
#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    uint8_t payload[512],frame[520],report[64];
    for(unsigned i=0;i<sizeof(payload);++i)payload[i]=(uint8_t)i;
    for(size_t size=0;size<=512;++size) {
        size_t n=reader_encode_frame(0x7F,payload,size,frame,sizeof(frame));
        skpass_hid_assembly_t a={0};
        for(size_t i=0;i<skpass_hid_fragments(n);++i) {
            assert(skpass_hid_fragment(frame,n,255,(uint8_t)i,report));
            int result=skpass_hid_receive(&a,report,64);
            assert(result==(i+1==skpass_hid_fragments(n)?1:0));
        }
        assert(a.size==n && !memcmp(a.data,frame,n));
    }
    assert(!skpass_hid_fragments(0) && !skpass_hid_fragments(521));
    skpass_hid_assembly_t a={0};
    skpass_hid_fragment(frame,520,1,0,report);assert(skpass_hid_receive(&a,report,64)==0);
    skpass_hid_fragment(frame,520,1,2,report);assert(skpass_hid_receive(&a,report,64)==-1); // missing fragment
    skpass_hid_fragment(frame,520,1,0,report);assert(skpass_hid_receive(&a,report,64)==0);
    skpass_hid_fragment(frame,520,2,1,report);assert(skpass_hid_receive(&a,report,64)==-1); // different frame
    skpass_hid_fragment(frame,520,3,0,report);assert(skpass_hid_receive(&a,report,64)==0);
    skpass_hid_fragment(frame,520,3,0,report);assert(skpass_hid_receive(&a,report,64)==0); // new start replaces old
    report[5]=57;assert(skpass_hid_receive(&a,report,64)==-1); // shortened non-final fragment
    for(unsigned field=0;field<6;++field) {
        skpass_hid_fragment(frame,520,1,0,report);
        report[field]=255;assert(skpass_hid_receive(&a,report,64)==(field==2?0:-1));
    }
    skpass_hid_fragment(frame,520,1,0,report);assert(skpass_hid_receive(&a,report,63)==-1);
    skpass_hid_fragment(frame,520,1,8,report);assert(!skpass_hid_fragment(frame,520,1,9,report));
    // After corrupt/lost traffic a clean new frame must recover.
    size_t n=reader_encode_frame(0x31,NULL,0,frame,sizeof(frame));
    assert(skpass_hid_fragment(frame,n,0,0,report));
    assert(skpass_hid_receive(&a,report,64)==1 && a.size==8 && !memcmp(a.data,frame,8));
    puts("HID: all payload sizes, missing/corrupt fragments, sequence changes and resynchronization passed");
    return 0;
}
