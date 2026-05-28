#include "PetStateMachine.h"

namespace
{
const QString IdleState = QStringLiteral("idle");
const QString DragState = QStringLiteral("drag");
const QString ErrorState = QStringLiteral("error");
const QString ThinkState = QStringLiteral("think");
const QString TalkState = QStringLiteral("talk");
const QString HappyState = QStringLiteral("happy");
const QString SleepState = QStringLiteral("sleep");
}

void PetStateMachine::setAvailableStates(const QSet<QString> &states)
{
    m_availableStates = states;

    if (!m_currentState.isEmpty())
    {
        m_currentState = resolveState(m_currentState);
    }
}

QString PetStateMachine::currentState() const
{
    return m_currentState;
}

QString PetStateMachine::start()
{
    m_dragging = false;
    m_currentState = resolveState(IdleState);
    return m_currentState;
}

QString PetStateMachine::requestState(const QString &stateName, StateRequestSource source)
{
    if (m_dragging)
    {
        m_currentState = resolveState(DragState);
        return m_currentState;
    }

    const QString requestedState = resolveState(stateName);
    if (shouldKeepCurrentState(requestedState, source))
    {
        return m_currentState;
    }

    m_currentState = requestedState;
    return m_currentState;
}

QString PetStateMachine::beginDrag()
{
    m_dragging = true;
    m_currentState = resolveState(DragState);
    return m_currentState;
}

QString PetStateMachine::endDrag()
{
    m_dragging = false;
    m_currentState = resolveState(IdleState);
    return m_currentState;
}

QString PetStateMachine::finishState(const QString &nextState)
{
    if (m_dragging)
    {
        m_currentState = resolveState(DragState);
        return m_currentState;
    }

    if (!nextState.isEmpty())
    {
        m_currentState = resolveState(nextState);
    }
    else
    {
        m_currentState = resolveState(IdleState);
    }

    return m_currentState;
}

QString PetStateMachine::resolveState(const QString &stateName) const
{
    if (m_availableStates.contains(stateName))
    {
        return stateName;
    }

    if (m_availableStates.contains(IdleState))
    {
        return IdleState;
    }

    return {};
}

int PetStateMachine::priorityOf(const QString &stateName) const
{
    if (stateName == DragState)
    {
        return 60;
    }

    if (stateName == ErrorState)
    {
        return 50;
    }

    if (stateName == ThinkState)
    {
        return 40;
    }

    if (stateName == TalkState)
    {
        return 30;
    }

    if (stateName == HappyState)
    {
        return 20;
    }

    if (stateName == SleepState)
    {
        return 10;
    }

    return 0;
}

bool PetStateMachine::shouldKeepCurrentState(const QString &requestedState, StateRequestSource source) const
{
    if (requestedState.isEmpty() || m_currentState.isEmpty())
    {
        return false;
    }

    if (source == StateRequestSource::Manual || source == StateRequestSource::System)
    {
        return false;
    }

    if (m_currentState == IdleState)
    {
        return false;
    }

    return priorityOf(m_currentState) > priorityOf(requestedState);
}
