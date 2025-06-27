// Copyright (C) 2025 Stdware Collections (https://www.github.com/stdware)
// SPDX-License-Identifier: MIT

#ifndef QJSONSETTINGS_H
#define QJSONSETTINGS_H

#include <QtCore/QSettings>

class QJsonSettings {
public:
    QJsonSettings() = delete;
    ~QJsonSettings() = delete;

    enum ReservedKey {
        Value,
        ValueType,
        ValueData,
    };
    static QString reservedKey(ReservedKey key);

    static bool read(QIODevice &dev, QSettings::SettingsMap &settings);
    static bool write(QIODevice &dev, const QSettings::SettingsMap &settings);

    static inline QSettings::Format registerFormat() {
        return QSettings::registerFormat(QStringLiteral("json"), read, write, Qt::CaseSensitive);
    }
};

#endif // QJSONSETTINGS_H