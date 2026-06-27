#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QMutex>
#include <QTimer>
#include <QLabel>
#include <QElapsedTimer>

class EZVideoCaptureWindow;
class EZVideoRenderer : public QOpenGLWidget, protected QOpenGLExtraFunctions
{
	Q_OBJECT

public:
	explicit EZVideoRenderer(QWidget *parent = nullptr);
	~EZVideoRenderer() override;

public:
    void setFlipHorizontal(bool enable);
    void setFlipVertical(bool enable);
	void setPreviewPresentFps(int fps);    // fps <= 0 表示 sync.

protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

public Q_SLOTS:
    /**
    * 设置一帧 NV12 图像数据。
    *
    * @param nv12   指向 NV12 连续缓冲区的指针（Y + UV）
    * @param width  图像宽度（像素）
    * @param height 图像高度（像素）
    * @param stride 每行 Y 数据的字节数（注意：可能 >= width）
    *
    * ⚠️ 该函数会内部 copy 一份数据，所以 nv12 调用后即可释放。
    * ⚠️ 建议从相机线程用 Qt::QueuedConnection 调用。
    */
    void onFrameReady(const QByteArray& nv12,
        int width,
        int height,
        int stride);
    void onFrameInfo(QString strInfo);
    void onTakePhoto();
    void start();
    void stop();

protected:
	void initializeGL() override;
	void resizeGL(int width, int height) override;
	void paintGL() override;
	void showEvent(QShowEvent* event) override;

protected:
    void wheelEvent(QWheelEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
    QMatrix4x4 m_transform;

private:
    void initTexturesIfNeeded();
    void updateStatusLabelPosition();
    void resetFpsInfo();

private:
    QMutex m_mutex;

    QByteArray          m_bufferNV12;       // 保存整块 NV12 数据
    int                 m_width = 0;        // 图像宽
    int                 m_height = 0;       // 图像高
    int                 m_stride = 0;       // Y 平面 stride（字节）

    bool                m_hasFrame = false;
    bool                m_texInited = false;
    int                 m_texWidth = 0;     // 当前纹理大小（为避免反复 realloc）
    int                 m_texHeight = 0;

    GLuint              m_texY = 0;         // Y 平面纹理
    GLuint              m_texUV = 0;        // UV 平面纹理

    QOpenGLShaderProgram m_program;

    bool m_bStopped = true;
    bool m_bErrorOccured = false;
    bool m_bFlipHorizontal = false;
    bool m_bFlipVertical = false;
    QString m_strFrameInfo = "No Frame";
    QString m_strStatus;
    QString m_strError;

	QLabel* m_pStatusLabel = nullptr;

    QElapsedTimer m_fpsTimerIn;
    QElapsedTimer m_fpsTimerRender;

    int m_inFrameCount = 0;
    int m_renderFrameCount = 0;

    double m_inputFps = 0.0;
    double m_renderFps = 0.0;
    EZVideoCaptureWindow* m_pParentWnd = nullptr;
    QTimer* m_pPresentTick = nullptr;
    int m_nPresentFps = 0;                  // 0 表示sync.
};

