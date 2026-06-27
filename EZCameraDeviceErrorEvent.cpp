#include "EZCameraDeviceErrorEvent.h"

EZCameraDeviceErrorEvent::EZCameraDeviceErrorEvent(const QString& msg, int value)
	: QEvent(EZCameraDeviceErrorEventType)
{
	this->m_message = msg;
	this->m_value = value;
}

EZCameraDeviceErrorEvent::~EZCameraDeviceErrorEvent()
{
}

