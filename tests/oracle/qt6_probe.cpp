// qt6_probe.cpp -- real Qt6 GUI/AT-SPI capture host.
// Exposes a named QLineEdit and QPushButton. Clicking changes the entry text
// and writes a process-external marker so an oracle can verify the Action.
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle("AHK Qt6 Probe");
    window.setAccessibleName("AHK Qt6 Probe");
    window.resize(480, 420);

    auto *layout = new QVBoxLayout(&window);
    auto *label = new QLabel("Qt6 accessibility capture host", &window);
    label->setAccessibleName("QT6-LABEL");
    auto *entry = new QLineEdit("你好-Qt6", &window);
    entry->setAccessibleName("QT6-ENTRY");
    entry->setAccessibleDescription("Editable Qt6 capture value");
    auto *list = new QListWidget(&window);
    list->setAccessibleName("QT6-LIST");
    list->addItems({"Alpha", "Bravo", "世界"});
    list->setCurrentRow(0);
    auto *slider = new QSlider(Qt::Horizontal, &window);
    slider->setAccessibleName("QT6-SLIDER");
    slider->setRange(0, 100);
    slider->setValue(25);
    auto *valueLabel = new QLabel("value=25", &window);
    valueLabel->setAccessibleName("QT6-VALUE-LABEL");
    auto *button = new QPushButton("Run", &window);
    button->setAccessibleName("QT6-BUTTON");
    button->setAccessibleDescription("Changes the entry and writes a marker");
    layout->addWidget(label);
    layout->addWidget(entry);
    layout->addWidget(list);
    layout->addWidget(slider);
    layout->addWidget(valueLabel);
    layout->addWidget(button);

    QObject::connect(list, &QListWidget::itemSelectionChanged, [=]() {
        QFile marker("/tmp/qt6-probe-selection");
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream stream(&marker);
            const auto selected = list->selectedItems();
            stream << (selected.isEmpty() ? QString() : selected.first()->text()) << "\n";
        }
    });
    QObject::connect(slider, &QSlider::valueChanged, [=](int value) {
        valueLabel->setText(QString("value=%1").arg(value));
        QFile marker("/tmp/qt6-probe-value");
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream stream(&marker);
            stream << value << "\n";
        }
    });
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
