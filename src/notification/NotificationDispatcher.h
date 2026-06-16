#pragma once

#include <QString>

#include <functional>

class NotificationDispatcher
{
public:
    using ShowCallback = std::function<bool(const QString &, const QString &)>;

    void setShowCallback(ShowCallback callback);
    bool showReminder(const QString &title, const QString &message) const;

private:
    ShowCallback m_showCallback;
};
