#pragma once

#include "LLMTypes.h"

#include <functional>

class LLMProvider
{
public:
    using ResponseCallback = std::function<void(const ChatResponse &)>;

    virtual ~LLMProvider() = default;

    virtual bool isBusy() const = 0;
    virtual void sendChatRequest(const ChatRequest &request, ResponseCallback callback) = 0;
    virtual void cancel() = 0;
};
