#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace atlas {

enum class AccountKind {
    Offline,
    Microsoft
};

struct AccountSession {
    AccountKind kind = AccountKind::Offline;
    QString playerName;
    QString uuid;
    QString accessToken;
    QString xuid;
    QString gamertag;
    QString clientId;

    bool isValidForLaunch() const;
    bool isMicrosoft() const { return kind == AccountKind::Microsoft; }
};

struct DeviceCodePrompt {
    QString userCode;
    QString verificationUri;
    QString message;
    int expiresInSeconds = 0;

    bool isValid() const;
};

class AuthService final : public QObject
{
    Q_OBJECT
public:
    explicit AuthService(const QString &dataDirectory, QObject *parent = nullptr);

    AccountSession offlineSession(const QString &playerName) const;
    AccountSession savedSession() const;
    bool hasSavedMicrosoftSession() const;
    void beginMicrosoftLogin(const QString &clientId);
    void restoreMicrosoftSession(const QString &clientId);
    void signOut();
    bool isBusy() const;

signals:
    void deviceCodeReady(const atlas::DeviceCodePrompt &prompt);
    void authenticationStarted();
    void sessionReady(const atlas::AccountSession &session);
    void authenticationError(const QString &message);
    void signedOut();

private slots:
    void onNetworkFinished();
    void pollDeviceToken();

private:
    enum class FlowStage {
        Idle,
        RequestDeviceCode,
        PollMicrosoftToken,
        RefreshMicrosoftToken,
        XboxLive,
        Xsts,
        MinecraftLogin,
        MinecraftProfile
    };

    void postForm(const QUrl &url, const QByteArray &form);
    void postJson(const QUrl &url, const QJsonObject &payload);
    void beginXboxExchange(const QString &microsoftAccessToken);
    void requestXsts(const QString &xboxToken);
    void requestMinecraftToken(const QString &userHash, const QString &xstsToken);
    void requestMinecraftProfile();
    void finishWithError(const QString &message);
    void completeMicrosoftSession(const QJsonObject &profile);
    bool loadPersistedToken(QString *refreshToken, QString *clientId, QString *error) const;
    bool savePersistedToken(const QString &refreshToken, const AccountSession &session, QString *error) const;
    void clearPersistedToken() const;

    QString m_dataDirectory;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_pollTimer = nullptr;
    FlowStage m_stage = FlowStage::Idle;
    QString m_clientId;
    QString m_deviceCode;
    QString m_microsoftAccessToken;
    QString m_refreshToken;
    QString m_xboxUserHash;
    QString m_xstsToken;
    QString m_minecraftAccessToken;
    qint64 m_deviceExpiresAtMs = 0;
    int m_pollIntervalMs = 5000;
    AccountSession m_savedSession;
};

} // namespace atlas

Q_DECLARE_METATYPE(atlas::AccountSession)
Q_DECLARE_METATYPE(atlas::DeviceCodePrompt)
