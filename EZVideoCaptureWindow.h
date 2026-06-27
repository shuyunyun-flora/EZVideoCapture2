#pragma once

#include <QMainWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class QComboBox;
class QSlider;
class QCheckBox;
class QPushButton;
class QLabel;
class EZVideoRenderer;
class EZCamera;
struct CameraInfo;
class EZVideoCaptureWindow  : public QMainWindow
{
	Q_OBJECT

public:
	EZVideoCaptureWindow(QWidget *parent = nullptr);
	~EZVideoCaptureWindow();

public:
	void setInFPSText(QString strText);
	void setRenderFPSText(QString strText);
	void showFpsInfo(bool bShow);

protected:
	void showEvent(QShowEvent* event) override;
	void closeEvent(QCloseEvent* event) override;
#ifdef Q_OS_WIN
	bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

Q_SIGNALS:
	void signalCameraDeviceChanged(QString strSymbolicLink, bool bConnect, bool bRemove);

private Q_SLOTS:
	void enumerateCameras();
	void onCameraDeviceChanged(QString strSymbolicLink, bool bConnect, bool bRemove);
	void onCameraSelectedIndexChanged(int index);
	void startCamera(QString strName);
	void stopCamera();

private:
	void initLayout();

private:
#ifdef Q_OS_WIN
	void registerCameraDeviceNotification();
	void unregisterCameraDeviceNotification();

	HDEVNOTIFY m_hCameraDeviceNotify = nullptr;
#endif

private:
	QComboBox* m_pCmbCameras = nullptr;
	QPushButton* m_pBtnTakePhoto = nullptr;
	EZVideoRenderer* m_pVideoRenderer = nullptr;
	EZCamera* m_pCamera = nullptr;
	QThread* m_pCameraThread = nullptr;
	bool m_bEnumeratingCameras = false;

	QSlider* m_pSldExposure = nullptr;
	QCheckBox* m_pChkExposureAuto = nullptr;

	QSlider* m_pSldBrightness = nullptr;
	QCheckBox* m_pChkBrightnessAuto = nullptr;

	QSlider* m_pSldContrast = nullptr;
	QCheckBox* m_pChkContrastAuto = nullptr;

	QCheckBox* m_pChkFlipH = nullptr;
	QCheckBox* m_pChkFlipV = nullptr;
	QLabel* m_pLblInPFS = nullptr;
	QLabel* m_pLblRenderFPS = nullptr;
	QComboBox* m_pCmbPreviewFps = nullptr;
	QLabel* m_pLblPreviewFps = nullptr;
	bool m_bFpsInfoVisible = true;

	long m_lExposureValue = 0;
	long m_lBrightnessValue = 0;
	long m_lContrastValue = 0;

	QList<CameraInfo> m_lstLatestCameraInfo;
	QList<CameraInfo> m_lstAllCameraInfo;
};

