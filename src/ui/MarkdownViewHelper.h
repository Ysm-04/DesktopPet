#pragma once

#include <QFont>
#include <QString>
#include <QUrl>

class QTextDocument;

namespace MarkdownViewHelper
{
void applyMarkdownToDocument(
    QTextDocument *document,
    const QString &markdown,
    const QFont &baseFont,
    bool compactBubble = true);
QString markdownToHtmlFragment(const QString &markdown, const QFont &baseFont);
bool openExternalUrlIfSafe(const QUrl &url);
}
