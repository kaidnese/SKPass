#include "FirmwareImage.h"
#include <QCoreApplication>
#include <QFile>
#include <iostream>
#include <cstdlib>
static void check(bool ok,const char *name){if(!ok){std::cerr<<name<<std::endl;std::exit(1);}}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    QFile file(QString::fromLocal8Bit(argv[1]));check(file.open(QIODevice::ReadOnly),"fixture open");
    auto bin=file.readAll();QString error;
    auto uf2=FirmwareImage::prepare(bin,false,&error);check(!uf2.isEmpty(),"built S3 application accepted");
    check(FirmwareImage::prepare(uf2,true,&error)==uf2,"UF2 roundtrip");
    auto bad=bin;bad[40]^=1;check(FirmwareImage::prepare(bad,false,&error).isEmpty(),"corrupt hash rejected");
    bad=bin;bad[12]=0;check(FirmwareImage::prepare(bad,false,&error).isEmpty(),"wrong chip rejected");
    bad=uf2;bad[28]=0;check(FirmwareImage::prepare(bad,true,&error).isEmpty(),"wrong family rejected");
    bad=uf2;bad.replace(512,512,uf2.first(512));check(FirmwareImage::prepare(bad,true,&error).isEmpty(),"duplicate rejected");
    check(FirmwareImage::prepare(uf2.first(uf2.size()-512),true,&error).isEmpty(),"missing block rejected");
    bad=uf2;bad.replace(0,512,uf2.mid(512,512));bad.replace(512,512,uf2.first(512));
    check(FirmwareImage::prepare(bad,true,&error)==bad,"out of order accepted");
    std::cout<<"Firmware image validation passed\n";
}
