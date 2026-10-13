#pragma once
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtEndian>

namespace wire {
constexpr quint32 MaxFrame = 10 * 1024 * 1024;
constexpr qint64 MaxFile = 5 * 1024 * 1024;
inline bool send(QTcpSocket *socket, const QJsonObject &message) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return false;
    const auto body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > int(MaxFrame)) return false;
    const auto size = qToBigEndian<quint32>(body.size());
    return socket->write(QByteArray(reinterpret_cast<const char *>(&size), 4) + body) == body.size() + 4;
}
// 0: incomplete; 1: complete object; -1: invalid frame.
inline int take(QByteArray &buffer, QJsonObject &message) {
    if (buffer.size() < 4) return 0;
    const auto size = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(buffer.constData()));
    if (!size || size > MaxFrame) return -1;
    if (buffer.size() < int(size + 4)) return 0;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(buffer.mid(4, size), &error);
    buffer.remove(0, size + 4);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return -1;
    message = doc.object();
    return 1;
}
}
