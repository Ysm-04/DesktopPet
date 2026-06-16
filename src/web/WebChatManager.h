#pragma once

#include "WebChatTypes.h"

#include <QByteArray>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <functional>

class QNetworkReply;

class WebChatManager
{
public:
    using WebChatCallback = std::function<void(const WebChatResponse &)>;

    WebChatManager();
    ~WebChatManager();

    bool isBusy() const;
    void sendWebChat(const WebChatRequest &request, WebChatCallback callback);
    void cancel();

private:
    void startOpenAIResponses(const WebChatRequest &request);
    void startGeminiGrounding(const WebChatRequest &request);
    void startRequest(const QUrl &url, const QByteArray &payload, int messageCount, const QByteArray &authorizationHeader = {});
    void finishReply();
    void finishWithError(const QString &message, int httpStatus = 0);
    void invokeCallback(const WebChatResponse &response);
    void clearReply();

    WebChatResponse parseOpenAIResponse(QNetworkReply *reply, const QByteArray &body) const;
    WebChatResponse parseGeminiResponse(QNetworkReply *reply, const QByteArray &body) const;

    QNetworkAccessManager m_networkManager;
    QPointer<QNetworkReply> m_currentReply;
    QMetaObject::Connection m_replyFinishedConnection;
    QTimer m_timeoutTimer;
    WebChatCallback m_callback;
    WebProviderKind m_currentKind = WebProviderKind::None;
    AIConfig m_currentConfig;
};
