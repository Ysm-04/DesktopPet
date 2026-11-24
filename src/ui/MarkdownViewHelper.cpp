#include "MarkdownViewHelper.h"

#include "../util/MarkdownFormatter.h"

#include <QColor>
#include <QDesktopServices>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextFragment>
#include <QVector>
#include <QtGlobal>

namespace
{
qreal headingPointSizeForLevel(int level, qreal basePointSize)
{
    if (level <= 1)
    {
        return basePointSize + 2.0;
    }
    if (level == 2)
    {
        return basePointSize + 1.0;
    }
    return basePointSize + 0.5;
}
}

namespace MarkdownViewHelper
{
void applyMarkdownToDocument(
    QTextDocument *document,
    const QString &markdown,
    const QFont &baseFont,
    bool compactBubble)
{
    if (document == nullptr)
    {
        return;
    }

    document->setDefaultFont(baseFont);
    document->setIndentWidth(compactBubble ? 18.0 : 20.0);
    document->setDefaultStyleSheet(QStringLiteral(
        "a { color: #175cd3; text-decoration: none; }"
        "code, pre { font-family: Consolas, 'Cascadia Mono', 'Courier New', monospace; }"));

    const QString normalized = MarkdownFormatter::normalizeMarkdownForDisplay(markdown);
    const QTextDocument::MarkdownFeatures features =
        QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
        | QTextDocument::MarkdownNoHTML;
    document->setMarkdown(normalized, features);

    const qreal basePointSize = baseFont.pointSizeF() > 0.0 ? baseFont.pointSizeF() : 10.0;
    struct InlineCodeRange
    {
        int start = 0;
        int end = 0;
    };
    QVector<InlineCodeRange> inlineCodeRanges;

    QTextCursor cursor(document);
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next())
    {
        QTextBlockFormat blockFormat = block.blockFormat();
        const int headingLevel = blockFormat.headingLevel();
        const bool hasNextBlock = block.next().isValid();
        const bool isCodeBlock = blockFormat.hasProperty(QTextFormat::BlockCodeFence)
            || blockFormat.nonBreakableLines();

        if (headingLevel > 0)
        {
            blockFormat.setTopMargin(block.blockNumber() == 0 ? 0.0 : (compactBubble ? 5.0 : 7.0));
            blockFormat.setBottomMargin(hasNextBlock ? (compactBubble ? 3.0 : 4.0) : 0.0);
            cursor.setPosition(block.position());
            cursor.setBlockFormat(blockFormat);

            QTextCharFormat headingCharFormat;
            headingCharFormat.setFontPointSize(headingPointSizeForLevel(headingLevel, basePointSize));
            headingCharFormat.setFontWeight(QFont::DemiBold);
            cursor.setPosition(block.position());
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(headingCharFormat);
            continue;
        }

        if (isCodeBlock)
        {
            blockFormat.setTopMargin(block.blockNumber() == 0 ? 0.0 : 2.0);
            blockFormat.setBottomMargin(hasNextBlock ? 2.0 : 0.0);
            blockFormat.setBackground(QColor(0xf4, 0xf6, 0xf8));
            cursor.setPosition(block.position());
            cursor.setBlockFormat(blockFormat);
            continue;
        }

        if (block.textList() != nullptr)
        {
            blockFormat.setTopMargin(0.0);
            blockFormat.setBottomMargin(hasNextBlock ? 2.0 : 0.0);
        }
        else
        {
            blockFormat.setTopMargin(0.0);
            blockFormat.setBottomMargin(hasNextBlock ? (compactBubble ? 5.0 : 6.0) : 0.0);
        }
        cursor.setPosition(block.position());
        cursor.setBlockFormat(blockFormat);

        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.length() <= 0)
            {
                continue;
            }

            if (fragment.charFormat().fontFixedPitch())
            {
                inlineCodeRanges.append({fragment.position(), fragment.position() + fragment.length()});
            }
        }
    }

    if (!inlineCodeRanges.isEmpty())
    {
        QTextCharFormat inlineCodeFormat;
        inlineCodeFormat.setBackground(QColor(0xf1, 0xf3, 0xf4));
        for (const InlineCodeRange &range : inlineCodeRanges)
        {
            cursor.setPosition(range.start);
            cursor.setPosition(range.end, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(inlineCodeFormat);
        }
    }
}

QString markdownToHtmlFragment(const QString &markdown, const QFont &baseFont)
{
    QTextDocument document;
    document.setDocumentMargin(0.0);
    applyMarkdownToDocument(&document, markdown, baseFont, false);
    return MarkdownFormatter::extractHtmlBodyFragment(document.toHtml()).trimmed();
}

bool openExternalUrlIfSafe(const QUrl &url)
{
    if (!MarkdownFormatter::isSafeExternalWebUrl(url))
    {
        return false;
    }

    return QDesktopServices::openUrl(url);
}
}
