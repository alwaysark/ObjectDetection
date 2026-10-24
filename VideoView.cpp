#include "VideoView.h"
#include <QPainter>
#include <QFont>

VideoView::VideoView(QWidget* parent) : QWidget(parent) {
    // 背景完全由 paintEvent 自己刷，省掉系统的一次预擦除
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void VideoView::setImage(const QImage& img) {
    m_image = img;      // 隐式共享：浅拷贝 + 引用计数，无像素拷贝
    update();           // 合并高频重绘请求，下次事件循环统一画
}

void VideoView::setPlaceholder(const QString& text) {
    m_placeholder = text;
    update();
}

void VideoView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x20, 0x20, 0x20));    // 与旧版样式表同色的"显示器底色"

    if (m_image.isNull()) {
        if (!m_placeholder.isEmpty()) {
            p.setPen(QColor(0xdd, 0xdd, 0xdd));
            QFont f = p.font();
            f.setPixelSize(14);
            p.setFont(f);
            p.drawText(rect(), Qt::AlignCenter, m_placeholder);
        }
        return;
    }

    // KeepAspectRatio：等比缩放到控件内的最大可用矩形，居中（多余的边就是"黑边"）
    const QSize scaled = m_image.size().scaled(rect().size(), Qt::KeepAspectRatio);
    const QRect target((width() - scaled.width()) / 2, (height() - scaled.height()) / 2,
                       scaled.width(), scaled.height());
    p.setRenderHint(QPainter::SmoothPixmapTransform);    // 缩放插值，画质与旧版 SmoothTransformation 对齐
    p.drawImage(target, m_image);
}
