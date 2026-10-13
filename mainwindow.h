#pragma once
#include "chatserver.h"
#include "directcall.h"
#include <QMainWindow>
#include <QPointer>
#include <QJsonArray>
class QComboBox; class QLineEdit; class QPushButton; class QLabel;
class QTextBrowser; class QListWidget; class QDialog; class QMessageBox;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void configure(bool server, const QString &host, quint16 port, const QString &nickname, bool start);
    void setDownloadDirectory(const QString &path) { downloads_ = path; }
    void sendFile(const QString &path);
private:
    enum class State { Idle, Connecting, LoggingIn, Online, Serving };
    State state_ = State::Idle;
    ChatServer server_;
    DirectCall direct_;
    QPointer<QTcpSocket> client_;
    QByteArray buffer_;
    QString nickname_, callId_, callPeer_, downloads_, pendingGroup_;
    bool incoming_ = false;
    QJsonArray groups_;
    QComboBox *role_, *group_;
    QLineEdit *nicknameEdit_, *host_, *port_, *message_;
    QPushButton *action_, *send_, *file_, *create_, *join_, *callButton_, *cancelCall_;
    QLabel *status_, *members_;
    QTextBrowser *log_;
    QListWidget *users_;
    QPointer<QMessageBox> incomingDialog_;
    QPointer<QDialog> callDialog_;
    QTextBrowser *callLog_ = nullptr;
    void updateUi();
    void appendLog(const QString &text);
    void startStop();
    void resetConnection(const QString &reason);
    void send(const QJsonObject &message);
    void readClient();
    void process(const QJsonObject &message);
    void refreshGroups();
    void sendText();
    void receiveFile(const QJsonObject &message);
    void beginCall();
    void finishCall(const QString &reason, bool notify = true);
    void showCallWindow();
};
