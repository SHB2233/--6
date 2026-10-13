#include "../chatserver.h"
#include <QtTest>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QtEndian>

class ChatServerTest : public QObject {
    Q_OBJECT
private slots:
    void loginAcknowledged() {
        ChatServer server;
        QVERIFY(server.start(0, QHostAddress::LocalHost));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        QByteArray payload = R"({"type":"join","nickname":"Alice"})";
        quint32 len = qToBigEndian<quint32>(payload.size());
        client.write(QByteArray(reinterpret_cast<char *>(&len), 4) + payload);
        QTRY_VERIFY(client.bytesAvailable() > 4);
        const auto data = client.readAll();
        QVERIFY(data.contains("join_ok"));
        server.stop();
    }
};
QTEST_GUILESS_MAIN(ChatServerTest)
#include "chatserver_test.moc"
