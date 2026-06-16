#include "NotificationDispatcher.h"

#include <utility>

void NotificationDispatcher::setShowCallback(ShowCallback callback)
{
    m_showCallback = std::move(callback);
}

bool NotificationDispatcher::showReminder(const QString &title, const QString &message) const
{
    if (m_showCallback)
    {
        return m_showCallback(title, message);
    }

    return false;
}
