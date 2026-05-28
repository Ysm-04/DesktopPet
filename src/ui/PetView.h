#pragma once

#include <QLabel>
#include <QPixmap>
#include <QString>

class PetView : public QLabel
{
public:
    explicit PetView(QWidget *parent = nullptr);

    void setFrame(const QPixmap &pixmap);
    void showFallbackText(const QString &text);
};
