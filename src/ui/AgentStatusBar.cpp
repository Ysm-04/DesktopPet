#include "AgentStatusBar.h"
#include "../agent_bridge/AgentStatusCodec.h"

#include <QApplication>
#include <QFont>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace
{
constexpr int AnchorSpacingPx = 8;
constexpr int MaxVisibleCapsules = 4;
constexpr int RowKeyRole = Qt::UserRole;
constexpr int SessionKeyRole = Qt::UserRole + 1;
constexpr int MoreRole = Qt::UserRole + 2;

QString stateColor(AgentState state)
{
    switch (state)
    {
    case AgentState::NeedsInput: return QStringLiteral("#996215");
    case AgentState::Error: return QStringLiteral("#b54c4c");
    case AgentState::Working: return QStringLiteral("#3768ad");
    case AgentState::Done: return QStringLiteral("#367b64");
    default: return QStringLiteral("#788497");
    }
}
QString stateText(const AgentSessionSnapshot &session)
{
    if (session.stale) return QStringLiteral("待同步");
    switch (session.state)
    {
    case AgentState::Working: return QStringLiteral("进行中");
    case AgentState::NeedsInput: return QStringLiteral("待你确认");
    case AgentState::Error: return QStringLiteral("遇到问题");
    case AgentState::Done: return QStringLiteral("本轮完成");
    default: return QStringLiteral("已结束");
    }
}
QString phaseText(const QString &phase)
{
    if (phase == QStringLiteral("started")) return QStringLiteral("执行中");
    if (phase == QStringLiteral("succeeded")) return QStringLiteral("成功");
    if (phase == QStringLiteral("failed")) return QStringLiteral("失败");
    if (phase == QStringLiteral("cancelled")) return QStringLiteral("已取消");
    return QStringLiteral("已结束");
}
QString kindText(const QString &kind)
{
    if (kind == QStringLiteral("command")) return QStringLiteral("命令");
    if (kind == QStringLiteral("read")) return QStringLiteral("读取");
    if (kind == QStringLiteral("edit")) return QStringLiteral("修改");
    if (kind == QStringLiteral("search")) return QStringLiteral("搜索");
    return QStringLiteral("工具");
}
QString ageText(qint64 updated, qint64 now)
{
    const qint64 seconds = qMax<qint64>(0, now - updated) / 1000;
    if (seconds < 10) return QStringLiteral("刚刚");
    if (seconds < 60) return QStringLiteral("%1 秒前").arg(seconds);
    return QStringLiteral("%1 分钟前").arg(seconds / 60);
}
QMap<QString, QVector<AgentSessionSnapshot>> grouped(const QVector<AgentSessionSnapshot> &sessions)
{
    QMap<QString, QVector<AgentSessionSnapshot>> result;
    for (const auto &session : sessions) result[session.source].append(session);
    return result;
}
QStringList orderedSources(const QMap<QString, QVector<AgentSessionSnapshot>> &groups)
{
    QStringList sources;
    for (const QString &known : {QStringLiteral("codex"), QStringLiteral("antigravity"), QStringLiteral("dsh")})
        if (groups.contains(known)) sources.append(known);
    for (const QString &source : groups.keys()) if (!sources.contains(source)) sources.append(source);
    return sources;
}
AgentSessionSnapshot primary(const QVector<AgentSessionSnapshot> &sessions, qint64 now)
{
    AgentSessionSnapshot selected = sessions.first();
    int best = -1;
    for (const auto &session : sessions)
    {
        const bool live = !session.stale && session.expiresAtMs > now && session.state != AgentState::Idle;
        const int priority = live ? AgentStatusCodec::statePriority(session.state) + 10 : 1;
        if (priority > best || (priority == best && session.updatedAtMs > selected.updatedAtMs))
        {
            best = priority;
            selected = session;
        }
    }
    return selected;
}
QScreen *screenForRect(const QRect &rect)
{
    if (rect.isValid()) if (QScreen *screen = QGuiApplication::screenAt(rect.center())) return screen;
    return QGuiApplication::primaryScreen();
}
}

AgentStatusBar::AgentStatusBar(QWidget *parent)
    : QWidget(parent), m_capsuleLayout(new QGridLayout(this)), m_detailPopup(std::make_unique<QWidget>())
{
    setObjectName(QStringLiteral("AgentStatusBar"));
    setWindowTitle(QStringLiteral("Agent 联动状态"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    QFont uiFont;
    uiFont.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
    uiFont.setPointSizeF(9);
    setFont(uiFont);
    m_detailPopup->setFont(uiFont);
    m_capsuleLayout->setContentsMargins(2, 2, 2, 2);
    m_capsuleLayout->setHorizontalSpacing(6);
    m_capsuleLayout->setVerticalSpacing(6);
    m_capsuleLayout->setSizeConstraint(QLayout::SetFixedSize);
    m_detailPopup->setWindowTitle(QStringLiteral("Agent 联动详情"));
    m_detailPopup->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_detailPopup->setObjectName(QStringLiteral("AgentDetailPopup"));
    m_detailPopup->setStyleSheet(QStringLiteral(
        "QWidget#AgentDetailPopup { background: #ffffff; border: 1px solid #e1e7ef; border-radius: 14px; }"
        "QLabel { color: #243042; border: none; background: transparent; }"
        "QPushButton { color: #66758b; background: transparent; border: none; padding: 5px 8px; border-radius: 7px; }"
        "QPushButton:hover { background: #eef2f7; }"
        "QTreeWidget { border: none; background: #ffffff; color: #243042; outline: none; }"
        "QTreeWidget::item { padding: 7px 3px; border: none; }"
        "QTreeWidget::item:hover { background: #f5f7fa; }"
        "QTreeWidget::item:selected { background: #edf3fc; color: #243042; }"
        "QHeaderView::section { background: #f5f7fa; color: #66758b; border: none; padding: 5px; }"));
    auto *layout = new QVBoxLayout(m_detailPopup.get());
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    m_titleLabel = new QLabel(QStringLiteral("Agent 联动"), m_detailPopup.get());
    m_titleLabel->setObjectName(QStringLiteral("AgentDetailTitle"));
    QFont titleFont = uiFont; titleFont.setPointSizeF(11); titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    header->addWidget(m_titleLabel, 1);
    auto *closeButton = new QPushButton(QStringLiteral("收起"), m_detailPopup.get());
    connect(closeButton, &QPushButton::clicked, this, [this] { m_detailPopup->hide(); });
    header->addWidget(closeButton);
    layout->addLayout(header);
    m_countLabel = new QLabel(m_detailPopup.get());
    m_countLabel->setObjectName(QStringLiteral("AgentDetailCount"));
    m_countLabel->setStyleSheet(QStringLiteral("color: #66758b;"));
    layout->addWidget(m_countLabel);
    m_tree = new QTreeWidget(m_detailPopup.get());
    m_tree->setObjectName(QStringLiteral("AgentConversationTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QStringLiteral("对话 / 活动"), QStringLiteral("状态"), QStringLiteral("最近更新")});
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_tree->setColumnWidth(1, 78);
    m_tree->setColumnWidth(2, 74);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        QTreeWidgetItem *item = m_tree->itemAt(position);
        if (!item) return;
        const QString key = item->data(0, SessionKeyRole).toString();
        if (key.isEmpty()) return;
        QMenu menu(m_tree);
        auto *hideAction = menu.addAction(QStringLiteral("隐藏此对话（不会停止任务）"));
        if (menu.exec(m_tree->viewport()->mapToGlobal(position)) == hideAction && m_dismissSessionCallback)
            m_dismissSessionCallback(key);
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item->data(0, MoreRole).toBool()) return;
        const QString key = item->data(0, SessionKeyRole).toString();
        if (m_showAllActivities.contains(key)) m_showAllActivities.remove(key);
        else m_showAllActivities.insert(key);
        rebuildDetailPopup(m_lastNowMs);
    });
    layout->addWidget(m_tree, 1);
    auto *footer = new QHBoxLayout;
    auto *hint = new QLabel(QStringLiteral("右键隐藏对话 · 记录仅存本次运行"), m_detailPopup.get());
    hint->setStyleSheet(QStringLiteral("color: #788497;"));
    footer->addWidget(hint, 1);
    auto *completedButton = new QPushButton(QStringLiteral("清除已结束"), m_detailPopup.get());
    connect(completedButton, &QPushButton::clicked, this, [this] { if (m_clearCompletedCallback) m_clearCompletedCallback(); });
    footer->addWidget(completedButton);
    auto *clearButton = new QPushButton(QStringLiteral("清空"), m_detailPopup.get());
    connect(clearButton, &QPushButton::clicked, this, [this] { if (m_clearAllCallback) m_clearAllCallback(); });
    footer->addWidget(clearButton);
    layout->addLayout(footer);
}

AgentStatusBar::~AgentStatusBar() { hideAll(); }
void AgentStatusBar::setDismissSessionCallback(std::function<void(const QString &)> callback) { m_dismissSessionCallback = std::move(callback); }
void AgentStatusBar::setClearAllCallback(std::function<void()> callback) { m_clearAllCallback = std::move(callback); }
void AgentStatusBar::setClearCompletedCallback(std::function<void()> callback) { m_clearCompletedCallback = std::move(callback); }

void AgentStatusBar::setSessions(const QVector<AgentSessionSnapshot> &sessions, qint64 nowMs)
{
    m_sessions = sessions;
    m_lastNowMs = nowMs;
    if (m_sessions.isEmpty()) { hideAll(); return; }
    rebuildCapsules();
    if (m_detailPopup->isVisible()) { rebuildDetailPopup(nowMs); updateDetailPopupPosition(); }
    updatePosition();
    if (!isVisible()) show();
}
void AgentStatusBar::updateAnchorRect(const QRect &rect)
{
    m_anchorRect = rect;
    if (isVisible()) updatePosition();
    if (m_detailPopup->isVisible()) updateDetailPopupPosition();
}
void AgentStatusBar::hideAll()
{
    m_detailPopup->hide();
    hide();
}
void AgentStatusBar::rebuildCapsules()
{
    const auto groups = grouped(m_sessions);
    const QStringList sources = orderedSources(groups);
    QSet<QString> wanted;
    int row = 0, column = 0, rowWidth = 0;
    const QScreen *screen = screenForRect(m_anchorRect);
    const int maxWidth = screen ? qMin(720, screen->availableGeometry().width() - 16) : 720;
    auto place = [&](const QString &source, const QString &label, const QString &tooltip, AgentState state) {
        wanted.insert(source);
        auto *button = m_capsules.value(source, nullptr);
        if (!button)
        {
            button = new QPushButton(this);
            button->setObjectName(QStringLiteral("AgentCapsule_") + source);
            button->setFocusPolicy(Qt::NoFocus);
            button->setCursor(Qt::PointingHandCursor);
            connect(button, &QPushButton::clicked, this, [this, source] { toggleDetailPopup(source); });
            m_capsules.insert(source, button);
        }
        button->setText(label);
        button->setToolTip(tooltip);
        button->setAccessibleName(label);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { background: #ffffff; color: %1; border: 1px solid #cbd7e6; border-radius: 13px; padding: 5px 11px; }"
            "QPushButton:hover { background: #eef3fa; }").arg(stateColor(state)));
        const int width = button->sizeHint().width();
        if (column && rowWidth + width > maxWidth) { ++row; column = 0; rowWidth = 0; }
        m_capsuleLayout->addWidget(button, row, column++);
        rowWidth += width + 6;
        button->show();
    };
    for (int i = 0; i < qMin(MaxVisibleCapsules, sources.size()); ++i)
    {
        const QString source = sources[i];
        const auto sessions = groups.value(source);
        const auto selected = primary(sessions, m_lastNowMs);
        int active = 0;
        for (const auto &session : sessions)
            if (!session.stale && session.state != AgentState::Done && session.state != AgentState::Idle) ++active;
        const QString badge = active > 1 ? QStringLiteral("  %1").arg(active) : QString();
        place(source, QStringLiteral("● %1 · %2%3").arg(AgentStatusCodec::sourceDisplayLabel(source), stateText(selected), badge),
            QStringLiteral("%1 · %2 个对话\n点击查看全部 Agent、对话与活动记录")
                .arg(AgentStatusCodec::sourceDisplayLabel(source)).arg(sessions.size()),
            selected.stale ? AgentState::Idle : selected.state);
    }
    if (sources.size() > MaxVisibleCapsules)
        place(QStringLiteral("more"), QStringLiteral("+%1").arg(sources.size() - MaxVisibleCapsules), QStringLiteral("查看全部 Agent"), AgentState::Idle);
    for (const QString &key : m_capsules.keys())
        if (!wanted.contains(key)) { delete m_capsules.take(key); }
}

void AgentStatusBar::rebuildDetailPopup(qint64 nowMs)
{
    const int scrollPosition = m_tree->verticalScrollBar()->value();
    m_tree->setUpdatesEnabled(false);
    const auto groups = grouped(m_sessions);
    const auto sources = orderedSources(groups);
    QSet<QString> wanted;
    auto row = [&](const QString &key, QTreeWidgetItem *parent) {
        wanted.insert(key);
        auto *item = m_rows.value(key, nullptr);
        if (!item)
        {
            item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
            item->setData(0, RowKeyRole, key);
            m_rows.insert(key, item);
        }
        return item;
    };
    int liveCount = 0;
    for (const auto &session : m_sessions)
        if (!session.stale && session.state != AgentState::Done && session.state != AgentState::Idle) ++liveCount;
    m_countLabel->setText(QStringLiteral("%1 个 Agent · %2 个活动对话 · %3 个最近对话")
        .arg(groups.size()).arg(liveCount).arg(m_sessions.size() - liveCount));
    int sourceIndex = 0;
    for (const QString &source : sources)
    {
        const QString sourceKey = QStringLiteral("source:") + source;
        const bool newSource = !m_rows.contains(sourceKey);
        auto *group = row(sourceKey, nullptr);
        if (m_tree->indexOfTopLevelItem(group) != sourceIndex)
        {
            m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(group));
            m_tree->insertTopLevelItem(sourceIndex, group);
        }
        ++sourceIndex;
        group->setText(0, AgentStatusCodec::sourceDisplayLabel(source));
        group->setText(1, QStringLiteral("%1 个对话").arg(groups.value(source).size()));
        QFont bold = font(); bold.setWeight(QFont::DemiBold); group->setFont(0, bold);
        if (newSource) group->setExpanded(true);
        auto sessions = groups.value(source);
        std::sort(sessions.begin(), sessions.end(), [](const auto &a, const auto &b) {
            if (a.updatedAtMs != b.updatedAtMs) return a.updatedAtMs > b.updatedAtMs;
            return a.sessionKey < b.sessionKey;
        });
        bool first = true;
        for (const auto &session : sessions)
        {
            const QString sessionRowKey = QStringLiteral("session:") + session.sessionKey;
            const bool newSession = !m_rows.contains(sessionRowKey);
            auto *conversation = row(sessionRowKey, group);
            conversation->setData(0, SessionKeyRole, session.sessionKey);
            QString title = session.title;
            if (title.isEmpty()) title = session.workspace.isEmpty() ? QStringLiteral("对话") : session.workspace;
            title += session.sessionId.isEmpty() ? QStringLiteral(" · 未识别对话") : QStringLiteral(" · %1").arg(session.sessionId.right(6));
            conversation->setText(0, title);
            conversation->setToolTip(0, title + QLatin1Char('\n') + session.summary);
            conversation->setText(1, stateText(session));
            conversation->setForeground(1, QColor(stateColor(session.stale ? AgentState::Idle : session.state)));
            conversation->setText(2, ageText(session.updatedAtMs, nowMs));
            if (newSession) conversation->setExpanded(first && (m_selectedSource.isEmpty() || m_selectedSource == source));
            first = false;
            const bool all = m_showAllActivities.contains(session.sessionKey);
            const int start = all ? 0 : qMax(0, session.activities.size() - 5);
            int activityIndex = 0;
            for (int i = start; i < session.activities.size(); ++i)
            {
                const auto &activity = session.activities[i];
                const QString activityKey = sessionRowKey + QStringLiteral("/activity:") + activity.id;
                auto *item = row(activityKey, conversation);
                if (conversation->indexOfChild(item) != activityIndex)
                {
                    conversation->takeChild(conversation->indexOfChild(item));
                    conversation->insertChild(activityIndex, item);
                }
                ++activityIndex;
                item->setData(0, SessionKeyRole, session.sessionKey);
                item->setText(0, kindText(activity.kind) + QStringLiteral(" · ") + (activity.summary.isEmpty() ? activity.tool : activity.summary));
                item->setToolTip(0, activity.tool + QLatin1Char('\n') + activity.summary);
                const bool pending = activity.phase == QStringLiteral("started") &&
                    (session.stale || session.state == AgentState::Done || session.state == AgentState::Idle);
                item->setText(1, pending ? QStringLiteral("待同步") : phaseText(activity.phase));
                item->setForeground(1, QColor(stateColor(activity.phase == QStringLiteral("failed") ? AgentState::Error :
                    activity.phase == QStringLiteral("succeeded") ? AgentState::Done : AgentState::Idle)));
                item->setText(2, ageText(activity.updatedAtMs, nowMs));
            }
            if (session.activities.size() > 5)
            {
                auto *more = row(sessionRowKey + QStringLiteral("/more"), conversation);
                if (conversation->indexOfChild(more) != activityIndex)
                {
                    conversation->takeChild(conversation->indexOfChild(more));
                    conversation->insertChild(activityIndex, more);
                }
                more->setData(0, SessionKeyRole, session.sessionKey);
                more->setData(0, MoreRole, true);
                more->setText(0, all ? QStringLiteral("收起较早活动") : QStringLiteral("查看全部 %1 条活动").arg(session.activities.size()));
                more->setForeground(0, QColor(QStringLiteral("#3768ad")));
            }
        }
    }
    // Remove children before parents; header widgets are permanent and never rebuilt.
    QStringList obsolete;
    for (const QString &key : m_rows.keys()) if (!wanted.contains(key)) obsolete.append(key);
    std::sort(obsolete.begin(), obsolete.end(), [](const auto &a, const auto &b) { return a.size() > b.size(); });
    for (const QString &key : obsolete) delete m_rows.take(key);
    for (const QString &key : m_showAllActivities.values())
        if (!m_rows.contains(QStringLiteral("session:") + key)) m_showAllActivities.remove(key);
    m_tree->setUpdatesEnabled(true);
    m_tree->verticalScrollBar()->setValue(scrollPosition);
    const QScreen *screen = screenForRect(m_anchorRect);
    const QRect bounds = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    m_detailPopup->setFixedSize(qMin(540, bounds.width() - 16), qMin(490, bounds.height() - 24));
}

void AgentStatusBar::toggleDetailPopup(const QString &source)
{
    if (m_sessions.isEmpty()) return;
    if (m_detailPopup->isVisible() && m_selectedSource == source) { m_detailPopup->hide(); return; }
    m_selectedSource = source;
    rebuildDetailPopup(m_lastNowMs);
    auto *group = m_rows.value(QStringLiteral("source:") + source, nullptr);
    if (group)
    {
        group->setExpanded(true);
        if (group->childCount()) group->child(0)->setExpanded(true);
        m_tree->scrollToItem(group, QAbstractItemView::PositionAtTop);
    }
    updateDetailPopupPosition();
    m_detailPopup->show();
}

void AgentStatusBar::updatePosition()
{
    if (!m_anchorRect.isValid())
    {
        return;
    }

    adjustSize();
    const QSize barSize = sizeHint();
    resize(barSize);
    const QScreen *screen = screenForRect(m_anchorRect);
    const QRect bounds = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    int targetX = m_anchorRect.center().x() - barSize.width() / 2;
    int targetY = m_anchorRect.bottom() + AnchorSpacingPx;
    if (targetY + barSize.height() > bounds.bottom())
    {
        targetY = m_anchorRect.top() - barSize.height() - AnchorSpacingPx;
    }

    targetX = qBound(bounds.left() + 4, targetX, qMax(bounds.left() + 4, bounds.right() - barSize.width() - 4));
    targetY = qBound(bounds.top() + 4, targetY, qMax(bounds.top() + 4, bounds.bottom() - barSize.height() - 4));
    move(targetX, targetY);
}

void AgentStatusBar::updateDetailPopupPosition()
{
    if (!m_detailPopup)
    {
        return;
    }

    const QRect reference = isVisible() ? frameGeometry() : m_anchorRect;
    if (!reference.isValid())
    {
        return;
    }

    const QSize popupSize = m_detailPopup->sizeHint().expandedTo(m_detailPopup->size());
    const QScreen *screen = screenForRect(reference);
    const QRect bounds = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    int targetX = reference.center().x() - popupSize.width() / 2;
    int targetY = reference.top() - popupSize.height() - AnchorSpacingPx;
    if (targetY < bounds.top() + 4)
    {
        targetY = reference.bottom() + AnchorSpacingPx;
    }

    targetX = qBound(bounds.left() + 4, targetX, qMax(bounds.left() + 4, bounds.right() - popupSize.width() - 4));
    targetY = qBound(bounds.top() + 4, targetY, qMax(bounds.top() + 4, bounds.bottom() - popupSize.height() - 4));
    m_detailPopup->move(targetX, targetY);
}
