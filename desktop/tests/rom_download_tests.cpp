#include "RomDownload.h"
#include "FirmwareImage.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);return 1;}} while(false)
static QByteArray read(const QString &p) {QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static bool save(const QString &p,const QByteArray &b) {QFile f(p);return f.open(QIODevice::WriteOnly) && f.write(b)==b.size();}
static quint32 word(const QByteArray &b,int at) {return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+at));}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);CHECK(argc==4);
    const QString bin=QString::fromLocal8Bit(argv[1]),first=QString::fromLocal8Bit(argv[2]),tool=QString::fromLocal8Bit(argv[3]);
    CHECK(RomDownload::isNativeUsb(0x303A,0x0009)); // actual ESP32-S3 ROM USB CDC
    CHECK(RomDownload::isNativeUsb(0x303A,0x1001));
    CHECK(!RomDownload::isNativeUsb(0x303A,0x0002)); // ESP32-S2
    CHECK(!RomDownload::isNativeUsb(0x1CCF,0x5252)); // normal reader, not ROM
    const auto table=read(QDir(first).filePath("partition-table.bin"));QString error;RomDownload::Plan plan;
    CHECK(RomDownload::validLayout(table,&error));
    QByteArray ota(8192,char(0xFF));CHECK(RomDownload::planUpdate(table,ota,&plan,&error));
    CHECK(plan.address==0x620000 && plan.metadataAddress==0xF000 && plan.sequence==2);
    CHECK(word(plan.metadata,0)==2 && word(plan.metadata,24)==2);
    // Independent zlib.crc32(LE32(2), 0xFFFFFFFF) / ESP-IDF ROM CRC vector.
    CHECK(word(plan.metadata,28)==0x55F63774);
    ota.replace(0,4096,plan.metadata);CHECK(RomDownload::planUpdate(table,ota,&plan,&error));
    CHECK(plan.address==0x20000 && plan.metadataAddress==0x10000 && plan.sequence==3);
    ota.replace(4096,4096,plan.metadata);CHECK(RomDownload::planUpdate(table,ota,&plan,&error));
    CHECK(plan.address==0x620000 && plan.metadataAddress==0xF000 && plan.sequence==4);
    auto bad=table;bad[10]^=1;CHECK(!RomDownload::validLayout(bad,&error));
    bad=ota;bad[4096+28]^=1;CHECK(RomDownload::planUpdate(table,bad,&plan,&error));CHECK(plan.sequence==3);
    bad=ota;bad[4096+24]=1;CHECK(!RomDownload::planUpdate(table,bad,&plan,&error));
    CHECK(!RomDownload::planUpdate(table,QByteArray(8192,0),&plan,&error));
    CHECK(!RomDownload::planUpdate(table,ota.first(4096),&plan,&error));
    CHECK(FirmwareImage::validBootloader(read(QDir(first).filePath("bootloader.bin")),&error));
    CHECK(!FirmwareImage::validBootloader(QByteArray(0x8001,0),&error));
    QTemporaryDir test;CHECK(test.isValid());qputenv("SKPASS_FAKE_FLASH_DIR",test.path().toUtf8());
    CHECK(save(test.filePath("partition.bin"),table));CHECK(save(test.filePath("otadata.bin"),QByteArray(8192,char(0xFF))));
    RomDownload::Options options;options.firmware=bin;options.port="FAKE_ONLY";options.tool=tool;
    options.firstFlashDirectory=first;options.workRoot=test.filePath("rom-work");
    auto report=[](int,const QString &){};
    auto result=RomDownload::run(options,report);CHECK(result.error.isEmpty() && result.committed);
    auto commands=read(test.filePath("commands.txt"));
    CHECK(commands.indexOf("write_flash")<commands.indexOf("verify_flash"));
    CHECK(commands.indexOf("verify_flash")<commands.indexOf("write_flash --flash_size keep 0xf000"));
    CHECK(commands.contains("--no-stub") && commands.contains("--after no_reset"));
    CHECK(!commands.contains("write_mem") && !commands.contains("watchdog_reset") && !commands.contains("hard_reset"));
    CHECK(!commands.contains("C:\\"));CHECK(!commands.contains("erase_flash"));
    for(const auto &failure:{"read_flash","write_flash","verify_flash"}) {
        CHECK(save(test.filePath("commands.txt"),{}));qputenv("SKPASS_FAKE_FAIL",failure);
        result=RomDownload::run(options,report);CHECK(!result.error.isEmpty() && !result.committed);
        CHECK(!read(test.filePath("commands.txt")).contains("write_mem"));
    }
    qunsetenv("SKPASS_FAKE_FAIL");CHECK(save(test.filePath("commands.txt"),{}));
    options.firstFlash=true;
    result=RomDownload::run(options,report);CHECK(result.error.isEmpty() && result.committed);
    commands=read(test.filePath("commands.txt"));CHECK(commands.contains("0x0") && commands.contains("0x8000") && commands.contains("0x20000"));
    CHECK(!commands.contains("read_flash") && !commands.contains("write_mem") && !commands.contains("watchdog_reset"));
    // Out-of-order UF2 is legal; ROM reconstruction must honor block numbers.
    auto uf2=FirmwareImage::prepare(read(bin),false,&error);CHECK(uf2.size()>512);
    auto block=uf2.first(512);uf2.replace(0,512,uf2.mid(512,512));uf2.replace(512,512,block);
    CHECK(save(test.filePath("out-of-order.uf2"),uf2));options.firmware=test.filePath("out-of-order.uf2");options.firstFlash=false;
    result=RomDownload::run(options,report);CHECK(result.error.isEmpty());
    CHECK(read(test.filePath("written-0x620000.bin")).startsWith(read(bin)));
    std::puts("ROM partition/OTA planning, commit ordering, failure isolation, first flash and UF2 reconstruction passed");
}
