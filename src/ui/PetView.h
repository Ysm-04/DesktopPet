#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>
#include "CharacterEffectsController.h"
#include <QTransform>

class QPaintEvent;

class PetView : public QWidget
{
public:
    explicit PetView(QWidget *parent = nullptr);

    void setFrame(const QPixmap &pixmap);
    void showFallbackText(const QString &text);
    const QPixmap &pixmap() const;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void configureEffects(const CharacterEffectSpec &spec, bool enabled, bool lowPower);
    CharacterEffectsController &effects() { return m_effects; }
    QRectF contentRect() const;
    QPointF sourcePoint(const QPointF &viewPoint) const;
    QTransform frameTransform() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap m_frame;
    QString m_fallbackText;
    CharacterEffectsController m_effects;
    QPixmap m_closedFrame;
    int padding() const;
};
