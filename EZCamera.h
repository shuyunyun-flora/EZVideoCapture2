#pragma once

#include <QObject>

struct CameraInfo
{
	QString friendlyName;
	QString symbolicLink;
	CameraInfo(QString strName, QString strLink) : friendlyName(strName), symbolicLink(strLink) {}
};
class QWidget;
class EZCamera  : public QObject
{
	Q_OBJECT

public:
	EZCamera(QObject *parent, QString strDeviceName);
	~EZCamera();

public:
	bool initControlInterfaces();
	void releaseControlInterfaces();
	void handleFrame(void* data, int width, int height, int stride);
	QString getFrameInfo() const;
	bool isAutoExposureSupported(long& flags) const;
	bool getExposureRange(long& min, long& max, long& step, long& def, long& flags) const;
	bool setExposureAuto(bool enable);
	bool setExposureValue(long value);
	bool getExposureValue(long& value, long& flags) const;
	bool isAutoBrightnessSupported(long& flags) const;
	bool getBrightnessRange(long& min, long& max, long& step, long& def, long& flags) const;
	bool setBrightnessAuto(bool enable);
	bool setBrightnessValue(long value);
	bool getBrightnessValue(long& value, long& flags) const;
	bool isAutoContrastSupported(long& flags) const;
	bool getContrastRange(long& min, long& max, long& step, long& def, long& flags) const;
	bool setContrastAuto(bool enable);
	bool setContrastValue(long value);
	bool getContrastValue(long& value, long& flags) const;

private:
	bool getVideoProcAmpRange(long property, long& min, long& max, long& step, long& def, long& flags) const;
	bool setVideoProcAmpRange(long property, long value);
	bool setVideoProcAmpAuto(long property, bool enable);

public:
	static QList<CameraInfo> getAvailableCameraNames();
	static bool getExposureRangeTest(QString strCameraName, long& min, long& max, long& step, long& def, long& flags);
	static bool extractVidPid(const std::string& symbolicLink, std::string& vid, std::string& pid);
	static bool extractUniqueId(const std::string& symbolicLink, std::string& uniqueId);

public Q_SLOTS:
	void start();
	void stop();

Q_SIGNALS:
	void signalFrameReady(const QByteArray& data, int width, int height, int stride);
	void signalFrameInfo(QString strInfo);

private:
	bool CreateVideoDeviceSource(void** ppSource);
	void EnumerateCaptureFormats(void* v_pSource);
	void SetDeviceFormat(void* v_pSource, unsigned long dwFormatIndex);
	void SetDeviceMaxFrameRate(void* v_pSource, unsigned long dwTypeIndex);
	long SetHighestNV12(void* v_pSource);

private:
	void* m_callback = nullptr;
	QString m_strName = "";

	void* m_pMediaSource = nullptr;
	void* m_pAttributes = nullptr;
	void* m_pSourceReader = nullptr;
	void* m_pCapFilter = nullptr;
	void* m_pCamCtrl = nullptr;
	void* m_pVideoProcAmp = nullptr;

	std::atomic_bool m_bIsRunning{ false };
	QByteArray m_lastFrameNv12;
	int m_nFPS = 0;
	int m_nFrameWidth = 0;
	int m_nFrameHeight = 0;
	QString m_strFormat = "";

public:
	QWidget* m_pRenderWidget = nullptr;
	long m_lExposure = 0;
	bool m_bExposureAuto = false;
	long m_lBrightness = 0;
	bool m_bBrightnessAuto = false;
	long m_lContrast = 0;
	bool m_bContrastAuto = false;
};

