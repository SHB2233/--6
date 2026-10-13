#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("QtTcpChat"); app.setApplicationVersion("1.0");
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("实验6：TCP聊天室与客户端直接TCP文字呼叫")); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption(QCommandLineOption("server", "Start graphical server"));
    parser.addOption(QCommandLineOption("headless", "Start server without a window"));
    parser.addOption(QCommandLineOption("connect", "Connect client on startup"));
    parser.addOption(QCommandLineOption("host", "Server IPv4 or hostname", "host", "127.0.0.1"));
    parser.addOption(QCommandLineOption("port", "TCP port", "port", "8889"));
    parser.addOption(QCommandLineOption("nickname", "Client nickname", "nickname", QStringLiteral("访客")));
    parser.addOption(QCommandLineOption("download-dir", "Opt-in automatic download directory; otherwise ask where to save", "directory"));
    parser.addOption(QCommandLineOption("ring-timeout", "Server ring timeout in milliseconds (tests)", "milliseconds", "30000"));
    parser.process(app);
    bool ok; const uint port = parser.value("port").toUInt(&ok);
    if (!ok || port < 1 || port > 65535) return 2;
    if (parser.isSet("headless")) {
        ChatServer server;
        const int timeout = parser.value("ring-timeout").toInt(&ok); if (!ok || timeout < 1) return 2;
        server.setRingTimeout(timeout);
        QObject::connect(&server, &ChatServer::log, &app, [](const QString &text) { QTextStream out(stdout); out.setCodec("UTF-8"); out << text << Qt::endl; });
        if (!server.start(quint16(port))) return 1;
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &server, &ChatServer::stop);
        return app.exec();
    }
    MainWindow window;
    window.setDownloadDirectory(parser.value("download-dir"));
    window.configure(parser.isSet("server"), parser.value("host"), quint16(port), parser.value("nickname"), false);
    window.show();
    if (parser.isSet("server") || parser.isSet("connect")) QTimer::singleShot(0, &window, [&window, &parser, port] { window.configure(parser.isSet("server"), parser.value("host"), quint16(port), parser.value("nickname"), true); });
    return app.exec();
}
