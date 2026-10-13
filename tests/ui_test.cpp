#include "../mainwindow.h"
#include <QtTest>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QTextBrowser>
#include <QMessageBox>
#include <QInputDialog>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

class UiTest : public QObject {
    Q_OBJECT
    template<class T> T *widget(QObject &window, const char *name) { return window.findChild<T *>(name); }
    void snapshot(QWidget &window, const QString &name) {
        const auto folder = qEnvironmentVariable("SOCKET_EVIDENCE_DIR");
        if (folder.isEmpty()) return;
        QDir().mkpath(folder); QVERIFY(window.grab().save(QDir(folder).filePath(name + ".png")));
    }
    void say(MainWindow &window, const QString &text) {
        widget<QLineEdit>(window, "message")->setText(text);
        QTest::mouseClick(widget<QPushButton>(window, "send"), Qt::LeftButton);
    }
    QString log(MainWindow &window) { return widget<QTextBrowser>(window, "chatLog")->toPlainText(); }
    bool selectUser(MainWindow &window, const QString &user) {
        auto *list = widget<QListWidget>(window, "users"); const auto found = list->findItems(user, Qt::MatchExactly); if (found.isEmpty()) return false; list->setCurrentItem(found[0]); return true;
    }
private slots:
    void disconnectedCannotSend() {
        MainWindow window; auto *button = widget<QPushButton>(window, "send"); QVERIFY(button); QVERIFY(!button->isEnabled());
    }
    void failedConnectionCanRetry() {
        QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost, 0)); const auto port = probe.serverPort(); probe.close();
        MainWindow window; window.show(); widget<QLineEdit>(window, "port")->setText(QString::number(port));
        auto *action = widget<QPushButton>(window, "action"); QTest::mouseClick(action, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(action->text(), QStringLiteral("连接服务器"), 5000);
        QVERIFY(widget<QLineEdit>(window, "port")->isEnabled());
        ChatServer server; QVERIFY(server.start(port, QHostAddress::LocalHost));
        QTest::mouseClick(action, Qt::LeftButton); QTRY_COMPARE(widget<QLabel>(window, "status")->text(), QStringLiteral("已登录"));
        QTest::mouseClick(action, Qt::LeftButton); QTRY_COMPARE(action->text(), QStringLiteral("连接服务器"));
        QTest::mouseClick(action, Qt::LeftButton); QTRY_COMPARE(widget<QLabel>(window, "status")->text(), QStringLiteral("已登录"));
    }
    void completeGuiScenario() {
        QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost, 0)); const auto port = probe.serverPort(); probe.close();
        const auto demoPort = qEnvironmentVariableIntValue("SOCKET_DEMO_PORT");
        const quint16 testPort = demoPort > 0 ? quint16(demoPort) : port;
        MainWindow server, alice, bob, charlie;
        QProcess callCapture;
        bool captured = false;
        connect(server.findChild<ChatServer *>(), &ChatServer::log, &server, [&](const QString &text) {
            const auto tool = qEnvironmentVariable("SOCKET_DUMPCAP");
            if (tool.isEmpty() || captured || !text.startsWith(QStringLiteral("呼叫已接听："))) return;
            const auto match = QRegularExpression(QStringLiteral("端口 (\\d+)$")).match(text);
            if (!match.hasMatch()) return;
            captured = true;
            callCapture.setProcessChannelMode(QProcess::MergedChannels);
            callCapture.start(tool, {"-i", "\\Device\\NPF_Loopback", "-f", "tcp port " + match.captured(1), "-a", "duration:8", "-w", QDir(qEnvironmentVariable("SOCKET_EVIDENCE_DIR")).filePath("direct_call.pcapng")});
            QVERIFY(callCapture.waitForStarted(3000));
            QByteArray output;
            for (int i = 0; i < 8 && !output.contains("Capturing on"); ++i) { callCapture.waitForReadyRead(500); output += callCapture.readAll(); }
            QVERIFY2(output.contains("Capturing on"), output.constData());
        });
        server.configure(true, "127.0.0.1", testPort, "Server", true);
        alice.configure(false, "127.0.0.1", testPort, "Alice", true);
        bob.configure(false, "127.0.0.1", testPort, "Bob", true);
        charlie.configure(false, "127.0.0.1", testPort, "Charlie", true);
        server.show(); alice.show(); bob.show(); charlie.show();
        QTRY_COMPARE(widget<QLabel>(alice, "status")->text(), QStringLiteral("已登录"));
        QTRY_COMPARE(widget<QLabel>(bob, "status")->text(), QStringLiteral("已登录"));
        QTRY_COMPARE(widget<QLabel>(charlie, "status")->text(), QStringLiteral("已登录"));
        QTRY_COMPARE(widget<QListWidget>(alice, "users")->count(), 3);
        say(alice, QStringLiteral("大家好，这是公共大厅的TCP群聊。"));
        QTRY_VERIFY(log(charlie).contains(QStringLiteral("公共大厅的TCP群聊")));
        say(alice, QStringLiteral("/w Bob 这条私聊只应由Alice和Bob看到。"));
        QTRY_VERIFY(log(bob).contains(QStringLiteral("这条私聊只应")));
        QVERIFY(!log(charlie).contains(QStringLiteral("这条私聊只应")));
        snapshot(charlie, "01_lobby");
        QTimer::singleShot(0, &alice, [&alice] {
            auto *dialog = alice.findChild<QInputDialog *>(); if (dialog) { dialog->setTextValue(QStringLiteral("实验讨论组")); dialog->accept(); }
        });
        QTest::mouseClick(widget<QPushButton>(alice, "createGroup"), Qt::LeftButton);
        QTRY_COMPARE(widget<QComboBox>(alice, "group")->count(), 2);
        const auto groupId = widget<QComboBox>(alice, "group")->currentData().toString(); QVERIFY(groupId != "lobby");
        auto *bobGroup = widget<QComboBox>(bob, "group"); QTRY_VERIFY(bobGroup->findData(groupId) >= 0); bobGroup->setCurrentIndex(bobGroup->findData(groupId));
        QVERIFY(!widget<QPushButton>(bob, "send")->isEnabled());
        QTest::mouseClick(widget<QPushButton>(bob, "joinGroup"), Qt::LeftButton);
        QTRY_VERIFY(widget<QPushButton>(bob, "send")->isEnabled());
        say(alice, QStringLiteral("独立群消息：仅Alice与Bob可见。"));
        QTRY_VERIFY(log(bob).contains(QStringLiteral("独立群消息"))); QVERIFY(!log(charlie).contains(QStringLiteral("独立群消息")));
        QTemporaryDir temporary; QVERIFY(temporary.isValid()); bob.setDownloadDirectory(temporary.path() + "/received");
        const auto source = temporary.path() + QStringLiteral("/中文文件.bin"); QFile f(source); QVERIFY(f.open(QIODevice::WriteOnly)); QByteArray bytes("\x00\xff\x42\n", 4); QCOMPARE(f.write(bytes), qint64(4)); f.close();
        alice.sendFile(source); const auto destination = temporary.path() + QStringLiteral("/received/中文文件.bin");
        QTRY_VERIFY(QFile::exists(destination)); QFile received(destination); QVERIFY(received.open(QIODevice::ReadOnly)); QCOMPARE(received.readAll(), bytes);
        const auto empty = temporary.path() + "/empty.txt"; QFile e(empty); QVERIFY(e.open(QIODevice::WriteOnly)); e.close(); alice.sendFile(empty);
        QTRY_VERIFY(QFile::exists(temporary.path() + "/received/empty.txt")); QCOMPARE(QFileInfo(temporary.path() + "/received/empty.txt").size(), qint64(0));
        snapshot(alice, "02_groups"); snapshot(bob, "03_files");
        QVERIFY(selectUser(alice, "Bob")); QTest::mouseClick(widget<QPushButton>(alice, "call"), Qt::LeftButton);
        QTRY_VERIFY(widget<QMessageBox>(bob, "incomingCall"));
        auto *incoming = widget<QMessageBox>(bob, "incomingCall"); snapshot(*incoming, "04_incoming_call");
        QTest::mouseClick(incoming->button(QMessageBox::Yes), Qt::LeftButton);
        QTRY_VERIFY(widget<QDialog>(alice, "directCallWindow")); QTRY_VERIFY(widget<QDialog>(bob, "directCallWindow"));
        auto *aliceCall = widget<QDialog>(alice, "directCallWindow"); auto *bobCall = widget<QDialog>(bob, "directCallWindow");
        widget<QLineEdit>(*aliceCall, "directMessage")->setText(QStringLiteral("这条文字通过客户端之间的TCP连接发送。")); QTest::mouseClick(widget<QPushButton>(*aliceCall, "directSend"), Qt::LeftButton);
        QTRY_VERIFY(widget<QTextBrowser>(*bobCall, "directLog")->toPlainText().contains(QStringLiteral("客户端之间的TCP")));
        widget<QLineEdit>(*bobCall, "directMessage")->setText(QStringLiteral("已收到，直连双向通信成功。")); QTest::mouseClick(widget<QPushButton>(*bobCall, "directSend"), Qt::LeftButton);
        QTRY_VERIFY(widget<QTextBrowser>(*aliceCall, "directLog")->toPlainText().contains(QStringLiteral("双向通信成功")));
        snapshot(*aliceCall, "05_direct_call"); snapshot(server, "06_server");
        QTest::mouseClick(widget<QPushButton>(alice, "endCall"), Qt::LeftButton);
        QTRY_VERIFY(!widget<QPushButton>(bob, "endCall")->isEnabled());
        say(bob, QStringLiteral("通话结束后，群聊仍然正常。")); QTRY_VERIFY(log(alice).contains(QStringLiteral("群聊仍然正常")));
        // Reject once, then cancel once; a late dialog completion must not revive the call.
        QTest::mouseClick(widget<QPushButton>(alice, "call"), Qt::LeftButton); QTRY_VERIFY(widget<QMessageBox>(bob, "incomingCall"));
        QTest::mouseClick(widget<QMessageBox>(bob, "incomingCall")->button(QMessageBox::No), Qt::LeftButton); QTRY_VERIFY(widget<QPushButton>(alice, "call")->isEnabled());
        QTest::mouseClick(widget<QPushButton>(alice, "call"), Qt::LeftButton); QTRY_VERIFY(widget<QMessageBox>(bob, "incomingCall"));
        QTest::mouseClick(widget<QPushButton>(alice, "endCall"), Qt::LeftButton); QTRY_VERIFY(widget<QPushButton>(bob, "call")->isEnabled());
        QTest::mouseClick(widget<QPushButton>(charlie, "action"), Qt::LeftButton); QTRY_COMPARE(widget<QListWidget>(alice, "users")->count(), 2);
        if (captured) { QVERIFY(callCapture.waitForFinished(12000)); QCOMPARE(callCapture.exitCode(), 0); }
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
