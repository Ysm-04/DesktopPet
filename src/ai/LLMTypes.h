#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

struct ChatMessage
{
    QString role;
    QString content;
    QDateTime timestamp;
    QString provider;
    QString model;
    QString characterId;
};

struct ChatRequest
{
    QVector<ChatMessage> messages;
};

struct ChatResponse
{
    bool success = false;
    QString content;
    QString errorMessage;
    int httpStatus = 0;
};
