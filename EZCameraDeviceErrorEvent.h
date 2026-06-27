#pragma once

#include <QEvent>
#include <QString>

static const QEvent::Type EZCameraDeviceErrorEventType = static_cast<QEvent::Type>(QEvent::User + 1);
class EZCameraDeviceErrorEvent  : public QEvent
{
public:
	explicit EZCameraDeviceErrorEvent(const QString& msg, int value);
	~EZCameraDeviceErrorEvent();

public:
	QString message() const { return m_message; }
	int value() const { return m_value; }

private:
	QString m_message;
	int m_value;
};

