#include "NfcDetails.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QDate>
#include <cmath>
#include <limits>

namespace {
bool integer(const QJsonObject &o, const char *key, double low, double high, bool required = false) {
    if (!o.contains(key)) return !required;
    const auto v = o.value(key);
    if (!v.isDouble()) return false;
    const double n = v.toDouble();
    return std::isfinite(n) && std::floor(n) == n && n >= low && n <= high;
}
bool string(const QJsonObject &o, const char *key, int max) {
    if (!o.contains(key)) return true;
    const auto v = o.value(key);
    if (!v.isString() || v.toString().toUtf8().size() > max) return false;
    for (const auto c : v.toString()) if (c.unicode() < 32 || c.unicode() == 127) return false;
    return true;
}
bool date(const QJsonObject &o, const char *key, bool time = false) {
    if (!o.contains(key)) return true;
    const auto text = o.value(key).toString();
    if (text.isEmpty()) return true;
    if (text.size() != 8 && !(time && text.size() == 14)) return false;
    if (!QRegularExpression("^[0-9]+$").match(text).hasMatch() ||
        !QDate::fromString(text.left(8), "yyyyMMdd").isValid()) return false;
    return text.size() == 8 || (text.mid(8,2).toInt() < 24 && text.mid(10,2).toInt() < 60 && text.mid(12,2).toInt() < 60);
}
}

void NfcDetails::reset() { scan=0; count=0; uid.clear(); activeUid.clear(); parts.clear(); complete=false; }
void NfcDetails::activated(const QByteArray &id) { activeUid = id.toHex().toUpper(); }
NfcDetails::Result NfcDetails::feed(const QByteArray &p, NfcDocument &doc, QString *error) {
    const auto reject = [&](const QString &message) { if(error)*error=message; return Result::Rejected; };
    if(error)error->clear();
    if(p.isEmpty() || p.size()>512)return reject(QStringLiteral("NFC 详情长度无效"));
    QJsonParseError parse;
    const auto parsed=QJsonDocument::fromJson(p,&parse);
    if(parse.error!=QJsonParseError::NoError || !parsed.isObject())return reject(QStringLiteral("NFC 详情 JSON 无效"));
    const auto o=parsed.object();
    if(!integer(o,"schema",1,1,true) || !integer(o,"scan",1,4294967295.0,true) ||
       !integer(o,"part",0,10,true) || !integer(o,"count",1,11,true) || !string(o,"uid",20) ||
       !o.contains("uid"))return reject(QStringLiteral("NFC 详情头字段无效"));
    const auto id=o.value("uid").toString().toLatin1();
    if(!QRegularExpression("^(?:[0-9A-F]{8}|[0-9A-F]{14}|[0-9A-F]{16}|[0-9A-F]{20})$").match(QString::fromLatin1(id)).hasMatch())
        return reject(QStringLiteral("NFC 详情 UID 无效"));
    const int part=o.value("part").toInt(), total=o.value("count").toInt();
    const auto next=quint32(o.value("scan").toDouble());
    if(part>=total)return reject(QStringLiteral("NFC 详情分片越界"));
    if(!activeUid.isEmpty() && id!=activeUid)return reject(QStringLiteral("忽略非当前卡片的 NFC 详情"));
    const double minimum=std::numeric_limits<qint32>::min(), maximum=std::numeric_limits<qint32>::max();
    if(!integer(o,"balance",minimum,maximum))return reject(QStringLiteral("NFC 余额字段无效"));
    if(part==0) {
        for(const auto key:{"kind","status","currency","warning","number","issuer","start","expiry","label","sw"})
            if(!string(o,key,64))return reject(QStringLiteral("NFC 文本字段无效"));
        if(o.value("kind").toString().isEmpty() || !o.contains("status") ||
           !QStringList{"ok","partial","unrecognized","rf_error"}.contains(o.value("status").toString()) ||
           !integer(o,"protocol",0,255,true) || !integer(o,"interface",0,255,true))return reject(QStringLiteral("NFC 类型/状态无效"));
        if(o.contains("currency") && !QStringList{"CNY","JPY","HKD"}.contains(o.value("currency").toString()))return reject(QStringLiteral("NFC 币种无效"));
        if(o.contains("number") && !QRegularExpression("^[0-9]{1,24}$").match(o.value("number").toString()).hasMatch())return reject(QStringLiteral("NFC 卡号字段无效"));
        if(!date(o,"start") || !date(o,"expiry"))return reject(QStringLiteral("NFC 日期无效"));
        if(o.contains("estimate") && !o.value("estimate").isBool())return reject(QStringLiteral("NFC 估算标记无效"));
        for(const auto key:{"blocks","block_size","sectors","pages","first_page","last_page"})
            if(!integer(o,key,0,65535))return reject(QStringLiteral("NFC 存储信息无效"));
        if(!integer(o,"raw_balance",0,4294967295.0))return reject(QStringLiteral("NFC 原始余额无效"));
    } else {
        if(!string(o,"date",14) || !date(o,"date",true) || !string(o,"raw",46) ||
           !integer(o,"type",0,255,true) || !integer(o,"entry",0,65535,true) ||
           !integer(o,"exit",0,65535,true) || !integer(o,"amount",minimum,maximum))return reject(QStringLiteral("NFC 交易记录无效"));
        if(o.contains("raw") && !QRegularExpression("^(?:[0-9A-F]{2}){1,23}$").match(o.value("raw").toString()).hasMatch())return reject(QStringLiteral("NFC 原始记录无效"));
    }
    if(next!=scan) {
        // RFC 1982 serial ordering also accepts uint32 wrap, never old scans.
        if(scan && (quint32(next-scan)>=0x80000000u))return reject(QStringLiteral("忽略过期 NFC 详情"));
        scan=next;count=total;uid=id;parts.clear();complete=false;
    } else if(id!=uid || total!=count)return reject(QStringLiteral("NFC 分片身份或数量不一致"));
    if(complete || parts.contains(part))return reject(QStringLiteral("忽略重复 NFC 分片"));
    parts.insert(part,o);
    if(parts.size()!=count)return Result::Pending;
    if(!parts.contains(0))return reject(QStringLiteral("NFC 详情缺少摘要"));
    doc.summary=parts.value(0);doc.records.clear();
    for(int x=1;x<count;x++)doc.records.append(parts.value(x));
    complete=true;return Result::Complete;
}

QString NfcDetails::typeName(const QJsonObject &o) {
    const auto k=o.value("kind").toString();
    if(k.startsWith("transit_"))return QStringLiteral("交通卡");
    if(k=="emv")return QStringLiteral("银行卡（EMV / PBOC）");
    if(k=="classic") {
        const int blocks=o.value("blocks").toInt();
        return blocks==64?"MIFARE Classic 1K":blocks==256?"MIFARE Classic 4K":blocks==20?"MIFARE Classic Mini":"MIFARE Classic";
    }
    if(k=="ntag213" || k=="ntag215" || k=="ntag216")return k.toUpper();
    if(k=="ultralight")return "MIFARE Ultralight";
    if(k=="amusement_ic")return "Amusement IC";
    if(k=="felica")return "FeliCa";
    if(k=="type2")return QStringLiteral("Type 2 存储卡");
    if(k=="iso15693")return "ISO15693 / Type 5";
    if(k=="iso_dep")return QStringLiteral("ISO-DEP / Type 4 智能卡");
    return QStringLiteral("未识别卡片");
}
QString NfcDetails::money(qint32 value, const QString &currency) {
    if(currency=="JPY")return QStringLiteral("¥ %1 JPY").arg(value);
    if(currency=="CNY" || currency=="HKD")return QStringLiteral("%1 %2 %3").arg(currency=="CNY"?"¥":"HK$",QString::number(double(value)/100.0,'f',2),currency);
    return QStringLiteral("未取得");
}
QString NfcDetails::warningText(const QString &w) {
    const QMap<QString,QString> messages={
        {"octopus_legacy_formula_estimate",QStringLiteral("余额按历史公式估算；新卡的偏移可能不同，请以官方查询为准。")},
        {"rf_session_failed",QStringLiteral("射频通信失败；已取得字段保留，重新贴卡可重试。")},
        {"felica_restore_failed",QStringLiteral("FeliCa 系统恢复失败；读卡器将重新开始发现。")},
        {"probe_budget_exhausted",QStringLiteral("公开信息探测达到时限，部分字段未取得。")},
        {"public_file_unavailable",QStringLiteral("公开卡片文件不可读，部分字段未取得。")},
        {"public_pan_unavailable",QStringLiteral("银行卡未开放卡号读取，未取得 PAN。")},
        {"ppse_aid_missing",QStringLiteral("未取得公开银行卡应用 AID。")},
        {"cc_or_version_unavailable",QStringLiteral("未取得 CC 或芯片版本信息。")},
        {"memory_information_unavailable",QStringLiteral("未取得公开存储容量信息。")},
        {"unknown_card_number_layout",QStringLiteral("卡号格式不符合当前公开文件布局，未解析。")},
        {"apdu_response_too_long",QStringLiteral("APDU 响应超过解析限制。")}
    };
    return messages.value(w,w);
}
