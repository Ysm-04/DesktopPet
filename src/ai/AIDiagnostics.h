#pragma once

#include "../config/AIConfig.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace AIDiagnostics
{
QString oneLine(QString text);
QString diagnosticContext(const AIConfig &config, const QUrl &url);
QString errorMessageFromBody(const QByteArray &body);
QString responseBodySummary(const QByteArray &body);
QString safeTextSummary(const QString &text, int maxLength = 240);
}
