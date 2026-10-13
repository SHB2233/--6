#ifndef DIRECTCALL_H
#define DIRECTCALL_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

class QJsonObject;
class QTcpServer;
class QTcpSocket;

// One directly connected text call. Signalling/ringing belong to the main window.
class DirectCall : public QObject {
    Q_OBJECT
public:
    explicit DirectCall(QObject *parent = nullptr);
    ~DirectCall() override;

    quint16 startListening(const QString &callId, const QString &peerName);
    void connectToPeer(const QString &callId, const QString &peerName,
                       const QString &host, quint16 port);
    void sendText(const QString &text);
    void end();
    bool isConnected() const;
    quint16 localPort() const;
    void setConnectTimeout(int ms);

signals:
    void connected();
    void textReceived(const QString &text);
    void ended(const QString &reason);
    void errorOccurred(const QString &message);

private:
    void begin(const QString &callId, const QString &peerName, bool incoming);
    void attachSocket(QTcpSocket *socket);
    void readFrames(QTcpSocket *socket);
    void handleFrame(const QJsonObject &object);
    bool writeFrame(const QJsonObject &object);
    void rejectCandidate();
    void finish(const QString &reason, bool failed = false);
    void closeResources(bool graceful = false);

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_socket = nullptr;
    QTimer m_timer;
    QByteArray m_buffer;
    QString m_callId;
    QString m_peerName;
    int m_connectTimeout = 10000;
    bool m_active = false;
    bool m_incoming = false;
    bool m_connected = false;
};

#endif // DIRECTCALL_H
