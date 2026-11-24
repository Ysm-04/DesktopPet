#pragma once

#include "../agent_bridge/AgentStatusTypes.h"

#include <QRect>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

#include <functional>
#include <memory>

class QGridLayout;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class AgentStatusBar : public QWidget
{
public:
    explicit AgentStatusBar(QWidget *parent = nullptr);
    ~AgentStatusBar() override;

    void setDismissSessionCallback(std::function<void(const QString &sessionKey)> callback);
    void setClearAllCallback(std::function<void()> callback);
    void setClearCompletedCallback(std::function<void()> callback);

    void setSessions(const QVector<AgentSessionSnapshot> &sessions, qint64 nowMs);
    void updateAnchorRect(const QRect &petVisibleRect);
    void hideAll();

private:
    void rebuildCapsules();
    void rebuildDetailPopup(qint64 nowMs);
    void toggleDetailPopup(const QString &source = {});
    void updatePosition();
    void updateDetailPopupPosition();

    QVector<AgentSessionSnapshot> m_sessions;
    QRect m_anchorRect;
    qint64 m_lastNowMs = 0;
    QGridLayout *m_capsuleLayout = nullptr;
    std::unique_ptr<QWidget> m_detailPopup;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QTreeWidget *m_tree = nullptr;
    QMap<QString, QPushButton *> m_capsules;
    QMap<QString, QTreeWidgetItem *> m_rows;
    QSet<QString> m_showAllActivities;
    QString m_selectedSource;
    std::function<void()> m_clearCompletedCallback;
    std::function<void(const QString &)> m_dismissSessionCallback;
    std::function<void()> m_clearAllCallback;
};
