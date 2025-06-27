#include <QtCore/QCoreApplication>

#include <qjsonsettings.h>

static auto JSONFormat = QSettings::InvalidFormat;

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    // Register JSON format
    JSONFormat = QJsonSettings::registerFormat();

    QSettings settings("settings.json", JSONFormat);
    settings.setValue("a", 1);
    settings.setValue("b/c", true);
    settings.setValue("d/e/f", "Hello World!");
    settings.setValue("foo", "foo");
    settings.setValue("foo/bar", "foo/bar");
    settings.sync();

    return 0;
}