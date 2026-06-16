#include "PetView.h"

PetView::PetView(QWidget *parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setAttribute(Qt::WA_TranslucentBackground);
}

void PetView::setFrame(const QPixmap &pixmap)
{
    clear();
    setPixmap(pixmap);
}

void PetView::showFallbackText(const QString &text)
{
    clear();
    setText(text);
}
