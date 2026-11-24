#include "PetView.h"

#include <QPainter>
#include <QStyle>
#include <cmath>

PetView::PetView(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_effects.changed = [this] { update(); };
}

void PetView::configureEffects(const CharacterEffectSpec &spec, bool enabled, bool lowPower)
{
    m_effects.configure(spec, enabled, lowPower);
    m_closedFrame = enabled && !spec.closedFrame.isEmpty() ? QPixmap(spec.closedFrame) : QPixmap();
    updateGeometry();
    update();
}

int PetView::padding() const
{
    if (!m_effects.active() || m_frame.isNull()) return 0;
    const QSizeF s = m_frame.deviceIndependentSize();
    const auto &spec = m_effects.spec();
    const double radians = spec.maxAngleDeg * 3.141592653589793 / 180;
    return qCeil(qMax(s.width(), s.height()) * (std::sin(radians) + spec.amplitude * 1.1)) + 8;
}

QRectF PetView::contentRect() const
{
    return QStyle::alignedRect(layoutDirection(), Qt::AlignCenter,
                               m_frame.deviceIndependentSize().toSize(), rect());
}

QTransform PetView::frameTransform() const
{
    const QRectF content = contentRect();
    const QPointF pivot(content.center().x(), content.bottom());
    const auto pose = m_effects.pose();
    QTransform t;
    t.translate(pivot.x(), pivot.y());
    t.rotate(pose.angle);
    t.scale(1, pose.scaleY);
    t.translate(-pivot.x(), -pivot.y());
    return t;
}

QPointF PetView::sourcePoint(const QPointF &viewPoint) const
{
    return frameTransform().inverted().map(viewPoint) - contentRect().topLeft();
}

void PetView::setFrame(const QPixmap &pixmap)
{
    if (m_fallbackText.isEmpty() && m_frame.cacheKey() == pixmap.cacheKey())
    {
        return;
    }

    const QSize previousHint = sizeHint();
    m_frame = pixmap;
    m_fallbackText.clear();
    if (sizeHint() != previousHint)
    {
        updateGeometry();
    }
    // Qt merges queued updates; keep the old frame until the next paint.
    update();
}

void PetView::showFallbackText(const QString &text)
{
    if (m_frame.isNull() && m_fallbackText == text)
    {
        return;
    }

    const QSize previousHint = sizeHint();
    m_frame = {};
    m_fallbackText = text;
    if (sizeHint() != previousHint)
    {
        updateGeometry();
    }
    update();
}

const QPixmap &PetView::pixmap() const
{
    return m_frame;
}

QSize PetView::sizeHint() const
{
    if (!m_frame.isNull())
    {
        return m_frame.deviceIndependentSize().toSize() + QSize(padding() * 2, padding() * 2);
    }
    return m_fallbackText.isEmpty() ? QSize(0, 0) : fontMetrics().size(Qt::TextSingleLine, m_fallbackText);
}

QSize PetView::minimumSizeHint() const
{
    return sizeHint();
}

void PetView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!m_frame.isNull())
    {
        const QRectF frameRect = contentRect();
        const auto pose = m_effects.pose();
        painter.save();
        painter.setTransform(frameTransform());
        if (pose.closed && !m_closedFrame.isNull())
            painter.drawPixmap(frameRect, m_closedFrame, QRectF(QPointF(0, 0), m_closedFrame.size()));
        else
            painter.drawPixmap(frameRect.topLeft(), m_frame);
        painter.restore();
        if (!pose.badge.isEmpty())
        {
            const QRectF badge(frameRect.right() - 34, frameRect.top() + 4, 32, 24);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(30, 40, 55, 215));
            painter.drawRoundedRect(badge, 10, 10);
            painter.setPen(Qt::white);
            painter.drawText(badge, Qt::AlignCenter, pose.badge);
        }
    }
    else if (!m_fallbackText.isEmpty())
    {
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextSingleLine, m_fallbackText);
    }
}
