// End-to-end smoke test for the Qt6 + C++ toolchain provisioned by
// .cursor/install.sh. It exercises QtCore (signals/slots, QMetaObject
// introspection), QtGui and QtWidgets (QSS-styled widgets rendered off-screen
// to a PNG). The techniques mirror snippets from this repository's README
// (QSS styling, metaobject property enumeration, QTimer::singleShot usage).
//
// Run headlessly with:  QT_QPA_PLATFORM=offscreen ./qt_env_smoke [output.png]

#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMetaObject>
#include <QMetaProperty>
#include <QTimer>
#include <QPixmap>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QString out = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                   : QStringLiteral("render.png");

    QWidget window;
    window.setObjectName("root");
    window.setWindowTitle("Qt 环境自检 / Qt env smoke test");
    window.resize(420, 220);
    window.setStyleSheet(
        "#root { background: #1e2430; }"
        "QLabel { color: #e6e6e6; font-size: 20px; font-weight: bold; }"
        "QLabel#sub { color: #7fd1b9; font-size: 14px; font-weight: normal; }"
        "QPushButton { background: #2d7d64; color: white; border-radius: 8px;"
        "  padding: 10px 18px; font-size: 15px; }");

    auto *layout = new QVBoxLayout(&window);
    auto *title = new QLabel(QStringLiteral("Qt %1 运行成功").arg(QT_VERSION_STR), &window);
    title->setAlignment(Qt::AlignCenter);
    auto *sub = new QLabel(QStringLiteral("qt- 学习仓库开发环境已就绪"), &window);
    sub->setObjectName("sub");
    sub->setAlignment(Qt::AlignCenter);
    auto *button = new QPushButton(QStringLiteral("点击测试信号槽"), &window);
    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(sub);
    layout->addWidget(button, 0, Qt::AlignCenter);
    layout->addStretch();

    QObject::connect(button, &QPushButton::clicked, [] {
        qInfo() << "[signal] button clicked -> slot invoked OK";
    });

    const QMetaObject *mo = button->metaObject();
    qInfo() << "[introspection] QPushButton class:" << mo->className()
            << "property count:" << mo->propertyCount();
    QMetaProperty textProp = mo->property(mo->indexOfProperty("text"));
    qInfo() << "[introspection] text property type:" << textProp.typeName()
            << "value:" << button->property("text").toString();

    window.show();

    int rc = 0;
    QTimer::singleShot(0, [&] {
        button->click();
        QPixmap shot = window.grab();
        if (shot.save(out)) {
            qInfo() << "[render] saved widget screenshot to" << out << shot.size();
        } else {
            qWarning() << "[render] FAILED to save screenshot to" << out;
            rc = 1;
        }
        QApplication::quit();
    });

    app.exec();
    return rc;
}
