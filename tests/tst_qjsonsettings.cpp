#include <QtCore/QCoreApplication>
#include <QtCore/QRect>
#include <QtCore/QFile>
#include <QtTest/QtTest>

#include <qjsonsettings.h>

static QSettings::Format format = QSettings::InvalidFormat;

static bool readJson(const QString &path, QJsonObject &out) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    out = doc.object();
    return true;
}

class Test : public QObject {
    Q_OBJECT
public:
    Test(QObject *parent = nullptr) : QObject(parent) {
    }

private:
    QString settingsPath;

    void removeSettingsFile() {
        std::ignore = QFile::remove(settingsPath);
    }

private Q_SLOTS:
    void initTestCase() {
        format = QJsonSettings::registerFormat();
        QVERIFY(format != QSettings::InvalidFormat);

        settingsPath =
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("settings.json");
    }

    void cleanup() {
        removeSettingsFile();
    }

    void testFormat() {
        QList<QStringList> testKeysList = {
            {"foo"},
            {"foo", "bar"},
            {"foo", "bar", "baz"},
            {"foo", "bar", "baz", "qux"},
        };

        // Write settings
        {
            QSettings settings(settingsPath, format);

            for (qsizetype i = 0; i < testKeysList.size(); ++i) {
                QString mergedKeys = testKeysList[i].join(QLatin1Char('/'));
                settings.setValue(mergedKeys, int(i + 1));
            }

            settings.sync();
        }

        // Read JSON
        {
            QJsonObject settingsObject;
            QVERIFY(readJson(settingsPath, settingsObject));

            for (qsizetype i = 0; i < testKeysList.size(); ++i) {
                const auto &keys = testKeysList[i];
                QJsonValue value = settingsObject;
                for (const auto &key : keys) {
                    QVERIFY(value.isObject());

                    auto obj = value.toObject();
                    auto it = obj.find(key);
                    QVERIFY(it != obj.end());
                    value = it.value();
                }

                if (value.isObject()) {
                    auto obj = value.toObject();

                    auto it = obj.find(QJsonSettings::reservedKey(QJsonSettings::Value));
                    QVERIFY(it != obj.end());

                    value = it.value();
                }
                QVERIFY(value.isDouble() && value.toInt() == i + 1);
            }
        }

        // Read settings
        {
            QSettings settings(settingsPath, format);

            for (qsizetype i = 0; i < testKeysList.size(); ++i) {
                QString mergedKeys = testKeysList[i].join(QLatin1Char('/'));
                auto value = settings.value(mergedKeys);
                QVERIFY(value.isValid() && value.toInt() == i + 1);
            }
        }

        removeSettingsFile();
    }

    void testVariants() {
        const QList<QPair<QString, QVariant>> testPairs = {
            {"int", 123},
            {"double", 3.14},
            {"string", "Hello, world!"},
            {"bool", true},
            {"point", QPoint(10, 20)},
            {"rect", QRect(10, 20, 30, 40)},
            {"size", QSize(50, 60)},
            {"dateTime", QDateTime::currentDateTime()},
            {"stringList", QStringList({"foo", "bar", "baz"})},
            {"byteArray", QByteArray("Hello, world!")},
            {"jsonValue", QJsonValue(123)},
            {"jsonObject", QJsonObject({{"foo", "bar"}, {"baz", 123}})},
            {"jsonArray", QJsonArray({"foo", "bar", 123})},
            {"variantList", QVariantList({"foo", 123, true})},
            {"variantMap", QVariantMap({{"foo", "bar"}, {"baz", 123}})},
            {"invalid", QVariant()},
        };

        // Write settings
        {
            QSettings settings(settingsPath, format);

            for (const auto &pair : testPairs) {
                settings.setValue(pair.first, pair.second);
            }

            settings.sync();
        }

        // Read settings
        {
            QSettings settings(settingsPath, format);

            for (const auto &pair : testPairs) {
                auto value = settings.value(pair.first);
                QVERIFY(value == pair.second);
            }
        }

        removeSettingsFile();
    }

    void testModify() {
        const QList<QPair<QString, QVariant>> testPairs1 = {
            {"foo", "abc"},
            {"bar", 123  },
            {"baz", true },
        };
        const QList<QPair<QString, QVariant>> testPairs2 = {
            {"foo", "xyz"},
            {"bar", 456  },
            {"baz", false},
        };

        // Write settings
        {
            QSettings settings(settingsPath, format);

            for (const auto &pair : testPairs1) {
                settings.setValue(pair.first, pair.second);
            }

            settings.sync();
        }

        // Write settings again
        {
            QSettings settings(settingsPath, format);

            for (const auto &pair : testPairs2) {
                settings.setValue(pair.first, pair.second);
            }

            settings.sync();
        }

        // Read settings
        {
            QSettings settings(settingsPath, format);

            for (const auto &pair : testPairs2) {
                auto value = settings.value(pair.first);
                QVERIFY(value == pair.second);
            }
        }
    }
};

QTEST_MAIN(Test)

#include "tst_qjsonsettings.moc"