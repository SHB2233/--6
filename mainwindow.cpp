#include "mainwindow.h"
#include "wire.h"
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), server_(this), direct_(this) {
    setWindowTitle(QStringLiteral("实验6 · TCP Socket 聊天室")); resize(1000, 700);
    auto *central = new QWidget(this); auto *root = new QVBoxLayout(central);
    auto *row = new QHBoxLayout;
    role_ = new QComboBox; role_->setObjectName("role"); role_->addItems({QStringLiteral("客户端"), QStringLiteral("服务器")});
    nicknameEdit_ = new QLineEdit(QStringLiteral("访客")); nicknameEdit_->setObjectName("nickname"); nicknameEdit_->setMaxLength(24); nicknameEdit_->setToolTip(QStringLiteral("昵称为1至24字符，不含空白"));
    host_ = new QLineEdit("127.0.0.1"); host_->setObjectName("host");
    port_ = new QLineEdit("8889"); port_->setObjectName("port"); port_->setMaximumWidth(70);
    action_ = new QPushButton; action_->setObjectName("action");
    status_ = new QLabel; status_->setObjectName("status");
    row->addWidget(role_); row->addWidget(new QLabel(QStringLiteral("昵称"))); row->addWidget(nicknameEdit_);
    row->addWidget(new QLabel(QStringLiteral("服务器"))); row->addWidget(host_); row->addWidget(port_); row->addWidget(action_); row->addWidget(status_);
    root->addLayout(row);
    auto *split = new QSplitter; log_ = new QTextBrowser; log_->setObjectName("chatLog"); log_->setOpenExternalLinks(false);
    users_ = new QListWidget; users_->setObjectName("users"); users_->setMinimumWidth(160); split->addWidget(log_); split->addWidget(users_); split->setStretchFactor(0, 4); split->setStretchFactor(1, 1); root->addWidget(split, 1);
    auto *messageRow = new QHBoxLayout;
    message_ = new QLineEdit; message_->setObjectName("message"); message_->setPlaceholderText(QStringLiteral("群聊文字；私聊：/w 昵称 消息，或选择右侧用户后呼叫"));
    send_ = new QPushButton(QStringLiteral("发送")); send_->setObjectName("send");
    file_ = new QPushButton(QStringLiteral("发送文件")); file_->setObjectName("file");
    messageRow->addWidget(message_, 1); messageRow->addWidget(send_); messageRow->addWidget(file_);
    auto *groupRow = new QHBoxLayout;
    group_ = new QComboBox; group_->setObjectName("group"); group_->setMinimumWidth(200);
    create_ = new QPushButton(QStringLiteral("创建群组")); create_->setObjectName("createGroup");
    join_ = new QPushButton(QStringLiteral("加入群组")); join_->setObjectName("joinGroup");
    members_ = new QLabel; members_->setObjectName("members"); members_->setWordWrap(true);
    groupRow->addWidget(new QLabel(QStringLiteral("当前群"))); groupRow->addWidget(group_); groupRow->addWidget(create_); groupRow->addWidget(join_); groupRow->addWidget(members_, 1);
    root->addLayout(groupRow); root->addLayout(messageRow);
    auto *callRow = new QHBoxLayout;
    callButton_ = new QPushButton(QStringLiteral("呼叫选中用户")); callButton_->setObjectName("call");
    cancelCall_ = new QPushButton(QStringLiteral("取消／结束呼叫")); cancelCall_->setObjectName("endCall");
    callRow->addWidget(callButton_); callRow->addWidget(cancelCall_);
    callRow->addWidget(new QLabel(QStringLiteral("呼叫接听后建立独立 TCP 文字会话 · 文件 ≤ 5 MiB")), 1); root->addLayout(callRow);
    setCentralWidget(central);
    setStyleSheet(QStringLiteral("QMainWindow{background:#f4f7fb} QPushButton{padding:7px 12px} QLineEdit,QComboBox{padding:6px} QTextBrowser,QListWidget{background:white;border:1px solid #d5dce7} QLabel{color:#243449}"));
    connect(role_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateUi(); });
    connect(action_, &QPushButton::clicked, this, &MainWindow::startStop);
    connect(send_, &QPushButton::clicked, this, &MainWindow::sendText);
    connect(message_, &QLineEdit::returnPressed, this, &MainWindow::sendText);
    connect(file_, &QPushButton::clicked, this, [this] { const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择文件")); if (!path.isEmpty()) sendFile(path); });
    connect(create_, &QPushButton::clicked, this, [this] {
        bool ok; const auto name = QInputDialog::getText(this, QStringLiteral("创建群组"), QStringLiteral("群名称（最多32字符）"), QLineEdit::Normal, {}, &ok);
        if (ok) send({{"type", "group_create"}, {"name", name}});
    });
    connect(join_, &QPushButton::clicked, this, [this] { send({{"type", "group_join"}, {"groupId", group_->currentData().toString()}}); });
    connect(group_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateUi(); });
    connect(callButton_, &QPushButton::clicked, this, &MainWindow::beginCall);
    connect(cancelCall_, &QPushButton::clicked, this, [this] { finishCall(QStringLiteral("本方结束或取消")); });
    connect(&server_, &ChatServer::log, this, &MainWindow::appendLog);
    connect(&server_, &ChatServer::usersChanged, this, [this](const QStringList &users) { users_->clear(); users_->addItems(users); });
    connect(&direct_, &DirectCall::connected, this, [this] { if (!callId_.isEmpty()) { appendLog(QStringLiteral("与 %1 的独立TCP连接已建立").arg(callPeer_)); showCallWindow(); } });
    connect(&direct_, &DirectCall::textReceived, this, [this](const QString &text) { if (callLog_) callLog_->append(callPeer_.toHtmlEscaped() + ": " + text.toHtmlEscaped()); });
    connect(&direct_, &DirectCall::errorOccurred, this, [this](const QString &reason) { appendLog(QStringLiteral("直连错误：") + reason); });
    connect(&direct_, &DirectCall::ended, this, [this](const QString &reason) { if (!callId_.isEmpty()) finishCall(reason); });
    updateUi();
}
MainWindow::~MainWindow() { resetConnection({}); server_.stop(); }
void MainWindow::configure(bool serving, const QString &host, quint16 port, const QString &name, bool start) {
    role_->setCurrentIndex(serving ? 1 : 0); host_->setText(host); port_->setText(QString::number(port)); nicknameEdit_->setText(name); if (start) startStop();
}
void MainWindow::appendLog(const QString &text) {
    log_->append(QStringLiteral("<span style='color:#7d8a9e'>[%1]</span> %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), text.toHtmlEscaped()));
}
void MainWindow::updateUi() {
    const bool idle = state_ == State::Idle, online = state_ == State::Online, serving = role_->currentIndex() == 1;
    role_->setEnabled(idle); nicknameEdit_->setEnabled(idle && !serving); host_->setEnabled(idle && !serving); port_->setEnabled(idle);
    action_->setText(idle ? (serving ? QStringLiteral("启动服务器") : QStringLiteral("连接服务器")) : QStringLiteral("断开/停止"));
    QString status;
    switch(state_) { case State::Idle: status = QStringLiteral("未连接"); break; case State::Connecting: status = QStringLiteral("连接中"); break; case State::LoggingIn: status = QStringLiteral("登录中"); break; case State::Online: status = QStringLiteral("已登录"); break; case State::Serving: status = QStringLiteral("服务器运行中"); break; }
    status_->setText(status);
    bool joined = false; QStringList members;
    for (const auto &entry : groups_) { const auto g = entry.toObject(); if (g.value("id").toString() == group_->currentData().toString()) { joined = g.value("joined").toBool(); for (const auto &n : g.value("members").toArray()) members << n.toString(); } }
    members_->setText(QStringLiteral("成员：") + members.join(", "));
    send_->setEnabled(online && joined); message_->setEnabled(online); file_->setEnabled(online && joined);
    group_->setEnabled(online); create_->setEnabled(online); join_->setEnabled(online && !joined && group_->count());
    callButton_->setEnabled(online && callId_.isEmpty()); cancelCall_->setEnabled(online && !callId_.isEmpty());
}
void MainWindow::startStop() {
    if (state_ == State::Serving) { server_.stop(); state_ = State::Idle; updateUi(); return; }
    if (state_ != State::Idle) { resetConnection(QStringLiteral("本方断开连接")); return; }
    bool ok = false; const auto p = port_->text().toUInt(&ok);
    if (!ok || !p || p > 65535) { appendLog(QStringLiteral("端口必须为1到65535")); return; }
    if (role_->currentIndex() == 1) {
        if (!server_.start(quint16(p))) { appendLog(server_.errorString()); return; }
        state_ = State::Serving; updateUi(); return;
    }
    nickname_ = nicknameEdit_->text().trimmed();
    for (const auto ch : nickname_) if (ch.isSpace()) { appendLog(QStringLiteral("昵称不能包含空白字符")); return; }
    if (nickname_.isEmpty() || host_->text().trimmed().isEmpty()) { appendLog(QStringLiteral("请输入昵称与服务器地址")); return; }
    auto *socket = new QTcpSocket(this); client_ = socket; buffer_.clear(); state_ = State::Connecting; updateUi();
    connect(socket, &QTcpSocket::connected, this, [this, socket] { if (client_ != socket) return; state_ = State::LoggingIn; updateUi(); send({{"type", "join"}, {"nickname", nickname_}}); });
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] { if (client_ == socket) readClient(); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] { if (client_ == socket) resetConnection(QStringLiteral("与服务器连接已断开")); });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) { if (client_ == socket) resetConnection(QStringLiteral("网络错误：") + socket->errorString()); });
    QPointer<QTcpSocket> guard(socket);
    QTimer::singleShot(10000, this, [this, guard] { if (guard && client_ == guard && (state_ == State::Connecting || state_ == State::LoggingIn)) resetConnection(QStringLiteral("连接或登录超时")); });
    socket->connectToHost(host_->text().trimmed(), quint16(p));
}
void MainWindow::resetConnection(const QString &reason) {
    finishCall(QStringLiteral("服务器连接断开"));
    if (client_) { auto *socket = client_.data(); client_.clear(); disconnect(socket, nullptr, this, nullptr); socket->abort(); socket->deleteLater(); }
    buffer_.clear(); groups_ = {}; group_->clear(); users_->clear(); state_ = State::Idle; updateUi();
    if (!reason.isEmpty()) appendLog(reason);
}
void MainWindow::send(const QJsonObject &message) { if (!wire::send(client_, message)) appendLog(QStringLiteral("消息未发送：连接不可用或消息过大")); }
void MainWindow::readClient() {
    if (!client_) return; buffer_ += client_->readAll();
    while (client_) { QJsonObject m; const int result = wire::take(buffer_, m); if (!result) return; if (result < 0) { resetConnection(QStringLiteral("服务器数据帧无效")); return; } process(m); }
}
void MainWindow::refreshGroups() {
    const auto previous = pendingGroup_.isEmpty() ? group_->currentData().toString() : pendingGroup_;
    group_->blockSignals(true); group_->clear();
    for (const auto &entry : groups_) { const auto g = entry.toObject(); group_->addItem(g.value("name").toString() + (g.value("joined").toBool() ? QStringLiteral("（已加入）") : QStringLiteral("（未加入）")), g.value("id").toString()); }
    int index = group_->findData(previous); if (index < 0) index = group_->findData("lobby"); group_->setCurrentIndex(index); group_->blockSignals(false); pendingGroup_.clear(); updateUi();
}
void MainWindow::process(const QJsonObject &m) {
    const auto type = m.value("type").toString();
    if (type == "join_ok") { nickname_ = m.value("nickname").toString(); state_ = State::Online; appendLog(QStringLiteral("登录成功：") + nickname_); updateUi(); }
    else if (type == "system" || type == "error") appendLog((type == "error" ? QStringLiteral("错误：") : QStringLiteral("系统：")) + m.value("text").toString());
    else if (type == "users") { const auto selected = users_->currentItem() ? users_->currentItem()->text() : QString(); users_->clear(); for (const auto &n : m.value("users").toArray()) users_->addItem(n.toString()); const auto matches = users_->findItems(selected, Qt::MatchExactly); if (!matches.isEmpty()) users_->setCurrentItem(matches[0]); }
    else if (type == "groups") { groups_ = m.value("groups").toArray(); refreshGroups(); }
    else if (type == "group_joined") { pendingGroup_ = m.value("groupId").toString(); appendLog(QStringLiteral("已加入群组：") + m.value("name").toString()); }
    else if (type == "chat") appendLog(QStringLiteral("[%1] %2：%3").arg(m.value("groupName").toString(), m.value("from").toString(), m.value("text").toString()));
    else if (type == "private") appendLog(QStringLiteral("[私聊 %1 → %2] %3").arg(m.value("from").toString(), m.value("to").toString(), m.value("text").toString()));
    else if (type == "file") receiveFile(m);
    else if (type == "call_incoming") {
        const auto id = m.value("callId").toString();
        if (!callId_.isEmpty()) { send({{"type", "call_response"}, {"callId", id}, {"accept", false}}); return; }
        callId_ = id; callPeer_ = m.value("from").toString(); incoming_ = true; updateUi();
        auto *dialog = new QMessageBox(QMessageBox::Question, QStringLiteral("TCP文字呼叫"), callPeer_ + QStringLiteral(" 邀请建立直接TCP文字会话"), QMessageBox::Yes | QMessageBox::No, this);
        dialog->setObjectName("incomingCall"); dialog->setButtonText(QMessageBox::Yes, QStringLiteral("接听")); dialog->setButtonText(QMessageBox::No, QStringLiteral("拒绝")); incomingDialog_ = dialog;
        connect(dialog, &QMessageBox::finished, this, [this, id, dialog](int result) {
            dialog->deleteLater(); if (incomingDialog_ == dialog) incomingDialog_.clear(); if (callId_ != id) return;
            const auto port = result == QMessageBox::Yes ? direct_.startListening(id, callPeer_) : 0;
            send({{"type", "call_response"}, {"callId", id}, {"accept", port != 0}, {"port", int(port)}});
            if (!port) finishCall(QStringLiteral("已拒绝或监听失败"), false); else appendLog(QStringLiteral("已接听，等待直连握手"));
        }); dialog->open();
    } else if (type == "call_accepted" && m.value("callId").toString() == callId_ && !incoming_) {
        direct_.connectToPeer(callId_, callPeer_, m.value("host").toString(), quint16(m.value("port").toInt()));
    } else if (type == "call_ended" && m.value("callId").toString() == callId_) finishCall(m.value("reason").toString(), false);
}
void MainWindow::sendText() {
    if (state_ != State::Online) return;
    const auto text = message_->text().trimmed(); if (text.isEmpty()) return;
    if (text.startsWith("/w ")) {
        const auto match = QRegularExpression(QStringLiteral("^/w\\s+(\\S+)\\s+(.+)$")).match(text);
        if (!match.hasMatch()) { appendLog(QStringLiteral("私聊格式：/w 昵称 消息")); return; }
        send({{"type", "private"}, {"to", match.captured(1)}, {"text", match.captured(2)}});
    } else send({{"type", "chat"}, {"groupId", group_->currentData().toString()}, {"text", text}});
    message_->clear();
}
void MainWindow::sendFile(const QString &path) {
    if (state_ != State::Online) return;
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { appendLog(file.errorString()); return; }
    if (file.size() > wire::MaxFile) { appendLog(QStringLiteral("文件超过5 MiB，未发送")); return; }
    const auto data = file.readAll(); if (file.error() != QFileDevice::NoError || data.size() > wire::MaxFile) { appendLog(QStringLiteral("文件读取失败或文件过大")); return; }
    send({{"type", "file"}, {"groupId", group_->currentData().toString()}, {"name", QFileInfo(path).fileName()}, {"data", QString::fromLatin1(data.toBase64())}});
}
void MainWindow::receiveFile(const QJsonObject &m) {
    const auto name = QFileInfo(m.value("name").toString()).fileName();
    const auto result = QByteArray::fromBase64Encoding(m.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (name.isEmpty() || name == "." || name == ".." || !result || result.decoded.size() > wire::MaxFile) { appendLog(QStringLiteral("收到无效文件")); return; }
    const auto context = QStringLiteral("[%1] 来自 %2 的文件 %3").arg(m.value("groupName").toString(), m.value("from").toString(), name);
    QString path;
    if (downloads_.isEmpty()) path = QFileDialog::getSaveFileName(this, context, QDir::home().filePath(name));
    else { QDir().mkpath(downloads_); path = QDir(downloads_).filePath(name); for (int n = 1; QFileInfo::exists(path); ++n) path = QDir(downloads_).filePath(QString::number(n) + "_" + name); }
    if (path.isEmpty()) { appendLog(context + QStringLiteral("：已取消保存")); return; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(result.decoded) != result.decoded.size() || !file.commit()) { appendLog(context + QStringLiteral("：保存失败，") + file.errorString()); return; }
    appendLog(context + QStringLiteral("：已保存到 ") + path);
}
void MainWindow::beginCall() {
    if (state_ != State::Online || !callId_.isEmpty() || !users_->currentItem()) return;
    callPeer_ = users_->currentItem()->text(); if (callPeer_ == nickname_) { appendLog(QStringLiteral("请选择其他在线用户")); return; }
    callId_ = QUuid::createUuid().toString(QUuid::WithoutBraces); incoming_ = false;
    send({{"type", "call_request"}, {"callId", callId_}, {"to", callPeer_}}); appendLog(QStringLiteral("正在呼叫 %1，等待接听…").arg(callPeer_)); updateUi();
}
void MainWindow::finishCall(const QString &reason, bool notify) {
    if (callId_.isEmpty()) return;
    const auto id = callId_; callId_.clear();
    if (notify && client_) send({{"type", "call_end"}, {"callId", id}});
    if (incomingDialog_) { auto dialog = incomingDialog_; incomingDialog_.clear(); dialog->close(); dialog->deleteLater(); }
    direct_.end();
    if (callDialog_) { auto dialog = callDialog_; callDialog_.clear(); dialog->close(); dialog->deleteLater(); } callLog_ = nullptr;
    appendLog(QStringLiteral("呼叫结束：") + reason); callPeer_.clear(); updateUi();
}
void MainWindow::showCallWindow() {
    if (callDialog_) return;
    auto *dialog = new QDialog(this); dialog->setObjectName("directCallWindow"); dialog->setWindowTitle(QStringLiteral("独立TCP文字会话 · ") + callPeer_); dialog->resize(580, 400); callDialog_ = dialog;
    auto *layout = new QVBoxLayout(dialog); layout->addWidget(new QLabel(QStringLiteral("已建立客户端间TCP连接；此窗口消息不经聊天室服务器转发")));
    callLog_ = new QTextBrowser; callLog_->setObjectName("directLog"); layout->addWidget(callLog_, 1);
    auto *row = new QHBoxLayout; auto *edit = new QLineEdit; edit->setObjectName("directMessage"); auto *button = new QPushButton(QStringLiteral("发送")); button->setObjectName("directSend"); row->addWidget(edit, 1); row->addWidget(button); layout->addLayout(row);
    auto transmit = [this, edit] { const auto text = edit->text().trimmed(); if (text.isEmpty() || !direct_.isConnected()) return; direct_.sendText(text); if (callLog_) callLog_->append(QStringLiteral("我：") + text.toHtmlEscaped()); edit->clear(); };
    connect(button, &QPushButton::clicked, this, transmit); connect(edit, &QLineEdit::returnPressed, this, transmit);
    connect(dialog, &QDialog::finished, this, [this] { if (!callId_.isEmpty()) finishCall(QStringLiteral("关闭通话窗口")); });
    dialog->show();
}
