#include "NoPageStepSlider.h"
#include <QMouseEvent>
#include <QStyle>
#include <qstyleoption.h>

NoPageStepSlider::NoPageStepSlider(Qt::Orientation orientation, QWidget *parent)
	: QSlider(orientation, parent)
{
}

NoPageStepSlider::~NoPageStepSlider()
{
}

void NoPageStepSlider::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        QStyleOptionSlider opt;
        initStyleOption(&opt);

        QRect handleRect = style()->subControlRect(
            QStyle::CC_Slider,
            &opt,
            QStyle::SC_SliderHandle,
            this);

        // 只允许点击滑块本体时进入默认逻辑
        if (!handleRect.contains(ev->position().toPoint()))
        {
            ev->accept();
            return; // 直接吃掉，不让 page step 发生
        }
    }

    QSlider::mousePressEvent(ev);
}

