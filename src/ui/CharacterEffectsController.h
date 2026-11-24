#pragma once

#include "../character/CharacterPackage.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <functional>

struct CharacterEffectPose
{
    double scaleY = 1;
    double angle = 0;
    bool closed = false;
    QString badge;
};

class CharacterEffectsController : public QObject
{
public:
    explicit CharacterEffectsController(QObject *parent = nullptr);
    void configure(const CharacterEffectSpec &spec, bool enabled, bool lowPower);
    void setState(const QString &state);
    void setPresented(bool presented);
    void dragTo(double globalX);
    void endDrag();
    void setAttention(bool attention);
    CharacterEffectPose pose() const;
    const CharacterEffectSpec &spec() const { return m_spec; }
    bool active() const { return m_enabled && m_spec.enabled(); }
    bool timerActive() const { return m_timer.isActive(); }
    quint64 tickCount() const { return m_ticks; }
    std::function<void()> changed;

private:
    void refresh();
    void tick();
    void scheduleBlink();
    CharacterEffectSpec m_spec;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QString m_state;
    bool m_enabled = false, m_presented = false, m_lowPower = false, m_attention = false;
    bool m_dragging = false, m_haveDragPosition = false, m_closed = false;
    double m_lastX = 0, m_angle = 0;
    qint64 m_lastDragMs = 0, m_releaseMs = -1, m_nextBlinkMs = 0, m_closedUntilMs = 0;
    quint64 m_ticks = 0;
};
