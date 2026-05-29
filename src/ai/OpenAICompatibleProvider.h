#pragma once

#include "LLMProvider.h"
#include "../config/AIConfig.h"

#include <QByteArray>
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
    void sendStreamingChatRequest(
        const ChatRequest &request,
        StreamCallback streamCallback,
        ResponseCallback callback) override;
    void cancel() override;

private:
    QJsonObject buildPayload(const ChatRequest &request, bool stream) const;
    QUrl requestUrl() const;
    ChatResponse parseResponse(QNetworkReply *reply, const QByteArray &body) const;
    void sendChatRequestInternal(
        const ChatRequest &request,
        bool stream,
        StreamCallback streamCallback,
        ResponseCallback callback);
    void handleStreamReadyRead();
    void processStreamBuffer();
    bool handleStreamPayload(const QByteArray &payload);
    ChatResponse finishStreamingResponse(QNetworkReply *reply, const QByteArray &body) const;
    void finishWithError(const QString &message, int httpStatus = 0);
    void clearReply();

    AIConfig m_config;
    QNetworkAccessManager m_networkManager;
    QPointer<QNetworkReply> m_currentReply;
    QMetaObject::Connection m_replyFinishedConnection;
    QMetaObject::Connection m_readyReadConnection;
    QTimer m_timeoutTimer;
    ResponseCallback m_callback;
    StreamCallback m_streamCallback;
    QByteArray m_streamBuffer;
    QByteArray m_streamRawBody;
    QString m_streamedContent;
    bool m_streaming = false;
    bool m_streamDone = false;
};
