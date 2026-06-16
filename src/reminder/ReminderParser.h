#pragma once

#include "ReminderTypes.h"

#include <QDateTime>
#include <QString>

class ReminderParser
{
public:
    ReminderCommand parse(const QString &text, const QDateTime &now = QDateTime::currentDateTime()) const;

private:
    ReminderCommand parseCreateCommand(const QString &text, const QDateTime &now) const;
    ReminderCommand parseRecurringCreateCommand(const QString &text, const QDateTime &now) const;
    QString extractTitle(QString text, const QString &timeExpression) const;
};
