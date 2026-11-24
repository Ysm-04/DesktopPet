#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>

namespace CharacterTool
{
struct Result
{
    int code = 0;
    QJsonObject data;
    QString error;
};
using Progress = std::function<void(const QJsonObject &)>;
Result execute(const QString &command, const QJsonObject &recipe, Progress progress,
               const std::function<bool()> &cancelled = {});
}
