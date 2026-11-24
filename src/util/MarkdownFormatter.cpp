#include "MarkdownFormatter.h"

#include <QStringList>
#include <QtGlobal>

namespace
{
struct CodeFenceMatch
{
    bool isFence = false;
    QChar marker;
    int length = 0;
};

QString stripUpToThreeLeadingSpaces(const QString &line)
{
    int index = 0;
    while (index < line.size() && index < 3 && line.at(index) == QLatin1Char(' '))
    {
        ++index;
    }
    return line.mid(index);
}

CodeFenceMatch parseCodeFence(const QString &line)
{
    const QString trimmedLeft = stripUpToThreeLeadingSpaces(line);
    if (trimmedLeft.size() < 3)
    {
        return {};
    }

    const QChar first = trimmedLeft.at(0);
    if (first != QLatin1Char('`') && first != QLatin1Char('~'))
    {
        return {};
    }

    int count = 0;
    while (count < trimmedLeft.size() && trimmedLeft.at(count) == first)
    {
        ++count;
    }

    if (count < 3)
    {
        return {};
    }

    if (first == QLatin1Char('`') && trimmedLeft.mid(count).contains(QLatin1Char('`')))
    {
        return {};
    }

    return {true, first, count};
}

bool isClosingCodeFence(const QString &line, QChar openMarker, int openLength)
{
    const QString trimmedLeft = stripUpToThreeLeadingSpaces(line);
    int count = 0;
    while (count < trimmedLeft.size() && trimmedLeft.at(count) == openMarker)
    {
        ++count;
    }

    if (count < openLength)
    {
        return false;
    }

    return trimmedLeft.mid(count).trimmed().isEmpty();
}

bool isHeadingLine(const QString &line)
{
    const QString trimmedLeft = stripUpToThreeLeadingSpaces(line);
    int count = 0;
    while (count < trimmedLeft.size() && count < 6 && trimmedLeft.at(count) == QLatin1Char('#'))
    {
        ++count;
    }

    if (count == 0)
    {
        return false;
    }

    return count == trimmedLeft.size()
        || trimmedLeft.at(count) == QLatin1Char(' ')
        || trimmedLeft.at(count) == QLatin1Char('\t');
}

bool isThematicBreakLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.size() < 3)
    {
        return false;
    }

    const QChar marker = trimmed.at(0);
    if (marker != QLatin1Char('-') && marker != QLatin1Char('*') && marker != QLatin1Char('_'))
    {
        return false;
    }

    int markerCount = 0;
    for (const QChar ch : trimmed)
    {
        if (ch == marker)
        {
            ++markerCount;
        }
        else if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t'))
        {
            return false;
        }
    }

    return markerCount >= 3;
}

bool isListItemLine(const QString &line)
{
    const QString trimmedLeft = line.trimmed();
    if (trimmedLeft.size() >= 2
        && (trimmedLeft.at(0) == QLatin1Char('-')
            || trimmedLeft.at(0) == QLatin1Char('*')
            || trimmedLeft.at(0) == QLatin1Char('+'))
        && (trimmedLeft.at(1) == QLatin1Char(' ') || trimmedLeft.at(1) == QLatin1Char('\t')))
    {
        return true;
    }

    int digits = 0;
    while (digits < trimmedLeft.size() && digits < 9 && trimmedLeft.at(digits).isDigit())
    {
        ++digits;
    }

    if (digits > 0
        && digits + 1 < trimmedLeft.size()
        && (trimmedLeft.at(digits) == QLatin1Char('.') || trimmedLeft.at(digits) == QLatin1Char(')'))
        && (trimmedLeft.at(digits + 1) == QLatin1Char(' ') || trimmedLeft.at(digits + 1) == QLatin1Char('\t')))
    {
        return true;
    }

    return false;
}

bool isBlockquoteLine(const QString &line)
{
    return stripUpToThreeLeadingSpaces(line).startsWith(QLatin1Char('>'));
}

bool isTableLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1Char('|')))
    {
        return true;
    }

    return trimmed.contains(QStringLiteral("|---")) || trimmed.contains(QStringLiteral("---|"));
}
}

namespace MarkdownFormatter
{
QString normalizeMarkdownForDisplay(const QString &markdown)
{
    QString normalized = markdown;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList lines = normalized.split(QLatin1Char('\n'));
    QStringList outputLines;
    outputLines.reserve(lines.size());

    bool inCodeFence = false;
    QChar fenceMarker;
    int fenceLength = 0;

    for (int index = 0; index < lines.size(); ++index)
    {
        const QString &line = lines.at(index);
        if (inCodeFence)
        {
            outputLines.append(line);
            if (isClosingCodeFence(line, fenceMarker, fenceLength))
            {
                inCodeFence = false;
                fenceMarker = QChar();
                fenceLength = 0;
            }
            continue;
        }

        const CodeFenceMatch opening = parseCodeFence(line);
        if (opening.isFence)
        {
            inCodeFence = true;
            fenceMarker = opening.marker;
            fenceLength = opening.length;
            outputLines.append(line);
            continue;
        }

        if (index + 1 >= lines.size())
        {
            outputLines.append(line);
            continue;
        }

        const QString &nextLine = lines.at(index + 1);
        const bool bothNonEmpty = !line.trimmed().isEmpty() && !nextLine.trimmed().isEmpty();
        const bool alreadyHardBreak = line.endsWith(QStringLiteral("  ")) || line.endsWith(QLatin1Char('\\'));
        const bool currentIsBlockSyntax = isHeadingLine(line)
            || isThematicBreakLine(line)
            || isTableLine(line);
        const bool nextIsBlockSyntax = parseCodeFence(nextLine).isFence
            || isHeadingLine(nextLine)
            || isListItemLine(nextLine)
            || isBlockquoteLine(nextLine)
            || isThematicBreakLine(nextLine)
            || isTableLine(nextLine);

        if (bothNonEmpty && !alreadyHardBreak && !currentIsBlockSyntax && !nextIsBlockSyntax)
        {
            outputLines.append(line + QStringLiteral("  "));
        }
        else
        {
            outputLines.append(line);
        }
    }

    return outputLines.join(QLatin1Char('\n'));
}

QString formatCitationMarkdown(int index, const QString &title, const QString &url)
{
    QString safeUrl = url.trimmed();
    if (safeUrl.isEmpty())
    {
        return {};
    }

    safeUrl.replace(QLatin1Char(' '), QStringLiteral("%20"));
    safeUrl.replace(QLatin1Char('<'), QStringLiteral("%3C"));
    safeUrl.replace(QLatin1Char('>'), QStringLiteral("%3E"));

    QString cleanTitle = title.trimmed();
    cleanTitle.replace(QLatin1Char('\r'), QLatin1Char(' '));
    cleanTitle.replace(QLatin1Char('\n'), QLatin1Char(' '));
    cleanTitle = cleanTitle.simplified();

    if (cleanTitle.isEmpty() || cleanTitle.compare(url.trimmed(), Qt::CaseInsensitive) == 0)
    {
        return QStringLiteral("%1. <%2>").arg(index).arg(safeUrl);
    }

    QString escapedTitle = cleanTitle;
    escapedTitle.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escapedTitle.replace(QLatin1Char('['), QStringLiteral("\\["));
    escapedTitle.replace(QLatin1Char(']'), QStringLiteral("\\]"));
    return QStringLiteral("%1. [%2](<%3>)").arg(index).arg(escapedTitle, safeUrl);
}

bool isSafeExternalWebUrl(const QUrl &url)
{
    if (!url.isValid())
    {
        return false;
    }

    const QString scheme = url.scheme().trimmed().toLower();
    return (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
        && !url.host().trimmed().isEmpty();
}

QString extractHtmlBodyFragment(const QString &html)
{
    const int bodyOpen = html.indexOf(QStringLiteral("<body"), 0, Qt::CaseInsensitive);
    if (bodyOpen < 0)
    {
        return html;
    }

    const int tagEnd = html.indexOf(QLatin1Char('>'), bodyOpen);
    if (tagEnd < 0)
    {
        return html;
    }

    const int bodyClose = html.lastIndexOf(QStringLiteral("</body>"), -1, Qt::CaseInsensitive);
    const int contentStart = tagEnd + 1;
    if (bodyClose <= contentStart)
    {
        return html;
    }

    return html.mid(contentStart, bodyClose - contentStart);
}
}
