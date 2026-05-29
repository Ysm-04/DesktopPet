#pragma once

#include "LLMProvider.h"
#include "../config/AIConfig.h"

#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

class OpenAICompatibleProvider : public LLMProvider
{
public:
    explicit OpenAICompatibleProvider(const AIConfig &config);
    ~OpenAICompatibleProvider() override;

    bool isBusy() const override;
    void sendChatRequest(const ChatRequest &request, ResponseCallback callback) override;
    void cancel() override;

private:
    QJsonObject buildPayload(const ChatRequest &request) const;
    QUrl requestUrl() const;
    ChatResponse parseResponse(QNetworkReply *reply, const QByteArray &body) const;
    void finishWithError(const QString &message, int httpStatus = 0);
    void clearReply();

    AIConfig m_config;
    QNetworkAccessManager m_networkManager;
    QPointer<QNetworkReply> m_currentReply;
    QMetaObject::Connection m_replyFinishedConnection;
    QTimer m_timeoutTimer;
    ResponseCallback m_callback;
};
