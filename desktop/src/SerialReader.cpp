#include "SerialReader.h"
#include <QSerialPortInfo>
#include <QJsonDocument>
#include <QRegularExpression>
extern "C" {
#include "ce_protocol.h"
}
SerialReader::SerialReader(QObject *parent):QObject(parent) {
    hidPoll.setInterval(10);
    connect(&hidPoll,&QTimer::timeout,this,&SerialReader::pollHid);
    hidPoll.start();
    connect(&port,&QSerialPort::readyRead,this,[this]{parser.feed(port.readAll());});
    connect(&parser,&FrameParser::frame,this,[this](quint8 msg,QByteArray p,QByteArray raw){
        if(msg==SKPASS_HID_HELLO_REPLY && hid.handle) {
            QByteArray expected(1,char(1));
            for(unsigned i=0;i<4;++i)expected.append(char(hid.session>>(8*i)));
            hidAck=p==expected;
            return;
        }
        if(msg==1 && (p.startsWith("SKPass-") || p.startsWith("pn7150_cardio"))) {
            if(hid.handle && !hidAck)return;
            if(!confirmed)emit state(QStringLiteral("已确认 SKPass ")+(hid.handle?"HID":"CDC")+QStringLiteral("：")+QString::fromLatin1(p));
            confirmed=true;
            heartbeat.restart();
            const bool changed=firmware!=p;
            firmware=p;
            if(changed) {
                const bool interrupted=infoRequested;infoRequested=false;infoTimeout.stop();
                deviceInfo={};emit deviceInfoChanged(deviceInfo);
                if(interrupted)emit deviceInfoFailed(QStringLiteral("设备固件身份已变化，请重新查询。"));
            }
            if(changed)emit connectionChanged(ConnectionState::Connected,
                connectionLabel+QStringLiteral(" · 已确认 SKPass 读卡器"));
            emit availabilityChanged(canWrite());
        }
        if(msg==2 || msg==0xB1) {
            // Ignore unsolicited reports from firmware 2.3.x; only a user's
            // explicit query may publish information in the update page.
            if(!infoRequested || !confirmed || !heartbeat.isValid() || heartbeat.elapsed()>5000)return;
            const auto doc=QJsonDocument::fromJson(p);
            const auto info=doc.object();
            const auto hash=info.value("app_sha256").toString();
            if(!doc.isObject() || info.value("schema").toInt()!=1 ||
               info.value("firmware").toString()!=QString::fromLatin1(firmware) ||
               info.value("usb_state").toString()!="connected" ||
               info.value("version").toString().isEmpty() ||
               info.value("built").toString().isEmpty() ||
               // ESP-IDF 5.2's default returns a nine-character prefix even
               // into a 65-byte buffer. Accept legacy prefixes as identifiers;
               // firmware 2.3.1 sends all 64 characters from its descriptor.
               !QRegularExpression("^[0-9a-fA-F]{8,64}$").match(hash).hasMatch()) {
                emit state(QStringLiteral("忽略无效或与心跳不匹配的设备信息"));return;
            }
            infoTimeout.stop();infoRequested=false;
            deviceInfo=info;emit deviceInfoChanged(info);
            return;
        }
        if(confirmed)emit frame(msg,p,raw);
    });
    connect(&parser,&FrameParser::rejected,this,&SerialReader::badFramesChanged);
    connect(&port,&QSerialPort::errorOccurred,this,[this](QSerialPort::SerialPortError e){
        if(e==QSerialPort::ResourceError || e==QSerialPort::DeviceNotFoundError) {
            closeTransport();parser.reset();clearDevice();
            emit connectionChanged(reconnect?ConnectionState::Connecting:ConnectionState::Disconnected,
                reconnect?QStringLiteral("串口连接中断，正在等待设备重新出现"):QStringLiteral("串口连接已关闭"));
            emit state(QStringLiteral("串口断开，等待自动重连"));
        }
    });
    connect(&retry,&QTimer::timeout,this,&SerialReader::checkConnection);
    infoTimeout.setSingleShot(true);infoTimeout.setInterval(3000);
    connect(&infoTimeout,&QTimer::timeout,this,[this]{
        infoRequested=false;
        emit deviceInfoFailed(QStringLiteral("设备信息查询超时；当前固件可能不支持查询，请更新固件后重试。"));
    });
    retry.start(1000);
}
SerialReader::~SerialReader() { reconnect=false;closeTransport(); }
void SerialReader::closeTransport() {
    if(hid.handle) { sendHidCommand(SKPASS_HID_CLOSE);skpass_hid_close(&hid); }
    port.close();hidAssembly={};hidAck=false;hidHello.invalidate();
}
bool SerialReader::sendRaw(const QByteArray &raw) {
    if(hid.handle)return skpass_hid_send(&hid,reinterpret_cast<const uint8_t *>(raw.constData()),size_t(raw.size()),250)!=0;
    return port.isOpen() && port.write(raw)==raw.size();
}
bool SerialReader::sendHidCommand(quint8 msg) {
    QByteArray raw=QByteArray::fromHex("CA5A01");raw.append(char(msg));raw.append(char(4));raw.append(char(0));
    for(unsigned i=0;i<4;++i)raw.append(char(hid.session>>(8*i)));
    const auto crc=FrameParser::crc16(raw.mid(2));raw.append(char(crc));raw.append(char(crc>>8));
    return sendRaw(raw);
}
void SerialReader::pollHid() {
    if(!hid.handle)return;
    bool failed=false;
    for(int i=0;i<32;++i) {
        uint8_t report[64];int result=skpass_hid_read(&hid,report);
        if(result<0){failed=true;break;}
        if(!result)break;
        result=skpass_hid_receive(&hidAssembly,report,64);
        if(result<0){parser.reset();continue;}
        if(result==1)parser.feed(QByteArray(reinterpret_cast<const char *>(hidAssembly.data),int(hidAssembly.size)));
        if(!hid.handle)return; // a frame may have entered firmware update mode
    }
    if(!failed && (!hidHello.isValid() || hidHello.elapsed()>=1000)) {
        failed=!sendHidCommand(SKPASS_HID_HELLO);hidHello.restart();
    }
    if(failed) {
        closeTransport();parser.reset();clearDevice();
        emit connectionChanged(reconnect?ConnectionState::Connecting:ConnectionState::Disconnected,
            QStringLiteral("USB HID 已断开；等待读卡器重新连接"));
        emit state(QStringLiteral("SKPass HID 连接中断，等待自动重连"));
    }
}
void SerialReader::clearDevice() {
    infoTimeout.stop();
    const bool interrupted=infoRequested;infoRequested=false;
    confirmed=false;firmware.clear();deviceInfo={};heartbeat.invalidate();
    ceBlocked=false;
    emit deviceInfoChanged(deviceInfo);emit availabilityChanged(false);
    if(interrupted)emit deviceInfoFailed(QStringLiteral("连接已中断，设备信息查询取消。"));
}
void SerialReader::checkConnection() {
    // A port can remain open when the application/USB stops responding.
    // Only fresh valid heartbeats keep it confirmed; NFC failures do not.
    if(confirmed && heartbeat.isValid() && heartbeat.elapsed()>5000) {
        closeTransport();parser.reset();clearDevice();
        emit connectionChanged(reconnect?ConnectionState::Connecting:ConnectionState::Disconnected,
            QStringLiteral("设备心跳超时，连接已失效")+(reconnect?QStringLiteral("；正在等待重连"):QString()));
        emit state(QStringLiteral("超过 5 秒未收到 SKPass 心跳，已清除设备版本和连接状态"));
        return;
    }
    if(!reconnect)return;
    if(!transportOpen())attempt();
    else if(!confirmed && opened.elapsed()>5000) {
        closeTransport();parser.reset();clearDevice();
        emit connectionChanged(ConnectionState::Connecting,QStringLiteral("未收到 SKPass 心跳，请检查设备和固件；正在重试连接"));
        emit state(QStringLiteral("未收到 SKPass 心跳，请确认固件、USB 连接和设备占用情况"));
    }
}
void SerialReader::open(const QString &name) {
    stop();desired=name;reconnect=true;serialNumber.clear();
    for(const auto &p:QSerialPortInfo::availablePorts())if(p.portName()==name)serialNumber=p.serialNumber();
    attempt();
}
void SerialReader::stop() {
    reconnect=false;closeTransport();parser.reset();clearDevice();
    emit connectionChanged(ConnectionState::Disconnected,QStringLiteral("设备连接已关闭；游戏可接管读卡器"));
}
void SerialReader::waitForFirmwareReconnect() {
    stop();desired="auto";serialNumber.clear();reconnect=true;
    emit connectionChanged(ConnectionState::Connecting,QStringLiteral("等待 SKPass USB 设备自动重连"));
    // Use the existing retry timer; do not attempt I/O while MSC is finishing.
}
bool SerialReader::sendWrite(const QByteArray &payload,QString *error) {
    if(!canWrite() || payload.size()!=32){if(error)*error=QStringLiteral("需要支持写卡协议的 write 固件及有效写卡请求");return false;}
    QByteArray raw=QByteArray::fromHex("CA5A0120");raw.append(char(payload.size()));raw.append(char(0));raw+=payload;
    quint16 crc=FrameParser::crc16(raw.mid(2));raw.append(char(crc));raw.append(char(crc>>8));
    if(!sendRaw(raw)) {
        if(error)*error=QStringLiteral("发送未完成，结果未知；不会自动重发，请重新读卡核对");
        stop();return false;
    }
    return true; /* queued to OS, not proof that the card was written */
}
bool SerialReader::canAdvanced() const {
    return isConnected() && !ceBlocked && firmware.contains("-adv-");
}
bool SerialReader::sendCardEmulation(const QByteArray &payload,QString *error){
    ce_request_t request={};
    if(!canCardEmulation() || !CE_Decode_Request(reinterpret_cast<const uint8_t *>(payload.constData()),payload.size(),&request)){
        if(error)*error=QStringLiteral("请连接支持卡模拟的固件（-ce-），并检查配置");return false;
    }
    QByteArray raw=QByteArray::fromHex("CA5A0123");raw.append(char(payload.size()));raw.append(char(payload.size()>>8));raw+=payload;
    quint16 crc=FrameParser::crc16(raw.mid(2));raw.append(char(crc));raw.append(char(crc>>8));
    if(!sendRaw(raw)){if(error)*error=QStringLiteral("卡模拟请求发送失败，请查询设备模式");return false;}return true;
}
bool SerialReader::sendAdvanced(const QByteArray &payload,QString *error){
    if(!canAdvanced() || payload.size()<36 || payload.size()>292){if(error)*error=QStringLiteral("请连接支持高级读写的固件（带 -adv- 标识）");return false;}
    QByteArray raw=QByteArray::fromHex("CA5A0122");raw.append(char(payload.size()));raw.append(char(payload.size()>>8));raw+=payload;
    quint16 crc=FrameParser::crc16(raw.mid(2));raw.append(char(crc));raw.append(char(crc>>8));
    if(!sendRaw(raw)){if(error)*error=QStringLiteral("高级请求发送失败，状态未知；不会自动重发");stop();return false;}return true;
}
bool SerialReader::enterFirmwareUpdate(QString *error) {
    if(!canUpdate()) {
        if(error)*error=QStringLiteral("请先连接支持 UF2 的 2.2 或更新固件；旧固件需先通过 ESP-IDF 完整烧录。 ");
        return false;
    }
    QByteArray raw=QByteArray::fromHex("CA5A0130");
    raw.append(char(0)); raw.append(char(0));
    quint16 crc=FrameParser::crc16(raw.mid(2));
    raw.append(char(crc)); raw.append(char(crc>>8));
    if(!sendRaw(raw)) {
        if(error)*error=QStringLiteral("进入固件更新模式命令发送失败");
        return false;
    }
    if(port.isOpen())port.flush();
    // Keep reconnect available on rejection; acceptance stops the reader transport.
    return true;
}
bool SerialReader::requestDeviceInfo(QString *error) {
    if(!isConnected()) {
        if(error)*error=QStringLiteral("请先连接 SKPass 固件");return false;
    }
    if(infoRequested) {
        if(error)*error=QStringLiteral("正在查询设备信息，请稍候");return false;
    }
    QByteArray raw=QByteArray::fromHex("CA5A0131");raw.append(char(0));raw.append(char(0));
    quint16 crc=FrameParser::crc16(raw.mid(2));raw.append(char(crc));raw.append(char(crc>>8));
    if(!sendRaw(raw)) {
        if(error)*error=QStringLiteral("设备信息查询命令发送失败");return false;
    }
    infoRequested=true;infoTimeout.start();if(port.isOpen())port.flush();return true;
}
bool SerialReader::enterRomDownload(bool uart,QString *error) {
    if(!canRomDownload()) {
        if(error)*error=QStringLiteral("软件进入 ROM 下载需要 2.7.0 或更新固件；旧固件可先用 UF2 更新，或按 BOOT + RESET 手动进入。 ");
        return false;
    }
    QByteArray raw=QByteArray::fromHex("CA5A01340100");raw.append(char(uart?1:0));
    const auto crc=FrameParser::crc16(raw.mid(2));raw.append(char(crc));raw.append(char(crc>>8));
    if(!sendRaw(raw)) {if(error)*error=QStringLiteral("进入 ROM 下载命令发送失败");return false;}
    if(port.isOpen())port.flush();
    return true;
}
void SerialReader::attempt() {
    QString candidate=desired;
    if(desired=="auto" || desired.startsWith("hid:")) {
        const QString wanted=desired.startsWith("hid:")?desired.mid(4):QString();
        wchar_t selected[128]={};
        const int result=skpass_hid_open(&hid,wanted.isEmpty()?nullptr:reinterpret_cast<const wchar_t *>(wanted.utf16()),selected);
        if(result==1) {
            hidAssembly={};hidAck=false;connectionLabel=QStringLiteral("USB HID · ")+QString::fromWCharArray(selected);
            clearDevice();opened.restart();
            if(!sendHidCommand(SKPASS_HID_HELLO)){closeTransport();return;}
            hidHello.restart();
            emit state(connectionLabel+QStringLiteral(" 已打开，等待固件确认"));
            emit connectionChanged(ConnectionState::AwaitingHeartbeat,connectionLabel+QStringLiteral(" · 正在确认设备"));
            return;
        }
        if(result<0) {
            const auto text=QStringLiteral("SKPass HID 被游戏或其他程序占用，请断开另一程序后重试");
            emit connectionChanged(ConnectionState::Connecting,text);emit state(text);return;
        }
        if(desired.startsWith("hid:")) {
            emit connectionChanged(ConnectionState::Connecting,QStringLiteral("等待 SKPass USB HID 设备插入"));return;
        }
        // Only old firmware without data HID may use automatic CDC fallback.
        candidate.clear();
        for(const auto &p:QSerialPortInfo::availablePorts())
            if(p.hasVendorIdentifier() && p.hasProductIdentifier() && p.vendorIdentifier()==0x1CCF && p.productIdentifier()==0x5252) {candidate=p.portName();break;}
        if(candidate.isEmpty()) {
            emit connectionChanged(ConnectionState::Connecting,QStringLiteral("等待 SKPass USB 读卡器插入（自动识别，无需选择 COM）"));return;
        }
    }
    if(!serialNumber.isEmpty()) {
        for(const auto &p:QSerialPortInfo::availablePorts()) {
            if(p.serialNumber()==serialNumber && p.hasVendorIdentifier() && p.hasProductIdentifier() &&
               p.vendorIdentifier()==0x1CCF && p.productIdentifier()==0x5252) {candidate=p.portName();break;}
        }
    }
    port.setPortName(candidate);port.setBaudRate(115200);
    connectionLabel=candidate+QStringLiteral(" · CDC 兼容模式");
    emit connectionChanged(ConnectionState::Connecting,QStringLiteral("正在连接 ")+candidate);
    port.setDataBits(QSerialPort::Data8);port.setParity(QSerialPort::NoParity);
    port.setStopBits(QSerialPort::OneStop);port.setFlowControl(QSerialPort::NoFlowControl);
    if(!port.open(QIODevice::ReadWrite)) {
        const QString text=QStringLiteral("等待端口：")+candidate+" · "+port.errorString();
        emit connectionChanged(ConnectionState::Connecting,text);emit state(text);return;
    }
    port.setDataTerminalReady(true); // tud_cdc_connected requires host DTR.
    clearDevice();opened.restart();emit state(QStringLiteral("已连接，等待心跳：")+candidate);
    emit connectionChanged(ConnectionState::AwaitingHeartbeat,candidate+QStringLiteral(" 已打开，等待 SKPass 固件心跳确认"));
}
