#include "mainwindow.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QtEndian>

namespace {
const int RoleClient = 0;
const int RoleServer = 1;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), server_(new QTcpServer(this))
{
    buildUi();
    setWindowTitle(QStringLiteral("TCP Socket 聊天室"));
    resize(900, 620);

    connect(server_, &QTcpServer::newConnection,
            this, &MainWindow::acceptPendingConnections);
}

void MainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);

    QHBoxLayout *connectionRow = new QHBoxLayout;
    roleCombo_ = new QComboBox(central);
    roleCombo_->addItem(QStringLiteral("客户端"));
    roleCombo_->addItem(QStringLiteral("服务器"));
    nicknameEdit_ = new QLineEdit(QStringLiteral("访客"), central);
    nicknameEdit_->setMaxLength(24);
    hostEdit_ = new QLineEdit(QStringLiteral("127.0.0.1"), central);
    hostEdit_->setMinimumWidth(145);
    portEdit_ = new QLineEdit(QStringLiteral("8888"), central);
    portEdit_->setMaximumWidth(75);
    actionButton_ = new QPushButton(QStringLiteral("连接服务器"), central);
    statusLabel_ = new QLabel(QStringLiteral("未连接"), central);

    connectionRow->addWidget(new QLabel(QStringLiteral("模式"), central));
    connectionRow->addWidget(roleCombo_);
    connectionRow->addWidget(new QLabel(QStringLiteral("昵称"), central));
    connectionRow->addWidget(nicknameEdit_);
    connectionRow->addWidget(new QLabel(QStringLiteral("服务器地址"), central));
    connectionRow->addWidget(hostEdit_);
    connectionRow->addWidget(new QLabel(QStringLiteral("端口"), central));
    connectionRow->addWidget(portEdit_);
    connectionRow->addWidget(actionButton_);
    connectionRow->addWidget(statusLabel_);
    root->addLayout(connectionRow);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, central);
    chatView_ = new QTextBrowser(splitter);
    chatView_->setOpenExternalLinks(false);
    userList_ = new QListWidget(splitter);
    userList_->setMinimumWidth(150);
    splitter->addWidget(chatView_);
    splitter->addWidget(userList_);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    QHBoxLayout *messageRow = new QHBoxLayout;
    messageEdit_ = new QLineEdit(central);
    messageEdit_->setPlaceholderText(QStringLiteral("输入消息；私聊格式：/w 昵称 消息"));
    sendButton_ = new QPushButton(QStringLiteral("发送"), central);
    fileButton_ = new QPushButton(QStringLiteral("发送文件"), central);
    messageRow->addWidget(messageEdit_, 1);
    messageRow->addWidget(sendButton_);
    messageRow->addWidget(fileButton_);
    root->addLayout(messageRow);

    QLabel *hint = new QLabel(
        QStringLiteral("聊天支持多人广播、私聊（/w 昵称 消息）和文件传输（单个文件最大 5 MB）。"), central);
    hint->setStyleSheet(QStringLiteral("color: #666;"));
    root->addWidget(hint);

    setCentralWidget(central);
    connect(roleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRoleChanged);
    connect(actionButton_, &QPushButton::clicked,
            this, &MainWindow::onStartStopClicked);
    connect(sendButton_, &QPushButton::clicked,
            this, &MainWindow::onSendClicked);
    connect(messageEdit_, &QLineEdit::returnPressed,
            this, &MainWindow::onSendClicked);
    connect(fileButton_, &QPushButton::clicked,
            this, &MainWindow::onChooseFileClicked);
    onRoleChanged(roleCombo_->currentIndex());
}

void MainWindow::onRoleChanged(int index)
{
    if (running_)
        return;
    const bool isServer = (index == RoleServer);
    hostEdit_->setEnabled(!isServer);
    actionButton_->setText(isServer ? QStringLiteral("启动服务器")
                                    : QStringLiteral("连接服务器"));
    statusLabel_->setText(QStringLiteral("未连接"));
}

void MainWindow::onStartStopClicked()
{
    if (running_) {
        if (roleCombo_->currentIndex() == RoleServer)
            stopServer();
        else
            disconnectFromServer();
        return;
    }

    if (roleCombo_->currentIndex() == RoleServer)
        startServer();
    else
        connectToServer();
}

void MainWindow::setRunningUi(bool running, const QString &status)
{
    running_ = running;
    statusLabel_->setText(status);
    roleCombo_->setEnabled(!running);
    nicknameEdit_->setEnabled(!running);
    hostEdit_->setEnabled(!running && roleCombo_->currentIndex() == RoleClient);
    portEdit_->setEnabled(!running);
    actionButton_->setText(running ? QStringLiteral("断开/停止")
                                   : (roleCombo_->currentIndex() == RoleServer
                                          ? QStringLiteral("启动服务器")
                                          : QStringLiteral("连接服务器")));
    sendButton_->setEnabled(running && roleCombo_->currentIndex() == RoleClient);
    fileButton_->setEnabled(running && roleCombo_->currentIndex() == RoleClient);
    messageEdit_->setEnabled(running && roleCombo_->currentIndex() == RoleClient);
}

void MainWindow::appendLog(const QString &text)
{
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    chatView_->append(QStringLiteral("<span style='color:#888'>[%1]</span> %2")
                          .arg(time, text.toHtmlEscaped()));
}

void MainWindow::startServer()
{
    bool ok = false;
    const uint port = portEdit_->text().toUInt(&ok);
    if (!ok || port == 0 || port > 65535) {
        QMessageBox::warning(this, QStringLiteral("端口无效"),
                             QStringLiteral("请输入 1 到 65535 之间的端口号。"));
        return;
    }
    if (!server_->listen(QHostAddress::AnyIPv4, static_cast<quint16>(port))) {
        QMessageBox::critical(this, QStringLiteral("启动失败"), server_->errorString());
        return;
    }
    appendLog(QStringLiteral("服务器已启动，监听端口 %1。其他设备请使用本机局域网 IP 连接。")
                  .arg(port));
    setRunningUi(true, QStringLiteral("服务器运行中"));
}

void MainWindow::stopServer()
{
    server_->close();
    const auto sockets = peers_.keys();
    for (QTcpSocket *socket : sockets) {
        sendFrame(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("system")},
                                      {QStringLiteral("text"), QStringLiteral("服务器正在关闭。")}});
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState)
            socket->waitForDisconnected(200);
    }
    for (QTcpSocket *socket : peers_.keys()) {
        peers_.remove(socket);
        socket->deleteLater();
    }
    userList_->clear();
    appendLog(QStringLiteral("服务器已停止。"));
    setRunningUi(false, QStringLiteral("未连接"));
}

void MainWindow::connectToServer()
{
    const QString nickname = nicknameEdit_->text().trimmed();
    if (nickname.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("昵称为空"),
                             QStringLiteral("请先输入昵称。"));
        nicknameEdit_->setFocus();
        return;
    }
    bool ok = false;
    const uint port = portEdit_->text().toUInt(&ok);
    if (!ok || port == 0 || port > 65535 || hostEdit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("地址无效"),
                             QStringLiteral("请检查服务器地址和端口号。"));
        return;
    }

    client_ = new QTcpSocket(this);
    connect(client_, &QTcpSocket::connected, this, [this]() {
        appendLog(QStringLiteral("已连接服务器，正在登录……"));
        setRunningUi(true, QStringLiteral("已连接"));
        sendClientMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("join")},
                                      {QStringLiteral("nickname"), nicknameEdit_->text().trimmed()}});
    });
    connect(client_, &QTcpSocket::readyRead, this, &MainWindow::readClientSocket);
    connect(client_, &QTcpSocket::disconnected, this, [this]() {
        appendLog(QStringLiteral("与服务器的连接已断开。"));
        clientBuffer_.clear();
        userList_->clear();
        client_ = nullptr;
        setRunningUi(false, QStringLiteral("未连接"));
    });
    connect(client_, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        if (client_)
            appendLog(QStringLiteral("网络错误：%1").arg(client_->errorString()));
    });

    appendLog(QStringLiteral("正在连接 %1:%2……")
                  .arg(hostEdit_->text().trimmed(), QString::number(port)));
    setRunningUi(true, QStringLiteral("连接中"));
    client_->connectToHost(hostEdit_->text().trimmed(), static_cast<quint16>(port));
}

void MainWindow::disconnectFromServer()
{
    if (client_)
        client_->disconnectFromHost();
}

void MainWindow::acceptPendingConnections()
{
    while (server_->hasPendingConnections()) {
        QTcpSocket *socket = server_->nextPendingConnection();
        peers_.insert(socket, Peer{});
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket]() { readServerSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, this,
                [this, socket]() { handlePeerDisconnected(socket); });
        appendLog(QStringLiteral("客户端已连接：%1:%2")
                      .arg(socket->peerAddress().toString())
                      .arg(socket->peerPort()));
    }
}

void MainWindow::readServerSocket(QTcpSocket *socket)
{
    if (!peers_.contains(socket))
        return;
    QByteArray buffer = peers_.value(socket).buffer;
    buffer.append(socket->readAll());
    handleFrame(socket, buffer, true);
    if (peers_.contains(socket))
        peers_[socket].buffer = buffer;
}

void MainWindow::readClientSocket()
{
    if (!client_)
        return;
    clientBuffer_.append(client_->readAll());
    handleFrame(client_, clientBuffer_, false);
}

void MainWindow::handleFrame(QTcpSocket *socket, QByteArray &buffer, bool serverSide)
{
    while (buffer.size() >= 4) {
        const quint32 frameSize = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(buffer.constData()));
        if (frameSize == 0 || frameSize > MaxFrameBytes) {
            appendLog(QStringLiteral("收到无效数据帧，连接将关闭。"));
            socket->disconnectFromHost();
            buffer.clear();
            return;
        }
        if (buffer.size() < static_cast<int>(frameSize + 4))
            return;

        const QByteArray payload = buffer.mid(4, static_cast<int>(frameSize));
        buffer.remove(0, static_cast<int>(frameSize + 4));
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            appendLog(QStringLiteral("收到格式错误的消息，已忽略。"));
            continue;
        }
        if (serverSide)
            processServerMessage(socket, document.object());
        else
            processClientMessage(document.object());
    }
}

void MainWindow::sendFrame(QTcpSocket *socket, const QJsonObject &message)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > static_cast<int>(MaxFrameBytes))
        return;
    const quint32 length = qToBigEndian<quint32>(static_cast<quint32>(payload.size()));
    QByteArray frame(reinterpret_cast<const char *>(&length), 4);
    frame.append(payload);
    socket->write(frame);
}

void MainWindow::sendClientMessage(const QJsonObject &message)
{
    sendFrame(client_, message);
}

void MainWindow::processServerMessage(QTcpSocket *socket, const QJsonObject &message)
{
    if (!peers_.contains(socket))
        return;
    Peer &peer = peers_[socket];
    const QString type = message.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("join")) {
        if (!peer.nickname.isEmpty())
            return;
        const QString nickname = message.value(QStringLiteral("nickname")).toString().trimmed();
        if (nickname.isEmpty() || nickname.size() > 24) {
            sendFrame(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("error")},
                                          {QStringLiteral("text"), QStringLiteral("昵称不能为空且不能超过 24 个字符。")}});
            socket->disconnectFromHost();
            return;
        }
        for (auto it = peers_.cbegin(); it != peers_.cend(); ++it) {
            if (it.key() != socket && !it->nickname.isEmpty()
                && it->nickname.compare(nickname, Qt::CaseInsensitive) == 0) {
                sendFrame(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("error")},
                                              {QStringLiteral("text"), QStringLiteral("这个昵称已经有人使用。")}});
                socket->disconnectFromHost();
                return;
            }
        }
        peer.nickname = nickname;
        appendLog(QStringLiteral("用户 %1 登录。此连接来自 %2")
                      .arg(nickname, socket->peerAddress().toString()));
        sendFrame(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("system")},
                                      {QStringLiteral("text"), QStringLiteral("登录成功，欢迎来到聊天室。")}});
        broadcastSystem(QStringLiteral("%1 加入了聊天室。 ").arg(nickname));
        sendUserList();
        return;
    }

    if (peer.nickname.isEmpty()) {
        sendFrame(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("error")},
                                      {QStringLiteral("text"), QStringLiteral("请先登录聊天室。")}});
        return;
    }

    if (type == QStringLiteral("chat")) {
        const QString text = message.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty())
            return;
        appendLog(QStringLiteral("%1: %2").arg(peer.nickname, text));
        broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("chat")},
                              {QStringLiteral("from"), peer.nickname},
                              {QStringLiteral("text"), text}});
    } else if (type == QStringLiteral("private")) {
        const QString recipient = message.value(QStringLiteral("to")).toString().trimmed();
        const QString text = message.value(QStringLiteral("text")).toString().trimmed();
        if (recipient.isEmpty() || text.isEmpty())
            return;
        QTcpSocket *destination = nullptr;
        for (auto it = peers_.cbegin(); it != peers_.cend(); ++it) {
            if (it->nickname.compare(recipient, Qt::CaseInsensitive) == 0) {
                destination = it.key();
                break;
            }
        }
        if (!destination) {
            sendFrame(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("error")},
                                          {QStringLiteral("text"), QStringLiteral("用户 %1 当前不在线。").arg(recipient)}});
            return;
        }
        appendLog(QStringLiteral("[private] %1 -> %2: %3").arg(peer.nickname, recipient, text));
        const QJsonObject outgoing{{QStringLiteral("type"), QStringLiteral("private")},
                                   {QStringLiteral("from"), peer.nickname},
                                   {QStringLiteral("to"), recipient},
                                   {QStringLiteral("text"), text}};
        sendFrame(destination, outgoing);
        if (destination != socket)
            sendFrame(socket, outgoing);
    } else if (type == QStringLiteral("file")) {
        const QString fileName = QFileInfo(message.value(QStringLiteral("name")).toString()).fileName();
        const QString data = message.value(QStringLiteral("data")).toString();
        if (fileName.isEmpty() || data.isEmpty() || data.size() > 7 * 1024 * 1024) {
            sendFrame(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("error")},
                                          {QStringLiteral("text"), QStringLiteral("文件消息无效或超过 5 MB。")}});
            return;
        }
        const QByteArray bytes = QByteArray::fromBase64(data.toLatin1());
        if (bytes.size() > MaxFileBytes) {
            sendFrame(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("error")},
                                          {QStringLiteral("text"), QStringLiteral("文件超过 5 MB，未发送。")}});
            return;
        }
        QJsonObject outgoing = message;
        outgoing.insert(QStringLiteral("from"), peer.nickname);
        outgoing.insert(QStringLiteral("name"), fileName);
        for (auto it = peers_.cbegin(); it != peers_.cend(); ++it) {
            if (it.key() != socket && !it->nickname.isEmpty())
                sendFrame(it.key(), outgoing);
        }
        appendLog(QStringLiteral("%1 向聊天室发送文件：%2 (%3 字节)")
                      .arg(peer.nickname, fileName).arg(bytes.size()));
    }
}

void MainWindow::processClientMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("system")) {
        appendLog(QStringLiteral("系统：%1").arg(message.value(QStringLiteral("text")).toString()));
    } else if (type == QStringLiteral("error")) {
        appendLog(QStringLiteral("错误：%1").arg(message.value(QStringLiteral("text")).toString()));
    } else if (type == QStringLiteral("chat")) {
        appendLog(QStringLiteral("%1：%2")
                      .arg(message.value(QStringLiteral("from")).toString(),
                           message.value(QStringLiteral("text")).toString()));
    } else if (type == QStringLiteral("private")) {
        appendLog(QStringLiteral("[私聊] %1：%2")
                      .arg(message.value(QStringLiteral("from")).toString(),
                           message.value(QStringLiteral("text")).toString()));
    } else if (type == QStringLiteral("file")) {
        saveIncomingFile(message);
    } else if (type == QStringLiteral("users")) {
        refreshUserList(message.value(QStringLiteral("users")).toArray());
    }
}

void MainWindow::broadcast(const QJsonObject &message)
{
    for (auto it = peers_.cbegin(); it != peers_.cend(); ++it) {
        if (!it->nickname.isEmpty())
            sendFrame(it.key(), message);
    }
}

void MainWindow::broadcastSystem(const QString &text)
{
    broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("system")},
                          {QStringLiteral("text"), text}});
}

void MainWindow::sendUserList()
{
    QJsonArray users;
    for (auto it = peers_.cbegin(); it != peers_.cend(); ++it) {
        if (!it->nickname.isEmpty())
            users.append(it->nickname);
    }
    refreshUserList(users);
    broadcast(QJsonObject{{QStringLiteral("type"), QStringLiteral("users")},
                          {QStringLiteral("users"), users}});
}

void MainWindow::refreshUserList(const QJsonArray &users)
{
    userList_->clear();
    for (const QJsonValue &value : users)
        userList_->addItem(value.toString());
}

void MainWindow::handlePeerDisconnected(QTcpSocket *socket)
{
    const QString nickname = peers_.value(socket).nickname;
    peers_.remove(socket);
    socket->deleteLater();
    if (!nickname.isEmpty()) {
        appendLog(QStringLiteral("用户 %1 离开了聊天室。 ").arg(nickname));
        broadcastSystem(QStringLiteral("%1 离开了聊天室。 ").arg(nickname));
        sendUserList();
    } else {
        appendLog(QStringLiteral("一个未登录客户端断开连接。"));
    }
}

void MainWindow::sendTextMessage()
{
    if (!client_ || client_->state() != QAbstractSocket::ConnectedState)
        return;
    const QString text = messageEdit_->text().trimmed();
    if (text.isEmpty())
        return;

    if (text.startsWith(QStringLiteral("/w "))) {
        const int recipientStart = 3;
        const int separator = text.indexOf(QRegularExpression(QStringLiteral("\\s+")), recipientStart);
        if (separator < 0) {
            appendLog(QStringLiteral("私聊格式：/w 昵称 消息"));
            return;
        }
        const QString recipient = text.mid(recipientStart, separator - recipientStart).trimmed();
        const QString body = text.mid(separator).trimmed();
        if (recipient.isEmpty() || body.isEmpty()) {
            appendLog(QStringLiteral("私聊格式：/w 昵称 消息"));
            return;
        }
        sendClientMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("private")},
                                      {QStringLiteral("to"), recipient},
                                      {QStringLiteral("text"), body}});
    } else {
        sendClientMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("chat")},
                                      {QStringLiteral("text"), text}});
    }
    messageEdit_->clear();
}

void MainWindow::onSendClicked()
{
    sendTextMessage();
}

void MainWindow::onChooseFileClicked()
{
    if (!client_ || client_->state() != QAbstractSocket::ConnectedState)
        return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择要发送的文件"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("读取失败"), file.errorString());
        return;
    }
    if (file.size() > MaxFileBytes) {
        QMessageBox::warning(this, QStringLiteral("文件过大"),
                             QStringLiteral("单个文件最大支持 5 MB。"));
        return;
    }
    const QByteArray bytes = file.readAll();
    sendClientMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("file")},
                                  {QStringLiteral("name"), QFileInfo(path).fileName()},
                                  {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())}});
    appendLog(QStringLiteral("已发送文件：%1").arg(QFileInfo(path).fileName()));
}

void MainWindow::saveIncomingFile(const QJsonObject &message)
{
    const QString sender = message.value(QStringLiteral("from")).toString();
    const QString name = QFileInfo(message.value(QStringLiteral("name")).toString()).fileName();
    const QByteArray data = QByteArray::fromBase64(
        message.value(QStringLiteral("data")).toString().toLatin1());
    if (name.isEmpty() || data.size() > MaxFileBytes) {
        appendLog(QStringLiteral("收到无效文件，已忽略。"));
        return;
    }
    const QString suggestedPath = QDir::home().filePath(name);
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存来自 %1 的文件").arg(sender), suggestedPath);
    if (path.isEmpty()) {
        appendLog(QStringLiteral("已忽略来自 %1 的文件：%2").arg(sender, name));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), file.errorString());
        return;
    }
    appendLog(QStringLiteral("已保存来自 %1 的文件：%2").arg(sender, path));
}