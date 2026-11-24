#include "CharacterEffects.h"
#include "CharacterMediaSpec.h"
#include <QFile>
#include <QJsonArray>
#include <QtEndian>
#include <cmath>

namespace
{
bool fail(QString *error, const QString &message)
{
    if (error) *error = QStringLiteral("effects: ") + message;
    return false;
}
bool keys(const QJsonObject &object, const QStringList &allowed)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}
bool number(const QJsonObject &o, const QString &key, double min, double max, double fallback, double *result)
{
    const QJsonValue v = o.value(key);
    const double n = v.isUndefined() ? fallback : v.toDouble(qQNaN());
    if (!std::isfinite(n) || n < min || n > max) return false;
    *result = n;
    return true;
}
bool integer(const QJsonObject &o, const QString &key, int min, int max, int fallback, int *result)
{
    double n = 0;
    if (!number(o, key, min, max, fallback, &n) || n != std::floor(n)) return false;
    *result = int(n);
    return true;
}
QByteArray pngDimensions(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray header = f.read(24);
    if (header.size() != 24 || header.left(8) != QByteArray::fromHex("89504e470d0a1a0a")
        || header.mid(12, 4) != "IHDR") return {};
    return header.mid(16, 8);
}
}

bool CharacterEffects::parse(const QJsonObject &root, const QString &directory,
                             const QMap<QString, CharacterState> &states,
                             CharacterEffectSpec *out, QString *error)
{
    *out = {};
    if (!root.contains("effects")) return true;
    if (root.value("schemaVersion").toInt() != 2 || !root.value("effects").isObject())
        return fail(error, QStringLiteral("仅 v2 支持 effects 对象。"));
    const auto effects = root.value("effects").toObject();
    if (!keys(effects, {"breathing", "dragSway", "blink", "statusBadge"}))
        return fail(error, QStringLiteral("包含未知字段。"));
    for (auto it = effects.begin(); it != effects.end(); ++it)
        if (!it.value().isObject()) return fail(error, QStringLiteral("各效果必须是对象。"));
    if (effects.contains("breathing"))
    {
        const auto o = effects.value("breathing").toObject();
        if (!keys(o, {"states", "amplitude", "periodMs"}) || !o.value("states").isArray()
            || o.value("states").toArray().isEmpty()
            || !number(o, "amplitude", 0.001, 0.02, 0.008, &out->amplitude)
            || !integer(o, "periodMs", 1500, 10000, 3600, &out->periodMs))
            return fail(error, QStringLiteral("呼吸范围或字段错误。"));
        for (const auto &v : o.value("states").toArray())
        {
            if (!v.isString() || !states.contains(v.toString()) || out->breathingStates.contains(v.toString()))
                return fail(error, QStringLiteral("呼吸 states 必须引用存在且不重复的状态。"));
            out->breathingStates.append(v.toString());
        }
    }
    if (effects.contains("dragSway"))
    {
        const auto o = effects.value("dragSway").toObject();
        if (!keys(o, {"maxAngleDeg"}) || !number(o, "maxAngleDeg", 0.1, 6, 4, &out->maxAngleDeg))
            return fail(error, QStringLiteral("拖拽摆角范围为 0.1–6 度。"));
    }
    if (effects.contains("blink"))
    {
        const auto o = effects.value("blink").toObject();
        out->blinkState = o.value("state").toString();
        const auto state = states.value(out->blinkState);
        if (!keys(o, {"state", "closedFrame", "intervalMs", "holdMs"})
            || out->blinkState != "idle" || state.mediaType != "frames" || state.framePaths.size() != 1
            || !integer(o, "holdMs", 50, 300, 120, &out->holdMs))
            return fail(error, QStringLiteral("眨眼仅支持 idle 单张 PNG，holdMs 范围 50–300。"));
        if (o.contains("intervalMs"))
        {
            const auto a = o.value("intervalMs").toArray();
            if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble()
                || a[0].toDouble() != a[0].toInt() || a[1].toDouble() != a[1].toInt()
                || a[0].toInt() < 1000 || a[1].toInt() > 60000 || a[0].toInt() > a[1].toInt())
                return fail(error, QStringLiteral("眨眼 intervalMs 必须为递增的 1000–60000ms 整数区间。"));
            out->intervalMinMs = a[0].toInt(); out->intervalMaxMs = a[1].toInt();
        }
        if (!CharacterMediaSpec::resolveResource(directory, o.value("closedFrame").toString(), &out->closedFrame, error))
            return false;
        const auto dimensions = pngDimensions(out->closedFrame);
        if (dimensions.isEmpty() || dimensions != pngDimensions(state.framePaths.first()))
            return fail(error, QStringLiteral("闭眼素材须为与 idle 同画布的完整 PNG。"));
    }
    if (effects.contains("statusBadge"))
    {
        const auto o = effects.value("statusBadge").toObject();
        if (!keys(o, {"enabled"}) || !o.value("enabled").isBool())
            return fail(error, QStringLiteral("statusBadge.enabled 必须为布尔值。"));
        out->statusBadge = o.value("enabled").toBool();
    }
    return true;
}
