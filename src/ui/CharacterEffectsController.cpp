#include "CharacterEffectsController.h"
#include <QRandomGenerator>
#include <cmath>

CharacterEffectsController::CharacterEffectsController(QObject *parent) : QObject(parent)
{
    m_clock.start();
    connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
}
void CharacterEffectsController::configure(const CharacterEffectSpec &spec, bool enabled, bool lowPower)
{
    m_timer.stop();
    m_spec = spec; m_enabled = enabled; m_lowPower = lowPower;
    m_badgeState.clear();
    m_angle = 0; m_releaseMs = -1; m_dragging = m_haveDragPosition = m_closed = false;
    m_clock.restart(); scheduleBlink(); refresh();
    if (changed) changed();
}
void CharacterEffectsController::scheduleBlink()
{
    m_nextBlinkMs = m_clock.elapsed() + QRandomGenerator::global()->bounded(
        m_spec.intervalMinMs, m_spec.intervalMaxMs + 1);
}
void CharacterEffectsController::setState(const QString &state)
{
    if (m_state == state) return;
    m_state = state; m_closed = false; scheduleBlink(); refresh();
    if (changed) changed();
}
void CharacterEffectsController::setBadgeState(const QString &state)
{
    if (m_badgeState == state) return;
    m_badgeState = state;
    if (changed && active() && m_spec.statusBadge) changed();
}
void CharacterEffectsController::setAttention(bool attention)
{
    if (m_attention == attention) return;
    m_attention = attention;
    if (changed) changed();
}
void CharacterEffectsController::setPresented(bool presented)
{
    m_presented = presented;
    if (!presented)
    {
        m_angle = 0; m_releaseMs = -1; m_dragging = m_haveDragPosition = m_closed = false;
    }
    m_clock.restart(); scheduleBlink(); refresh();
    if (changed) changed();
}
void CharacterEffectsController::dragTo(double x)
{
    if (!active() || !m_presented || m_spec.maxAngleDeg <= 0) return;
    const qint64 now = m_clock.elapsed();
    if (m_haveDragPosition)
        m_angle = qBound(-m_spec.maxAngleDeg, (x - m_lastX) / qMax<qint64>(1, now - m_lastDragMs) * 0.8, m_spec.maxAngleDeg);
    m_dragging = m_haveDragPosition = true;
    m_lastX = x; m_lastDragMs = now; m_releaseMs = -1; refresh();
    if (changed) changed();
}
void CharacterEffectsController::endDrag()
{
    m_dragging = m_haveDragPosition = false;
    if (std::abs(m_angle) > 0.001) m_releaseMs = m_clock.elapsed();
    refresh();
}
void CharacterEffectsController::refresh()
{
    if (!active() || !m_presented) { m_timer.stop(); return; }
    const bool moving = m_dragging || m_releaseMs >= 0;
    if (moving || m_spec.breathingStates.contains(m_state))
        m_timer.start(moving ? (m_lowPower ? 67 : 33) : (m_lowPower ? 100 : 50));
    else if (!m_spec.closedFrame.isEmpty() && m_state == m_spec.blinkState)
        m_timer.start(int(qMax<qint64>(1, (m_closed ? m_closedUntilMs : m_nextBlinkMs) - m_clock.elapsed())));
    else m_timer.stop();
}
void CharacterEffectsController::tick()
{
    ++m_ticks;
    const qint64 now = m_clock.elapsed();
    if (m_releaseMs >= 0)
    {
        m_angle *= 0.67;
        if (now - m_releaseMs >= 800 || std::abs(m_angle) < 0.02)
        { m_angle = 0; m_releaseMs = -1; }
    }
    if (!m_spec.closedFrame.isEmpty() && m_state == m_spec.blinkState)
    {
        if (m_closed && now >= m_closedUntilMs) { m_closed = false; scheduleBlink(); }
        else if (!m_closed && now >= m_nextBlinkMs) { m_closed = true; m_closedUntilMs = now + m_spec.holdMs; }
    }
    refresh();
    if (changed) changed();
}
CharacterEffectPose CharacterEffectsController::pose() const
{
    CharacterEffectPose p;
    if (!active()) return p;
    if (m_presented && m_spec.breathingStates.contains(m_state))
        p.scaleY = 1 + m_spec.amplitude * std::sin(m_clock.elapsed() * 6.283185307179586 / m_spec.periodMs);
    p.angle = m_angle; p.closed = m_closed;
    if (m_spec.statusBadge)
    {
        const QString badgeState = m_badgeState.isEmpty() ? m_state : m_badgeState;
        if (m_attention || badgeState == QStringLiteral("error")) p.badge = QStringLiteral("!");
        else if (badgeState == QStringLiteral("think")) p.badge = QStringLiteral("···");
        else if (badgeState == QStringLiteral("sleep")) p.badge = QStringLiteral("Zzz");
    }
    return p;
}
