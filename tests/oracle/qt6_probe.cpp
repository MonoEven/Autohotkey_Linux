// qt6_probe.cpp -- real Qt6 GUI/AT-SPI capture host.
// Exposes a named QLineEdit and QPushButton. Clicking changes the entry text
// and writes a process-external marker so an oracle can verify the Action.
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle("AHK Qt6 Probe");
    window.setAccessibleName("AHK Qt6 Probe");
    window.resize(420, 180);

    auto *layout = new QVBoxLayout(&window);
    auto *label = new QLabel("Qt6 accessibility capture host", &window);
    label->setAccessibleName("QT6-LABEL");
    auto *entry = new QLineEdit("你好-Qt6", &window);
    entry->setAccessibleName("QT6-ENTRY");
    entry->setAccessibleDescription("Editable Qt6 capture value");
    auto *button = new QPushButton("Run", &window);
    button->setAccessibleName("QT6-BUTTON");
    button->setAccessibleDescription("Changes the entry and writes a marker");
    layout->addWidget(label);
    layout->addWidget(entry);
    layout->addWidget(button);

    QObject::connect(button, &QPushButton::clicked, [&]() {
        entry->setText("clicked-Qt6");
        QFile marker("/tmp/qt6-probe-click");
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream stream(&marker);
            stream << "clicked\n";
        }
    });
    window.show();
    return app.exec();
}
