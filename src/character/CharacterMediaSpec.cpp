#include "CharacterMediaSpec.h"
#include "AlphaVideoSpec.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QDirIterator>
#include <cmath>

namespace
{
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}

bool CharacterMediaSpec::preflightPackage(const QString &directory, QString *error)
{
    const QFileInfo root(directory);
    if (!root.isDir() || root.isSymLink() || root.isJunction())
        return fail(error, QStringLiteral("角色包必须是普通本地目录。"));
    const QDir dir(root.absoluteFilePath());
    QDirIterator it(dir.absolutePath(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    quint64 bytes = 0;
    int count = 0;
    while (it.hasNext())
    {
        it.next();
        const QFileInfo entry = it.fileInfo();
        if (entry.isSymLink() || entry.isJunction())
            return fail(error, QStringLiteral("角色包禁止符号链接和目录联接。"));
        if (++count > 20000 || dir.relativeFilePath(entry.absoluteFilePath()).count('/') > 16)
            return fail(error, QStringLiteral("角色包超过 20000 项或 16 层目录。"));
        if (entry.isFile()) bytes += quint64(entry.size());
        if (bytes > 1024ULL * 1024 * 1024)
            return fail(error, QStringLiteral("角色包超过 1GiB。"));
    }
    if (QFileInfo(dir.filePath("character.json")).size() > 1024 * 1024)
        return fail(error, QStringLiteral("character.json 超过 1MiB。"));
    return true;
}

bool CharacterMediaSpec::isSupportedSchema(const QJsonValue &value)
{
    return value.isDouble() && (value.toDouble() == 1.0 || value.toDouble() == 2.0);
}

bool CharacterMediaSpec::isSafeRelativePath(QString path)
{
    path = path.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // Also reject Windows drive-relative paths and alternate data streams.
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.startsWith(QLatin1Char('/'))
        || path.contains(QLatin1Char(':')) || path.contains(QChar::Null))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &part : parts)
    {
        // Preserve harmless legacy spellings such as "./idle/".
        if (part.isEmpty() || part == QStringLiteral(".")) continue;
        if (part == QStringLiteral("..") || part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')))
            return false;
    }
    return QDir::cleanPath(path) != QStringLiteral(".");
}

bool CharacterMediaSpec::resolveResource(const QString &root, const QString &relativePath,
                                         QString *resolved, QString *error)
{
    if (!isSafeRelativePath(relativePath))
        return fail(error, QStringLiteral("资源路径必须是包内相对路径：") + relativePath);
    const QDir directory(root);
    QString normalized = relativePath.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    normalized = QDir::cleanPath(normalized);
    QString current = directory.absolutePath();
    for (const QString &part : normalized.split(QLatin1Char('/')))
    {
        current = QDir(current).filePath(part);
        const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction())
            return fail(error, QStringLiteral("资源路径不允许包含符号链接或目录联接：") + relativePath);
    }
    const QString canonicalRoot = directory.canonicalPath();
    const QString canonical = QFileInfo(current).canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonical.isEmpty()
        || !canonical.startsWith(canonicalRoot + QLatin1Char('/'), Qt::CaseInsensitive))
        return fail(error, QStringLiteral("资源不存在或不在角色包内：") + relativePath);
    if (resolved) *resolved = current;
    return true;
}

bool CharacterMediaSpec::parseState(int schema, const QString &name, const QJsonObject &object,
                                    const QString &root, CharacterState *result, QString *error)
{
    CharacterState state;
    state.name = name;
    QJsonObject media = object;
    if (schema == 2)
    {
        if (object.contains(QStringLiteral("path")) || object.contains(QStringLiteral("fps"))
            || !object.value(QStringLiteral("media")).isObject())
            return fail(error, QStringLiteral("v2 状态必须使用 media，不能混用顶层 path/fps：") + name);
        media = object.value(QStringLiteral("media")).toObject();
        state.mediaType = media.value(QStringLiteral("type")).toString();
        if (state.mediaType != QStringLiteral("frames") && state.mediaType != QStringLiteral("webp")
            && state.mediaType != QStringLiteral("alpha_video"))
            return fail(error, QStringLiteral("当前版本不支持媒体类型：") + state.mediaType + QStringLiteral("，状态：") + name);
        QSet<QString> allowed{QStringLiteral("type"), QStringLiteral("path")};
        if (state.mediaType == QStringLiteral("frames")) allowed.insert(QStringLiteral("fps"));
        if (state.mediaType == QStringLiteral("alpha_video")) allowed.insert(QStringLiteral("layout"));
        for (auto it = media.constBegin(); it != media.constEnd(); ++it)
            if (!allowed.contains(it.key()))
                return fail(error, QStringLiteral("media 含未知字段：") + it.key());
        if ((object.contains(QStringLiteral("loop")) && !object.value(QStringLiteral("loop")).isBool())
            || (object.contains(QStringLiteral("next")) && !object.value(QStringLiteral("next")).isString()))
            return fail(error, QStringLiteral("loop 必须为布尔值，next 必须为状态名字符串：") + name);
    }
    else if (object.contains(QStringLiteral("media")))
        return fail(error, QStringLiteral("media 需要 schemaVersion: 2：") + name);

    state.relativePath = media.value(QStringLiteral("path")).toString().trimmed();
    state.loop = object.value(QStringLiteral("loop")).toBool(true);
    state.nextState = object.value(QStringLiteral("next")).toString().trimmed();
    if (state.mediaType == QStringLiteral("alpha_video"))
    {
        state.videoLayout = media.value(QStringLiteral("layout")).toString();
        if (!resolveResource(root, state.relativePath, &state.mediaPath, error)) return false;
        if (!AlphaVideoSpec::inspectFile(state.mediaPath, state.videoLayout, nullptr, error)) return false;
        *result = state;
        return true;
    }
    if (state.mediaType == QStringLiteral("webp"))
    {
        if (!resolveResource(root, state.relativePath, &state.mediaPath, error)) return false;
        const QFileInfo file(state.mediaPath);
        if (!file.isFile() || file.suffix().compare(QStringLiteral("webp"), Qt::CaseInsensitive) != 0
            || file.size() <= 0 || file.size() > 32 * 1024 * 1024)
            return fail(error, QStringLiteral("webp.path 必须指向 32 MiB 以内的包内 .webp 文件：") + name);
        *result = state;
        return true;
    }
    const QJsonValue fpsValue = media.value(QStringLiteral("fps"));
    const double fps = fpsValue.toDouble(0);
    if (!fpsValue.isDouble() || fps <= 0 || std::floor(fps) != fps || fps > 2147483647.0
        || (schema == 2 && fps > 240))
        return fail(error, QStringLiteral("fps 必须为正整数（v2 范围 1–240）：") + name);
    state.fps = int(fps);

    QString directoryPath;
    if (!resolveResource(root, state.relativePath, &directoryPath, error)) return false;
    const QDir directory(directoryPath);
    if (!QFileInfo(directoryPath).isDir())
        return fail(error, QStringLiteral("frames.path 必须是目录：") + name);
    const QFileInfoList entries = directory.entryInfoList(
        {QStringLiteral("*.png"), QStringLiteral("*.PNG")}, QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries)
    {
        QString framePath;
        if (!resolveResource(root, QDir(root).relativeFilePath(entry.absoluteFilePath()), &framePath, error))
            return false;
        state.framePaths.append(framePath);
    }
    if (state.framePaths.isEmpty())
        return fail(error, QStringLiteral("状态目录没有 PNG 帧：") + name);
    *result = state;
    return true;
}
