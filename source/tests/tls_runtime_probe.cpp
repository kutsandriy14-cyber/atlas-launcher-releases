#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

int main(int argc, char *argv[]) {
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    QTextStream errors(stderr);

    const bool supported = QSslSocket::supportsSsl();
    output << "supportsSsl=" << (supported ? "true" : "false") << "\n";
    output << "build=" << QSslSocket::sslLibraryBuildVersionString() << "\n";
    output << "runtime=" << QSslSocket::sslLibraryVersionString() << "\n";
    output.flush();
    if (!supported) {
        errors << "Qt cannot initialize an SSL backend.\n";
        errors.flush();
        return 1;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QStringLiteral("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AtlasLauncher-TlsProbe/0.2.1"));
    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(25000);
    loop.exec();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    const qint64 bytes = reply->bytesAvailable();
    output << "httpStatus=" << status << "\n";
    output << "responseBytes=" << bytes << "\n";
    output.flush();

    reply->deleteLater();
    if (error != QNetworkReply::NoError || status < 200 || status >= 300 || bytes <= 0) {
        errors << "Mojang HTTPS probe failed: " << errorText << "\n";
        errors.flush();
        return 2;
    }
    return 0;
}
