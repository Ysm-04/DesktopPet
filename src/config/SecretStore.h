#pragma once

#include <QString>

class SecretStore
{
public:
    struct Result
    {
        bool success = false;
        QString value;
        QString errorMessage;
    };

    static bool isEncryptionAvailable();
    static QString preferredStorageName();
    static Result protectText(const QString &plainText);
    static Result unprotectText(const QString &protectedText);
};
