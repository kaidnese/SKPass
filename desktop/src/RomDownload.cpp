#include "RomDownload.h"
#include "FirmwareImage.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSerialPortInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

namespace {
quint32 word(const QByteArray &b,int at) {return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+at));}
void put(QByteArray &b,int at,quint32 value) {qToLittleEndian(value,reinterpret_cast<uchar*>(b.data()+at));}
quint32 otaCrc(const QByteArray &bytes) {
    // esp_rom_crc32_le(UINT32_MAX, &ota_seq, 4): initial internal CRC=0.
    quint32 crc=0;
    for(uchar v:bytes) {crc^=v;for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xEDB88320u:0);}
    return ~crc;
}
QByteArray read(const QString &path) {QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
bool save(const QString &path,const QByteArray &bytes) {QFile f(path);return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size() && f.flush();}
QString artifactPath(const QString &name) {
    const QDir app(QCoreApplication::applicationDirPath());
    for(const auto &relative:{"tools/"+name,"../tools/"+name}) {
        const auto path=app.filePath(relative);if(QFileInfo::exists(path))return QFileInfo(path).absoluteFilePath();
    }
    return app.filePath("tools/"+name);
}
QString resolvePort(const RomDownload::Options &o,const RomDownload::Reporter &report,QString &error) {
    if(!o.port.isEmpty())return o.port;
    report(0,QStringLiteral("等待 ESP32-S3 原生 USB 下载串口重新枚举…"));
    QElapsedTimer waiting;waiting.start();
    do {
        auto ports=RomDownload::nativePorts();for(const auto &previous:o.previousNativePorts)ports.removeAll(previous);
        if(ports.size()>1){error=QStringLiteral("发现多个 Espressif USB 串口，请明确选择目标端口后重试。");return {};}
        if(ports.size()==1)return ports.first();
        QThread::msleep(250);
    }while(waiting.elapsed()<20000);
    error=QStringLiteral("20 秒内没有发现 ESP32-S3 下载串口。请检查设备管理器中的 COM 端口；ROM 的 DFU 接口未安装驱动不影响正常 COM 下载。也可使用 BOOT + RESET 手动进入后选择端口。");
    return {};
}
bool command(const QString &tool,const QString &port,const QString &work,const QStringList &args,
             int timeout,const RomDownload::Reporter &report,QString &error,QByteArray &output) {
    output.clear();QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);
    auto environment=QProcessEnvironment::systemEnvironment();
    // The standalone tool must extract beside this EXE, never in C:\\ TEMP.
    environment.insert("TEMP",work);environment.insert("TMP",work);environment.insert("TMPDIR",work);
    process.setProcessEnvironment(environment);process.setWorkingDirectory(work);
    QStringList all={"--chip","esp32s3","--port",port,"--baud","115200","--before","no_reset",
                     "--after","no_reset","--connect-attempts","3"};
    // Each operation opens and closes its own process/port. Keep every one
    // on the S3 ROM protocol: uploading a USB stub for flash_id can leave
    // the next process unable to configure the ROM CDC port after close.
    // ROM supports our read/write/MD5 commands; UART0 follows the same path.
    all<<"--no-stub";
    all<<args;process.start(tool,all);
    if(!process.waitForStarted(10000)){error=QStringLiteral("无法启动 esptool：")+process.errorString();return false;}
    QElapsedTimer elapsed;elapsed.start();
    while(!process.waitForFinished(200)) {
        output+=process.readAll();
        if(elapsed.elapsed()>timeout) {
#ifdef Q_OS_WIN
            QProcess killer;
            killer.start(QDir(qEnvironmentVariable("SystemRoot")).filePath("System32/taskkill.exe"),
                         {"/PID",QString::number(process.processId()),"/T","/F"});
            killer.waitForFinished(5000);
#endif
            process.kill();process.waitForFinished(3000);
            error=QStringLiteral("esptool 操作超时，固件写入／启动状态需核对。 ");return false;
        }
    }
    output+=process.readAll();
    report(-1,QString::fromLocal8Bit(output.right(12000)).trimmed());
    if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0) {
        error=QStringLiteral("esptool 失败：\n")+QString::fromLocal8Bit(output.right(5000));return false;
    }
    return true;
}
}
QString RomDownload::toolPath() {return artifactPath("esptool/esptool.exe");}
QString RomDownload::firstFlashPath() {return artifactPath("first-flash");}
bool RomDownload::isNativeUsb(quint16 vendor,quint16 product) {
    // ESP32-S3 ROM USB-OTG CDC+DFU = 0009; USB Serial/JTAG = 1001.
    // 0002 is ESP32-S2 and must not be selected for this S3 project.
    return vendor==0x303A && (product==0x0009 || product==0x1001);
}
QStringList RomDownload::nativePorts() {
    QStringList result;
    for(const auto &p:QSerialPortInfo::availablePorts())
        if(p.hasVendorIdentifier() && p.hasProductIdentifier() &&
           isNativeUsb(p.vendorIdentifier(),p.productIdentifier()))result<<p.portName();
    return result;
}
bool RomDownload::validLayout(const QByteArray &b,QString *error) {
    auto reject=[&]{if(error)*error=QStringLiteral("设备分区表不是 SKPass N16R8 双 OTA 布局；请使用首次烧录／修复模式。 ");return false;};
    if(b.size()<0xC00)return reject();
    bool ota0=false,ota1=false,data=false,md5=false;
    for(int at=0;at+32<=0xC00;at+=32) {
        const auto entry=b.mid(at,32);
        if(entry.startsWith(QByteArray::fromHex("EBEB"))) {
            if(entry.mid(16,16)!=QCryptographicHash::hash(b.first(at),QCryptographicHash::Md5))return reject();
            md5=true;break;
        }
        if(!entry.startsWith(QByteArray::fromHex("AA50")))return reject();
        const int type=uchar(entry[2]),subtype=uchar(entry[3]);
        const quint32 offset=word(entry,4),size=word(entry,8),flags=word(entry,28);
        if(flags || size==0 || quint64(offset)+size>0x1000000)return reject();
        if(type==0) {
            if(subtype==0x10 && offset==0x20000 && size==0x600000 && !ota0)ota0=true;
            else if(subtype==0x11 && offset==0x620000 && size==0x600000 && !ota1)ota1=true;
            else return reject();
        } else if(type==1 && subtype==0) {
            if(offset!=0xF000 || size!=0x2000 || data)return reject();data=true;
        } else if(type!=1 || !((subtype==2 && offset==0x9000 && size==0x6000) ||
                               (subtype==1 && offset==0x11000 && size==0x1000) ||
                               (subtype==0x82 && offset==0xC20000 && size==0x3E0000)))return reject();
    }
    return md5 && ota0 && ota1 && data?true:reject();
}
bool RomDownload::planUpdate(const QByteArray &table,const QByteArray &ota,Plan *plan,QString *error) {
    if(!validLayout(table,error))return false;
    auto reject=[&](const QString &s){if(error)*error=s;return false;};
    if(!plan || ota.size()!=8192)return reject(QStringLiteral("OTA 元数据不完整，停止更新。"));
    int active=-1;quint32 sequence=0;
    for(int i=0;i<2;++i) {
        const auto entry=ota.mid(i*4096,32);const auto seq=word(entry,0),state=word(entry,24);
        if(seq==0xFFFFFFFF || state==3 || state==4 || word(entry,28)!=otaCrc(entry.first(4)))continue;
        if(seq==0 || (state!=0 && state!=1 && state!=2 && state!=0xFFFFFFFF))
            return reject(QStringLiteral("OTA 状态异常，停止更新。"));
        if(active<0 || seq>sequence){active=i;sequence=seq;}
    }
    if(active<0 && ota!=QByteArray(8192,char(0xFF)))return reject(QStringLiteral("两份 OTA 元数据都无效，请使用首次烧录／修复。"));
    if(active>=0 && word(ota,active*4096+24)==1)return reject(QStringLiteral("当前固件仍在等待启动验证，请先完成验证后更新。"));
    if(sequence>=0xFFFFFFFD)return reject(QStringLiteral("OTA 序号即将溢出，请使用首次烧录／修复。"));
    const int oldSlot=active<0?0:int((sequence-1)%2);
    plan->address=oldSlot==0?0x620000:0x20000;
    plan->sequence=active<0?2:sequence+1;
    plan->metadataAddress=0xF000+(active==0?4096:0);
    plan->metadata=QByteArray(4096,char(0xFF));
    put(plan->metadata,0,plan->sequence);put(plan->metadata,24,2); // ESP_OTA_IMG_VALID
    put(plan->metadata,28,otaCrc(plan->metadata.first(4)));
    return true;
}
RomDownload::Result RomDownload::run(const Options &o,const Reporter &report) {
    Result result;
    auto fail=[&](const QString &s){result.error=s;return result;};
    if(!QFileInfo::exists(o.tool))return fail(QStringLiteral("未找到随附 esptool，请使用完整 SKPass 发行包。"));
    QFile source(o.firmware);
    if(!source.open(QIODevice::ReadOnly) || source.size()>FirmwareImage::MaxSize*2)return fail(QStringLiteral("无法读取固件或文件过大。"));
    QString error;
    const auto uf2=FirmwareImage::prepare(source.readAll(),o.firmware.endsWith(".uf2",Qt::CaseInsensitive),&error);
    if(uf2.isEmpty())return fail(error);
    QByteArray app(uf2.size()/2,char(0xFF));
    for(int at=0;at<uf2.size();at+=512)
        app.replace(int(word(uf2,at+20))*256,256,uf2.mid(at+32,256));
    const auto root=o.workRoot.isEmpty()?QDir(QCoreApplication::applicationDirPath()).filePath("rom-work"):o.workRoot;
    if(!QDir().mkpath(root))return fail(QStringLiteral("EXE 附近的 ROM 工作目录无法写入。"));
    QTemporaryDir work(QDir(root).filePath("update-XXXXXX"));
    if(!work.isValid())return fail(QStringLiteral("无法创建 ROM 工作目录。"));
    const QString image=work.filePath("application.bin");
    if(!save(image,app))return fail(QStringLiteral("无法准备应用镜像。"));
    const auto port=resolvePort(o,report,error);if(port.isEmpty())return fail(error);
    QByteArray output;
    auto execute=[&](const QStringList &args,int timeout=60000) {
        return command(o.tool,port,work.path(),args,timeout,report,error,output);
    };
    report(5,QStringLiteral("连接 %1 并检查芯片与 Flash 容量…").arg(port));
    if(!execute({"flash_id"}))return fail(error);
    if(!output.contains("Detected flash size: 16MB"))return fail(QStringLiteral("仅支持本工程的 ESP32-S3 N16R8（16MB Flash），设备容量不匹配。"));
    const QString partition=work.filePath("partition.bin"),ota=work.filePath("otadata.bin");
    QStringList writes;
    Plan plan;
    if(o.firstFlash) {
        const QDir files(o.firstFlashDirectory);
        const auto boot=read(files.filePath("bootloader.bin")),table=read(files.filePath("partition-table.bin")),initial=read(files.filePath("ota_data_initial.bin"));
        if(!FirmwareImage::validBootloader(boot,&error) || !validLayout(table,&error))return fail(error);
        if(initial!=QByteArray(8192,char(0xFF)))return fail(QStringLiteral("首次烧录配套 OTA 初始数据无效。"));
        // Copy immutable snapshots; source files cannot change halfway through.
        const QString bootFile=work.filePath("bootloader.bin");
        if(!save(bootFile,boot) || !save(partition,table) || !save(ota,initial))return fail(QStringLiteral("无法准备首次烧录配套文件。"));
        writes={"0x0",bootFile,"0x8000",partition,"0xf000",ota,"0x20000",image};
    } else {
        report(10,QStringLiteral("读取设备分区表和 OTA 状态；不会覆盖当前启动分区。"));
        if(!execute({"read_flash","0x8000","0x1000",partition}) ||
           !execute({"read_flash","0xf000","0x2000",ota}))return fail(error);
        if(!planUpdate(read(partition),read(ota),&plan,&error))return fail(error);
        writes={QString("0x%1").arg(plan.address,0,16),image};
    }
    report(20,QStringLiteral("写入应用固件，请保持供电，不要关闭上位机…"));
    QStringList write={"write_flash","--flash_mode","keep","--flash_freq","keep","--flash_size","keep"};write<<writes;
    // Up to 6 MiB at conservative UART0 115200 can take many minutes.
    const int transferTimeout=120000+int(app.size()/6000)*1000;
    if(!execute(write,transferTimeout))return fail(error);
    report(75,QStringLiteral("校验写入内容摘要…"));
    QStringList verify={"verify_flash"};verify<<writes;
    if(!execute(verify,transferTimeout))return fail(error);
    if(!o.firstFlash) {
        // Only select the new image after a successful digest verification. The other
        // metadata sector and the previous application remain untouched.
        const QString metadata=work.filePath("new-otadata.bin");
        if(!save(metadata,plan.metadata))return fail(QStringLiteral("无法生成 OTA 启动记录。"));
        const QString address=QString("0x%1").arg(plan.metadataAddress,0,16);
        if(!execute({"write_flash","--flash_size","keep",address,metadata}) ||
           !execute({"verify_flash",address,metadata}))return fail(error);
    }
    result.committed=true;
    report(100,QStringLiteral("串口烧录与校验完成。请重新插拔 USB。"));
    return result;
}
