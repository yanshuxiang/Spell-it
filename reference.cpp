#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QVariantAnimation>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QVector>
#include <QDebug>
#include <cmath>

// ==========================================
// 1. 数据模型与接口定义 (供业务层调用)
// ==========================================

struct BookData {
    QString title;
    int progress;
    QColor color;
    QString tag;
};

// 核心接口类：将 UI 操作回调给业务逻辑
class IAppController {
public:
    virtual ~IAppController() = default;
    
    // 接口：获取词书列表
    virtual QVector<BookData> getBooks() = 0;
    
    // 接口：按钮点击事件
    virtual void onReviewClicked(const BookData& book) = 0;
    virtual void onStudyClicked(const BookData& book) = 0;
    
    // 接口：底部导航栏切换
    virtual void onNavTabChanged(int tabIndex) = 0;
};

// ==========================================
// 2. 自定义 UI 控件
// ==========================================

// 自定义圆角卡片
class BookCardWidget : public QWidget {
    BookData m_book;
    IAppController* m_controller;
    
    QLabel* m_titleLabel;
    QLabel* m_progressLabel;
    QPushButton* m_btnReview;
    QPushButton* m_btnStudy;
    QWidget* m_coverWidget;
    
public:
    BookCardWidget(IAppController* ctrl, QWidget* parent = nullptr) 
        : QWidget(parent), m_controller(ctrl) {
        
        setAttribute(Qt::WA_TranslucentBackground);
        
        // 布局构建
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(24, 24, 24, 24);
        
        // 上半部分：封面 + 信息
        QHBoxLayout* topLayout = new QHBoxLayout();
        m_coverWidget = new QWidget(this);
        m_coverWidget->setFixedSize(80, 110);
        
        QVBoxLayout* infoLayout = new QVBoxLayout();
        m_titleLabel = new QLabel(this);
        m_titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #1f2937;");
        m_progressLabel = new QLabel(this);
        m_progressLabel->setStyleSheet("font-size: 10px; color: #9ca3af; font-family: monospace;");
        infoLayout->addWidget(m_titleLabel);
        infoLayout->addWidget(m_progressLabel);
        infoLayout->addStretch();
        
        topLayout->addWidget(m_coverWidget);
        topLayout->addSpacing(15);
        topLayout->addLayout(infoLayout);
        
        mainLayout->addLayout(topLayout);
        mainLayout->addStretch();
        
        // 下半部分：按钮
        QHBoxLayout* btnLayout = new QHBoxLayout();
        m_btnReview = new QPushButton("复习", this);
        m_btnStudy = new QPushButton("✨ 学习", this);
        
        // 极简风格按钮样式
        m_btnReview->setStyleSheet("QPushButton { border: 1px solid #e5e7eb; border-radius: 12px; padding: 12px; color: #6b7280; font-size: 13px; background: transparent; } QPushButton:hover { background: #f9fafb; }");
        m_btnStudy->setStyleSheet("QPushButton { background-color: #111827; border-radius: 12px; padding: 12px; color: white; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #000000; }");
        
        btnLayout->addWidget(m_btnReview);
        btnLayout->addWidget(m_btnStudy);
        mainLayout->addLayout(btnLayout);

        // 绑定事件到接口
        connect(m_btnReview, &QPushButton::clicked, this, [this](){
            if(m_controller) m_controller->onReviewClicked(m_book);
        });
        connect(m_btnStudy, &QPushButton::clicked, this, [this](){
            if(m_controller) m_controller->onStudyClicked(m_book);
        });
        
        // 添加阴影
        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(30);
        shadow->setColor(QColor(0, 0, 0, 15));
        shadow->setOffset(0, 10);
        setGraphicsEffect(shadow);
    }

    void setBook(const BookData& book) {
        m_book = book;
        m_titleLabel->setText(book.title);
        m_progressLabel->setText(QString("PROGRESS %1%").arg(book.progress));
        
        // 动态设置封面颜色
        QString coverStyle = QString("background-color: %1; border-radius: 6px;").arg(book.color.name());
        m_coverWidget->setStyleSheet(coverStyle);
    }
    
    // 设置交互状态 (侧边卡片不可点击)
    void setInteractive(bool active) {
        m_btnReview->setEnabled(active);
        m_btnStudy->setEnabled(active);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(rect().adjusted(1,1,-1,-1), 24, 24);
        p.fillPath(path, Qt::white);
        p.setPen(QPen(QColor("#f3f4f6"), 1));
        p.drawPath(path);
    }
};

// 画廊滑动核心组件
class CarouselGallery : public QWidget {
    IAppController* m_controller;
    QVector<BookData> m_books;
    
    QList<BookCardWidget*> m_cardWidgets;
    QList<QGraphicsOpacityEffect*> m_opacities;
    
    float m_currentOffset = 0.0f;
    int m_dragStartX = 0;
    float m_dragStartOffset = 0.0f;
    bool m_isDragging = false;

public:
    CarouselGallery(IAppController* ctrl, QWidget* parent = nullptr) 
        : QWidget(parent), m_controller(ctrl) {
        
        m_books = m_controller->getBooks();
        
        // 创建 5 个虚拟卡片实例以实现无限循环和两侧预览
        for (int i = 0; i < 5; ++i) {
            auto card = new BookCardWidget(m_controller, this);
            auto opacity = new QGraphicsOpacityEffect(card);
            card->setGraphicsEffect(opacity);
            
            m_cardWidgets.append(card);
            m_opacities.append(opacity);
        }
    }

    void updateCards() {
        if (m_books.isEmpty()) return;
        
        int baseW = width() * 0.75;
        int baseH = height() * 0.75;
        int centerX = width() / 2;
        int centerY = height() / 2 - 20; // 整体向上偏移，为底部腾出空间

        for (int i = 0; i < 5; ++i) {
            // 计算虚拟偏移索引 (-2, -1, 0, 1, 2)
            int vIndex = std::round(m_currentOffset) + (i - 2);
            float offset = vIndex - m_currentOffset;
            
            // 真实的数组索引 (无限循环逻辑)
            int realIndex = ((vIndex % m_books.size()) + m_books.size()) % m_books.size();
            
            auto card = m_cardWidgets[i];
            card->setBook(m_books[realIndex]);
            
            // 计算 3D 景深与位置
            float absOffset = std::abs(offset);
            float scale = 1.0f - (0.14f * std::min(absOffset, 2.0f));
            
            int w = baseW * scale;
            int h = baseH * scale;
            int x = centerX - w / 2 + (offset * baseW * 1.04);
            int y = centerY - h / 2;
            
            card->setGeometry(x, y, w, h);
            
            // 设置透明度和层级
            float alpha = (absOffset > 1.2f) ? 0.0f : (1.0f - (0.5f * absOffset));
            m_opacities[i]->setOpacity(alpha);
            card->raise(); // 简易 Z-index 排序，实际应用中可以按 offset 排序
            
            // 只有正中间的卡片可点击
            card->setInteractive(absOffset < 0.1f);
        }
    }

protected:
    void resizeEvent(QResizeEvent *) override {
        updateCards();
    }

    void mousePressEvent(QMouseEvent *e) override {
        m_dragStartX = e->x();
        m_dragStartOffset = m_currentOffset;
        m_isDragging = true;
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!m_isDragging) return;
        float dx = e->x() - m_dragStartX;
        m_currentOffset = m_dragStartOffset - (dx / (width() * 0.75));
        updateCards();
    }

    void mouseReleaseEvent(QMouseEvent *) override {
        if (!m_isDragging) return;
        m_isDragging = false;
        
        // 松手后吸附到最近的整数索引
        float targetOffset = std::round(m_currentOffset);
        
        QVariantAnimation* anim = new QVariantAnimation(this);
        anim->setDuration(400);
        anim->setStartValue(m_currentOffset);
        anim->setEndValue(targetOffset);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& val){
            m_currentOffset = val.toFloat();
            updateCards();
        });
        connect(anim, &QVariantAnimation::finished, anim, &QObject::deleteLater);
        anim->start();
    }
};

// 底部悬浮导航栏
class FloatingNavWidget : public QWidget {
    IAppController* m_controller;
public:
    FloatingNavWidget(IAppController* ctrl, QWidget* parent = nullptr) 
        : QWidget(parent), m_controller(ctrl) {
        
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedHeight(60);
        
        QHBoxLayout* layout = new QHBoxLayout(this);
        layout->setContentsMargins(30, 0, 30, 0);
        
        // 简单使用文本代替图标 (实际应用可用 QIcon)
        QStringList icons = {"书本", "日历", "统计"};
        for(int i=0; i<icons.size(); ++i) {
            QPushButton* btn = new QPushButton(icons[i], this);
            btn->setFixedSize(50, 40);
            btn->setStyleSheet("QPushButton { border: none; background: transparent; color: #9ca3af; font-weight: bold; } QPushButton:hover { color: #111827; }");
            layout->addWidget(btn);
            
            connect(btn, &QPushButton::clicked, this, [this, i](){
                if(m_controller) m_controller->onNavTabChanged(i);
            });
        }
        
        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 20));
        shadow->setOffset(0, 5);
        setGraphicsEffect(shadow);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        // 毛玻璃效果背景
        path.addRoundedRect(rect().adjusted(1,1,-1,-1), 30, 30);
        p.fillPath(path, QColor(255, 255, 255, 220)); 
        p.setPen(QPen(QColor(255, 255, 255, 180), 1));
        p.drawPath(path);
    }
};

// ==========================================
// 3. 业务逻辑层与应用组装
// ==========================================

// 实现接口类
class MyAppLogic : public IAppController {
public:
    QVector<BookData> getBooks() override {
        return {
            {"Oxford 5000", 35, QColor("#111827"), "CORE"},
            {"GRE Essential", 12, QColor("#1e1b4b"), "ADVANCED"},
            {"IELTS Vocab", 85, QColor("#27272a"), "EXAM"}
        };
    }
    
    void onReviewClicked(const BookData& book) override {
        qDebug() << "[接口调用] 开始复习:" << book.title;
        // 在这里添加跳转到复习界面的代码
    }
    
    void onStudyClicked(const BookData& book) override {
        qDebug() << "[接口调用] AI 学习启动:" << book.title;
        // 在这里调用 Gemini API 或打开 AI 弹窗
    }
    
    void onNavTabChanged(int tabIndex) override {
        qDebug() << "[接口调用] 切换到底部导航栏索引:" << tabIndex;
    }
};

// 主窗口 (模拟手机外壳)
class MainWindow : public QWidget {
public:
    MainWindow(IAppController* ctrl, QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(380, 760); // 模拟手机比例
        setStyleSheet("background-color: #f9fafb;");
        
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 40, 0, 30);
        
        // 顶部统计
        QLabel* statsLabel = new QLabel("今日已学 7 · 今日复习 0", this);
        statsLabel->setAlignment(Qt::AlignCenter);
        statsLabel->setStyleSheet("color: #9ca3af; font-size: 11px; letter-spacing: 2px;");
        mainLayout->addWidget(statsLabel);
        
        // 中间画廊
        CarouselGallery* gallery = new CarouselGallery(ctrl, this);
        mainLayout->addWidget(gallery, 1);
        
        // 底部悬浮导航栏
        FloatingNavWidget* nav = new FloatingNavWidget(ctrl, this);
        mainLayout->addWidget(nav);
        mainLayout->setAlignment(nav, Qt::AlignHCenter);
        
        // 底部格言
        QLabel* quoteLabel = new QLabel("长期主义的核心是无视中断", this);
        quoteLabel->setAlignment(Qt::AlignCenter);
        quoteLabel->setStyleSheet("color: #9ca3af; font-size: 10px; margin-top: 15px;");
        mainLayout->addWidget(quoteLabel);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // 实例化业务逻辑与主 UI
    MyAppLogic logic;
    MainWindow window(&logic);
    window.show();
    
    return app.exec();
}