#include "SecretStore.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#include <QScopeGuard>
#include <QString>

#include <windows.h>
#include <dpapi.h>
#endif

bool SecretStore::isEncryptionAvailable()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QString SecretStore::preferredStorageName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows-dpapi");
#else
    return QStringLiteral("plain-json");
#endif
}

SecretStore::Result SecretStore::protectText(const QString &plainText)
{
#ifdef Q_OS_WIN
    const QByteArray plainBytes = plainText.toUtf8();
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plainBytes.constData()));
    input.cbData = static_cast<DWORD>(plainBytes.size());

    DATA_BLOB output;
    if (!CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
    {
        return {false, {}, QStringLiteral("CryptProtectData failed.")};
    }

    const auto cleanup = qScopeGuard([&output]() {
        LocalFree(output.pbData);
    });

    const QByteArray encrypted(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
    return {true, QString::fromLatin1(encrypted.toBase64()), {}};
#else
    Q_UNUSED(plainText);
    return {false, {}, QStringLiteral("Encrypted secret storage is not available on this platform.")};
#endif
}

SecretStore::Result SecretStore::unprotectText(const QString &protectedText)
{
#ifdef Q_OS_WIN
    const QByteArray encrypted = QByteArray::fromBase64(protectedText.toLatin1());
    if (encrypted.isEmpty() && !protectedText.isEmpty())
    {
        return {false, {}, QStringLiteral("Encrypted secret is not valid base64.")};
    }

    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()));
    input.cbData = static_cast<DWORD>(encrypted.size());

    DATA_BLOB output;
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
    {
        return {false, {}, QStringLiteral("CryptUnprotectData failed.")};
    }

    const auto cleanup = qScopeGuard([&output]() {
        LocalFree(output.pbData);
    });

    const QByteArray plainBytes(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
    return {true, QString::fromUtf8(plainBytes), {}};
#else
    Q_UNUSED(protectedText);
    return {false, {}, QStringLiteral("Encrypted secret storage is not available on this platform.")};
#endif
}
