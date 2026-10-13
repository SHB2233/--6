#include "directcall.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>

namespace {
constexpr quint32 MaxFrameBytes = 10 * 1024 * 1024;
}

DirectCall::DirectCall(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        finish(QStringLiteral("通话连接超时"), true);
    });
}

DirectCall::~DirectCall()
{
    closeResources();
}

void DirectCall::begin(const QString &callId, const QString &peerName, bool incoming)
{
    end();
    m_active = true;
    m_incoming = incoming;
    m_connected = false;
    m_callId = callId;
    m_peerName = peerName;
    m_buffer.clear();
    m_timer.start(m_connectTimeout);
}

quint16 DirectCall::startListening(const QString &callId, const QString &peerName)
{
    begin(callId, peerName, true);
    auto *server = new QTcpServer(this);
    m_server = server;
    server->setMaxPendingConnections(1);
    connect(server, &QTcpServer::newConnection, this, [this, server] {
        if (m_server != server || !m_active)
            return;
        while (server->hasPendingConnections()) {
            QTcpSocket *socket = server->nextPendingConnection();
            if (m_socket) {
                socket->abort();
                socket->deleteLater();
            } else {
                attachSocket(socket);
            }
        }
    });
    if (!server->listen(QHostAddress::AnyIPv4, 0)) {
        const QString reason = QStringLiteral("无法监听通话端口：") + server->errorString();
        finish(reason, true);
        return 0;
    }
    return server->serverPort();
}

void DirectCall::connectToPeer(const QString &callId, const QString &peerName,
                               const QString &host, quint16 port)
{
    begin(callId, peerName, false);
    auto *socket = new QTcpSocket(this);
    attachSocket(socket);
    connect(socket, &QTcpSocket::connected, this, [this, socket] {
        if (m_socket == socket && m_active)
            writeFrame({{"type", "call_hello"}, {"callId", m_callId}});
    });
    socket->connectToHost(host, port, QIODevice::ReadWrite, QAbstractSocket::IPv4Protocol);
}

void DirectCall::attachSocket(QTcpSocket *socket)
{
    m_socket = socket;
    m_buffer.clear();
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        if (m_socket == socket && m_active)
            readFrames(socket);
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        if (m_socket != socket || !m_active)
            return;
        if (m_incoming && !m_connected)
            rejectCandidate();
        else
            finish(QStringLiteral("对方已断开通话"));
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, socket](QAbstractSocket::SocketError error) {
        if (m_socket != socket || !m_active)
            return;
        // RemoteHostClosedError is followed by disconnected; it is a normal hangup.
        if (error == QAbstractSocket::RemoteHostClosedError)
            return;
        if (m_incoming && !m_connected)
            rejectCandidate();
        else
            finish(QStringLiteral("通话连接失败：") + socket->errorString(), true);
    });
    if (socket->bytesAvailable() > 0)
        readFrames(socket);
}

void DirectCall::readFrames(QTcpSocket *socket)
{
    m_buffer += socket->readAll();
    while (m_active && m_socket == socket && m_buffer.size() >= 4) {
        const quint32 size = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_buffer.constData()));
        if (size == 0 || size > MaxFrameBytes) {
            if (m_incoming && !m_connected)
                rejectCandidate();
            else
                finish(QStringLiteral("通话数据帧长度无效"), true);
            return;
        }
        if (m_buffer.size() < int(size) + 4)
            return;
        const QByteArray payload = m_buffer.mid(4, int(size));
        m_buffer.remove(0, int(size) + 4);
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            if (m_incoming && !m_connected)
                rejectCandidate();
            else
                finish(QStringLiteral("通话数据格式无效"), true);
            return;
        }
        handleFrame(document.object());
    }
}

void DirectCall::handleFrame(const QJsonObject &object)
{
    const QString type = object.value("type").toString();
    if (!m_connected) {
        const QString expectedType = m_incoming ? QStringLiteral("call_hello")
                                               : QStringLiteral("call_ready");
        if (type != expectedType || !object.value("callId").isString()
                || object.value("callId").toString() != m_callId) {
            if (m_incoming)
                rejectCandidate();
            else
                finish(QStringLiteral("通话关联验证失败"), true);
            return;
        }
        if (m_incoming && !writeFrame({{"type", "call_ready"}, {"callId", m_callId}}))
            return;
        m_connected = true;
        m_timer.stop();
        if (m_server)
            m_server->close();
        emit connected();
        return;
    }
    if (type == "call_chat" && object.value("text").isString()) {
        emit textReceived(object.value("text").toString());
    } else if (type == "call_end") {
        finish(QStringLiteral("对方已结束通话"));
    } else {
        finish(QStringLiteral("通话消息类型无效"), true);
    }
}

bool DirectCall::writeFrame(const QJsonObject &object)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return false;
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.size() > int(MaxFrameBytes)) {
        emit errorOccurred(QStringLiteral("通话文字过长"));
        return false;
    }
    QByteArray bytes(4, '\0');
    qToBigEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar *>(bytes.data()));
    bytes += payload;
    if (m_socket->write(bytes) != bytes.size()) {
        finish(QStringLiteral("通话消息发送失败"), true);
        return false;
    }
    return true;
}

void DirectCall::sendText(const QString &text)
{
    if (m_connected)
        writeFrame({{"type", "call_chat"}, {"text", text}});
}

void DirectCall::rejectCandidate()
{
    QTcpSocket *socket = m_socket;
    m_socket = nullptr;
    m_buffer.clear();
    if (socket) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
}

void DirectCall::closeResources(bool graceful)
{
    m_timer.stop();
    if (m_server) {
        m_server->disconnect(this);
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    QTcpSocket *socket = m_socket;
    m_socket = nullptr;
    m_buffer.clear();
    if (socket) {
        socket->disconnect(this);
        if (graceful) {
            socket->flush();
            socket->disconnectFromHost();
        } else {
            socket->abort();
        }
        socket->deleteLater();
    }
}

void DirectCall::finish(const QString &reason, bool failed)
{
    if (!m_active)
        return;
    m_active = false;
    m_connected = false;
    closeResources();
    if (failed)
        emit errorOccurred(reason);
    emit ended(reason);
}

void DirectCall::end()
{
    if (!m_active)
        return;
    if (m_connected) {
        writeFrame({{"type", "call_end"}});
        // Drain the final small control frame before detaching this session.
        if (!m_active)
            return;
        closeResources(true);
    }
    finish(QStringLiteral("通话已结束"));
}

bool DirectCall::isConnected() const
{
    return m_connected;
}

quint16 DirectCall::localPort() const
{
    return m_server && m_server->isListening() ? m_server->serverPort() : 0;
}

void DirectCall::setConnectTimeout(int ms)
{
    m_connectTimeout = qMax(1, ms);
    if (m_active && !m_connected)
        m_timer.start(m_connectTimeout);
}
