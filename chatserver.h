#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QSet>
#include <QJsonObject>

class ChatServer : public QObject {
    Q_OBJECT
public:
    explicit ChatServer(QObject *parent = nullptr);
    ~ChatServer() override { stop(); }
    bool start(quint16 port, const QHostAddress &host = QHostAddress::AnyIPv4);
    void stop();
    quint16 port() const { return server_.serverPort(); }
    QString errorString() const { return server_.errorString(); }
    void setRingTimeout(int ms) { ringTimeout_ = ms; }
signals:
    void log(const QString &text);
    void usersChanged(const QStringList &users);
private:
    struct Peer { QByteArray buffer; QString nickname; };
    struct Group { QString name; QSet<QString> members; };
    struct Call { QTcpSocket *caller; QTcpSocket *callee; bool accepted = false; };
    QTcpServer server_;
    QHash<QTcpSocket *, Peer> peers_;
    QHash<QString, Group> groups_;
    QHash<QString, Call> calls_;
    int ringTimeout_ = 30000;
    void read(QTcpSocket *socket);
    void process(QTcpSocket *socket, const QJsonObject &message);
    void remove(QTcpSocket *socket);
    void error(QTcpSocket *socket, const QString &text);
    QTcpSocket *findUser(const QString &name) const;
    void updateLists();
    void broadcast(const QJsonObject &message, const QString &group = QString());
    void endCall(const QString &id, const QString &reason);
    bool busy(QTcpSocket *socket) const;
};
