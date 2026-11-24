#pragma once

#include <QString>
#include <QUrl>

namespace MarkdownFormatter
{
QString normalizeMarkdownForDisplay(const QString &markdown);
QString formatCitationMarkdown(int index, const QString &title, const QString &url);
bool isSafeExternalWebUrl(const QUrl &url);
QString extractHtmlBodyFragment(const QString &html);
}
