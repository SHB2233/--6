#include "chatserver.h"
#include "wire.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QTimer>
#include <QUuid>

ChatServer::ChatServer(QObject *parent) : QObject(parent), server_(this) {
    connect(&server_, &QTcpServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) {
            auto *socket = server_.nextPendingConnection();
            peers_.insert(socket, {});
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] { remove(socket); });
        }
    });
}
bool ChatServer::start(quint16 port, const QHostAddress &host) {
    if (!server_.listen(host, port)) return false;
    groups_.insert("lobby", {QStringLiteral("公共大厅"), {}});
    emit log(QStringLiteral("服务器监听 TCP %1").arg(server_.serverPort()));
    return true;
}
void ChatServer::stop() {
    server_.close();
    for (const auto &id : calls_.keys()) endCall(id, QStringLiteral("服务器停止"));
    const auto sockets = peers_.keys();
    for (auto *socket : sockets) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->deleteLater();
    }
    peers_.clear(); groups_.clear();
    emit usersChanged({});
    emit log(QStringLiteral("服务器已停止"));
}
void ChatServer::error(QTcpSocket *socket, const QString &text) {
    wire::send(socket, {{"type", "error"}, {"text", text}});
}
QTcpSocket *ChatServer::findUser(const QString &name) const {
    for (auto it = peers_.cbegin(); it != peers_.cend(); ++it)
        if (!it->nickname.isEmpty() && it->nickname.compare(name, Qt::CaseInsensitive) == 0) return it.key();
    return nullptr;
}
void ChatServer::read(QTcpSocket *socket) {
    if (!peers_.contains(socket)) return;
    auto buffer = peers_[socket].buffer;
    buffer += socket->readAll();
    while (peers_.contains(socket)) {
        QJsonObject message;
        const int result = wire::take(buffer, message);
        if (result == 0) break;
        if (result < 0) { error(socket, QStringLiteral("无效的数据帧")); socket->disconnectFromHost(); break; }
        process(socket, message);
    }
    if (peers_.contains(socket)) peers_[socket].buffer = buffer;
}
void ChatServer::broadcast(const QJsonObject &message, const QString &group) {
    for (auto it = peers_.cbegin(); it != peers_.cend(); ++it)
        if (!it->nickname.isEmpty() && (group.isEmpty() || groups_.value(group).members.contains(it->nickname)))
            wire::send(it.key(), message);
}
void ChatServer::updateLists() {
    QStringList users;
    for (const auto &peer : peers_) if (!peer.nickname.isEmpty()) users << peer.nickname;
    users.sort(Qt::CaseInsensitive);
    emit usersChanged(users);
    broadcast({{"type", "users"}, {"users", QJsonArray::fromStringList(users)}});
    for (auto it = peers_.cbegin(); it != peers_.cend(); ++it) {
        if (it->nickname.isEmpty()) continue;
        QJsonArray list;
        for (auto g = groups_.cbegin(); g != groups_.cend(); ++g) {
            auto members = g->members.values(); members.sort(Qt::CaseInsensitive);
            list.append(QJsonObject{{"id", g.key()}, {"name", g->name},
                {"members", QJsonArray::fromStringList(members)}, {"joined", g->members.contains(it->nickname)}});
        }
        wire::send(it.key(), {{"type", "groups"}, {"groups", list}});
    }
}
bool ChatServer::busy(QTcpSocket *socket) const {
    for (const auto &call : calls_) if (call.caller == socket || call.callee == socket) return true;
    return false;
}
void ChatServer::endCall(const QString &id, const QString &reason) {
    if (!calls_.contains(id)) return;
    const auto call = calls_.take(id);
    const QJsonObject message{{"type", "call_ended"}, {"callId", id}, {"reason", reason}};
    wire::send(call.caller, message); wire::send(call.callee, message);
    emit log(QStringLiteral("呼叫 %1 结束：%2").arg(id, reason));
}
void ChatServer::remove(QTcpSocket *socket) {
    if (!peers_.contains(socket)) return;
    for (const auto &id : calls_.keys()) {
        const auto call = calls_.value(id);
        if (call.caller == socket || call.callee == socket) endCall(id, QStringLiteral("用户离线"));
    }
    const auto name = peers_.take(socket).nickname;
    socket->deleteLater();
    if (name.isEmpty()) return;
    for (auto &group : groups_) group.members.remove(name);
    broadcast({{"type", "system"}, {"text", name + QStringLiteral(" 离开了聊天室")}});
    emit log(name + QStringLiteral(" 下线"));
    updateLists();
}
void ChatServer::process(QTcpSocket *socket, const QJsonObject &m) {
    const auto type = m.value("type").toString();
    if (type == "join") {
        if (!peers_[socket].nickname.isEmpty()) return;
        const auto name = m.value("nickname").toString().trimmed();
        bool hasSpace = false; for (const auto ch : name) if (ch.isSpace()) hasSpace = true;
        if (name.isEmpty() || name.size() > 24 || hasSpace || findUser(name)) {
            error(socket, QStringLiteral("昵称需为1至24字符、不含空白，且不能与在线用户重复"));
            socket->disconnectFromHost(); return;
        }
        peers_[socket].nickname = name;
        groups_["lobby"].members.insert(name);
        wire::send(socket, {{"type", "join_ok"}, {"nickname", name}});
        broadcast({{"type", "system"}, {"text", name + QStringLiteral(" 加入了聊天室")}});
        emit log(name + QStringLiteral(" 登录成功"));
        updateLists(); return;
    }
    const auto name = peers_.value(socket).nickname;
    if (name.isEmpty()) { error(socket, QStringLiteral("请先登录")); return; }
    if (type == "group_create" || type == "group_join") {
        QString id = m.value("groupId").toString();
        if (type == "group_create") {
            const auto label = m.value("name").toString().trimmed();
            bool duplicate = false;
            for (const auto &g : groups_) if (g.name.compare(label, Qt::CaseInsensitive) == 0) duplicate = true;
            if (label.isEmpty() || label.size() > 32 || duplicate) { error(socket, QStringLiteral("群名为空、超过32字符或已存在")); return; }
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            groups_.insert(id, {label, {}});
        }
        if (!groups_.contains(id)) { error(socket, QStringLiteral("群组不存在")); return; }
        groups_[id].members.insert(name);
        wire::send(socket, {{"type", "group_joined"}, {"groupId", id}, {"name", groups_[id].name}});
        updateLists(); return;
    }
    if (type == "chat" || type == "file") {
        const auto id = m.value("groupId").toString("lobby");
        if (!groups_.contains(id) || !groups_[id].members.contains(name)) { error(socket, QStringLiteral("请先加入目标群组")); return; }
        QJsonObject out{{"type", type}, {"from", name}, {"groupId", id}, {"groupName", groups_[id].name}};
        if (type == "chat") {
            const auto text = m.value("text").toString().trimmed();
            if (text.isEmpty()) return;
            out.insert("text", text);
        } else {
            const auto fileName = QFileInfo(m.value("name").toString()).fileName();
            const auto encoded = m.value("data").toString().toLatin1();
            const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
            if (fileName.isEmpty() || !m.value("data").isString() || !decoded || decoded.decoded.size() > wire::MaxFile) {
                error(socket, QStringLiteral("文件无效或超过5 MiB")); return;
            }
            out.insert("name", fileName); out.insert("data", QString::fromLatin1(encoded));
            int count = 0;
            for (auto it = peers_.cbegin(); it != peers_.cend(); ++it)
                if (it.key() != socket && groups_[id].members.contains(it->nickname)) { wire::send(it.key(), out); ++count; }
            wire::send(socket, {{"type", "system"}, {"text", QStringLiteral("文件 %1 已转发给群“%2”的 %3 位其他成员；接收方可选择保存或取消").arg(fileName, groups_[id].name).arg(count)}});
            emit log(QStringLiteral("[%1] %2 发送文件 %3 (%4 字节)").arg(groups_[id].name, name, fileName).arg(decoded.decoded.size()));
            return;
        }
        broadcast(out, id);
        emit log(QStringLiteral("[%1] %2: %3").arg(groups_[id].name, name, out.value("text").toString()));
        return;
    }
    if (type == "private") {
        auto *target = findUser(m.value("to").toString().trimmed());
        const auto text = m.value("text").toString().trimmed();
        if (!target) { error(socket, QStringLiteral("目标用户不在线")); return; }
        if (text.isEmpty()) return;
        const QJsonObject out{{"type", "private"}, {"from", name}, {"to", peers_[target].nickname}, {"text", text}};
        wire::send(target, out); if (target != socket) wire::send(socket, out);
        return;
    }
    const auto id = m.value("callId").toString();
    if (type == "call_request") {
        auto *target = findUser(m.value("to").toString());
        QString reason;
        if (id.isEmpty() || id.size() > 64 || calls_.contains(id)) reason = QStringLiteral("呼叫编号无效");
        else if (!target) reason = QStringLiteral("用户不在线");
        else if (target == socket) reason = QStringLiteral("不能呼叫自己");
        else if (busy(socket) || busy(target)) reason = QStringLiteral("用户忙线");
        if (!reason.isEmpty()) { wire::send(socket, {{"type", "call_ended"}, {"callId", id}, {"reason", reason}}); return; }
        calls_.insert(id, {socket, target, false});
        wire::send(socket, {{"type", "call_ringing"}, {"callId", id}});
        wire::send(target, {{"type", "call_incoming"}, {"callId", id}, {"from", name}});
        QTimer::singleShot(ringTimeout_, this, [this, id] {
            if (calls_.contains(id) && !calls_[id].accepted) endCall(id, QStringLiteral("30秒未接听"));
        });
        return;
    }
    if (!calls_.contains(id)) return;
    auto &call = calls_[id];
    if (type == "call_response" && call.callee == socket && !call.accepted) {
        if (!m.value("accept").toBool()) { endCall(id, QStringLiteral("对方拒绝呼叫")); return; }
        const int port = m.value("port").toInt();
        if (port < 1 || port > 65535) { endCall(id, QStringLiteral("直连监听端口无效")); return; }
        call.accepted = true;
        wire::send(call.caller, {{"type", "call_accepted"}, {"callId", id},
            {"host", socket->peerAddress().toString()}, {"port", port}, {"from", name}});
        emit log(QStringLiteral("呼叫已接听：%1 → %2，直连端口 %3").arg(peers_[call.caller].nickname, name).arg(port));
    } else if ((type == "call_cancel" || type == "call_end") && (call.caller == socket || call.callee == socket)) {
        endCall(id, QStringLiteral("通话已结束或取消"));
    }
}
