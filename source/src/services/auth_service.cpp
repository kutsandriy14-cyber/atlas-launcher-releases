#include "services/auth_service.h"

#include "infrastructure/json_store.h"
#include "infrastructure/logger.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace atlas {
namespace {

constexpr auto kMicrosoftTenant = "consumers";
constexpr auto kMicrosoftDeviceCodeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
constexpr auto kMicrosoftTokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
constexpr auto kXboxLiveAuthUrl = "https://user.auth.xboxlive.com/user/authenticate";
constexpr auto kXstsAuthUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
constexpr auto kMinecraftLoginUrl = "https://api.minecraftservices.com/authentication/login_with_xbox";
constexpr auto kMinecraftProfileUrl = "https://api.minecraftservices.com/minecraft/profile";

QString replyErrorMessage(const QJsonObject &body, int httpStatus, const QString &networkError)
{
    const QString code = body.value(QStringLiteral("error")).toString();
    const QString description = body.value(QStringLiteral("error_description")).toString();
    const QString message = body.value(QStringLiteral("message")).toString();
    const QString detail = !description.isEmpty() ? description : (!message.isEmpty() ? message : networkError);
    if (!code.isEmpty() && !detail.isEmpty()) return QStringLiteral("%1: %2").arg(code, detail);
    if (!detail.isEmpty()) return detail;
    return httpStatus > 0 ? QStringLiteral("HTTP %1").arg(httpStatus) : QStringLiteral("Сервис авторизации не вернул подробности ошибки.");
}

QJsonObject parseObject(const QByteArray &data)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    return error.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject();
}

QString protectedTokenPath(const QString &dataDirectory)
{
    return QDir(dataDirectory).filePath(QStringLiteral("accounts/microsoft.json"));
}

#ifdef Q_OS_WIN
bool protectForCurrentWindowsUser(const QByteArray &plain, QByteArray *protectedData, QString *error)
{
    if (!protectedData) return false;
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
    input.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB output{};
    const QString description = QStringLiteral("Atlas Launcher Microsoft refresh token");
    if (!CryptProtectData(&input, reinterpret_cast<LPCWSTR>(description.utf16()), nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error) *error = QStringLiteral("Windows DPAPI не смог защитить refresh token (код %1).").arg(GetLastError());
        return false;
    }
    *protectedData = QByteArray(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return true;
}

bool unprotectForCurrentWindowsUser(const QByteArray &protectedData, QByteArray *plain, QString *error)
{
    if (!plain) return false;
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(protectedData.constData()));
    input.cbData = static_cast<DWORD>(protectedData.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error) *error = QStringLiteral("Windows DPAPI не смог открыть сохранённую сессию (код %1).").arg(GetLastError());
        return false;
    }
    *plain = QByteArray(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return true;
}
#endif

} // namespace

bool AccountSession::isValidForLaunch() const
{
    return !playerName.trimmed().isEmpty() && !uuid.trimmed().isEmpty()
        && (kind == AccountKind::Offline || !accessToken.trimmed().isEmpty());
}

bool DeviceCodePrompt::isValid() const
{
    return !userCode.trimmed().isEmpty() && verificationUri.startsWith(QStringLiteral("https://")) && expiresInSeconds > 0;
}

AuthService::AuthService(const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_dataDirectory(QDir::cleanPath(dataDirectory)), m_network(new QNetworkAccessManager(this)),
      m_pollTimer(new QTimer(this))
{
    qRegisterMetaType<AccountSession>();
    qRegisterMetaType<DeviceCodePrompt>();
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &AuthService::pollDeviceToken);

    QString refreshToken;
    QString clientId;
    QString error;
    if (loadPersistedToken(&refreshToken, &clientId, &error)) {
        QJsonObject saved;
        JsonStore::readObject(protectedTokenPath(m_dataDirectory), &saved, nullptr);
        m_savedSession.kind = AccountKind::Microsoft;
        m_savedSession.playerName = saved.value(QStringLiteral("playerName")).toString();
        m_savedSession.uuid = saved.value(QStringLiteral("uuid")).toString();
        m_savedSession.xuid = saved.value(QStringLiteral("xuid")).toString();
        m_savedSession.gamertag = saved.value(QStringLiteral("gamertag")).toString();
        m_savedSession.clientId = clientId;
    } else if (!error.isEmpty()) {
        Logger::warning(QStringLiteral("Microsoft session was not restored: %1").arg(error));
    }
}

AccountSession AuthService::offlineSession(const QString &playerName) const
{
    const QString safeName = playerName.trimmed().isEmpty() ? QStringLiteral("Player") : playerName.trimmed().left(16);
    const QByteArray digest = QCryptographicHash::hash((QStringLiteral("OfflinePlayer:") + safeName).toUtf8(), QCryptographicHash::Md5).toHex();
    AccountSession session;
    session.kind = AccountKind::Offline;
    session.playerName = safeName;
    session.uuid = QString::fromLatin1(digest);
    session.accessToken = QStringLiteral("0");
    return session;
}

AccountSession AuthService::savedSession() const
{
    return m_savedSession;
}

bool AuthService::hasSavedMicrosoftSession() const
{
    return m_savedSession.kind == AccountKind::Microsoft && !m_savedSession.clientId.isEmpty();
}

bool AuthService::isBusy() const
{
    return m_stage != FlowStage::Idle;
}

void AuthService::beginMicrosoftLogin(const QString &clientId)
{
    if (isBusy()) {
        emit authenticationError(QStringLiteral("Авторизация уже выполняется. Завершите текущий вход или дождитесь результата."));
        return;
    }
    m_clientId = clientId.trimmed();
    if (m_clientId.isEmpty()) {
        emit authenticationError(QStringLiteral("Укажите собственный Microsoft Application (client) ID в настройках Atlas."));
        return;
    }
    m_savedSession = {};
    m_deviceCode.clear();
    m_refreshToken.clear();
    m_microsoftAccessToken.clear();
    m_stage = FlowStage::RequestDeviceCode;
    emit authenticationStarted();

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("scope"), QStringLiteral("XboxLive.signin offline_access openid profile"));
    postForm(QUrl(QString::fromLatin1(kMicrosoftDeviceCodeUrl)), form.query(QUrl::FullyEncoded).toUtf8());
}

void AuthService::restoreMicrosoftSession(const QString &clientId)
{
    if (isBusy()) {
        emit authenticationError(QStringLiteral("Авторизация уже выполняется."));
        return;
    }
    QString refreshToken;
    QString persistedClientId;
    QString error;
    if (!loadPersistedToken(&refreshToken, &persistedClientId, &error)) {
        emit authenticationError(error.isEmpty()
            ? QStringLiteral("Сохранённая Microsoft-сессия не найдена.")
            : error);
        return;
    }
    const QString requestedClientId = clientId.trimmed();
    if (!requestedClientId.isEmpty() && requestedClientId != persistedClientId) {
        emit authenticationError(QStringLiteral("Client ID изменился. Для безопасности войдите в Microsoft заново."));
        return;
    }
    m_clientId = persistedClientId;
    m_refreshToken = refreshToken;
    m_stage = FlowStage::RefreshMicrosoftToken;
    emit authenticationStarted();

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    form.addQueryItem(QStringLiteral("scope"), QStringLiteral("XboxLive.signin offline_access"));
    postForm(QUrl(QString::fromLatin1(kMicrosoftTokenUrl)), form.query(QUrl::FullyEncoded).toUtf8());
}

void AuthService::signOut()
{
    m_pollTimer->stop();
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_stage = FlowStage::Idle;
    m_deviceCode.clear();
    m_microsoftAccessToken.clear();
    m_refreshToken.clear();
    m_xboxUserHash.clear();
    m_xstsToken.clear();
    m_minecraftAccessToken.clear();
    m_savedSession = {};
    clearPersistedToken();
    emit signedOut();
}

void AuthService::postForm(const QUrl &url, const QByteArray &form)
{
    if (m_reply) {
        finishWithError(QStringLiteral("Внутренняя ошибка: предыдущий сетевой запрос ещё не завершён."));
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AtlasLauncher/0.2 (Microsoft authentication)"));
    request.setTransferTimeout(60000);
    m_reply = m_network->post(request, form);
    connect(m_reply, &QNetworkReply::finished, this, &AuthService::onNetworkFinished);
}

void AuthService::postJson(const QUrl &url, const QJsonObject &payload)
{
    if (m_reply) {
        finishWithError(QStringLiteral("Внутренняя ошибка: предыдущий сетевой запрос ещё не завершён."));
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AtlasLauncher/0.2 (Microsoft authentication)"));
    request.setTransferTimeout(60000);
    m_reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &AuthService::onNetworkFinished);
}

void AuthService::onNetworkFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) return;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkError = reply->errorString();
    const QByteArray bytes = reply->readAll();
    const QJsonObject body = parseObject(bytes);
    const QNetworkReply::NetworkError networkCode = reply->error();
    reply->deleteLater();

    const FlowStage completedStage = m_stage;
    if (completedStage == FlowStage::Idle) return;

    if (completedStage == FlowStage::PollMicrosoftToken && body.value(QStringLiteral("error")).toString() == QStringLiteral("authorization_pending")) {
        m_pollTimer->start(m_pollIntervalMs);
        return;
    }
    if (completedStage == FlowStage::PollMicrosoftToken && body.value(QStringLiteral("error")).toString() == QStringLiteral("slow_down")) {
        m_pollIntervalMs += 5000;
        m_pollTimer->start(m_pollIntervalMs);
        return;
    }
    const bool success = networkCode == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300 && !body.isEmpty();
    if (!success) {
        if (completedStage == FlowStage::RefreshMicrosoftToken) clearPersistedToken();
        finishWithError(replyErrorMessage(body, httpStatus, networkError));
        return;
    }

    switch (completedStage) {
    case FlowStage::RequestDeviceCode: {
        DeviceCodePrompt prompt;
        prompt.userCode = body.value(QStringLiteral("user_code")).toString();
        prompt.verificationUri = body.value(QStringLiteral("verification_uri")).toString();
        prompt.message = body.value(QStringLiteral("message")).toString();
        prompt.expiresInSeconds = body.value(QStringLiteral("expires_in")).toInt();
        m_deviceCode = body.value(QStringLiteral("device_code")).toString();
        const int intervalSeconds = qMax(5, body.value(QStringLiteral("interval")).toInt(5));
        m_pollIntervalMs = intervalSeconds * 1000;
        m_deviceExpiresAtMs = QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(prompt.expiresInSeconds) * 1000;
        if (m_deviceCode.isEmpty() || !prompt.isValid()) {
            finishWithError(QStringLiteral("Microsoft не вернул корректный код устройства."));
            return;
        }
        m_stage = FlowStage::PollMicrosoftToken;
        emit deviceCodeReady(prompt);
        m_pollTimer->start(m_pollIntervalMs);
        return;
    }
    case FlowStage::PollMicrosoftToken:
    case FlowStage::RefreshMicrosoftToken:
        m_microsoftAccessToken = body.value(QStringLiteral("access_token")).toString();
        if (body.contains(QStringLiteral("refresh_token"))) m_refreshToken = body.value(QStringLiteral("refresh_token")).toString();
        if (m_microsoftAccessToken.isEmpty() || m_refreshToken.isEmpty()) {
            finishWithError(QStringLiteral("Microsoft не вернул токен, необходимый для продолжения входа."));
            return;
        }
        beginXboxExchange(m_microsoftAccessToken);
        return;
    case FlowStage::XboxLive: {
        const QJsonObject claims = body.value(QStringLiteral("DisplayClaims")).toObject();
        const QJsonArray xui = claims.value(QStringLiteral("xui")).toArray();
        m_xboxUserHash = xui.isEmpty() ? QString() : xui.first().toObject().value(QStringLiteral("uhs")).toString();
        const QString token = body.value(QStringLiteral("Token")).toString();
        if (m_xboxUserHash.isEmpty() || token.isEmpty()) {
            finishWithError(QStringLiteral("Xbox Live не вернул данные пользователя."));
            return;
        }
        requestXsts(token);
        return;
    }
    case FlowStage::Xsts: {
        m_xstsToken = body.value(QStringLiteral("Token")).toString();
        if (m_xstsToken.isEmpty() || m_xboxUserHash.isEmpty()) {
            finishWithError(QStringLiteral("XSTS не выдал токен доступа."));
            return;
        }
        requestMinecraftToken(m_xboxUserHash, m_xstsToken);
        return;
    }
    case FlowStage::MinecraftLogin:
        m_minecraftAccessToken = body.value(QStringLiteral("access_token")).toString();
        if (m_minecraftAccessToken.isEmpty()) {
            finishWithError(QStringLiteral("Minecraft Services не вернул токен запуска."));
            return;
        }
        requestMinecraftProfile();
        return;
    case FlowStage::MinecraftProfile:
        completeMicrosoftSession(body);
        return;
    case FlowStage::Idle:
        return;
    }
}

void AuthService::pollDeviceToken()
{
    if (m_stage != FlowStage::PollMicrosoftToken) return;
    if (QDateTime::currentMSecsSinceEpoch() >= m_deviceExpiresAtMs) {
        finishWithError(QStringLiteral("Срок действия кода Microsoft истёк. Начните вход заново."));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    form.addQueryItem(QStringLiteral("device_code"), m_deviceCode);
    postForm(QUrl(QString::fromLatin1(kMicrosoftTokenUrl)), form.query(QUrl::FullyEncoded).toUtf8());
}

void AuthService::beginXboxExchange(const QString &microsoftAccessToken)
{
    m_stage = FlowStage::XboxLive;
    QJsonObject properties{
        {QStringLiteral("AuthMethod"), QStringLiteral("RPS")},
        {QStringLiteral("SiteName"), QStringLiteral("user.auth.xboxlive.com")},
        {QStringLiteral("RpsTicket"), QStringLiteral("d=") + microsoftAccessToken}
    };
    postJson(QUrl(QString::fromLatin1(kXboxLiveAuthUrl)), QJsonObject{
        {QStringLiteral("Properties"), properties},
        {QStringLiteral("RelyingParty"), QStringLiteral("http://auth.xboxlive.com")},
        {QStringLiteral("TokenType"), QStringLiteral("JWT")}
    });
}

void AuthService::requestXsts(const QString &xboxToken)
{
    m_stage = FlowStage::Xsts;
    postJson(QUrl(QString::fromLatin1(kXstsAuthUrl)), QJsonObject{
        {QStringLiteral("Properties"), QJsonObject{
            {QStringLiteral("SandboxId"), QStringLiteral("RETAIL")},
            {QStringLiteral("UserTokens"), QJsonArray{ xboxToken }}
        }},
        {QStringLiteral("RelyingParty"), QStringLiteral("rp://api.minecraftservices.com/")},
        {QStringLiteral("TokenType"), QStringLiteral("JWT")}
    });
}

void AuthService::requestMinecraftToken(const QString &userHash, const QString &xstsToken)
{
    m_stage = FlowStage::MinecraftLogin;
    postJson(QUrl(QString::fromLatin1(kMinecraftLoginUrl)), QJsonObject{
        {QStringLiteral("identityToken"), QStringLiteral("XBL3.0 x=%1;%2").arg(userHash, xstsToken)}
    });
}

void AuthService::requestMinecraftProfile()
{
    m_stage = FlowStage::MinecraftProfile;
    QNetworkRequest request(QUrl(QString::fromLatin1(kMinecraftProfileUrl)));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_minecraftAccessToken.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AtlasLauncher/0.2 (Microsoft authentication)"));
    request.setTransferTimeout(60000);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &AuthService::onNetworkFinished);
}

void AuthService::completeMicrosoftSession(const QJsonObject &profile)
{
    AccountSession session;
    session.kind = AccountKind::Microsoft;
    session.playerName = profile.value(QStringLiteral("name")).toString();
    session.uuid = profile.value(QStringLiteral("id")).toString();
    session.accessToken = m_minecraftAccessToken;
    session.xuid = m_xboxUserHash;
    session.gamertag = session.playerName;
    session.clientId = m_clientId;
    if (!session.isValidForLaunch()) {
        finishWithError(QStringLiteral("Minecraft Services не вернул действительный игровой профиль. Убедитесь, что на аккаунте есть Minecraft Java Edition."));
        return;
    }
    QString error;
    if (!savePersistedToken(m_refreshToken, session, &error)) {
        Logger::warning(QStringLiteral("Microsoft session is usable but was not persisted: %1").arg(error));
    }
    m_savedSession = session;
    m_stage = FlowStage::Idle;
    m_deviceCode.clear();
    m_microsoftAccessToken.clear();
    m_xstsToken.clear();
    emit sessionReady(session);
}

void AuthService::finishWithError(const QString &message)
{
    m_pollTimer->stop();
    m_stage = FlowStage::Idle;
    m_deviceCode.clear();
    m_microsoftAccessToken.clear();
    m_refreshToken.clear();
    m_xboxUserHash.clear();
    m_xstsToken.clear();
    m_minecraftAccessToken.clear();
    Logger::warning(QStringLiteral("Microsoft authentication failed: %1").arg(message));
    emit authenticationError(message);
}

bool AuthService::loadPersistedToken(QString *refreshToken, QString *clientId, QString *error) const
{
    if (refreshToken) refreshToken->clear();
    if (clientId) clientId->clear();
    QJsonObject object;
    QString readError;
    if (!JsonStore::readObject(protectedTokenPath(m_dataDirectory), &object, &readError)) {
        if (!readError.isEmpty() && error) *error = readError;
        return false;
    }
    const QString encoded = object.value(QStringLiteral("refreshTokenProtected")).toString();
    const QString storedClientId = object.value(QStringLiteral("clientId")).toString();
    if (encoded.isEmpty() || storedClientId.isEmpty()) {
        if (error) *error = QStringLiteral("Сохранённая Microsoft-сессия повреждена или неполна.");
        return false;
    }
#ifdef Q_OS_WIN
    QByteArray plain;
    if (!unprotectForCurrentWindowsUser(QByteArray::fromBase64(encoded.toLatin1()), &plain, error)) return false;
    const QString token = QString::fromUtf8(plain);
    if (token.isEmpty()) {
        if (error) *error = QStringLiteral("Сохранённый refresh token пуст.");
        return false;
    }
    if (refreshToken) *refreshToken = token;
    if (clientId) *clientId = storedClientId;
    return true;
#else
    if (error) *error = QStringLiteral("Сохранение Microsoft-сессии доступно только в Windows-сборке Atlas через DPAPI.");
    return false;
#endif
}

bool AuthService::savePersistedToken(const QString &refreshToken, const AccountSession &session, QString *error) const
{
    if (refreshToken.isEmpty() || session.clientId.isEmpty()) {
        if (error) *error = QStringLiteral("Нельзя сохранить неполную Microsoft-сессию.");
        return false;
    }
#ifdef Q_OS_WIN
    QByteArray protectedData;
    if (!protectForCurrentWindowsUser(refreshToken.toUtf8(), &protectedData, error)) return false;
    const QString path = protectedTokenPath(m_dataDirectory);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("Не удалось создать папку для Microsoft-сессии.");
        return false;
    }
    const QJsonObject object{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("clientId"), session.clientId},
        {QStringLiteral("refreshTokenProtected"), QString::fromLatin1(protectedData.toBase64())},
        {QStringLiteral("playerName"), session.playerName},
        {QStringLiteral("uuid"), session.uuid},
        {QStringLiteral("xuid"), session.xuid},
        {QStringLiteral("gamertag"), session.gamertag}
    };
    return JsonStore::writeObject(path, object, error);
#else
    Q_UNUSED(session)
    if (error) *error = QStringLiteral("Сохранение Microsoft-сессии доступно только в Windows-сборке Atlas через DPAPI.");
    return false;
#endif
}

void AuthService::clearPersistedToken() const
{
    QFile::remove(protectedTokenPath(m_dataDirectory));
}

} // namespace atlas
