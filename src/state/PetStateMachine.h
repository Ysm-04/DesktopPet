#pragma once

#include <QSet>
#include <QString>

enum class StateRequestSource
{
    Manual,
    Automatic,
    System,
};

class PetStateMachine
{
public:
    void setAvailableStates(const QSet<QString> &states);

    QString currentState() const;
    QString start();
    QString requestState(const QString &stateName, StateRequestSource source = StateRequestSource::Manual);
    QString beginDrag();
    QString endDrag();
    QString finishState(const QString &nextState);

private:
    QString resolveState(const QString &stateName) const;
    int priorityOf(const QString &stateName) const;
    bool shouldKeepCurrentState(const QString &requestedState, StateRequestSource source) const;

    QSet<QString> m_availableStates;
    QString m_currentState;
    bool m_dragging = false;
};
