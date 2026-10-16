#pragma once
#include <QByteArray>
#include <QString>

struct CardEvent {
    quint8 type = 0, status = 0;
    QByteArray idm, pmm, spad, mifare;
    static bool parse(const QByteArray &p, CardEvent &e, QString &error) {
        if (p.size() < 20) {error = QStringLiteral("刷卡帧太短"); return false;}
        auto byte = [&](qsizetype i) {return quint8(p.at(i));};
        e = {}; e.type = byte(0); e.status = byte(1);
        e.idm = p.mid(2,8); e.pmm = p.mid(10,8);
        qsizetype s = byte(18);
        if ((s != 0 && s != 16) || 19+s >= p.size()) {error = QStringLiteral("SPAD长度无效");return false;}
        e.spad = p.mid(19,s);
        qsizetype m = byte(19+s);
        if ((m != 0 && m != 48) || 20+s+m != p.size()) {error = QStringLiteral("MIFARE长度无效");return false;}
        e.mifare = p.mid(20+s,m);
        return true;
    }
};
inline QString issuerFromDfc(const QByteArray &dfc) {
    const auto h = dfc.toHex().toUpper();
    if(h=="0078") return QStringLiteral("SEGA (Lite-S)");
    if(h=="0068") return QStringLiteral("Konami (Lite-S)");
    if(h=="002A") return QStringLiteral("Bandai Namco Entertainment (Lite-S)");
    if(h=="003A") return QStringLiteral("Bandai Namco Games (Lite)");
    if(h=="0079") return QStringLiteral("Taito / NESiCA");
    if(h=="8CA2") return QStringLiteral("Bandai Namco / 旧 Banapass (Edy)");
    if(h=="0000") return QStringLiteral("测试保留，发行方不确定");
    return QStringLiteral("未知 / DFC未取得");
}
inline QString issuerFromCode(const QString &code) {
    if(code.startsWith("500") || code.startsWith("501") || code.startsWith("01036")) return "SEGA";
    if(code.startsWith("510") || code.startsWith('3')) return "Bandai Namco";
    if(code.startsWith("520")) return "Konami";
    if(code.startsWith("530")) return "Taito";
    return QStringLiteral("未知前缀");
}
inline QString statusText(quint8 s) {
    switch(s) {
    case 0:return QStringLiteral("读取成功；卡号仍需解析/核对");
    case 0x11:return QStringLiteral("SPAD读取失败，详见服务码/块号扫描日志");
    case 0x12:return QStringLiteral("MIFARE认证未成功；可能是当前密钥不匹配、会话/射频失败，不能判断为空白卡");
    case 0x13:return QStringLiteral("MIFARE读块失败");
    case 0x14:return QStringLiteral("PMm未取得");
    case 0x15:return QStringLiteral("未激活MIFARE Tag-command接口");
    case 0x16:return QStringLiteral("当前卡片不支持Access Code读取");
    default:return QStringLiteral("错误码 0x%1").arg(s,2,16,QChar('0'));
    }
}
