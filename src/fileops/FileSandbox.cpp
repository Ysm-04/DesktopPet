#include "FileSandbox.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QtGlobal>

namespace
{
bool setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}

QString cleanedAbsolutePath(const QString &path)
{
    QString cleaned = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    cleaned.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return cleaned;
}

bool isWindowsSystemPath(const QString &path)
{
    const QString cleaned = cleanedAbsolutePath(path).toUpper();
    QStringList deniedPrefixes = {
        QStringLiteral("C:/WINDOWS"),
        QStringLiteral("C:/PROGRAM FILES"),
        QStringLiteral("C:/PROGRAM FILES (X86)"),
        QStringLiteral("C:/PROGRAMDATA"),
        QStringLiteral("C:/SYSTEM VOLUME INFORMATION"),
        QStringLiteral("C:/$RECYCLE.BIN")
    };

    const auto addEnvironmentPath = [&deniedPrefixes](const char *name) {
        const QString value = qEnvironmentVariable(name).trimmed();
        if (!value.isEmpty())
        {
            deniedPrefixes.append(cleanedAbsolutePath(value).toUpper());
        }
    };
    addEnvironmentPath("WINDIR");
    addEnvironmentPath("SystemRoot");
    addEnvironmentPath("ProgramFiles");
    addEnvironmentPath("ProgramFiles(x86)");
    addEnvironmentPath("ProgramData");

    for (const QString &prefix : deniedPrefixes)
    {
        if (cleaned == prefix || cleaned.startsWith(prefix + QLatin1Char('/')))
        {
            return true;
        }
    }
    return false;
}

bool hasSymlinkSegment(const QString &path)
{
    QFileInfo info(path);
    if (info.isSymLink())
    {
        return true;
    }

    QDir directory(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    while (directory.exists())
    {
        const QFileInfo directoryInfo(directory.absolutePath());
        if (directoryInfo.isSymLink())
        {
            return true;
        }

        if (!directory.cdUp())
        {
            break;
        }
    }
    return false;
}

bool isSupportedTextSuffix(const QString &suffix)
{
    static const QStringList supported = {
        QStringLiteral("txt"),
        QStringLiteral("md"),
        QStringLiteral("markdown"),
        QStringLiteral("log"),
        QStringLiteral("json"),
        QStringLiteral("csv"),
        QStringLiteral("ini"),
        QStringLiteral("xml"),
        QStringLiteral("yaml"),
        QStringLiteral("yml")
    };
    return supported.contains(suffix.trimmed().toLower());
}
}

bool FileSandbox::validateReadableFile(const QString &filePath, QString *errorMessage) const
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile())
    {
        return setError(errorMessage, QStringLiteral("请选择一个存在的文件。"));
    }
    if (!info.isReadable())
    {
        return setError(errorMessage, QStringLiteral("文件不可读取。"));
    }
    if (hasSymlinkSegment(info.absoluteFilePath()))
    {
        return setError(errorMessage, QStringLiteral("不允许操作符号链接或符号链接目录内的文件。"));
    }
    if (isWindowsSystemPath(info.absoluteFilePath()))
    {
        return setError(errorMessage, QStringLiteral("不允许访问系统目录内的文件。"));
    }
    return true;
}

bool FileSandbox::validateReadableTextFile(const QString &filePath, QString *errorMessage) const
{
    if (!validateReadableFile(filePath, errorMessage))
    {
        return false;
    }

    const QFileInfo info(filePath);
    if (!isSupportedTextSuffix(info.suffix()))
    {
        return setError(errorMessage, QStringLiteral("只允许读取常见文本文件：txt、md、log、json、csv、ini、xml、yaml。"));
    }
    return true;
}

bool FileSandbox::validateReadableDirectory(const QString &directoryPath, QString *errorMessage) const
{
    const QFileInfo info(directoryPath);
    if (!info.exists() || !info.isDir())
    {
        return setError(errorMessage, QStringLiteral("请选择一个存在的文件夹。"));
    }
    if (!info.isReadable())
    {
        return setError(errorMessage, QStringLiteral("文件夹不可读取。"));
    }
    if (hasSymlinkSegment(info.absoluteFilePath()))
    {
        return setError(errorMessage, QStringLiteral("不允许操作符号链接或符号链接目录。"));
    }
    if (isWindowsSystemPath(info.absoluteFilePath()))
    {
        return setError(errorMessage, QStringLiteral("不允许访问系统目录。"));
    }
    return true;
}

bool FileSandbox::validateWritableDirectory(const QString &directoryPath, QString *errorMessage) const
{
    const QFileInfo info(directoryPath);
    if (!info.exists() || !info.isDir())
    {
        return setError(errorMessage, QStringLiteral("请选择一个存在的目标文件夹。"));
    }
    if (!info.isWritable())
    {
        return setError(errorMessage, QStringLiteral("目标文件夹不可写。"));
    }
    if (hasSymlinkSegment(info.absoluteFilePath()))
    {
        return setError(errorMessage, QStringLiteral("不允许写入符号链接目录。"));
    }
    if (isWindowsSystemPath(info.absoluteFilePath()))
    {
        return setError(errorMessage, QStringLiteral("不允许写入系统目录。"));
    }
    return true;
}

bool FileSandbox::validateNewFileTarget(const QString &filePath, QString *errorMessage) const
{
    const QFileInfo info(filePath);
    if (info.exists())
    {
        return setError(errorMessage, QStringLiteral("目标文件已存在，本版不会覆盖文件。"));
    }
    return validateWritableDirectory(info.absolutePath(), errorMessage);
}

bool FileSandbox::validateSafeFileName(const QString &fileName, QString *errorMessage) const
{
    const QString trimmed = fileName.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral(".") || trimmed == QStringLiteral(".."))
    {
        return setError(errorMessage, QStringLiteral("文件名不能为空。"));
    }
    if (trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\')) || trimmed.contains(QLatin1Char(':')))
    {
        return setError(errorMessage, QStringLiteral("文件名不能包含路径分隔符或冒号。"));
    }
    if (trimmed.contains(QLatin1Char('*')) || trimmed.contains(QLatin1Char('?')) || trimmed.contains(QLatin1Char('"'))
        || trimmed.contains(QLatin1Char('<')) || trimmed.contains(QLatin1Char('>')) || trimmed.contains(QLatin1Char('|')))
    {
        return setError(errorMessage, QStringLiteral("文件名包含 Windows 不允许的字符。"));
    }
    return true;
}
