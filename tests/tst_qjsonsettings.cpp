#include <utility>
#include <limits>

#include <QtCore/QCoreApplication>
#include <QtCore/QRect>
#include <QtCore/QLine>
#include <QtCore/QPoint>
#include <QtCore/QSize>
#include <QtCore/QFile>
#include <QtCore/QVariant>
#include <QtCore/QUuid>
#include <QtGui/QColor>
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

static QString randomSettingsFileName() {
    QUuid uuid = QUuid::createUuid();
    return "settings-" + uuid.toString(QUuid::WithoutBraces).replace("-", "_") + ".json";
}

class Test : public QObject {
    Q_OBJECT
public:
    explicit Test(QObject *parent = nullptr) : QObject(parent) {
    }

private:
    QString settingsPath;

    // There is a cache inside QSettings. We need to make a copy of the written settings file.
    void refreshSettingsFiles() {
        if (settingsPath.isEmpty()) {
            settingsPath = randomSettingsFileName();
            return;
        }

        if (QFile::exists(settingsPath)) {
            QString newSettingsPath = randomSettingsFileName();
            QVERIFY(QFile::copy(settingsPath, newSettingsPath));
            QVERIFY(QFile::remove(settingsPath));
            settingsPath = newSettingsPath;
        }
    }

    void removeSettingsFiles() {
        if (settingsPath.isEmpty() || !QFile::exists(settingsPath)) {
            return;
        }
        std::ignore = QFile::remove(settingsPath);
        settingsPath.clear();
    }

private Q_SLOTS:
    void initTestCase() {
        format = QJsonSettings::registerFormat();
        QVERIFY(format != QSettings::InvalidFormat);
    }

    void init() {
        refreshSettingsFiles();
    }

    void cleanup() {
        removeSettingsFiles();
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

        refreshSettingsFiles();

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
    }

    void testVariants() {
        const QList<QPair<QString, QVariant>> testPairs = {
            {"bool", true},
            {"int", 810},
            {"uint", quint32(19)},
            {"longlong", std::numeric_limits<qlonglong>::lowest() + 1},
            {"ulonglong", std::numeric_limits<qulonglong>::max() - 1},
            {"double", 3.14},
            {"float", 3.14f},
            {"string", "Hello, world!"},
            {"jsonArray", QJsonArray({"foo", "bar", 123})},
            {"stringList", QStringList({"foo", "bar", "baz"})},
            {"byteArray", QByteArray("\x01\x02\x03\x04")},
            {"rect", QRect(10, 20, 30, 40)},
            {"rectF", QRectF(10.5, 20.5, 30.5, 40.5)},
            {"size", QSize(50, 60)},
            {"sizeF", QSizeF(50.5, 60.5)},
            {"point", QPoint(10, 20)},
            {"pointF", QPointF(10.5, 20.5)},
            {"line", QLine(QPoint(10, 20), QPoint(30, 40))},
            {"lineF", QLineF(QPointF(10.5, 20.5), QPointF(30.5, 40.5))},
            {"variantPair", QVariant::fromValue(QVariantPair(123, "Hello, world!"))},
            {"variantList", QVariantList({"foo", 123, true})},
            {"variantMap", QVariantMap({{"foo", "bar"}, {"baz", 123}})},
            {"variantHash", QVariantHash({{"foo", "bar"}, {"baz", 123}})},
            {"jsonValue", QJsonValue(123)},
            {"jsonObject", QJsonObject({{"foo", "bar"}, {"baz", 123}})},
            {"jsonDocument", QJsonDocument(QJsonObject({{"foo", "bar"}, {"baz", 123}}))},
            {"dateTime", QDateTime::currentDateTime()},
            {"color", QColor(255, 255, 255)},
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

        refreshSettingsFiles();

        // Read settings
        {
            QSettings settings(settingsPath, format);
            for (const auto &pair : testPairs) {
                auto value = settings.value(pair.first);
                QVERIFY(value == pair.second);
            }
        }
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

        refreshSettingsFiles();

        // Write settings again
        {
            QSettings settings(settingsPath, format);
            for (const auto &pair : testPairs2) {
                settings.setValue(pair.first, pair.second);
            }
            settings.sync();
        }

        refreshSettingsFiles();

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