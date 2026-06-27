#include "EZVideoCaptureWindow.h"
#include <QCoreApplication>
#include <QLayout>
#include <QFileDialog>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QThread>
#include <QMessageBox>
#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include "EZVideoRenderer.h"
#include "EZCamera.h"
#include "EZCameraDeviceErrorEvent.h"
#include "NoPageStepSlider.h"

#ifdef Q_OS_WIN
#include <dbt.h>
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>
#include <strmif.h>
#endif

EZVideoCaptureWindow::EZVideoCaptureWindow(QWidget *parent)
	: QMainWindow(parent)
{
	this->initLayout();
	this->enumerateCameras();

	connect(this, &EZVideoCaptureWindow::signalCameraDeviceChanged, this, &EZVideoCaptureWindow::onCameraDeviceChanged);
}

EZVideoCaptureWindow::~EZVideoCaptureWindow()
{
#ifdef Q_OS_WIN
	this->unregisterCameraDeviceNotification();
#endif
}

void EZVideoCaptureWindow::showEvent(QShowEvent* event)
{
	QMainWindow::showEvent(event);

	static bool firstShow = true;
	if (firstShow)
	{
		this->showFpsInfo(false);
		firstShow = false;
	}

#ifdef Q_OS_WIN
	static bool s_registered = false;
	if (!s_registered)
	{
		this->registerCameraDeviceNotification();
		s_registered = true;
	}
#endif
}

void EZVideoCaptureWindow::closeEvent(QCloseEvent* event)
{
	qDebug() << "EZVideoCaptureWindow is closing";

	this->stopCamera();

	QMainWindow::closeEvent(event);
}

void EZVideoCaptureWindow::setInFPSText(QString strText)
{
	if (this->m_pLblInPFS != nullptr)
	{
		this->m_pLblInPFS->setText(strText);
	}
}

void EZVideoCaptureWindow::setRenderFPSText(QString strText)
{
	if (this->m_pLblRenderFPS != nullptr)
	{
		this->m_pLblRenderFPS->setText(strText);
	}
}

void EZVideoCaptureWindow::showFpsInfo(bool bShow)
{
	if (m_bFpsInfoVisible == bShow)
	{
		return;
	}
	m_bFpsInfoVisible = bShow;

	if (m_pLblInPFS)
	{
		if (!bShow)
		{
			m_pLblInPFS->setText("");
		}
		m_pLblInPFS->setVisible(bShow);
	}
	if (m_pLblRenderFPS)
	{
		if (!bShow)
		{
			m_pLblRenderFPS->setText("");
		}
		m_pLblRenderFPS->setVisible(bShow);
	}
	if (m_pLblPreviewFps) m_pLblPreviewFps->setVisible(bShow);
	if (m_pCmbPreviewFps) m_pCmbPreviewFps->setVisible(bShow);
}

#ifdef Q_OS_WIN
void EZVideoCaptureWindow::registerCameraDeviceNotification()
{
	if (m_hCameraDeviceNotify)
	{
		return;
	}

	DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
	filter.dbcc_size = sizeof(filter);
	filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	filter.dbcc_classguid = KSCATEGORY_VIDEO_CAMERA;   // 监控摄像头

	m_hCameraDeviceNotify = RegisterDeviceNotificationW(
		reinterpret_cast<HANDLE>(winId()),
		&filter,
		DEVICE_NOTIFY_WINDOW_HANDLE);

	if (!m_hCameraDeviceNotify)
	{
		qDebug() << "RegisterDeviceNotificationW failed for KSCATEGORY_VIDEO_CAMERA";
		return;
	}

	qDebug() << "Camera device notification registered";
}

void EZVideoCaptureWindow::unregisterCameraDeviceNotification()
{
	if (m_hCameraDeviceNotify)
	{
		UnregisterDeviceNotification(m_hCameraDeviceNotify);
		m_hCameraDeviceNotify = nullptr;
	}
}
bool EZVideoCaptureWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
	if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
	{
		return QMainWindow::nativeEvent(eventType, message, result);
	}

	MSG* msg = static_cast<MSG*>(message);
	if (nullptr == msg)
	{
		return QMainWindow::nativeEvent(eventType, message, result);
	}

	if (msg->message == WM_DEVICECHANGE)
	{
		const auto wParam = msg->wParam;
		if (wParam == DBT_DEVICEARRIVAL || 
			wParam == DBT_DEVICEREMOVECOMPLETE || 
			wParam == DBT_DEVNODES_CHANGED)
		{
			bool bDeviceConnect = wParam == DBT_DEVICEARRIVAL;
			bool bDeviceRemove = wParam == DBT_DEVICEREMOVECOMPLETE;
			qDebug() << "WM_DEVICECHANGE received, wParam =" << Qt::hex << wParam;

			auto* hdr = reinterpret_cast<PDEV_BROADCAST_HDR>(msg->lParam);

			QString strSymbolicLink;
			if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
			{
				auto* dev = reinterpret_cast<PDEV_BROADCAST_DEVICEINTERFACE_W>(hdr);
				strSymbolicLink = QString::fromWCharArray(dev->dbcc_name);
				qDebug() << "Camera interface changed:" << strSymbolicLink;
			}
			else
			{
				qDebug() << "Camera device tree changed";
			}

			if (!strSymbolicLink.isEmpty())
			{
				// 不要在 nativeEvent 里直接做重活，切回 Qt 事件循环
				QMetaObject::invokeMethod(this, [this, strSymbolicLink, bDeviceConnect, bDeviceRemove]()
					{
						emit signalCameraDeviceChanged(strSymbolicLink, bDeviceConnect, bDeviceRemove);
					}, Qt::QueuedConnection);
			}
		}

		switch (msg->wParam)
		{
			case DBT_DEVICEARRIVAL:
			{
				qDebug() << "Device inserted.";
			}
			break;

			case DBT_DEVICEREMOVECOMPLETE:
			{
				qDebug() << "Device removed.";
			}
			break;

			case DBT_DEVNODES_CHANGED:
			{
				qDebug() << "Device tree changed.";
			}
			break;

			default:
				qDebug() << "WM_DEVICECHANGE, wParam =" << Qt::hex << msg->wParam;
				break;
		}
	}

	return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void EZVideoCaptureWindow::initLayout()
{
	QWidget* central = new QWidget(this);
	this->setCentralWidget(central);

	QGridLayout* pGridLayout = new QGridLayout(central);

	pGridLayout->setContentsMargins(8, 8, 8, 8);
	pGridLayout->setHorizontalSpacing(2);
	pGridLayout->setVerticalSpacing(2);

	pGridLayout->setColumnMinimumWidth(0, 60);
	pGridLayout->setColumnStretch(1, 1);
	pGridLayout->setRowMinimumHeight(0, 32);
	pGridLayout->setRowStretch(1, 1);

	// 顶部： Camera 选择，拍照
	pGridLayout->addWidget(new QLabel(tr("Camera:")), 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
	this->m_pCmbCameras = new QComboBox();
	this->m_pCmbCameras->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	this->m_pCmbCameras->setMinimumWidth(200);
	connect(this->m_pCmbCameras, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EZVideoCaptureWindow::onCameraSelectedIndexChanged);
	pGridLayout->addWidget(this->m_pCmbCameras, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);

	this->m_pBtnTakePhoto = new QPushButton(tr("Take Photo"));
	this->m_pBtnTakePhoto->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	this->m_pBtnTakePhoto->setMinimumWidth(100);
	pGridLayout->addWidget(this->m_pBtnTakePhoto, 0, 2, Qt::AlignLeft | Qt::AlignVCenter);

	// 中间： 视频显示区域
	this->m_pVideoRenderer = new EZVideoRenderer(this);
	this->m_pVideoRenderer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	pGridLayout->addWidget(this->m_pVideoRenderer, 1, 0, 1, 3);

	connect(this->m_pBtnTakePhoto, &QPushButton::clicked, this->m_pVideoRenderer, &EZVideoRenderer::onTakePhoto);

	// 底部面板
	auto* pBottomPanel = new QWidget(this);
	auto* pBottomLayout = new QVBoxLayout(pBottomPanel);
	pBottomLayout->setContentsMargins(0, 0, 0, 0);
	pBottomLayout->setSpacing(6);

	// --------------------------
	// 第一行：曝光 / 亮度 / 对比度
	// --------------------------
	auto* pRow1 = new QHBoxLayout();
	pRow1->setSpacing(16);

	// Exposure group
	{
		auto* panel = new QWidget(pBottomPanel);
		auto* layout = new QGridLayout(panel);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setHorizontalSpacing(6);
		layout->setVerticalSpacing(4);

		auto* lbl = new QLabel(tr("Exposure"), panel);
		m_pSldExposure = new NoPageStepSlider(Qt::Horizontal, panel);
		m_pSldExposure->setRange(-10, 10);
		m_pChkExposureAuto = new QCheckBox(tr("Auto"), panel);

		layout->addWidget(lbl, 0, 0, 1, 2);
		layout->addWidget(m_pSldExposure, 1, 0);
		layout->addWidget(m_pChkExposureAuto, 1, 1);

		pRow1->addWidget(panel, 1);
	}

	// Brightness group
	{
		auto* panel = new QWidget(pBottomPanel);
		auto* layout = new QGridLayout(panel);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setHorizontalSpacing(6);
		layout->setVerticalSpacing(4);

		auto* lbl = new QLabel(tr("Brightness"), panel);
		m_pSldBrightness = new NoPageStepSlider(Qt::Horizontal, panel);
		m_pSldBrightness->setRange(-64, 64);
		m_pChkBrightnessAuto = new QCheckBox(tr("Auto"), panel);

		layout->addWidget(lbl, 0, 0, 1, 2);
		layout->addWidget(m_pSldBrightness, 1, 0);
		layout->addWidget(m_pChkBrightnessAuto, 1, 1);

		pRow1->addWidget(panel, 1);
	}

	// Contrast group
	{
		auto* panel = new QWidget(pBottomPanel);
		auto* layout = new QGridLayout(panel);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setHorizontalSpacing(6);
		layout->setVerticalSpacing(4);

		auto* lbl = new QLabel(tr("Contrast"), panel);
		m_pSldContrast = new NoPageStepSlider(Qt::Horizontal, panel);
		m_pSldContrast->setRange(0, 100);
		m_pChkContrastAuto = new QCheckBox(tr("Auto"), panel);

		layout->addWidget(lbl, 0, 0, 1, 2);
		layout->addWidget(m_pSldContrast, 1, 0);
		layout->addWidget(m_pChkContrastAuto, 1, 1);

		pRow1->addWidget(panel, 1);
	}

	pBottomLayout->addLayout(pRow1);

	// --------------------------
	// 第二行：Flip H / Flip V
	// --------------------------
	auto* pRow2 = new QHBoxLayout();
	pRow2->setSpacing(20);

	m_pChkFlipH = new QCheckBox(tr("Flip Horizontally"), pBottomPanel);
	m_pChkFlipV = new QCheckBox(tr("Flip Vertically"), pBottomPanel);
	m_pLblInPFS = new QLabel(tr(""), pBottomPanel);
	m_pLblRenderFPS = new QLabel(tr(""), pBottomPanel);

	auto* pPreviewLayout = new QHBoxLayout();
	pPreviewLayout->setContentsMargins(0, 0, 0, 0);
	pPreviewLayout->setSpacing(4);   // 这里控制 FPS 和 ComboBox 的距离

	m_pLblPreviewFps = new QLabel(tr("FPS:"), pBottomPanel);
	m_pLblPreviewFps->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	m_pCmbPreviewFps = new QComboBox(pBottomPanel);
	m_pCmbPreviewFps->setFixedWidth(80);
	m_pCmbPreviewFps->addItem("Sync");
	m_pCmbPreviewFps->addItem("5");
	m_pCmbPreviewFps->addItem("10");
	m_pCmbPreviewFps->addItem("15");
	m_pCmbPreviewFps->addItem("20");
	m_pCmbPreviewFps->addItem("30");
	m_pCmbPreviewFps->setCurrentIndex(0);
	pPreviewLayout->addWidget(m_pLblPreviewFps);
	pPreviewLayout->addWidget(m_pCmbPreviewFps);

	pRow2->addWidget(m_pChkFlipH);
	pRow2->addWidget(m_pChkFlipV);
	pRow2->addStretch();
	pRow2->addLayout(pPreviewLayout);
	pRow2->addSpacing(8);
	pRow2->addWidget(m_pLblInPFS);
	pRow2->addSpacing(8);
	pRow2->addWidget(m_pLblRenderFPS);

	pBottomLayout->addLayout(pRow2);

	// 底部整体加到主布局
	pGridLayout->addWidget(pBottomPanel, 2, 0, 1, 3);

	// 信号与槽函数
	// 曝光
	connect(this->m_pChkExposureAuto, &QCheckBox::toggled, this, [this](bool checked) {
		if (this->m_pCamera)
		{
			this->m_pCamera->setExposureAuto(checked);
			if (!checked) this->m_pCamera->setExposureValue(this->m_lExposureValue);
		}
		});
	connect(this->m_pSldExposure, &QSlider::valueChanged, this, [this](int value) {
		if (this->m_pCamera && !this->m_pChkExposureAuto->isChecked())
		{
			this->m_pCamera->setExposureValue(value);
			this->m_lExposureValue = value;
			this->m_pSldExposure->setToolTip(QString::number(value));
		}
		});
	connect(this->m_pSldExposure, &QSlider::sliderReleased, this, [this]() {
		if (this->m_pCamera && this->m_pChkExposureAuto->isChecked())
		{
			this->m_pSldExposure->setValue(this->m_lExposureValue);
		}
		});

	// 亮度
	connect(this->m_pChkBrightnessAuto, &QCheckBox::toggled, this, [this](bool checked) {
		if (this->m_pCamera)
		{
			this->m_pCamera->setBrightnessAuto(checked);
			if (!checked) this->m_pCamera->setBrightnessValue(this->m_lBrightnessValue);
		}
		});
	connect(this->m_pSldBrightness, &QSlider::valueChanged, this, [this](int value) {
		if (this->m_pCamera && !this->m_pChkBrightnessAuto->isChecked())
		{
			this->m_pCamera->setBrightnessValue(value);
			this->m_lBrightnessValue = value;
			this->m_pSldBrightness->setToolTip(QString::number(value));
		}
		});
	connect(this->m_pSldBrightness, &QSlider::sliderReleased, this, [this]() {
		if (this->m_pCamera && this->m_pChkBrightnessAuto->isChecked())
		{
			this->m_pSldBrightness->setValue(this->m_lBrightnessValue);
		}
		});

	// 对比度
	connect(this->m_pChkContrastAuto, &QCheckBox::toggled, this, [this](bool checked) {
		if (this->m_pCamera)
		{
			this->m_pCamera->setContrastAuto(checked);
			if (!checked) this->m_pCamera->setContrastValue(this->m_lContrastValue);
		}
		});
	connect(this->m_pSldContrast, &QSlider::valueChanged, this, [this](int value) {
		if (this->m_pCamera && !this->m_pChkContrastAuto->isChecked())
		{
			this->m_pCamera->setContrastValue(value);
			this->m_lContrastValue = value;
			this->m_pSldContrast->setToolTip(QString::number(value));
		}
		});
	connect(this->m_pSldContrast, &QSlider::sliderReleased, this, [this]() {
		if (this->m_pCamera && this->m_pChkContrastAuto->isChecked())
		{
			this->m_pSldContrast->setValue(this->m_lContrastValue);
		}
		});

	// 水平翻转
	connect(this->m_pChkFlipH, &QCheckBox::toggled, this, [this](bool checked) {
		this->m_pVideoRenderer->setFlipHorizontal(checked);
		});

	// 竖直翻转
	connect(this->m_pChkFlipV, &QCheckBox::toggled, this, [this](bool checked) {
		this->m_pVideoRenderer->setFlipVertical(checked);
		});

	// 预览FPS
	connect(this->m_pCmbPreviewFps, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (nullptr == this->m_pVideoRenderer)
		{
			return;
		}
		QString strText = this->m_pCmbPreviewFps->currentText();
		if (strText == "Sync")
		{
			this->m_pVideoRenderer->setPreviewPresentFps(0);
		}
		else
		{
			bool ok = false;
			int fps = strText.toInt(&ok);
			if (ok)
			{
				this->m_pVideoRenderer->setPreviewPresentFps(fps);
			}
		}
		});
}

void EZVideoCaptureWindow::enumerateCameras()
{
	this->m_bEnumeratingCameras = true;

	this->m_lstLatestCameraInfo = EZCamera::getAvailableCameraNames();
	this->m_pCmbCameras->addItem("None");
	for (const auto& item : this->m_lstLatestCameraInfo)
	{
		this->m_pCmbCameras->addItem(item.friendlyName);
	}

	this->m_lstAllCameraInfo = this->m_lstLatestCameraInfo;

	this->m_bEnumeratingCameras = false;
}

void EZVideoCaptureWindow::onCameraDeviceChanged(QString strSymbolicLink, bool bConnect, bool bRemove)
{
	if (strSymbolicLink.isEmpty())
	{
		return;
	}

	QTimer::singleShot(200, this, [this, strSymbolicLink, bConnect, bRemove]() {
		this->m_bEnumeratingCameras = true;

		QList<CameraInfo> oldList = m_lstLatestCameraInfo;
		QList<CameraInfo> newList = EZCamera::getAvailableCameraNames();

		QSet<QString> oldSet, newSet;
		for (const auto& c : oldList) oldSet.insert(c.symbolicLink);
		for (const auto& c : newList) newSet.insert(c.symbolicLink);

		bool bDeviceConnected = false;
		bool bDeviceRemoved = false;
		for (const auto& s : newSet)
		{
			if (!oldSet.contains(s))
			{
				qDebug() << "Added Camera:" << s;
				bDeviceConnected = true;
			}
		}

		for (const auto& s : oldSet)
		{
			if (!newSet.contains(s))
			{
				qDebug() << "Removed camera:" << s;
				bDeviceRemoved = true;
			}
		}

		this->m_lstLatestCameraInfo = newList;

		auto mergeByName = [](const QList<CameraInfo>& list1, const QList<CameraInfo>& list2)
			{
				QList<CameraInfo> result;
				QSet<QString> seenNames;

				for (const auto& cam : list1)
				{
					if (!seenNames.contains(cam.friendlyName))
					{
						result.append(cam);
						seenNames.insert(cam.friendlyName);
					}
				}

				for (const auto& cam : list2)
				{
					if (!seenNames.contains(cam.friendlyName))
					{
						result.append(cam);
						seenNames.insert(cam.friendlyName);
					}
				}

				return result;
			};

		this->m_lstAllCameraInfo = mergeByName(newList, this->m_lstAllCameraInfo);
		// 如果symbolicLink发生变化, 更新symbolicLink.
		for (auto& item : this->m_lstAllCameraInfo)
		{
			for (const auto& newItem : newList)
			{
				if (item.friendlyName == newItem.friendlyName)
				{
					item.symbolicLink = newItem.symbolicLink;
					break;
				}
			}
		}

		QString strCurrentCamera = this->m_pCmbCameras->currentText();
		this->m_pCmbCameras->clear();
		this->m_pCmbCameras->addItem("None");
		for (auto& item : this->m_lstAllCameraInfo)
		{
			this->m_pCmbCameras->addItem(item.friendlyName);
		}
		for (int i = 0; i < this->m_lstAllCameraInfo.size(); ++i)
		{
			const auto& item = this->m_lstAllCameraInfo[i];
			if (item.friendlyName == strCurrentCamera)
			{
				this->m_pCmbCameras->setCurrentIndex(i + 1);  // +1 因为 "None" 占了第0项
				break;
			}
		}

		if (strCurrentCamera != "None")
		{
			for (const auto& item : this->m_lstAllCameraInfo)
			{
				if (item.friendlyName == strCurrentCamera)
				{
					if (QString::compare(item.symbolicLink, strSymbolicLink, Qt::CaseInsensitive) == 0)
					{
						/*if (bDeviceConnected)
						{
							this->stopCamera();
							this->startCamera(strCurrentCamera);
						}
						if (bDeviceRemoved)
						{
							this->stopCamera();
							QCoreApplication::postEvent(this->m_pVideoRenderer, new EZCameraDeviceErrorEvent("Device lost", 111));
						}*/

						if (bConnect)
						{
							this->stopCamera();
							this->startCamera(strCurrentCamera);
						}
						if (bRemove)
						{
							this->stopCamera();
							QCoreApplication::postEvent(this->m_pVideoRenderer, new EZCameraDeviceErrorEvent("Device lost", 111));
						}
					}
					break;
				}
			}
		}

		this->m_bEnumeratingCameras = false;
		});
}

void EZVideoCaptureWindow::startCamera(QString strName)
{
	if (strName == "None")
	{
		this->showFpsInfo(false);
		return;
	}

	this->m_pCamera = new EZCamera(nullptr, strName);
	this->m_pCamera->m_pRenderWidget = this->m_pVideoRenderer;
	this->m_pCamera->initControlInterfaces();
	{
		long minV, maxV, stepV, defV, flags;
		if (this->m_pCamera->getExposureRange(minV, maxV, stepV, defV, flags))
		{
			bool bSupportAutoExposure = this->m_pCamera->isAutoExposureSupported(flags);
			qDebug() << "Exposure range: " << minV << maxV << stepV << defV << flags;
			flags = 0;
			long v;
			this->m_pCamera->getExposureValue(v, flags);
			bool bAuto = false;
			if (flags == CameraControl_Flags_Auto)		// 当前是自动模式.
			{
				this->m_lExposureValue = defV;
				bAuto = true;
			}
			else if (flags == CameraControl_Flags_Manual)	// 当前是手动模式.
			{
				this->m_lExposureValue = v;
			}
			this->m_pCamera->m_bExposureAuto = bAuto;
			this->m_pCamera->m_lExposure = this->m_lExposureValue;

			QSignalBlocker blocker(this->m_pSldExposure);
			QSignalBlocker blocker2(this->m_pChkExposureAuto);
			this->m_pSldExposure->setRange(minV, maxV);
			this->m_pSldExposure->setSingleStep(stepV);
			this->m_pSldExposure->setValue(this->m_lExposureValue);
			this->m_pSldExposure->setToolTip(QString::number(this->m_lExposureValue));
			this->m_pChkExposureAuto->setEnabled(bSupportAutoExposure);
			this->m_pChkExposureAuto->setChecked(bAuto);
		}
	}
	{
		long minV, maxV, stepV, defV, flags;
		if (this->m_pCamera->getBrightnessRange(minV, maxV, stepV, defV, flags))
		{
			bool bSupportAutoBrightness = this->m_pCamera->isAutoBrightnessSupported(flags);
			qDebug() << "Brightness range: " << minV << maxV << stepV << defV << flags;

			flags = 0;
			long v;
			this->m_pCamera->getBrightnessValue(v, flags);
			bool bAuto = false;
			if (flags == VideoProcAmp_Flags_Auto)			// 当前是自动模式.
			{
				this->m_lBrightnessValue = defV;
				bAuto = true;
			}
			else if (flags == VideoProcAmp_Flags_Manual)	// 当前是手动模式.
			{
				this->m_lBrightnessValue = v;
			}
			this->m_pCamera->m_bBrightnessAuto = bAuto;
			this->m_pCamera->m_lBrightness = this->m_lBrightnessValue;

			QSignalBlocker blocker(this->m_pSldBrightness);
			QSignalBlocker blocker2(this->m_pChkBrightnessAuto);
			this->m_pSldBrightness->setRange(minV, maxV);
			this->m_pSldBrightness->setSingleStep(stepV);
			this->m_pSldBrightness->setValue(this->m_lBrightnessValue);
			this->m_pSldBrightness->setToolTip(QString::number(this->m_lBrightnessValue));
			this->m_pChkBrightnessAuto->setEnabled(bSupportAutoBrightness);
			this->m_pChkBrightnessAuto->setChecked(bAuto);
		}
	}
	{
		long minV, maxV, stepV, defV, flags;
		if (this->m_pCamera->getContrastRange(minV, maxV, stepV, defV, flags))
		{
			bool bSupportAutoContrast = this->m_pCamera->isAutoContrastSupported(flags);
			qDebug() << "Contrast range: " << minV << maxV << stepV << defV << flags;

			flags = 0;
			long v;
			this->m_pCamera->getContrastValue(v, flags);
			bool bAuto = false;
			if (flags == VideoProcAmp_Flags_Auto)			// 当前是自动模式.
			{
				this->m_lContrastValue = defV;
				bAuto = true;
			}
			else if (flags == VideoProcAmp_Flags_Manual)	// 当前是手动模式.
			{
				this->m_lContrastValue = v;
			}
			this->m_pCamera->m_bContrastAuto = bAuto;
			this->m_pCamera->m_lContrast = this->m_lContrastValue;

			QSignalBlocker blocker(this->m_pSldContrast);
			QSignalBlocker blocker2(this->m_pChkContrastAuto);
			this->m_pSldContrast->setRange(minV, maxV);
			this->m_pSldContrast->setSingleStep(stepV);
			this->m_pSldContrast->setValue(this->m_lContrastValue);
			this->m_pSldContrast->setToolTip(QString::number(this->m_lContrastValue));
			this->m_pChkContrastAuto->setEnabled(bSupportAutoContrast);
			this->m_pChkContrastAuto->setChecked(bAuto);
		}
	}

	this->m_pCameraThread = new QThread();
	this->m_pCamera->moveToThread(this->m_pCameraThread);
	connect(this->m_pCameraThread, &QThread::started, this->m_pCamera, &EZCamera::start);
	connect(this->m_pCameraThread, &QThread::finished, this->m_pCamera, &EZCamera::deleteLater);
	connect(this->m_pCamera, &EZCamera::signalFrameReady, this->m_pVideoRenderer, &EZVideoRenderer::onFrameReady, Qt::QueuedConnection);
	connect(this->m_pCamera, &EZCamera::signalFrameInfo, this->m_pVideoRenderer, &EZVideoRenderer::onFrameInfo, Qt::QueuedConnection);

	this->m_pCameraThread->start();

	this->m_pVideoRenderer->start();

	this->showFpsInfo(true);
}

void EZVideoCaptureWindow::stopCamera()
{
	if (!m_pCamera) return;

	// 1) 先断开信号（避免新的 queued 事件继续进来）
	disconnect(m_pCamera, nullptr, this, nullptr);

	// 2) 请求相机线程执行 stop（不要在 UI 线程直接操作相机对象）
	QMetaObject::invokeMethod(m_pCamera, "stop", Qt::BlockingQueuedConnection);
	this->m_pCamera->releaseControlInterfaces();

	// 3) 结束线程，并等待完全退出
	m_pCameraThread->quit();				// m_pCamera会在线程退出时自动 deleteLater（因为连接了线程的 finished 信号），所以这里不需要手动 delete m_pCamera）.
	m_pCameraThread->wait();

	m_pCamera = nullptr;

	delete m_pCameraThread;
	m_pCameraThread = nullptr;
}

void EZVideoCaptureWindow::onCameraSelectedIndexChanged(int iIndex)
{
	if (this->m_bEnumeratingCameras)
	{
		return;
	}

	QString strName = this->m_pCmbCameras->itemText(iIndex);
	this->stopCamera();
	this->m_pVideoRenderer->stop();

	this->startCamera(strName);
}
