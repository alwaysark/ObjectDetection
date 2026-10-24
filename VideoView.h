//
// VideoView —— 视频显示控件（自绘，替代旧版 QLabel + QPixmap 方案）。
//
// 旧方案每帧要做两次转换：QImage→QPixmap（fromImage）+ scaled 再转一次；
// 本控件直接持有 QImage（隐式共享，浅拷贝），paintEvent 里用 QPainter::drawImage
// 按 KeepAspectRatio 等比缩放居中，一次完成。深色背景与无画面占位文字自绘。
//
// 线程约定：setImage 只在主线程调用（do_frameReady 槽里），无锁。
//
#ifndef FIRESMOKEDETECTIONZCODE_VIDEOVIEW_H
#define FIRESMOKEDETECTIONZCODE_VIDEOVIEW_H

#include <QWidget>
#include <QImage>
#include <QString>

class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);

    void setImage(const QImage& img);           // 新帧到了（触发一次异步重绘）
    QImage image() const { return m_image; }    // 当前帧（截图/告警快照用；无帧为空图）

    void setPlaceholder(const QString& text);   // 无画面时的提示文字

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage  m_image;
    QString m_placeholder;
};

#endif //FIRESMOKEDETECTIONZCODE_VIDEOVIEW_H
