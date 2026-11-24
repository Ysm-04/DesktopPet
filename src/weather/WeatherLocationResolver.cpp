#include "WeatherLocationResolver.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

namespace
{
struct CityAlias
{
    QString queryName;
    QString countryCode;
};

bool isAsciiText(const QString &text)
{
    for (const QChar ch : text)
    {
        if (ch.unicode() > 127)
        {
            return false;
        }
    }
    return true;
}

QString compactKey(QString text)
{
    text = text.trimmed().toLower();
    text.remove(QRegularExpression(QStringLiteral("[\\s,，、。．]+")));
    return text;
}

QString cityAliasKey(QString text)
{
    text = compactKey(text);
    if (text.size() > 2 && text.endsWith(QStringLiteral("市")))
    {
        text.chop(1);
    }
    return text;
}

const QVector<QPair<QString, QString>> &countryAliases()
{
    static const QVector<QPair<QString, QString>> aliases = {
        {QStringLiteral("中华人民共和国"), QStringLiteral("CN")},
        {QStringLiteral("United States"), QStringLiteral("US")},
        {QStringLiteral("United Kingdom"), QStringLiteral("GB")},
        {QStringLiteral("澳大利亚"), QStringLiteral("AU")},
        {QStringLiteral("马来西亚"), QStringLiteral("MY")},
        {QStringLiteral("印度尼西亚"), QStringLiteral("ID")},
        {QStringLiteral("新西兰"), QStringLiteral("NZ")},
        {QStringLiteral("新加坡"), QStringLiteral("SG")},
        {QStringLiteral("加拿大"), QStringLiteral("CA")},
        {QStringLiteral("菲律宾"), QStringLiteral("PH")},
        {QStringLiteral("俄罗斯"), QStringLiteral("RU")},
        {QStringLiteral("西班牙"), QStringLiteral("ES")},
        {QStringLiteral("阿联酋"), QStringLiteral("AE")},
        {QStringLiteral("土耳其"), QStringLiteral("TR")},
        {QStringLiteral("墨西哥"), QStringLiteral("MX")},
        {QStringLiteral("意大利"), QStringLiteral("IT")},
        {QStringLiteral("Australia"), QStringLiteral("AU")},
        {QStringLiteral("Malaysia"), QStringLiteral("MY")},
        {QStringLiteral("Indonesia"), QStringLiteral("ID")},
        {QStringLiteral("Singapore"), QStringLiteral("SG")},
        {QStringLiteral("Canada"), QStringLiteral("CA")},
        {QStringLiteral("Philippines"), QStringLiteral("PH")},
        {QStringLiteral("Russia"), QStringLiteral("RU")},
        {QStringLiteral("Germany"), QStringLiteral("DE")},
        {QStringLiteral("France"), QStringLiteral("FR")},
        {QStringLiteral("Britain"), QStringLiteral("GB")},
        {QStringLiteral("England"), QStringLiteral("GB")},
        {QStringLiteral("Japan"), QStringLiteral("JP")},
        {QStringLiteral("中国"), QStringLiteral("CN")},
        {QStringLiteral("美国"), QStringLiteral("US")},
        {QStringLiteral("英国"), QStringLiteral("GB")},
        {QStringLiteral("法国"), QStringLiteral("FR")},
        {QStringLiteral("德国"), QStringLiteral("DE")},
        {QStringLiteral("日本"), QStringLiteral("JP")},
        {QStringLiteral("韩国"), QStringLiteral("KR")},
        {QStringLiteral("印度"), QStringLiteral("IN")},
        {QStringLiteral("泰国"), QStringLiteral("TH")},
        {QStringLiteral("越南"), QStringLiteral("VN")},
        {QStringLiteral("巴西"), QStringLiteral("BR")},
        {QStringLiteral("埃及"), QStringLiteral("EG")},
        {QStringLiteral("China"), QStringLiteral("CN")},
        {QStringLiteral("USA"), QStringLiteral("US")},
        {QStringLiteral("Korea"), QStringLiteral("KR")},
        {QStringLiteral("India"), QStringLiteral("IN")},
        {QStringLiteral("Thailand"), QStringLiteral("TH")},
        {QStringLiteral("Vietnam"), QStringLiteral("VN")},
        {QStringLiteral("Brazil"), QStringLiteral("BR")},
        {QStringLiteral("Mexico"), QStringLiteral("MX")},
        {QStringLiteral("Egypt"), QStringLiteral("EG")},
        {QStringLiteral("Turkey"), QStringLiteral("TR")},
    };
    return aliases;
}

QString countryCodeForText(const QString &text)
{
    const QString key = compactKey(text);
    const auto isAsciiLetter = [](QChar character) {
        const ushort value = character.unicode();
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    };
    if (key.size() == 2 && isAsciiLetter(key.at(0)) && isAsciiLetter(key.at(1)))
    {
        return key.toUpper();
    }

    for (const auto &alias : countryAliases())
    {
        if (key == compactKey(alias.first))
        {
            return alias.second;
        }
    }
    return {};
}

const QHash<QString, CityAlias> &cityAliases()
{
    static const QHash<QString, CityAlias> aliases = {
        {QStringLiteral("纽约"), {QStringLiteral("New York"), QStringLiteral("US")}},
        {QStringLiteral("东京"), {QStringLiteral("Tokyo"), QStringLiteral("JP")}},
        {QStringLiteral("伦敦"), {QStringLiteral("London"), QStringLiteral("GB")}},
        {QStringLiteral("巴黎"), {QStringLiteral("Paris"), QStringLiteral("FR")}},
        {QStringLiteral("柏林"), {QStringLiteral("Berlin"), QStringLiteral("DE")}},
        {QStringLiteral("罗马"), {QStringLiteral("Rome"), QStringLiteral("IT")}},
        {QStringLiteral("马德里"), {QStringLiteral("Madrid"), QStringLiteral("ES")}},
        {QStringLiteral("莫斯科"), {QStringLiteral("Moscow"), QStringLiteral("RU")}},
        {QStringLiteral("首尔"), {QStringLiteral("Seoul"), QStringLiteral("KR")}},
        {QStringLiteral("曼谷"), {QStringLiteral("Bangkok"), QStringLiteral("TH")}},
        {QStringLiteral("吉隆坡"), {QStringLiteral("Kuala Lumpur"), QStringLiteral("MY")}},
        {QStringLiteral("雅加达"), {QStringLiteral("Jakarta"), QStringLiteral("ID")}},
        {QStringLiteral("河内"), {QStringLiteral("Hanoi"), QStringLiteral("VN")}},
        {QStringLiteral("马尼拉"), {QStringLiteral("Manila"), QStringLiteral("PH")}},
        {QStringLiteral("新德里"), {QStringLiteral("New Delhi"), QStringLiteral("IN")}},
        {QStringLiteral("孟买"), {QStringLiteral("Mumbai"), QStringLiteral("IN")}},
        {QStringLiteral("悉尼"), {QStringLiteral("Sydney"), QStringLiteral("AU")}},
        {QStringLiteral("墨尔本"), {QStringLiteral("Melbourne"), QStringLiteral("AU")}},
        {QStringLiteral("奥克兰"), {QStringLiteral("Auckland"), QStringLiteral("NZ")}},
        {QStringLiteral("多伦多"), {QStringLiteral("Toronto"), QStringLiteral("CA")}},
        {QStringLiteral("温哥华"), {QStringLiteral("Vancouver"), QStringLiteral("CA")}},
        {QStringLiteral("墨西哥城"), {QStringLiteral("Mexico City"), QStringLiteral("MX")}},
        {QStringLiteral("圣保罗"), {QStringLiteral("Sao Paulo"), QStringLiteral("BR")}},
        {QStringLiteral("里约热内卢"), {QStringLiteral("Rio de Janeiro"), QStringLiteral("BR")}},
        {QStringLiteral("开罗"), {QStringLiteral("Cairo"), QStringLiteral("EG")}},
        {QStringLiteral("伊斯坦布尔"), {QStringLiteral("Istanbul"), QStringLiteral("TR")}},
        {QStringLiteral("迪拜"), {QStringLiteral("Dubai"), QStringLiteral("AE")}},
    };
    return aliases;
}

QString candidateKey(const WeatherLocationCandidate &candidate)
{
    return compactKey(candidate.cityName)
        + QLatin1Char('|')
        + compactKey(candidate.adminName)
        + QLatin1Char('|')
        + compactKey(candidate.countryCode)
        + QLatin1Char('|')
        + QString::number(candidate.latitude, 'f', 4)
        + QLatin1Char('|')
        + QString::number(candidate.longitude, 'f', 4);
}

int placeRank(const QString &featureCode)
{
    const QString code = featureCode.trimmed().toUpper();
    if (code == QStringLiteral("PPLC"))
    {
        return 5;
    }
    if (code == QStringLiteral("PPLA"))
    {
        return 4;
    }
    if (code == QStringLiteral("PPLA2"))
    {
        return 3;
    }
    if (code == QStringLiteral("PPLA3") || code == QStringLiteral("PPLA4"))
    {
        return 2;
    }
    if (code == QStringLiteral("PPL"))
    {
        return 1;
    }
    return 0;
}

bool isPlausibleAlternative(
    const WeatherLocationCandidate &selected,
    const WeatherLocationCandidate &candidate)
{
    if (selected.population > 0 && candidate.population > 0)
    {
        const qint64 populationFloor = qMax<qint64>(50000, selected.population / 5);
        if (candidate.population >= populationFloor)
        {
            return true;
        }
    }

    const int selectedRank = placeRank(selected.featureCode);
    const int candidateRank = placeRank(candidate.featureCode);
    if (candidateRank >= 3 && candidateRank >= selectedRank - 1)
    {
        return true;
    }

    return selected.population <= 0
        && candidate.population <= 0
        && qAbs(candidateRank - selectedRank) <= 1;
}
}

WeatherLocationSearch WeatherLocationResolver::resolveSearch(const QString &cityText)
{
    WeatherLocationSearch result;
    result.originalName = cityText.trimmed();
    QString cityName = result.originalName.simplified();

    const QStringList qualifiedParts = cityName.split(
        QRegularExpression(QStringLiteral("[,，]")),
        Qt::SkipEmptyParts);
    if (qualifiedParts.size() >= 2)
    {
        const QString trailingCountryCode = countryCodeForText(qualifiedParts.last());
        const QString leadingCountryCode = countryCodeForText(qualifiedParts.first());
        if (!trailingCountryCode.isEmpty())
        {
            result.countryCode = trailingCountryCode;
            cityName = qualifiedParts.first().trimmed();
        }
        else if (!leadingCountryCode.isEmpty())
        {
            result.countryCode = leadingCountryCode;
            cityName = qualifiedParts.last().trimmed();
        }
    }

    if (result.countryCode.isEmpty())
    {
        for (const auto &alias : countryAliases())
        {
            const QString aliasText = alias.first.trimmed();
            if (aliasText.isEmpty() || cityName.size() <= aliasText.size() + 1)
            {
                continue;
            }

            if (isAsciiText(aliasText))
            {
                if (cityName.startsWith(aliasText, Qt::CaseInsensitive)
                    && cityName.at(aliasText.size()).isSpace())
                {
                    result.countryCode = alias.second;
                    cityName = cityName.mid(aliasText.size()).trimmed();
                    break;
                }
                if (cityName.endsWith(aliasText, Qt::CaseInsensitive)
                    && cityName.at(cityName.size() - aliasText.size() - 1).isSpace())
                {
                    result.countryCode = alias.second;
                    cityName = cityName.left(cityName.size() - aliasText.size()).trimmed();
                    break;
                }
                continue;
            }

            if (cityName.startsWith(aliasText, Qt::CaseInsensitive)
                && cityName.mid(aliasText.size()).trimmed().size() >= 2)
            {
                result.countryCode = alias.second;
                cityName = cityName.mid(aliasText.size()).trimmed();
                break;
            }
            if (cityName.endsWith(aliasText, Qt::CaseInsensitive)
                && cityName.left(cityName.size() - aliasText.size()).trimmed().size() >= 2)
            {
                result.countryCode = alias.second;
                cityName = cityName.left(cityName.size() - aliasText.size()).trimmed();
                break;
            }
        }
    }

    static const QRegularExpression domesticProvinceCityRegex(
        QStringLiteral("^[\\x{4e00}-\\x{9fff}]{2,8}(?:省|自治区|特别行政区)([\\x{4e00}-\\x{9fff}]{2,8}市?)$"));
    const QRegularExpressionMatch provinceCityMatch = domesticProvinceCityRegex.match(cityName);
    if (provinceCityMatch.hasMatch())
    {
        cityName = provinceCityMatch.captured(1).trimmed();
        if (result.countryCode.isEmpty())
        {
            result.countryCode = QStringLiteral("CN");
        }
    }

    const QString aliasKey = cityAliasKey(cityName);
    const auto alias = cityAliases().constFind(aliasKey);
    if (alias != cityAliases().constEnd())
    {
        result.queryName = alias->queryName;
        if (result.countryCode.isEmpty())
        {
            result.countryCode = alias->countryCode;
        }
    }
    else
    {
        result.queryName = cityName.trimmed();
        if (result.queryName.size() > 2 && result.queryName.endsWith(QStringLiteral("市")))
        {
            result.queryName.chop(1);
        }
    }

    result.countryCode = result.countryCode.trimmed().toUpper();
    return result;
}

bool WeatherLocationResolver::parseIpLocationResponse(
    const QByteArray &payload,
    WeatherIpLocationProvider provider,
    WeatherLocation *location)
{
    if (location == nullptr)
    {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("error")).toBool(false)
        || (root.contains(QStringLiteral("success")) && !root.value(QStringLiteral("success")).toBool(true)))
    {
        return false;
    }

    const QJsonValue latitude = root.value(QStringLiteral("latitude"));
    const QJsonValue longitude = root.value(QStringLiteral("longitude"));
    const QString cityName = root.value(QStringLiteral("city")).toString().trimmed();
    if (!latitude.isDouble() || !longitude.isDouble() || cityName.isEmpty())
    {
        return false;
    }

    WeatherLocation parsed;
    parsed.cityName = cityName;
    parsed.adminName = root.value(QStringLiteral("region")).toString().trimmed();
    parsed.countryName = provider == WeatherIpLocationProvider::IpApiCo
        ? root.value(QStringLiteral("country_name")).toString().trimmed()
        : root.value(QStringLiteral("country")).toString().trimmed();
    const QJsonValue timezone = root.value(QStringLiteral("timezone"));
    parsed.timezone = timezone.isObject()
        ? timezone.toObject().value(QStringLiteral("id")).toString().trimmed()
        : timezone.toString().trimmed();
    parsed.latitude = latitude.toDouble();
    parsed.longitude = longitude.toDouble();
    parsed.source = WeatherLocationSource::IpFallback;
    *location = parsed;
    return true;
}

QVector<WeatherLocationCandidate> WeatherLocationResolver::selectRelevantCandidates(
    const QVector<WeatherLocationCandidate> &candidates,
    const WeatherLocationSearch &search)
{
    QVector<WeatherLocationCandidate> uniqueCandidates;
    QSet<QString> seen;
    for (const WeatherLocationCandidate &candidate : candidates)
    {
        if (!search.countryCode.isEmpty()
            && !candidate.countryCode.isEmpty()
            && candidate.countryCode.compare(search.countryCode, Qt::CaseInsensitive) != 0)
        {
            continue;
        }

        const QString key = candidateKey(candidate);
        if (seen.contains(key))
        {
            continue;
        }
        seen.insert(key);
        uniqueCandidates.append(candidate);
    }

    if (uniqueCandidates.size() <= 1)
    {
        return uniqueCandidates;
    }

    QVector<WeatherLocationCandidate> relevantCandidates;
    relevantCandidates.append(uniqueCandidates.first());
    for (int index = 1; index < uniqueCandidates.size(); ++index)
    {
        if (isPlausibleAlternative(uniqueCandidates.first(), uniqueCandidates.at(index)))
        {
            relevantCandidates.append(uniqueCandidates.at(index));
        }
    }
    return relevantCandidates;
}

bool WeatherLocationResolver::hasAmbiguity(const QVector<WeatherLocationCandidate> &candidates)
{
    if (candidates.size() <= 1)
    {
        return false;
    }

    const QString firstKey = candidateKey(candidates.first());
    for (int index = 1; index < candidates.size(); ++index)
    {
        if (candidateKey(candidates.at(index)) != firstKey)
        {
            return true;
        }
    }
    return false;
}
