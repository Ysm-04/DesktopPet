#include "CharacterDialogueProvider.h"

#include <QRandomGenerator>
#include <QtGlobal>

QString CharacterDialogueProvider::sceneKey(CharacterDialogueScene scene)
{
    switch (scene)
    {
    case CharacterDialogueScene::Tap:
        return QStringLiteral("tap");
    case CharacterDialogueScene::Headpat:
        return QStringLiteral("headpat");
    case CharacterDialogueScene::DragDrop:
        return QStringLiteral("dragDrop");
    case CharacterDialogueScene::GreetMorning:
        return QStringLiteral("greetMorning");
    case CharacterDialogueScene::GreetAfternoon:
        return QStringLiteral("greetAfternoon");
    case CharacterDialogueScene::GreetEvening:
        return QStringLiteral("greetEvening");
    case CharacterDialogueScene::GreetLateNight:
        return QStringLiteral("greetLateNight");
    case CharacterDialogueScene::Sedentary:
        return QStringLiteral("sedentary");
    case CharacterDialogueScene::LateNightCare:
        return QStringLiteral("lateNightCare");
    case CharacterDialogueScene::AgentWorking:
        return QStringLiteral("agentWorking");
    case CharacterDialogueScene::AgentDone:
        return QStringLiteral("agentDone");
    case CharacterDialogueScene::AgentNeedsInput:
        return QStringLiteral("agentNeedsInput");
    case CharacterDialogueScene::AgentError:
        return QStringLiteral("agentError");
    }

    return QStringLiteral("tap");
}

CharacterDialogueScene CharacterDialogueProvider::greetingSceneForHour(int hour)
{
    if (hour >= 5 && hour <= 11)
    {
        return CharacterDialogueScene::GreetMorning;
    }

    if (hour >= 12 && hour <= 17)
    {
        return CharacterDialogueScene::GreetAfternoon;
    }

    if (hour >= 18 && hour <= 22)
    {
        return CharacterDialogueScene::GreetEvening;
    }

    return CharacterDialogueScene::GreetLateNight;
}

bool CharacterDialogueProvider::isLateNightHour(int hour)
{
    return hour >= 23 || hour < 5;
}

PetTouchZone CharacterDialogueProvider::classifyTouchZone(
    const QRect &visibleRect,
    const QPoint &point,
    double headRatio)
{
    if (!visibleRect.isValid() || visibleRect.height() <= 0 || !visibleRect.contains(point))
    {
        return PetTouchZone::None;
    }

    const double clampedRatio = qBound(0.1, headRatio, 0.8);
    const double relativeY = static_cast<double>(point.y() - visibleRect.top())
        / static_cast<double>(visibleRect.height());
    return relativeY <= clampedRatio ? PetTouchZone::Head : PetTouchZone::Body;
}

bool CharacterDialogueProvider::isCooldownElapsed(qint64 lastTriggerMs, qint64 nowMs, qint64 cooldownMs)
{
    if (lastTriggerMs < 0)
    {
        return true;
    }

    if (nowMs < lastTriggerMs)
    {
        return false;
    }

    return (nowMs - lastTriggerMs) >= qMax<qint64>(0, cooldownMs);
}

QStringList CharacterDialogueProvider::builtInFallbackLines(CharacterDialogueScene scene)
{
    switch (scene)
    {
    case CharacterDialogueScene::Tap:
        return {
            QStringLiteral("嗯？找我有事吗？双击我可以开始聊天哦。"),
            QStringLiteral("我在桌面这里陪着你呢，有需要随时叫我。"),
            QStringLiteral("想查天气或者设个小提醒吗？尽管交给我。"),
            QStringLiteral("稍微活动一下肩膀吧，我会帮你守好桌面的。"),
        };
    case CharacterDialogueScene::Headpat:
        return {
            QStringLiteral("哇……被摸头了，干劲好像又多了一点。"),
            QStringLiteral("嗯……好温暖，谢谢你的摸头。"),
            QStringLiteral("被这样照顾着，今天也会好好努力陪着你。"),
            QStringLiteral("嘿嘿，感觉状态变好了。"),
        };
    case CharacterDialogueScene::DragDrop:
        return {
            QStringLiteral("换好新位置啦，这里视野也不错。"),
            QStringLiteral("站稳了！放在这个角落刚合适。"),
            QStringLiteral("搬家完成，接下来继续一起专心做事吧。"),
        };
    case CharacterDialogueScene::GreetMorning:
        return {
            QStringLiteral("早安，新的一天开始啦，记得先喝口水再出发。"),
            QStringLiteral("早上好！今天有什么计划？有需要我记下的随时说。"),
            QStringLiteral("早安，精神状态还不错的话，我们就稳步推进吧。"),
        };
    case CharacterDialogueScene::GreetAfternoon:
        return {
            QStringLiteral("下午好，午后容易犯困，别忘了补充水分。"),
            QStringLiteral("午后时光也请加油，需要提醒或查天气的话随时告诉我。"),
            QStringLiteral("下午好，忙完手头这一段可以稍微舒展一下哦。"),
        };
    case CharacterDialogueScene::GreetEvening:
        return {
            QStringLiteral("晚上好，辛苦一天了，记得按时吃晚饭。"),
            QStringLiteral("傍晚了呢，工作和休息都要注意平衡。"),
            QStringLiteral("晚上好，今晚还想处理什么事情吗？我都陪着你。"),
        };
    case CharacterDialogueScene::GreetLateNight:
        return {
            QStringLiteral("夜深了还在忙吗？注意保护眼睛，别熬太晚。"),
            QStringLiteral("已经到深夜啦，如果差不多收尾了就早点休息吧。"),
            QStringLiteral("这么晚还亮着屏幕呢，我会安安静静陪着你的。"),
        };
    case CharacterDialogueScene::Sedentary:
        return {
            QStringLiteral("已经专注很久啦，站起来接杯水、活动一下肩颈吧。"),
            QStringLiteral("久坐有点累了吧？远眺窗外几十秒让眼睛放松一下。"),
            QStringLiteral("工作进度很重要，不过伸个懒腰再继续会更高效哦。"),
        };
    case CharacterDialogueScene::LateNightCare:
        return {
            QStringLiteral("时间不早啦，手头事务收个尾就准备休息吧。"),
            QStringLiteral("夜深了，连续熬夜会很消耗精力，明天再继续也来得及。"),
            QStringLiteral("记得保存好进度早点睡，晚安前的这一段我帮你看着时间。"),
        };
    case CharacterDialogueScene::AgentWorking:
        return {
            QStringLiteral("后台任务正在推进中，我先安静在一旁看着。"),
            QStringLiteral("正在处理工作流程，有关键进展我会提醒你。"),
            QStringLiteral("收到运行状态了，正在持续跟踪进度。"),
        };
    case CharacterDialogueScene::AgentDone:
        return {
            QStringLiteral("刚才的后台任务已经完成啦，记得抽空查看结果。"),
            QStringLiteral("任务顺利结束了，要顺便检查一下输出吗？"),
            QStringLiteral("已收到完成通知，辛苦了。"),
        };
    case CharacterDialogueScene::AgentNeedsInput:
        return {
            QStringLiteral("有任务正在等你确认或补充指示，方便时去处理一下吧。"),
            QStringLiteral("注意哦，外部助手正在等待你的反馈。"),
            QStringLiteral("前面卡在确认步骤了，切回去看一下吧。"),
        };
    case CharacterDialogueScene::AgentError:
        return {
            QStringLiteral("刚刚的后台任务似乎遇到了异常，建议去查看下具体日志。"),
            QStringLiteral("检测到任务报错，可能需要你人工介入排查一下。"),
            QStringLiteral("任务执行中断了，先别急，看看错误信息再处理。"),
        };
    }

    return {QStringLiteral("我在这里陪着你哦。")};
}

QStringList CharacterDialogueProvider::effectiveLines(
    const CharacterDialogues &packageDialogues,
    CharacterDialogueScene scene)
{
    const QStringList packageLines = packageDialogues.linesForGroup(sceneKey(scene));
    if (!packageLines.isEmpty())
    {
        return packageLines;
    }

    return builtInFallbackLines(scene);
}

QString CharacterDialogueProvider::pickLine(
    const CharacterDialogues &packageDialogues,
    CharacterDialogueScene scene,
    int randomSeedOverride)
{
    const QStringList candidates = effectiveLines(packageDialogues, scene);
    if (candidates.isEmpty())
    {
        return {};
    }

    if (candidates.size() == 1)
    {
        const QString only = candidates.first();
        m_lastLineByScene.insert(sceneKey(scene), only);
        return only;
    }

    const QString key = sceneKey(scene);
    const QString previous = m_lastLineByScene.value(key);
    QStringList pool;
    pool.reserve(candidates.size());
    for (const QString &line : candidates)
    {
        if (line != previous)
        {
            pool.append(line);
        }
    }
    if (pool.isEmpty())
    {
        pool = candidates;
    }

    const int index = randomSeedOverride >= 0
        ? (randomSeedOverride % pool.size())
        : QRandomGenerator::global()->bounded(pool.size());
    const QString chosen = pool.at(index);
    m_lastLineByScene.insert(key, chosen);
    return chosen;
}

void CharacterDialogueProvider::resetHistory()
{
    m_lastLineByScene.clear();
}
