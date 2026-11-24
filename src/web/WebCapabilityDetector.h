#pragma once

#include "../config/AIConfig.h"
#include "WebChatTypes.h"
#include "WebConfig.h"

namespace WebCapabilityDetector
{
WebCapability detect(const AIConfig &config, const WebConfig &webConfig = WebConfig());
QString providerKindName(WebProviderKind kind);
QString normalizedProviderMode(const QString &mode);
}
