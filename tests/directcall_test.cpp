#include "../directcall.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <QtTest>

namespace {
QByteArray frame(const QJsonObject &object)
{
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray bytes(4, '\0');
    qToBigEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar *>(bytes.data()));
    return bytes + payload;
}

QJsonObject takeFrame(QByteArray &bytes)
{
    if (bytes.size() < 4)
        return {};
    const quint32 size = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()));
    if (bytes.size() < int(size) + 4)
        return {};
    const QJsonObject object = QJsonDocument::fromJson(bytes.mid(4, int(size))).object();
    bytes.remove(0, int(size) + 4);
    return object;
}
}

class DirectCallTest : public QObject {
    Q_OBJECT
private slots:
    void chineseRoundTripAndReuse();
    void wrongCallIdLeavesListenerAvailable();
    void outgoingRequiresReadyAndHandlesFragments();
    void incomingHandlesFragmentedHello();
    void refusedConnectionEndsOnce();
    void associationTimeoutClosesResources();
    void unassociatedDisconnectAllowsRetry();
    void immediateReuseIgnoresOldSocketSignals();
    void outgoingAssociationTimeout();
    void outgoingRejectsWrongReadyCallId();
};

void DirectCallTest::chineseRoundTripAndReuse()
{
    DirectCall callee, caller;
    QSignalSpy calleeConnected(&callee, &DirectCall::connected);
    QSignalSpy callerConnected(&caller, &DirectCall::connected);
    QSignalSpy calleeText(&callee, &DirectCall::textReceived);
    QSignalSpy callerText(&caller, &DirectCall::textReceived);
    QSignalSpy calleeEnded(&callee, &DirectCall::ended);
    QSignalSpy callerEnded(&caller, &DirectCall::ended);
    for (int session = 0; session < 2; ++session) {
        const QString id = QString::number(session);
        const quint16 port = callee.startListening(id, QStringLiteral("主叫"));
        QVERIFY(port != 0);
        QCOMPARE(callee.localPort(), port);
        caller.connectToPeer(id, QStringLiteral("被叫"), QStringLiteral("127.0.0.1"), port);
        QTRY_COMPARE(calleeConnected.count(), session + 1);
        QTRY_COMPARE(callerConnected.count(), session + 1);
        QVERIFY(callee.isConnected());
        QVERIFY(caller.isConnected());
        caller.sendText(QStringLiteral("你好，TCP！🌏"));
        callee.sendText(QStringLiteral("收到\n第二行中文"));
        QTRY_COMPARE(calleeText.count(), session + 1);
        QTRY_COMPARE(callerText.count(), session + 1);
        QCOMPARE(calleeText.last().at(0).toString(), QStringLiteral("你好，TCP！🌏"));
        QCOMPARE(callerText.last().at(0).toString(), QStringLiteral("收到\n第二行中文"));
        caller.end();
        caller.end();
        QTRY_COMPARE(calleeEnded.count(), session + 1);
        QCOMPARE(callerEnded.count(), session + 1);
        QVERIFY(!caller.isConnected());
        QVERIFY(!callee.isConnected());
        QCOMPARE(callee.localPort(), quint16(0));
        QTest::qWait(20); // Let stale socket notifications run before reuse.
        QCOMPARE(calleeEnded.count(), session + 1);
    }
}

void DirectCallTest::wrongCallIdLeavesListenerAvailable()
{
    DirectCall callee, caller;
    callee.setConnectTimeout(1500);
    QSignalSpy connected(&callee, &DirectCall::connected);
    QSignalSpy ended(&callee, &DirectCall::ended);
    const quint16 port = callee.startListening(QStringLiteral("expected"), QStringLiteral("甲"));
    QVERIFY(port != 0);
    QTcpSocket intruder;
    intruder.connectToHost(QHostAddress::LocalHost, port);
    QTRY_COMPARE(intruder.state(), QAbstractSocket::ConnectedState);
    intruder.write(frame({{"type", "call_hello"}, {"callId", "wrong"}}));
    QTRY_COMPARE(intruder.state(), QAbstractSocket::UnconnectedState);
    QCOMPARE(connected.count(), 0);
    QCOMPARE(ended.count(), 0);
    QCOMPARE(callee.localPort(), port);
    caller.connectToPeer(QStringLiteral("expected"), QStringLiteral("乙"), QStringLiteral("127.0.0.1"), port);
    QTRY_COMPARE(connected.count(), 1);
    QTRY_VERIFY(caller.isConnected());
}

void DirectCallTest::outgoingRequiresReadyAndHandlesFragments()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    DirectCall caller;
    QSignalSpy connected(&caller, &DirectCall::connected);
    QSignalSpy text(&caller, &DirectCall::textReceived);
    QSignalSpy ended(&caller, &DirectCall::ended);
    caller.connectToPeer("abc", "peer", "127.0.0.1", server.serverPort());
    QTRY_VERIFY(server.hasPendingConnections());
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    QByteArray received;
    QTRY_VERIFY((received += peer->readAll()).size() > 4);
    const QJsonObject hello = takeFrame(received);
    QCOMPARE(hello.value("type").toString(), QStringLiteral("call_hello"));
    QCOMPARE(hello.value("callId").toString(), QStringLiteral("abc"));
    QCOMPARE(connected.count(), 0);
    const QByteArray ready = frame({{"type", "call_ready"}, {"callId", "abc"}});
    peer->write(ready.left(2));
    QTest::qWait(20);
    QCOMPARE(connected.count(), 0);
    peer->write(ready.mid(2, 5));
    QTest::qWait(20);
    QCOMPARE(connected.count(), 0);
    peer->write(ready.mid(7));
    QTRY_COMPARE(connected.count(), 1);
    const QByteArray chat = frame({{"type", "call_chat"}, {"text", QStringLiteral("拆包中文")}});
    peer->write(chat.left(3));
    QTest::qWait(20);
    QCOMPARE(text.count(), 0);
    peer->write(chat.mid(3) + frame({{"type", "call_chat"}, {"text", "next"}})
                + frame({{"type", "call_end"}}));
    QTRY_COMPARE(text.count(), 2);
    QCOMPARE(text.at(0).at(0).toString(), QStringLiteral("拆包中文"));
    QCOMPARE(text.at(1).at(0).toString(), QStringLiteral("next"));
    QTRY_COMPARE(ended.count(), 1);
    QVERIFY(!caller.isConnected());
}

void DirectCallTest::incomingHandlesFragmentedHello()
{
    DirectCall callee;
    QSignalSpy connected(&callee, &DirectCall::connected);
    QTcpSocket peer;
    peer.connectToHost(QHostAddress::LocalHost, callee.startListening("split", "peer"));
    QTRY_COMPARE(peer.state(), QAbstractSocket::ConnectedState);
    const QByteArray hello = frame({{"type", "call_hello"}, {"callId", "split"}});
    peer.write(hello.left(1));
    QTest::qWait(20);
    QCOMPARE(connected.count(), 0);
    peer.write(hello.mid(1));
    QTRY_COMPARE(connected.count(), 1);
    QByteArray received;
    QTRY_VERIFY((received += peer.readAll()).size() > 4);
    QCOMPARE(takeFrame(received).value("type").toString(), QStringLiteral("call_ready"));
}

void DirectCallTest::refusedConnectionEndsOnce()
{
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const quint16 port = reservation.serverPort();
    reservation.close();
    DirectCall caller;
    caller.setConnectTimeout(150);
    QSignalSpy errors(&caller, &DirectCall::errorOccurred);
    QSignalSpy ended(&caller, &DirectCall::ended);
    caller.connectToPeer("refused", "peer", "127.0.0.1", port);
    QTRY_COMPARE(ended.count(), 1);
    QCOMPARE(errors.count(), 1);
    QVERIFY(!errors.at(0).at(0).toString().isEmpty());
    QVERIFY(!caller.isConnected());
    caller.end();
    QTest::qWait(200);
    QCOMPARE(ended.count(), 1);
}

void DirectCallTest::associationTimeoutClosesResources()
{
    DirectCall callee;
    callee.setConnectTimeout(100);
    QSignalSpy errors(&callee, &DirectCall::errorOccurred);
    QSignalSpy ended(&callee, &DirectCall::ended);
    const quint16 port = callee.startListening("timeout", "peer");
    QVERIFY(port != 0);
    QTcpSocket silentPeer;
    silentPeer.connectToHost(QHostAddress::LocalHost, port);
    QTRY_COMPARE(silentPeer.state(), QAbstractSocket::ConnectedState);
    QTRY_COMPARE(ended.count(), 1);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(callee.localPort(), quint16(0));
    QTRY_COMPARE(silentPeer.state(), QAbstractSocket::UnconnectedState);
    QTcpSocket laterPeer;
    laterPeer.connectToHost(QHostAddress::LocalHost, port);
    QSignalSpy refused(&laterPeer, &QTcpSocket::errorOccurred);
    QTRY_VERIFY(!refused.isEmpty());
    callee.end();
    QTest::qWait(150);
    QCOMPARE(ended.count(), 1);
}

void DirectCallTest::unassociatedDisconnectAllowsRetry()
{
    DirectCall callee, caller;
    callee.setConnectTimeout(1500);
    const quint16 port = callee.startListening("retry", "peer");
    QVERIFY(port != 0);
    QTcpSocket abandoned;
    abandoned.connectToHost(QHostAddress::LocalHost, port);
    QTRY_COMPARE(abandoned.state(), QAbstractSocket::ConnectedState);
    abandoned.disconnectFromHost();
    QTRY_COMPARE(abandoned.state(), QAbstractSocket::UnconnectedState);
    QTest::qWait(20);
    caller.connectToPeer("retry", "peer", "127.0.0.1", port);
    QTRY_VERIFY(callee.isConnected());
    QTRY_VERIFY(caller.isConnected());
}

void DirectCallTest::immediateReuseIgnoresOldSocketSignals()
{
    DirectCall callee, caller;
    QSignalSpy ended(&caller, &DirectCall::ended);
    QSignalSpy connected(&caller, &DirectCall::connected);
    quint16 port = callee.startListening("old", "peer");
    caller.connectToPeer("old", "peer", "127.0.0.1", port);
    QTRY_COMPARE(connected.count(), 1);
    caller.end();
    port = callee.startListening("new", "peer");
    caller.connectToPeer("new", "peer", "127.0.0.1", port);
    QTRY_COMPARE(connected.count(), 2);
    QTRY_VERIFY(callee.isConnected());
    QCOMPARE(ended.count(), 1);
    QSignalSpy text(&callee, &DirectCall::textReceived);
    caller.sendText(QStringLiteral("新会话"));
    QTRY_COMPARE(text.count(), 1);
    QCOMPARE(text.at(0).at(0).toString(), QStringLiteral("新会话"));
    QVERIFY(caller.isConnected());
}

void DirectCallTest::outgoingAssociationTimeout()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    DirectCall caller;
    caller.setConnectTimeout(120);
    QSignalSpy connected(&caller, &DirectCall::connected);
    QSignalSpy errors(&caller, &DirectCall::errorOccurred);
    QSignalSpy ended(&caller, &DirectCall::ended);
    caller.connectToPeer("silent", "peer", "127.0.0.1", server.serverPort());
    QTRY_VERIFY(server.hasPendingConnections());
    QScopedPointer<QTcpSocket> silent(server.nextPendingConnection());
    QTRY_COMPARE(ended.count(), 1);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(connected.count(), 0);
    QTRY_COMPARE(silent->state(), QAbstractSocket::UnconnectedState);
    caller.end();
    QCOMPARE(ended.count(), 1);
}

void DirectCallTest::outgoingRejectsWrongReadyCallId()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    DirectCall caller;
    QSignalSpy connected(&caller, &DirectCall::connected);
    QSignalSpy errors(&caller, &DirectCall::errorOccurred);
    QSignalSpy ended(&caller, &DirectCall::ended);
    caller.connectToPeer("correct", "peer", "127.0.0.1", server.serverPort());
    QTRY_VERIFY(server.hasPendingConnections());
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    peer->write(frame({{"type", "call_ready"}, {"callId", "wrong"}}));
    QTRY_COMPARE(ended.count(), 1);
    QCOMPARE(connected.count(), 0);
    QCOMPARE(errors.count(), 1);
    QVERIFY(!caller.isConnected());
}

QTEST_GUILESS_MAIN(DirectCallTest)
#include "directcall_test.moc"
