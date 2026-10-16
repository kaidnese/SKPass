#include "NfcDetails.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);return 1;}}while(false)

static QByteArray part(int n, int count, const char *kind="transit_japan") {
    QJsonObject o{{"schema",1},{"scan",42},{"uid","011E65740A055390"},{"part",n},{"count",count}};
    if(n==0) o.insert("kind",kind),o.insert("status","ok"),o.insert("protocol",3),o.insert("interface",1),o.insert("currency","JPY"),o.insert("balance",1234),o.insert("number","1234567890");
    else o.insert("date","20261013120000"),o.insert("type",1),o.insert("entry",2),o.insert("exit",3),o.insert("amount",100),o.insert("balance",1234),o.insert("raw","000102030405060708090A0B0C0D0E0F");
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);NfcDetails parser;NfcDocument doc;QString error;
    CHECK(parser.feed(part(1,2),doc,&error)==NfcDetails::Result::Pending);
    CHECK(parser.feed(part(0,2),doc,&error)==NfcDetails::Result::Complete);
    CHECK(NfcDetails::typeName(doc.summary)==QStringLiteral("交通卡"));
    CHECK(doc.records.size()==1);
    CHECK(parser.feed(part(0,2),doc,&error)==NfcDetails::Result::Rejected);
    auto old=part(0,2);old.replace("42","41");CHECK(parser.feed(old,doc,&error)==NfcDetails::Result::Rejected);
    parser.reset();parser.activated(QByteArray::fromHex("011E65740A055390"));
    auto mismatch=part(0,1);mismatch.replace("011E65740A055390","9988776655443322");
    CHECK(parser.feed(mismatch,doc,&error)==NfcDetails::Result::Rejected);
    CHECK(parser.feed(QByteArray(513,'a'),doc,&error)==NfcDetails::Result::Rejected);
    CHECK(parser.feed("{oops}",doc,&error)==NfcDetails::Result::Rejected);
    for(const auto kind:{"transit_tunion","transit_pboc","transit_japan","transit_octopus"}) {
        parser.reset();CHECK(parser.feed(part(0,1,kind),doc,&error)==NfcDetails::Result::Complete);
        CHECK(NfcDetails::typeName(doc.summary)==QStringLiteral("交通卡"));
    }
    for(const auto field:{"balance","protocol","interface","part","count","scan","schema"}) {
        parser.reset();auto o=QJsonDocument::fromJson(part(0,1)).object();o.insert(field,"wrong");
        CHECK(parser.feed(QJsonDocument(o).toJson(QJsonDocument::Compact),doc,&error)==NfcDetails::Result::Rejected);
    }
    parser.reset();CHECK(parser.feed(part(0,2),doc,&error)==NfcDetails::Result::Pending);
    mismatch=part(1,2);mismatch.replace("011E65740A055390","9988776655443322");
    CHECK(parser.feed(mismatch,doc,&error)==NfcDetails::Result::Rejected);
    auto out=part(1,2);out.replace("\"part\":1","\"part\":2");CHECK(parser.feed(out,doc,&error)==NfcDetails::Result::Rejected);
    parser.reset();out=part(0,1);out.replace("1234","2147483648");CHECK(parser.feed(out,doc,&error)==NfcDetails::Result::Rejected);
    parser.reset();auto badDate=part(1,2);badDate.replace("20261013120000","20260231120000");CHECK(parser.feed(badDate,doc,&error)==NfcDetails::Result::Rejected);
    auto classic=QJsonDocument::fromJson(part(0,1,"classic")).object();classic.insert("blocks",64);
    CHECK(NfcDetails::typeName(classic)=="MIFARE Classic 1K");
    CHECK(NfcDetails::money(-123,"CNY")==QStringLiteral("¥ -1.23 CNY"));
    std::puts("NFC detail assembly, validation and transit label tests passed");return 0;
}
