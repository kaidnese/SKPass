#include "WriteProtocol.h"
#include <QRegularExpression>
namespace WriteProtocol {
QByteArray blocks(const QString &code) {
    if(!QRegularExpression("^[0-9]{20}$").match(code).hasMatch() || code==QString(20,'0'))return {};
    return QByteArray(22,0)+QByteArray::fromHex(code.toLatin1());
}
QByteArray request(quint32 id,const QByteArray &uid,const QByteArray &key,const QString &code,QString *error,bool writeSegaKey) {
    if(!id || (uid.size()!=4 && uid.size()!=7 && uid.size()!=10) || key.size()!=6 || blocks(code).size()!=32) {
        if(error)*error=QStringLiteral("需要目标UID、6字节当前Key A及非全零20位数字卡号");return {};
    }
    QByteArray out;for(int i=0;i<4;++i)out.append(char(id>>(i*8)));
    out.append(char(uid.size()));out+=uid;out+=QByteArray(10-uid.size(),0);
    out+=key;out+=QByteArray::fromHex(code.toLatin1());out.append(char(writeSegaKey?1:0));return out;
}
bool parse(const QByteArray &p,Result &r) {
    if(p.size()!=51)return false;
    quint8 len=quint8(p[7]);if(len!=0 && len!=4 && len!=7 && len!=10)return false;
    r={};for(int i=0;i<4;++i)r.id|=quint32(quint8(p[i]))<<(i*8);
    r.status=quint8(p[4]);r.stage=quint8(p[5]);r.detail=quint8(p[6]);
    r.uid=p.mid(8,len);r.verifiedMask=quint8(p[18]);r.data=p.mid(19,32);
    return r.status>=1 && r.status<=10 && r.stage<=9 && r.verifiedMask<=7;
}
QString statusText(const Result &r) {
    switch(r.status) {
    case 1:return QStringLiteral("已接收，15秒内请保持/重新放入目标卡；仅执行一次");
    case 2:return QStringLiteral("等待目标卡已过期，未执行写入");
    case 3:return QStringLiteral("拒绝请求：参数、重复ID或卡型不符合Classic 1K要求");
    case 4:return QStringLiteral("已有写卡任务，本请求未执行");
    case 5:return r.verifiedMask&4?QStringLiteral("Block1、Block2及Block3 Key A 写入并回读验证成功"):QStringLiteral("Block1、Block2写入及逐字节回读验证成功；未改UID/密钥");
    case 6:return QStringLiteral("认证失败，未写入；检查当前Key A，空白卡常为FFFFFFFFFFFF");
    case 7:return QStringLiteral("预读失败，未执行写入；检查密钥、访问控制和射频连接");
    case 8:return QStringLiteral("写入失败，可能已部分改变Block1/2；请重新读卡，不要自动重试");
    case 9:return QStringLiteral("回读验证失败，卡数据可能已改变；请重新读卡核对");
    case 10:return QStringLiteral("执行前CDC断开，待写请求已取消");
    default:return QStringLiteral("未知写卡状态");
    }
}
}
