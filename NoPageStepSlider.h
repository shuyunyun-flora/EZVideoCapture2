#pragma once

#include <QSlider>

class NoPageStepSlider  : public QSlider
{
	Q_OBJECT

public:
	NoPageStepSlider(Qt::Orientation orientation, QWidget *parent = nullptr);
	~NoPageStepSlider();

protected:
	void mousePressEvent(QMouseEvent* ev) override;
};

