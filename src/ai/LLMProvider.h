#pragma once

#include "LLMTypes.h"

#include <QtGlobal>

#include <functional>
#include <utility>

class LLMProvider
{
public:
    using ResponseCallback = std::function<void(const ChatResponse &)>;
    using StreamCallback = std::function<void(const QString &)>;

    virtual ~LLMProvider() = default;

    virtual bool isBusy() const = 0;
    virtual void sendChatRequest(const ChatRequest &request, ResponseCallback callback) = 0;
    virtual void sendStreamingChatRequest(
        const ChatRequest &request,
        StreamCallback streamCallback,
        ResponseCallback callback)
    {
        Q_UNUSED(streamCallback);
        sendChatRequest(request, std::move(callback));
    }
    virtual void cancel() = 0;
};
