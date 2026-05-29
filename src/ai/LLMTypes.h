#pragma once

#include <QString>
#include <QVector>

struct ChatMessage
{
    QString role;
    QString content;
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
