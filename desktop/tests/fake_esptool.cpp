// A file-backed downloader double. Never opens serial ports or real hardware.
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <cstdio>
static QByteArray read(const QString &p) {QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static bool save(const QString &p,const QByteArray &b) {QFile f(p);return f.open(QIODevice::WriteOnly) && f.write(b)==b.size();}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);auto args=app.arguments();QDir dir(qEnvironmentVariable("SKPASS_FAKE_FLASH_DIR"));
    if(!dir.exists())return 2;
    const QStringList operations={"flash_id","read_flash","write_flash","verify_flash"};
    int at=-1;for(int i=1;i<args.size();++i)if(operations.contains(args[i])){at=i;break;}
    if(at<0)return 3;
    const QString operation=args[at];
    // The real S3 ROM CDC became unusable after a stub process closed. Every
    // separately launched command in this workflow must keep ROM ownership.
    if(!args.contains("--no-stub")) {std::puts("Unexpected USB stub upload");return 9;}
    const int after=args.indexOf("--after");
    if(after<0 || args.value(after+1)!="no_reset") {std::puts("Unexpected automatic reset");return 10;}
    QFile log(dir.filePath("commands.txt"));if(!log.open(QIODevice::Append))return 4;
    log.write((args.mid(1).join(' ')+"\nTEMP="+qEnvironmentVariable("TEMP")+"\n").toUtf8());log.close();
    if(operation==qEnvironmentVariable("SKPASS_FAKE_FAIL")) {std::puts("Injected transport failure");return 1;}
    if(operation=="flash_id") {std::puts("Chip is ESP32-S3\nDetected flash size: 16MB");return 0;}
    if(operation=="read_flash") {
        const auto fixture=args[at+1]=="0x8000"?"partition.bin":"otadata.bin";
        return save(args[at+3],read(dir.filePath(fixture)))?0:5;
    }
    int first=at+1;while(first<args.size() && !args[first].startsWith("0x"))++first;
    for(int i=first;i+1<args.size();i+=2) {
        const auto address=args[i],file=args[i+1],store=dir.filePath("written-"+address+".bin");
        if(operation=="write_flash") {if(!save(store,read(file)))return 7;}
        else if(read(store)!=read(file))return 8;
    }
    std::puts("Hash of data verified.");return 0;
}
