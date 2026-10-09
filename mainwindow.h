#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QHash>
#include <QMainWindow>
#include <QTcpServer>
#include <QTcpSocket>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QJsonObject;
class QJsonArray;
class QTextBrowser;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onRoleChanged(int index);
    void onStartStopClicked();
    void onSendClicked();
    void onChooseFileClicked();

private:
    struct Peer {
        QByteArray buffer;
        QString nickname;
    };

    static constexpr quint32 MaxFrameBytes = 10 * 1024 * 1024;
    static constexpr qint64 MaxFileBytes = 5 * 1024 * 1024;

    QTcpServer *server_ = nullptr;
    QTcpSocket *client_ = nullptr;
    QHash<QTcpSocket *, Peer> peers_;
    QByteArray clientBuffer_;
    bool running_ = false;

    QComboBox *roleCombo_ = nullptr;
    QLineEdit *nicknameEdit_ = nullptr;
    QLineEdit *hostEdit_ = nullptr;
    QLineEdit *portEdit_ = nullptr;
    QPushButton *actionButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QTextBrowser *chatView_ = nullptr;
    QListWidget *userList_ = nullptr;
    QLineEdit *messageEdit_ = nullptr;
    QPushButton *sendButton_ = nullptr;
    QPushButton *fileButton_ = nullptr;

    void buildUi();
    void setRunningUi(bool running, const QString &status);
    void appendLog(const QString &text);
    void startServer();
    void stopServer();
    void connectToServer();
    void disconnectFromServer();
    void acceptPendingConnections();
    void readServerSocket(QTcpSocket *socket);
    void readClientSocket();
    void processServerMessage(QTcpSocket *socket, const QJsonObject &message);
    void processClientMessage(const QJsonObject &message);
    void handleFrame(QTcpSocket *socket, QByteArray &buffer, bool serverSide);
    void sendFrame(QTcpSocket *socket, const QJsonObject &message);
    void sendClientMessage(const QJsonObject &message);
    void broadcast(const QJsonObject &message);
    void broadcastSystem(const QString &text);
    void sendUserList();
    void refreshUserList(const QJsonArray &users);
    void handlePeerDisconnected(QTcpSocket *socket);
    void sendTextMessage();
    void saveIncomingFile(const QJsonObject &message);
};

#endif // MAINWINDOW_H