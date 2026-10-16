#include "ReaderProtocol.h"
#include <QUrl>
#include <cstring>
namespace {
quint16 u16(const QByteArray &p,int pos){return quint8(p[pos])|(quint16(quint8(p[pos+1]))<<8);}
quint32 u32(const QByteArray &p,int pos){return u16(p,pos)|(quint32(u16(p,pos+2))<<16);}
void put16(QByteArray &p,int pos,quint16 n){p[pos]=char(n);p[pos+1]=char(n>>8);}
void put32(QByteArray &p,int pos,quint32 n){put16(p,pos,n);put16(p,pos+2,n>>16);}
bool pair(int protocol,int subtype){return (protocol==0x80 && subtype>=1 && subtype<=3) ||
    (protocol==2 && subtype>=4 && subtype<=7) || (protocol==3 && subtype==8) || (protocol==6 && subtype==9);}
}
namespace ReaderProtocol {
bool target(const QByteArray &p,advanced_target_t &t){
    t={};if(p.size()!=ADV_TARGET_BYTES)return false;
    t.token=u32(p,0);t.uid_len=quint8(p[4]);memcpy(t.uid,p.constData()+5,10);
    t.protocol=quint8(p[15]);t.subtype=quint8(p[16]);t.first=u16(p,17);t.last=u16(p,19);
    t.unit=quint8(p[21]);t.available=quint8(p[22]);
    if(t.available>1 || t.uid_len>10 || (t.available && (!t.token || !t.uid_len)))return false;
    if(!t.subtype)return true;
    if(!pair(t.protocol,t.subtype) || t.first>t.last || (t.unit!=4 && t.unit!=16))return false;
    if(t.protocol==0x80)return t.first==0 && t.last==(t.subtype==3?19:63) && t.unit==16;
    if(t.protocol==2)return t.first==4 && t.last>=7 && t.last<=225 && t.unit==4 && t.uid_len==7 &&
        (t.subtype!=ADV_NTAG213 || t.last==39) && (t.subtype!=ADV_NTAG215 || t.last==129) &&
        (t.subtype!=ADV_NTAG216 || t.last==225);
    if(t.protocol==3)return !t.first && t.last==13 && t.unit==16 && t.uid_len==8;
    return !t.first && t.last<=255 && t.unit==4 && t.uid_len==8;
}
QByteArray request(const advanced_target_t &t,quint32 id,int op,int address,int count,
                   int flags,int keyType,const QByteArray &key,const QByteArray &data,QString *error){
    QByteArray p(36,0);put32(p,0,id);put32(p,4,t.token);p[8]=char(t.uid_len);memcpy(p.data()+9,t.uid,10);
    p[19]=char(t.protocol);p[20]=char(t.subtype);p[21]=char(op);p[22]=char(flags);
    put16(p,23,address);put16(p,25,count);p[27]=char(keyType);
    if(key.size()==6)memcpy(p.data()+28,key.constData(),6);put16(p,34,data.size());p+=data;
    advanced_request_t q;
    int status=ADV_INVALID;
    if(key.size()==6 && address>=0 && address<=65535 && count>0 && count<=65535 &&
       Advanced_Decode_Request(reinterpret_cast<const uint8_t *>(p.constData()),p.size(),&q))status=Advanced_Validate(&t,&q);
    if(status){if(error)*error=statusText(status);return {};}
    return p;
}
bool result(const QByteArray &p,advanced_result_t &r){
    r={};if(p.size()<30)return false;
    r.id=u32(p,0);r.target.token=u32(p,4);r.target.uid_len=quint8(p[8]);memcpy(r.target.uid,p.constData()+9,10);
    r.target.protocol=quint8(p[19]);r.target.subtype=quint8(p[20]);r.target.available=1;
    r.operation=quint8(p[21]);r.status=quint8(p[22]);r.detail=quint8(p[23]);r.address=u16(p,24);
    r.completed=u16(p,26);r.length=u16(p,28);
    if(!r.id || r.target.uid_len>10 || r.status>ADV_DUPLICATE || r.length>ADV_MAX_DATA || p.size()!=30+r.length)return false;
    memcpy(r.data,p.constData()+30,r.length);return true;
}
QString statusText(int s){
    switch(s){
    case ADV_OK:return QStringLiteral("操作成功；写入已回读核对");
    case ADV_ACCEPTED:return QStringLiteral("已接收，正在重新核对在场卡；请保持卡片位置");
    case STATUS_CARD_MISMATCH:return QStringLiteral("目标卡已离场、换卡或身份已过期，拒绝执行（STATUS_CARD_MISMATCH）");
    case ADV_INVALID:return QStringLiteral("参数、数据长度或地址范围不符合当前卡型");
    case ADV_BUSY:return QStringLiteral("已有读写或更新任务，未执行本次操作");
    case ADV_AUTH_FAILED:return QStringLiteral("认证失败，未写入；请核对 Key A / Key B 和密钥");
    case ADV_RF_FAILED:return QStringLiteral("射频读写失败；写入可能未完成，请重新读取核对，禁止自动重试");
    case ADV_VERIFY_FAILED:return QStringLiteral("写后验证失败，卡片可能已改变，请重新读取核对");
    case ADV_PROTECTED:return QStringLiteral("安全锁拒绝操作：Block 0 / 尾块确认或厂商字段不符合要求");
    case ADV_DISCONNECTED:return QStringLiteral("连接已断开，待执行操作已取消");
    default:return QStringLiteral("重复请求已拒绝，不会再次写入");
    }
}
QString subtypeName(int s){
    switch(s){case 1:return "MIFARE Classic 1K";case 2:return "MIFARE Classic 4K";
    case 3:return "MIFARE Classic Mini";case 4:return "NTAG213";case 5:return "NTAG215";
    case 6:return "NTAG216";case 7:return "MIFARE Ultralight";case 8:return "FeliCa Lite-S · 88B4";
    case 9:return "ISO15693 · 4 字节块";default:return QStringLiteral("只读 / 未取得可写容量");}
}
QByteArray ndef(const QString &text,bool url,int capacity,QString *error){
    QByteArray payload;char type;
    if(url){QUrl u(text,QUrl::StrictMode);if(!u.isValid() || u.host().isEmpty() ||
        (u.scheme()!="https" && u.scheme()!="http")){if(error)*error=QStringLiteral("请输入完整的 http:// 或 https:// 网页链接");return {};}
        payload.append(char(0));payload+=u.toEncoded();type='U';
    }else{if(text.isEmpty()){if(error)*error=QStringLiteral("请输入文本内容");return {};}
        payload.append(char(2));payload+="zh";payload+=text.toUtf8();type='T';}
    if(payload.size()>247){if(error)*error=QStringLiteral("NDEF 内容过长，本次最多约 240 字节");return {};}
    QByteArray record;record.append(char(0xD1));record.append(char(1));record.append(char(payload.size()));record.append(type);record+=payload;
    QByteArray out;out.append(char(3));out.append(char(record.size()));out+=record;out.append(char(0xFE));
    while(out.size()%4)out.append(char(0));
    if(out.size()>capacity || out.size()>ADV_MAX_DATA){if(error)*error=QStringLiteral("内容超过当前卡片用户区或单次操作容量");return {};}
    return out;
}
}
