#pragma once

#include <QSoundEffect>
#include <QString>

class ReminderSoundPlayer
{
public:
    void play(const QString &soundId, double volume);

private:
    QSoundEffect m_soundEffect;
};
