#include "AimeCode.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <numeric>
#include <vector>
namespace {
class MiniSolitaire {
    std::vector<int> deck;
    void move(int card) {
        auto p=std::find(deck.begin(),deck.end(),card);
        if(p!=deck.end()-1) std::iter_swap(p,p+1);
        else {deck.pop_back();deck.insert(deck.begin()+1,card);}
    }
    void cut(int point) {
        std::vector<int> result(deck.begin()+point,deck.end()-1);
        result.insert(result.end(),deck.begin(),deck.begin()+point);
        result.push_back(deck.back());deck.swap(result);
    }
    void outside() {
        auto a=std::find(deck.begin(),deck.end(),21),b=std::find(deck.begin(),deck.end(),22);
        if(a>b) std::swap(a,b);
        std::vector<int> result(b+1,deck.end());
        result.insert(result.end(),a,b+1);result.insert(result.end(),deck.begin(),a);
        deck.swap(result);
    }
    int top() const {return deck[deck[0]==22 ? 21 : deck[0]];}
    int hash() {
        for(;;) {move(21);move(22);move(22);outside();cut(deck.back()==22 ? 21 : deck.back());
            int p=top();if(p!=21 && p!=22) return p;}
    }
public:
    QString crypt(const QString &key,const QString &input,bool decode) {
        deck.resize(22);std::iota(deck.begin(),deck.end(),1);
        for(auto c:key) {hash();cut(c.unicode()-'0'+1);}
        QString out;
        for(auto c:input) {
            hash();int n=c.unicode()-'0'+1+(decode ? -top() : top());
            while(n<1)n+=10;
            out+=QChar('0'+(n-1)%10);
        }
        return out;
    }
};
}
namespace AimeCode {
QString digest(quint32 serial) {
    const QByteArray key="A1B3E86CF02974D6";
    const auto md5=QCryptographicHash::hash(QString::number(serial).rightJustified(8,'0').toLatin1(),QCryptographicHash::Md5);
    QString bits;
    for(char k:key) {
        int index=k>='A' ? k-'A'+10 : k-'0';
        quint8 b=quint8(md5[index]);
        for(int i=0;i<8;++i) bits+= (b & (1<<i)) ? '1' : '0';
    }
    std::reverse(bits.begin(),bits.end());bits=bits.rightJustified(6*23,'0');
    quint32 acc=0;
    for(int i=0;i<bits.size();i+=23) acc^=bits.mid(i,23).toUInt(nullptr,2);
    return QString::number(acc).rightJustified(7,'0');
}
QString fromSerial(const QString &serial,QString *error) {
    if(!QRegularExpression("^[0-9]{8}$").match(serial).hasMatch()) {
        if(error)*error=QStringLiteral("Serial必须是8位十进制数字；不能用UID代替");return {};
    }
    auto d=digest(serial.toUInt());
    return "01036"+MiniSolitaire().crypt(d,serial,false)+d;
}
QString serialFromCode(const QString &code,QString *error) {
    if(!QRegularExpression("^01036[0-9]{15}$").match(code).hasMatch()) {
        if(error)*error=QStringLiteral("反查仅支持01036格式");return {};
    }
    QString d=code.right(7), serial=MiniSolitaire().crypt(d,code.mid(5,8),true);
    if(digest(serial.toUInt())!=d) {if(error)*error=QStringLiteral("卡号摘要校验失败");return {};}
    return serial;
}
QString fromBcd(const QByteArray &bytes) {
    if(bytes.size()!=10) return {};
    QString code;
    for(char c:bytes) {
        quint8 b=quint8(c);
        if((b>>4)>9 || (b&15)>9) return {};
        code+=QChar('0'+(b>>4));code+=QChar('0'+(b&15));
    }
    if(code==QString(20,'0')) return {};
    return code;
}
QString fromMifare(const QByteArray &bytes,QString *error) {
    if(bytes.size()==48) {auto code=fromBcd(bytes.mid(38,10));if(!code.isEmpty())return code;}
    QSet<QString> candidates;
    for(qsizetype i=0;i+10<=bytes.size();++i) {
        auto code=fromBcd(bytes.mid(i,10));
        // A general scan of zeros/UIDs may resemble decimal data. Only accept
        // recognized formats; still report a scan result as an unverified candidate.
        if(!code.isEmpty() && (code.startsWith("01036") || code.startsWith('3') ||
            code.startsWith("500") || code.startsWith("501") || code.startsWith("510") ||
            code.startsWith("520") || code.startsWith("530"))) candidates.insert(code);
    }
    if(candidates.size()==1) {if(error)*error=QStringLiteral("扫描得到唯一BCD候选，需与卡背核对");return *candidates.begin();}
    if(error)*error=candidates.isEmpty() ? QStringLiteral("卡内未找到卡号") : QStringLiteral("多个BCD候选，无法确定卡号");
    return {};
}
QString derivedFromIdm(const QByteArray &idm) {
    if(idm.size()!=8 || idm==QByteArray(8,0)) return {};
    quint64 u=0;for(char c:idm)u=(u<<8)|quint8(c);
    // Unsigned two's complement magnitude also handles INT64_MIN safely.
    quint64 magnitude=(u & (quint64(1)<<63)) ? (~u+1) : u;
    return QString::number(magnitude).rightJustified(20,'0');
}
}
