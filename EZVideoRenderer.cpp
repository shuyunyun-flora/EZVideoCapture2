#include "EZVideoRenderer.h"
#include <qrandom.h>
#include <qelapsedtimer.h>
#include <QTooltip>
#include <QHelpEvent>
#include <QPainter>
#include <QMessageBox>
#include <QThread>
#include <QFileDialog>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QImageWriter>
#include <QStandardPaths>

#include "EZCameraDeviceErrorEvent.h"
#include "EZVideoCaptureWindow.h"

EZVideoRenderer::EZVideoRenderer(QWidget *parent)
	: QOpenGLWidget(parent)
{
    this->m_pStatusLabel = new QLabel(this);
	this->m_pStatusLabel->setText("No Frame");
    this->m_pStatusLabel->setAlignment(Qt::AlignCenter);

    m_pStatusLabel->setStyleSheet(
        "QLabel {"
        "  background: transparent;"
        "  color: green;"
        "  font-size: 16px;"
        "  border: none;"
        "}"
    );

    m_pStatusLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_pStatusLabel->adjustSize();
    m_pStatusLabel->raise();

    m_fpsTimerIn.start();
    m_fpsTimerRender.start();

    m_pPresentTick = new QTimer(this);
	m_pPresentTick->setTimerType(Qt::PreciseTimer);
    connect(m_pPresentTick, &QTimer::timeout, this, [this]() {
        if (m_nPresentFps > 0 && m_hasFrame && !m_bStopped && !m_bErrorOccured)
        {
            update();
        }
		});
}

EZVideoRenderer::~EZVideoRenderer()
{
	makeCurrent();
	if (m_texY) 
	{
		glDeleteTextures(1, &m_texY);
		m_texY = 0;
	}
	if (m_texUV) 
	{
		glDeleteTextures(1, &m_texUV);
		m_texUV = 0;
	}
	m_program.removeAllShaders();

	doneCurrent();
}

void EZVideoRenderer::showEvent(QShowEvent* event)
{
    QOpenGLWidget::showEvent(event);

    static bool first = true;
    if (first)
    {
        first = false;
        this->m_pParentWnd = qobject_cast<EZVideoCaptureWindow*>(this->window());
	}
}

void EZVideoRenderer::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() > 0) 
    {
        m_transform.scale(1.1);
    }
    else 
    {
        m_transform.scale(0.9);
    }

    update();
}

void EZVideoRenderer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) 
    {
        m_transform = QMatrix4x4();
		update();
    }
}

void EZVideoRenderer::initializeGL()
{
	this->initializeOpenGLFunctions();

    glGenTextures(1, &m_texY);
    glGenTextures(1, &m_texUV);

    glBindTexture(GL_TEXTURE_2D, m_texY);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, m_texUV);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    // 顶点着色器：简单画一个全屏矩形
    static const char* vsrc = R"(#version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTex;
        uniform mat4 modelViewMatrix; // Transformation matrix
        out vec2 vTex;
        void main()
        {
            gl_Position = modelViewMatrix * vec4(aPos, 0.0, 1.0);
            vTex = aTex;
        }
    )";

    // 片元着色器：NV12(Y + UV 4:2:0) → RGB
    static const char* fsrc = R"(#version 330 core
        in vec2 vTex;
        out vec4 FragColor;

        uniform sampler2D texY;
        uniform sampler2D texUV;

        void main()
        {
            float y  = texture(texY,  vTex).r;
            vec2 uv  = texture(texUV, vTex).rg;
            uv      -= vec2(0.5, 0.5);

            float r = y + 1.402 * uv.y;
            float g = y - 0.344 * uv.x - 0.714 * uv.y;
            float b = y + 1.772 * uv.x;

            FragColor = vec4(r, g, b, 1.0);
        }
    )";

    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc)) {
        qWarning() << "Vertex shader compile error:" << m_program.log();
    }
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc)) {
        qWarning() << "Fragment shader compile error:" << m_program.log();
    }
    if (!m_program.link()) {
        qWarning() << "Shader program link error:" << m_program.log();
    }

    glClearColor(0.f, 0.f, 0.f, 1.f);
}

void EZVideoRenderer::resizeGL(int width, int height)
{
	glViewport(0, 0, width, height);
}

void EZVideoRenderer::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    QByteArray localNV12;
    int width = 0, height = 0, stride = 0;
    bool bNoFrame = false;
    {
        QMutexLocker locker(&m_mutex);
        if (this->m_bStopped || !m_hasFrame || m_bufferNV12.isEmpty())
        {
            bNoFrame = true;
        }
        else
        {
            localNV12 = m_bufferNV12; // 拷贝一份到栈上，避免锁时间过长
            width = m_width;
            height = m_height;
            stride = m_stride;
        }
    }

    if (bNoFrame)
    {
        return;
    }

    m_width = width;   // 给 initTexturesIfNeeded 用
    m_height = height;

    initTexturesIfNeeded();

    const uint8_t* base = reinterpret_cast<const uint8_t*>(localNV12.constData());
    const uint8_t* yPtr = base;
    const uint8_t* uvPtr = base + stride * height;

    // 为了安全，设置像素对齐和行长度
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // ---- 上传 Y 平面 ----
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texY);

    // 行长度（像素数），Y 为 1 字节 / 像素
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
        0, 0,
        width, height,
        GL_RED,
        GL_UNSIGNED_BYTE,
        yPtr);

    // ---- 上传 UV 平面 ----
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texUV);

    // UV 每行 stride 字节，对 GL_RG8 来说每个 texel 是 2 字节
    // 因此行长度（像素）= stride / 2
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
        0, 0,
        width / 2, height / 2,
        GL_RG,
        GL_UNSIGNED_BYTE,
        uvPtr);

    // 恢复默认
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // ---- 绘制 ----
    if (!m_program.isLinked())
    {
        return;
    }

    m_program.bind();
    m_program.setUniformValue("modelViewMatrix", m_transform);
    m_program.setUniformValue("texY", 0);
    m_program.setUniformValue("texUV", 1);

    // === 关键：根据窗口和图像的宽高比做等比缩放 ===
    float imgAspect = (height > 0) ? float(width) / float(height) : 1.0f;
    float winAspect = (this->height() > 0) ? float(this->width()) / float(this->height()) : 1.0f;

    float sx = 1.0f;
    float sy = 1.0f;

    if (winAspect > imgAspect)
    {
        // 窗口更宽：高度先铺满，左右留黑边
        sx = imgAspect / winAspect; // < 1
        sy = 1.0f;
    }
    else
    {
        // 窗口更窄 / 更高：宽度先铺满，上下留黑边
        sx = 1.0f;
        sy = winAspect / imgAspect; // < 1
    }

    //const GLfloat verts[] = {
    //    // pos          // tex
    //    -sx, -sy,       0.f, 1.f,   // 左下
    //     sx, -sy,       1.f, 1.f,   // 右下
    //    -sx,  sy,       0.f, 0.f,   // 左上
    //     sx,  sy,       1.f, 0.f    // 右上
    //};
    float u0 = m_bFlipHorizontal ? 1.0f : 0.0f;
    float u1 = m_bFlipHorizontal ? 0.0f : 1.0f;
    float v0 = m_bFlipVertical ? 0.0f : 1.0f;
    float v1 = m_bFlipVertical ? 1.0f : 0.0f;

    const GLfloat verts[] = {
        // pos          // tex
        -sx, -sy,       u0, v0,   // 左下
         sx, -sy,       u1, v0,   // 右下
        -sx,  sy,       u0, v1,   // 左上
         sx,  sy,       u1, v1    // 右上
    };

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), verts + 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    m_program.release();

    // 统计渲染 FPS
    ++m_renderFrameCount;
    qint64 ms = m_fpsTimerRender.elapsed();
    if (ms >= 1000)
    {
        m_renderFps = m_renderFrameCount * 1000.0 / ms;
        m_renderFrameCount = 0;
        m_fpsTimerRender.restart();

        EZVideoCaptureWindow* pWnd = qobject_cast<EZVideoCaptureWindow*>(this->window());
        if (pWnd != nullptr)
        {
            pWnd->setRenderFPSText(QString("Render: %1 FPS").arg(qRound(m_renderFps)));
        }
    }
}

bool EZVideoRenderer::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip)
    {
        if (this->m_hasFrame)
        {
            QHelpEvent* helpEvent = static_cast<QHelpEvent*>(e);
            QToolTip::showText(helpEvent->globalPos(),
                this->m_strFrameInfo,
                this);
            return true;
        }
    }
    if (e->type() == EZCameraDeviceErrorEventType)
    {
		auto* ce = static_cast<EZCameraDeviceErrorEvent*>(e);
		this->m_pStatusLabel->setText(ce->message());
		this->m_hasFrame = false;

        // EZCamera callback的OnReadSample可能由多个不同线程调用，
        // 无法保证是EZCameraDeviceErrorEvent先处理还是OnFrameReady先处理，(也就是上一帧正常，下一帧就出错了，哪个事件先到达这个 renderer)，
        // 所以这里设置 m_bErrorOccured = true，在OnFrameReady中禁止再显示帧了.
		this->m_bErrorOccured = true;                   
        this->resetFpsInfo();
        this->update();

        return true;
    }

    return QOpenGLWidget::event(e);
}

void EZVideoRenderer::start()
{
    this->m_bStopped = false;
	this->m_bErrorOccured = false;
}

void EZVideoRenderer::stop()
{
    this->m_bStopped = true;
    {
        QMutexLocker locker(&m_mutex);

        this->m_bufferNV12.clear();
        this->m_width = 0;
        this->m_height = 0;
        this->m_stride = 0;
        this->m_hasFrame = false;
    }
	this->m_strStatus = "No Frame";
	this->m_pStatusLabel->setText(m_strStatus);
    this->resetFpsInfo();

    this->update();
}   

/**
 * 从 NV12 缓冲区设置一帧图像
 *
 * nv12 布局：
 *   Y:  stride * height bytes
 *   UV: stride * height / 2 bytes
 */
void EZVideoRenderer::onFrameReady(const QByteArray& nv12,
    int width,
    int height,
    int stride)
{
    if (this->m_bErrorOccured)
    {
        this->m_hasFrame = false;
    }
    else if (this->m_bStopped)
    {
        this->m_strStatus = "No Frame";
        this->m_hasFrame = false;
    }
    else if (nullptr == nv12.data() || width <= 0 || height <= 0 || stride <= 0) 
    {
        this->m_hasFrame = false;
        this->m_strStatus = "Frame capturing...";
    }
    else
    {
        const int yBytes = stride * height;
        const int uvBytes = stride * height / 2;
        const int total = yBytes + uvBytes;
        {
            QMutexLocker locker(&m_mutex);

            m_width = width;
            m_height = height;
            m_stride = stride;
            m_bufferNV12.resize(total);
            memcpy(m_bufferNV12.data(), nv12.data(), total);

            m_hasFrame = true;
        }

        this->m_strStatus = "";

        // 统计输入 FPS
        ++m_inFrameCount;
        qint64 ms = m_fpsTimerIn.elapsed();
        if (ms >= 1000)
        {
            m_inputFps = m_inFrameCount * 1000.0 / ms;
            m_inFrameCount = 0;
            m_fpsTimerIn.restart();

            EZVideoCaptureWindow* pWnd = qobject_cast<EZVideoCaptureWindow*>(this->window());
            if (pWnd != nullptr)
            {
                pWnd->setInFPSText(QString("In: %1 FPS").arg(qRound(m_inputFps)));
            }
        }
    }


	this->m_pStatusLabel->setText(m_strStatus);
    if (!this->m_hasFrame)
    {
        this->resetFpsInfo();
	}

    // 触发重绘（在 GUI 线程执行）
    // 如果 setFrame 是在其它线程调用，务必用 Qt::QueuedConnection 连接这个槽
    if (m_nPresentFps <= 0)
    {
        update();
    }
}

void EZVideoRenderer::onFrameInfo(QString strInfo)
{
    m_strFrameInfo = strInfo;
}

void EZVideoRenderer::onTakePhoto()
{
    QByteArray localNV12;
    int width = 0;
    int height = 0;
    int stride = 0;

    {
        QMutexLocker locker(&m_mutex);
        if (!this->m_hasFrame || this->m_bufferNV12.isEmpty() ||
            m_width <= 0 || m_height <= 0 || m_stride <= 0)
        {
            QMessageBox::warning(this, "Take Photo", "No frame available.");
            return;
        }

        localNV12 = m_bufferNV12;
        width = m_width;
        height = m_height;
        stride = m_stride;
    }

    // 选择保存目录
    QString dir;
    QFileDialog dlg(this, "Select Save Folder");
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);              // 在OPENGLWIDGET里如果弹FileDialog, 这里要禁用NativeDialog, 否则程序会无响应.
    if (dlg.exec() == QDialog::Accepted)
    {
        dir = dlg.selectedFiles().value(0);
    }
    else
    {
        return;
    }
    // 生成文件名：时分秒
    QString fileName = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
    QString filePath = QDir(dir).filePath(fileName);

    // NV12 -> RGB888
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull())
    {
        QMessageBox::warning(this, "Take Photo", "Failed to create image buffer.");
        return;
    }

    const uint8_t* base = reinterpret_cast<const uint8_t*>(localNV12.constData());
    const uint8_t* yPlane = base;
    const uint8_t* uvPlane = base + stride * height;

    auto clampToByte = [](int v) -> uchar
        {
            if (v < 0) return 0;
            if (v > 255) return 255;
            return static_cast<uchar>(v);
        };

    for (int y = 0; y < height; ++y)
    {
        uchar* dst = image.scanLine(y);
        const uint8_t* yRow = yPlane + y * stride;
        const uint8_t* uvRow = uvPlane + (y / 2) * stride;

        for (int x = 0; x < width; ++x)
        {
            int Y = yRow[x];
            int U = uvRow[(x / 2) * 2 + 0];
            int V = uvRow[(x / 2) * 2 + 1];

            int C = Y;
            int D = U - 128;
            int E = V - 128;

            int R = static_cast<int>(C + 1.402 * E);
            int G = static_cast<int>(C - 0.344136 * D - 0.714136 * E);
            int B = static_cast<int>(C + 1.772 * D);

            dst[x * 3 + 0] = clampToByte(R);
            dst[x * 3 + 1] = clampToByte(G);
            dst[x * 3 + 2] = clampToByte(B);
        }
    }

    // 如果预览做了翻转，拍照也保持一致
    if (m_bFlipHorizontal || m_bFlipVertical)
    {
        image = image.mirrored(m_bFlipHorizontal, m_bFlipVertical);
    }

    // QImageWriter可详细控制图像压缩等级.
    /* QImageWriter writer(filePath, "PNG");
    writer.setQuality(100);
    writer.setCompression(0);
    if (!writer.write(image))
    {
        QMessageBox::warning(this, "Take Photo", "Failed to save PNG.");
        return;
    }*/

	// 因为保存的是PNG格式，是无损压缩，所以直接用 QImage::save 就行了，简单方便.
    if (!image.save(filePath, "PNG"))
    {
        QMessageBox::warning(this, "Take Photo", "Failed to save PNG.");
        return;
    }

    QMessageBox::information(this, "Take Photo", QString("Saved to:\n%1").arg(filePath));
}

void EZVideoRenderer::setFlipHorizontal(bool enable)
{
    m_bFlipHorizontal = enable;
    update();
}

void EZVideoRenderer::setFlipVertical(bool enable)
{
    m_bFlipVertical = enable;
    update();
}

void EZVideoRenderer::setPreviewPresentFps(int fps)
{
    m_nPresentFps = fps;
    if (fps <= 0)
    {
        m_pPresentTick->stop();
    }
    else
    {
        m_pPresentTick->start((int)1000.0 / fps);
    }
}

void EZVideoRenderer::initTexturesIfNeeded()
{
    if (!m_hasFrame || m_width <= 0 || m_height <= 0)
    {
        return;
    }

    if (m_texInited &&
        m_texWidth == m_width &&
        m_texHeight == m_height)
    {
        // 尺寸没变，无需重新分配
        return;
    }

    // 初次或分辨率变化时分配纹理存储
    glBindTexture(GL_TEXTURE_2D, m_texY);
    glTexImage2D(GL_TEXTURE_2D, 0,
        GL_R8,
        m_width, m_height,
        0,
        GL_RED,
        GL_UNSIGNED_BYTE,
        nullptr);

    glBindTexture(GL_TEXTURE_2D, m_texUV);
    glTexImage2D(GL_TEXTURE_2D, 0,
        GL_RG8,
        m_width / 2, m_height / 2,
        0,
        GL_RG,
        GL_UNSIGNED_BYTE,
        nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);

    m_texWidth = m_width;
    m_texHeight = m_height;
    m_texInited = true;
}

void EZVideoRenderer::updateStatusLabelPosition()
{
    if (!m_pStatusLabel)
    {
        return;
    }

    const int margin = 20;
    const int labelHeight = 36;
    const int x = margin;
    const int w = width() - margin * 2;
    const int y = (height() - labelHeight) / 2;

    m_pStatusLabel->setGeometry(x, y, w, labelHeight);
}

void EZVideoRenderer::resetFpsInfo()
{
    if (this->m_pParentWnd != nullptr)
    {
        this->m_pParentWnd->setInFPSText("");
        this->m_pParentWnd->setRenderFPSText("");
	}
}

void EZVideoRenderer::resizeEvent(QResizeEvent* event)
{
    QOpenGLWidget::resizeEvent(event);
    updateStatusLabelPosition();
}



