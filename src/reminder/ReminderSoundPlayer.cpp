#include "ReminderSoundPlayer.h"

#include "ReminderSoundRepository.h"

#include <QFileInfo>
#include <QUrl>
#include <QtGlobal>

void ReminderSoundPlayer::play(const QString &soundId, double volume)
{
    const QString path = ReminderSoundRepository::soundPath(soundId);
    if (!QFileInfo::exists(path))
    {
        return;
    }

    m_soundEffect.stop();
    m_soundEffect.setLoopCount(1);
    m_soundEffect.setVolume(static_cast<float>(qBound(0.0, volume, 1.0)));
    m_soundEffect.setSource(QUrl::fromLocalFile(path));
    m_soundEffect.play();
}
