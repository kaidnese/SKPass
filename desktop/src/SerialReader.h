#pragma once
#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include "FrameParser.h"
#include "windows_hid.h"
class SerialReader : public QObject {
    Q_OBJECT
public:
    enum class ConnectionState { Disconnected, Connecting, AwaitingHeartbeat, Connected };
    Q_ENUM(ConnectionState)
    explicit SerialReader(QObject *parent=nullptr);
    ~SerialReader() override;
    void open(const QString &name);
    void stop();
    void waitForFirmwareReconnect();
    // Firmware identifiers may append feature/build tags after -write
    // (for example: ...-write-retry-rom-adv).  Accept the write capability
    // marker wherever it appears as a dash-delimited feature tag.
    bool canWrite() const {return confirmed && !ceBlocked &&
        (firmware.contains("-write-") || firmware.endsWith("-write")) && transportOpen();}
    bool canCardEmulation() const {return isConnected() && firmware.contains("-ce-");}
    bool cardEmulationBlocksReading() const {return ceBlocked;}
    bool sendCardEmulation(const QByteArray &,QString *error=nullptr);
    void setCardEmulationMode(int mode){ceBlocked=mode!=0;emit availabilityChanged(canWrite());}
    bool sendWrite(const QByteArray &payload,QString *error=nullptr);
    bool canAdvanced() const;
    bool sendAdvanced(const QByteArray &,QString *error=nullptr);
    bool enterFirmwareUpdate(QString *error=nullptr);
    bool enterRomDownload(bool uart,QString *error=nullptr);
    bool canRomDownload() const {return isConnected() && firmware.contains("-rom-");}
    bool requestDeviceInfo(QString *error=nullptr);
    bool isConnected() const {return confirmed && transportOpen();}
    bool canUpdate() const {return confirmed && transportOpen() && firmware.contains("-uf2-");}
    quint64 badFrames() const {return parser.badFrames();}
    QJsonObject deviceInformation() const {return deviceInfo;}
    QString firmwareId() const {return QString::fromLatin1(firmware);}
signals:
    void deviceInfoChanged(QJsonObject info);
    void deviceInfoFailed(QString error);
    void connectionChanged(SerialReader::ConnectionState state,QString detail);
    void frame(quint8 msg,QByteArray payload,QByteArray raw);
    void state(QString text);
    void badFramesChanged(quint64 total);
    void availabilityChanged(bool supportsWriting);
private:
    friend class SerialReaderTestAccess;
    QSerialPort port;
    FrameParser parser;
    QTimer retry;
    QTimer infoTimeout;
    QTimer hidPoll;
    skpass_hid_handle_t hid={};
    skpass_hid_assembly_t hidAssembly={};
    QElapsedTimer hidHello;
    QString connectionLabel;
    bool hidAck=false;
    QElapsedTimer opened;
    QElapsedTimer heartbeat;
    QString desired, serialNumber;
    QByteArray firmware;
    QJsonObject deviceInfo;
    bool reconnect=false, confirmed=false, infoRequested=false;
    bool ceBlocked=false;
    void attempt();
    void clearDevice();
    void checkConnection();
    bool transportOpen() const {return hid.handle || port.isOpen();}
    void closeTransport();
    bool sendRaw(const QByteArray &raw);
    bool sendHidCommand(quint8 msg);
    void pollHid();
};
