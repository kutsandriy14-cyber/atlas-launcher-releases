#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace atlas {

enum class LoaderKind {
    Vanilla,
    Fabric,
    LegacyFabric,
    Forge,
    NeoForge,
    Quilt,
    Unknown
};

inline QString loaderKindToString(LoaderKind kind)
{
    switch (kind) {
    case LoaderKind::Vanilla: return QStringLiteral("vanilla");
    case LoaderKind::Fabric: return QStringLiteral("fabric");
    case LoaderKind::LegacyFabric: return QStringLiteral("legacyfabric");
    case LoaderKind::Forge: return QStringLiteral("forge");
    case LoaderKind::NeoForge: return QStringLiteral("neoforge");
    case LoaderKind::Quilt: return QStringLiteral("quilt");
    case LoaderKind::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

inline LoaderKind loaderKindFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("vanilla")) return LoaderKind::Vanilla;
    if (normalized == QStringLiteral("fabric")) return LoaderKind::Fabric;
    if (normalized == QStringLiteral("legacyfabric") || normalized == QStringLiteral("legacy-fabric")) {
        return LoaderKind::LegacyFabric;
    }
    if (normalized == QStringLiteral("forge")) return LoaderKind::Forge;
    if (normalized == QStringLiteral("neoforge") || normalized == QStringLiteral("neo-forge")) {
        return LoaderKind::NeoForge;
    }
    if (normalized == QStringLiteral("quilt")) return LoaderKind::Quilt;
    return LoaderKind::Unknown;
}

struct LoaderSpec {
    LoaderKind kind = LoaderKind::Vanilla;
    QString version;

    QJsonObject toJson() const
    {
        return QJsonObject{
            {QStringLiteral("kind"), loaderKindToString(kind)},
            {QStringLiteral("version"), version}
        };
    }

    static LoaderSpec fromJson(const QJsonObject &object)
    {
        LoaderSpec spec;
        spec.kind = loaderKindFromString(object.value(QStringLiteral("kind")).toString());
        spec.version = object.value(QStringLiteral("version")).toString();
        return spec;
    }
};

enum class JavaRuntimeMode {
    Automatic,
    AtlasManaged,
    Custom
};

inline QString javaRuntimeModeToString(JavaRuntimeMode mode)
{
    switch (mode) {
    case JavaRuntimeMode::Automatic: return QStringLiteral("automatic");
    case JavaRuntimeMode::AtlasManaged: return QStringLiteral("atlas-managed");
    case JavaRuntimeMode::Custom: return QStringLiteral("custom");
    }
    return QStringLiteral("automatic");
}

inline JavaRuntimeMode javaRuntimeModeFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("atlas-managed") || normalized == QStringLiteral("managed")) {
        return JavaRuntimeMode::AtlasManaged;
    }
    if (normalized == QStringLiteral("custom")) return JavaRuntimeMode::Custom;
    return JavaRuntimeMode::Automatic;
}

struct JavaSpec {
    // Automatic and AtlasManaged use the Java Runtime installed inside Atlas.
    // Custom uses path, which must reference a compatible javaw.exe or java.exe.
    QString path;
    JavaRuntimeMode runtimeMode = JavaRuntimeMode::Automatic;
    // Used only in AtlasManaged mode. Zero means the version inferred from Minecraft.
    int managedMajor = 0;
    int minMemoryMiB = 1024;
    int maxMemoryMiB = 4096;
    QStringList jvmArguments;

    bool usesAtlasRuntime() const
    {
        return runtimeMode != JavaRuntimeMode::Custom;
    }

    QJsonObject toJson() const
    {
        QJsonArray args;
        for (const QString &argument : jvmArguments) {
            args.append(argument);
        }
        return QJsonObject{
            {QStringLiteral("path"), path},
            {QStringLiteral("runtimeMode"), javaRuntimeModeToString(runtimeMode)},
            {QStringLiteral("managedMajor"), managedMajor},
            // Kept for profiles opened by earlier Atlas releases.
            {QStringLiteral("useManagedRuntime"), usesAtlasRuntime()},
            {QStringLiteral("minMemoryMiB"), minMemoryMiB},
            {QStringLiteral("maxMemoryMiB"), maxMemoryMiB},
            {QStringLiteral("jvmArguments"), args}
        };
    }

    static JavaSpec fromJson(const QJsonObject &object)
    {
        JavaSpec spec;
        spec.path = object.value(QStringLiteral("path")).toString();
        if (object.contains(QStringLiteral("runtimeMode"))) {
            spec.runtimeMode = javaRuntimeModeFromString(object.value(QStringLiteral("runtimeMode")).toString());
        } else {
            // Old profiles used one boolean. Their managed Java becomes the safer Auto mode.
            const bool usedManagedRuntime = object.contains(QStringLiteral("useManagedRuntime"))
                ? object.value(QStringLiteral("useManagedRuntime")).toBool(true)
                : spec.path.trimmed().isEmpty();
            spec.runtimeMode = usedManagedRuntime ? JavaRuntimeMode::Automatic : JavaRuntimeMode::Custom;
        }
        if (spec.runtimeMode != JavaRuntimeMode::Custom) spec.path.clear();
        spec.managedMajor = object.value(QStringLiteral("managedMajor")).toInt(0);
        spec.minMemoryMiB = object.value(QStringLiteral("minMemoryMiB")).toInt(1024);
        spec.maxMemoryMiB = object.value(QStringLiteral("maxMemoryMiB")).toInt(4096);
        const QJsonArray args = object.value(QStringLiteral("jvmArguments")).toArray();
        for (const QJsonValue &value : args) {
            spec.jvmArguments.append(value.toString());
        }
        return spec;
    }
};

enum class LauncherWindowBehavior {
    KeepOpen,
    Minimize,
    CloseWindow
};

inline QString launcherWindowBehaviorToString(LauncherWindowBehavior behavior)
{
    switch (behavior) {
    case LauncherWindowBehavior::KeepOpen: return QStringLiteral("keep-open");
    case LauncherWindowBehavior::Minimize: return QStringLiteral("minimize");
    case LauncherWindowBehavior::CloseWindow: return QStringLiteral("close-window");
    }
    return QStringLiteral("keep-open");
}

inline LauncherWindowBehavior launcherWindowBehaviorFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("minimize")) return LauncherWindowBehavior::Minimize;
    if (normalized == QStringLiteral("close-window") || normalized == QStringLiteral("close")) {
        return LauncherWindowBehavior::CloseWindow;
    }
    return LauncherWindowBehavior::KeepOpen;
}

struct Instance {
    int schemaVersion = 3;
    QString id;
    QString name;
    QString minecraftVersion;
    LoaderSpec loader;
    JavaSpec java;
    // This is the actual game directory. InstanceService keeps instance.json in its
    // managed metadata directory even when this points to a user-chosen folder.
    QString rootPath;
    int resolutionWidth = 854;
    int resolutionHeight = 480;
    bool fullscreen = false;
    // Kept in JSON for previous Atlas releases. New profiles use launcherWindowBehavior.
    bool hideLauncherOnGameStart = false;
    LauncherWindowBehavior launcherWindowBehavior = LauncherWindowBehavior::KeepOpen;
    bool safeMode = false;
    QStringList gameArguments;
    QDateTime createdAt;

    QJsonObject toJson() const
    {
        QJsonArray gameArgs;
        for (const QString &argument : gameArguments) {
            gameArgs.append(argument);
        }
        return QJsonObject{
            {QStringLiteral("schemaVersion"), schemaVersion},
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("minecraft"), minecraftVersion},
            {QStringLiteral("loader"), loader.toJson()},
            {QStringLiteral("java"), java.toJson()},
            {QStringLiteral("rootPath"), rootPath},
            {QStringLiteral("resolutionWidth"), resolutionWidth},
            {QStringLiteral("resolutionHeight"), resolutionHeight},
            {QStringLiteral("fullscreen"), fullscreen},
            // Older Atlas versions understand only this value. It represents both
            // minimized and closed-window modes as a non-visible launcher.
            {QStringLiteral("hideLauncherOnGameStart"), launcherWindowBehavior != LauncherWindowBehavior::KeepOpen},
            {QStringLiteral("launcherWindowBehavior"), launcherWindowBehaviorToString(launcherWindowBehavior)},
            {QStringLiteral("safeMode"), safeMode},
            {QStringLiteral("gameArguments"), gameArgs},
            {QStringLiteral("createdAt"), createdAt.toUTC().toString(Qt::ISODate)}
        };
    }

    static Instance fromJson(const QJsonObject &object)
    {
        Instance instance;
        instance.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(1);
        instance.id = object.value(QStringLiteral("id")).toString();
        instance.name = object.value(QStringLiteral("name")).toString();
        instance.minecraftVersion = object.value(QStringLiteral("minecraft")).toString();
        instance.loader = LoaderSpec::fromJson(object.value(QStringLiteral("loader")).toObject());
        instance.java = JavaSpec::fromJson(object.value(QStringLiteral("java")).toObject());
        instance.rootPath = object.value(QStringLiteral("rootPath")).toString();
        instance.resolutionWidth = qBound(320, object.value(QStringLiteral("resolutionWidth")).toInt(854), 7680);
        instance.resolutionHeight = qBound(240, object.value(QStringLiteral("resolutionHeight")).toInt(480), 4320);
        instance.fullscreen = object.value(QStringLiteral("fullscreen")).toBool(false);
        instance.hideLauncherOnGameStart = object.value(QStringLiteral("hideLauncherOnGameStart")).toBool(false);
        if (object.contains(QStringLiteral("launcherWindowBehavior"))) {
            instance.launcherWindowBehavior = launcherWindowBehaviorFromString(
                object.value(QStringLiteral("launcherWindowBehavior")).toString());
        } else if (instance.hideLauncherOnGameStart) {
            // Existing profiles that hid Atlas keep their old behaviour: its window
            // is closed while Java Minecraft keeps running in a separate process.
            instance.launcherWindowBehavior = LauncherWindowBehavior::CloseWindow;
        }
        instance.safeMode = object.value(QStringLiteral("safeMode")).toBool(false);
        const QJsonArray gameArgs = object.value(QStringLiteral("gameArguments")).toArray();
        for (const QJsonValue &value : gameArgs) {
            instance.gameArguments.append(value.toString());
        }
        instance.createdAt = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
        if (!instance.createdAt.isValid()) {
            instance.createdAt = QDateTime::currentDateTimeUtc();
        }
        return instance;
    }
};

struct PackageFile {
    QString path;
    QString source;
    QString projectId;
    QString versionId;
    QString sha512;

    QJsonObject toJson() const
    {
        QJsonObject object{
            {QStringLiteral("path"), path},
            {QStringLiteral("source"), source}
        };
        if (!projectId.isEmpty()) object.insert(QStringLiteral("projectId"), projectId);
        if (!versionId.isEmpty()) object.insert(QStringLiteral("versionId"), versionId);
        if (!sha512.isEmpty()) object.insert(QStringLiteral("sha512"), sha512);
        return object;
    }

    static PackageFile fromJson(const QJsonObject &object)
    {
        PackageFile file;
        file.path = object.value(QStringLiteral("path")).toString();
        file.source = object.value(QStringLiteral("source")).toString();
        file.projectId = object.value(QStringLiteral("projectId")).toString();
        file.versionId = object.value(QStringLiteral("versionId")).toString();
        file.sha512 = object.value(QStringLiteral("sha512")).toString();
        return file;
    }
};

struct PackageManifest {
    QString format = QStringLiteral("atlas-launcher-package");
    int schemaVersion = 1;
    QString name;
    QString version;
    QString minecraftVersion;
    LoaderSpec loader;
    QVector<PackageFile> files;
    QStringList overrides;

    QJsonObject toJson() const
    {
        QJsonArray fileArray;
        for (const PackageFile &file : files) fileArray.append(file.toJson());
        QJsonArray overrideArray;
        for (const QString &overridePath : overrides) overrideArray.append(overridePath);
        return QJsonObject{
            {QStringLiteral("format"), format},
            {QStringLiteral("schemaVersion"), schemaVersion},
            {QStringLiteral("name"), name},
            {QStringLiteral("version"), version},
            {QStringLiteral("minecraft"), minecraftVersion},
            {QStringLiteral("loader"), loader.toJson()},
            {QStringLiteral("files"), fileArray},
            {QStringLiteral("overrides"), overrideArray}
        };
    }

    static PackageManifest fromJson(const QJsonObject &object)
    {
        PackageManifest manifest;
        manifest.format = object.value(QStringLiteral("format")).toString();
        manifest.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(1);
        manifest.name = object.value(QStringLiteral("name")).toString();
        manifest.version = object.value(QStringLiteral("version")).toString();
        manifest.minecraftVersion = object.value(QStringLiteral("minecraft")).toString();
        manifest.loader = LoaderSpec::fromJson(object.value(QStringLiteral("loader")).toObject());
        for (const QJsonValue &value : object.value(QStringLiteral("files")).toArray()) {
            manifest.files.append(PackageFile::fromJson(value.toObject()));
        }
        for (const QJsonValue &value : object.value(QStringLiteral("overrides")).toArray()) {
            manifest.overrides.append(value.toString());
        }
        return manifest;
    }
};

} // namespace atlas
