#include "qjsonsettings.h"

#include <utility>
#include <algorithm>

#include <QtCore/QIODevice>
#include <QtCore/QByteArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QVariant>
#include <QtCore/QRect>
#include <QtCore/QPoint>
#include <QtCore/QLine>
#include <QtCore/QDateTime>
#include <QtCore/QVarLengthArray>

// Qt 6.8
namespace _QSettingsPrivate {

    using namespace Qt::StringLiterals;

    static QStringList splitArgs(const QString &s, qsizetype idx) {
        qsizetype l = s.size();
        Q_ASSERT(l > 0);
        Q_ASSERT(s.at(idx) == u'(');
        Q_ASSERT(s.at(l - 1) == u')');

        QStringList result;
        QString item;

        for (++idx; idx < l; ++idx) {
            QChar c = s.at(idx);
            if (c == u')') {
                Q_ASSERT(idx == l - 1);
                result.append(item);
            } else if (c == u' ') {
                result.append(item);
                item.clear();
            } else {
                item.append(c);
            }
        }

        return result;
    }

    static QString variantToString(const QVariant &v) {
        QString result;

        switch (v.metaType().id()) {
            case QMetaType::UnknownType:
                result = "@Invalid()"_L1;
                break;

            case QMetaType::QByteArray: {
                QByteArray a = v.toByteArray();
                result = "@ByteArray("_L1 + QLatin1StringView(a) + u')';
                break;
            }

#if QT_CONFIG(shortcut)
            case QMetaType::QKeySequence:
#endif
            case QMetaType::QString:
            case QMetaType::LongLong:
            case QMetaType::ULongLong:
            case QMetaType::Int:
            case QMetaType::UInt:
            case QMetaType::Bool:
            case QMetaType::Float:
            case QMetaType::Double: {
                result = v.toString();
                if (result.contains(QChar::Null))
                    result = "@String("_L1 + result + u')';
                else if (result.startsWith(u'@'))
                    result.prepend(u'@');
                break;
            }
#ifndef QT_NO_GEOM_VARIANT
            case QMetaType::QRect: {
                QRect r = qvariant_cast<QRect>(v);
                result =
                    QString::asprintf("@Rect(%d %d %d %d)", r.x(), r.y(), r.width(), r.height());
                break;
            }
            case QMetaType::QSize: {
                QSize s = qvariant_cast<QSize>(v);
                result = QString::asprintf("@Size(%d %d)", s.width(), s.height());
                break;
            }
            case QMetaType::QPoint: {
                QPoint p = qvariant_cast<QPoint>(v);
                result = QString::asprintf("@Point(%d %d)", p.x(), p.y());
                break;
            }
#endif // !QT_NO_GEOM_VARIANT

            default: {
#ifndef QT_NO_DATASTREAM
                QDataStream::Version version;
                const char *typeSpec;
                if (v.userType() == QMetaType::QDateTime) {
                    version = QDataStream::Qt_5_6;
                    typeSpec = "@DateTime(";
                } else {
                    version = QDataStream::Qt_4_0;
                    typeSpec = "@Variant(";
                }
                QByteArray a;
                {
                    QDataStream s(&a, QIODevice::WriteOnly);
                    s.setVersion(version);
                    s << v;
                }

                result =
                    QLatin1StringView(typeSpec) + QLatin1StringView(a.constData(), a.size()) + u')';
#else
                Q_ASSERT(!"QSettings: Cannot save custom types without QDataStream support");
#endif
                break;
            }
        }

        return result;
    }

    static QVariant stringToVariant(const QString &s) {
        if (s.startsWith(u'@')) {
            if (s.endsWith(u')')) {
                if (s.startsWith("@ByteArray("_L1)) {
                    return QVariant(QStringView{s}.sliced(11).chopped(1).toLatin1());
                } else if (s.startsWith("@String("_L1)) {
                    return QVariant(QStringView{s}.sliced(8).chopped(1).toString());
                } else if (s.startsWith("@Variant("_L1) || s.startsWith("@DateTime("_L1)) {
#ifndef QT_NO_DATASTREAM
                    QDataStream::Version version;
                    int offset;
                    if (s.at(1) == u'D') {
                        version = QDataStream::Qt_5_6;
                        offset = 10;
                    } else {
                        version = QDataStream::Qt_4_0;
                        offset = 9;
                    }
                    QByteArray a = QStringView{s}.sliced(offset).toLatin1();
                    QDataStream stream(&a, QIODevice::ReadOnly);
                    stream.setVersion(version);
                    QVariant result;
                    stream >> result;
                    return result;
#else
                    Q_ASSERT(!"QSettings: Cannot load custom types without QDataStream support");
#endif
#ifndef QT_NO_GEOM_VARIANT
                } else if (s.startsWith("@Rect("_L1)) {
                    QStringList args = splitArgs(s, 5);
                    if (args.size() == 4)
                        return QVariant(QRect(args[0].toInt(), args[1].toInt(), args[2].toInt(),
                                              args[3].toInt()));
                } else if (s.startsWith("@Size("_L1)) {
                    QStringList args = splitArgs(s, 5);
                    if (args.size() == 2)
                        return QVariant(QSize(args[0].toInt(), args[1].toInt()));
                } else if (s.startsWith("@Point("_L1)) {
                    QStringList args = splitArgs(s, 6);
                    if (args.size() == 2)
                        return QVariant(QPoint(args[0].toInt(), args[1].toInt()));
#endif
                } else if (s == "@Invalid()"_L1) {
                    return QVariant();
                }
            }
            if (s.startsWith("@@"_L1))
                return QVariant(s.sliced(1));
        }

        return QVariant(s);
    }

}

// UTILS
namespace {

    static const QString kKeyValue = QStringLiteral("$value");

    static const QString kKeyValueType = QStringLiteral("$type");

    static const QString kKeyValueData = QStringLiteral("$data");

    static const QLatin1Char kSeparator = QLatin1Char('/');

    // READ
    QVariant jsonValueToVariant(const QJsonValue &value) {
        switch (value.type()) {
            case QJsonValue::Bool:
                return value.toBool();
            case QJsonValue::Double:
                return value.toDouble();
            case QJsonValue::String:
                return value.toString();
            case QJsonValue::Array:
                return value.toArray();
            case QJsonValue::Object: {
                const auto &obj = value.toObject();
                auto it = obj.find(kKeyValueType);
                if (it == obj.end()) {
                    return obj;
                }
                int type = it.value().toInt();

                it = obj.find(kKeyValueData);
                if (it == obj.end()) {
                    return obj;
                }
                const auto &value = it.value();

                switch (type) {
                    // Large integer types
                    case QMetaType::LongLong: {
                        return value.toString().toLongLong();
                    }
                    case QMetaType::ULongLong: {
                        return value.toString().toULongLong();
                    }

                    // String list
                    case QMetaType::QStringList: {
                        QStringList result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            result.reserve(arr.size());
                            for (const auto &v : arr) {
                                result.append(v.toString());
                            }
                        }
                        return result;
                    }

                    // Byte array
                    case QMetaType::QByteArray: {
                        QByteArray result;
                        if (value.isString()) {
                            result = value.toString().toLatin1();
                        }
                        return result;
                    }

                    // Simple structure types
                    case QMetaType::QRect: {
                        QRect result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            if (arr.size() == 4) {
                                result = {
                                    arr[0].toInt(),
                                    arr[1].toInt(),
                                    arr[2].toInt(),
                                    arr[3].toInt(),
                                };
                            }
                        }
                        return result;
                    }
                    case QMetaType::QRectF: {
                        QRectF result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            if (arr.size() == 4) {
                                result = {
                                    arr[0].toDouble(),
                                    arr[1].toDouble(),
                                    arr[2].toDouble(),
                                    arr[3].toDouble(),
                                };
                            }
                        }
                        return result;
                    }
                    case QMetaType::QSize: {
                        QSize result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            if (arr.size() == 2) {
                                result = {
                                    arr[0].toInt(),
                                    arr[1].toInt(),
                                };
                            }
                        }
                        return result;
                    }
                    case QMetaType::QSizeF: {
                        QSizeF result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            if (arr.size() == 2) {
                                result = {
                                    arr[0].toDouble(),
                                    arr[1].toDouble(),
                                };
                            }
                        }
                        return result;
                    }
                    case QMetaType::QPoint: {
                        QPoint result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            if (arr.size() == 2) {
                                result = {
                                    arr[0].toInt(),
                                    arr[1].toInt(),
                                };
                            }
                        }
                        return result;
                    }
                    case QMetaType::QPointF: {
                        QPointF result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            if (arr.size() == 2) {
                                result = {
                                    arr[0].toDouble(),
                                    arr[1].toDouble(),
                                };
                            }
                        }
                        return result;
                    }
                    case QMetaType::QLine: {
                        QLine result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            if (arr.size() == 4) {
                                result = {
                                    QPoint(arr[0].toInt(), arr[1].toInt()),
                                    QPoint(arr[2].toInt(), arr[3].toInt()),
                                };
                            }
                        }
                        return result;
                    }
                    case QMetaType::QLineF: {
                        QLineF result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            if (arr.size() == 4) {
                                result = {
                                    QPointF(arr[0].toDouble(), arr[1].toDouble()),
                                    QPointF(arr[2].toDouble(), arr[3].toDouble()),
                                };
                            }
                        }
                        return result;
                    }

                    // Variant container types
                    case QMetaType::QVariantPair: {
                        QVariantPair result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            if (arr.size() == 2) {
                                result = {
                                    jsonValueToVariant(arr[0]),
                                    jsonValueToVariant(arr[1]),
                                };
                            }
                        }
                        return QVariant::fromValue(result);
                    }
                    case QMetaType::QVariantList: {
                        QVariantList result;
                        if (value.isArray()) {
                            const auto &arr = value.toArray();
                            result.reserve(arr.size());
                            for (const auto &v : arr) {
                                result.append(jsonValueToVariant(v));
                            }
                        }
                        return result;
                    }
                    case QMetaType::QVariantMap: {
                        QVariantMap result;
                        if (value.isObject()) {
                            const auto &obj = value.toObject();
                            for (auto it = obj.begin(); it != obj.end(); ++it) {
                                result.insert(it.key(), jsonValueToVariant(it.value()));
                            }
                        }
                        return result;
                    }
                    case QMetaType::QVariantHash: {
                        QVariantHash result;
                        if (value.isObject()) {
                            const auto &obj = value.toObject();
                            for (auto it = obj.begin(); it != obj.end(); ++it) {
                                result.insert(it.key(), jsonValueToVariant(it.value()));
                            }
                        }
                        return result;
                    }

                    // Complex json types
                    case QMetaType::QJsonValue: {
                        return (QJsonValue) value;
                    }
                    case QMetaType::QJsonObject: {
                        return value.toObject();
                    }
                    case QMetaType::QJsonDocument: {
                        QJsonDocument doc;
                        if (value.isObject()) {
                            doc.setObject(value.toObject());
                        } else if (value.isArray()) {
                            doc.setArray(value.toArray());
                        }
                        return doc;
                    }

                    // Unknown type
                    case QMetaType::UnknownType: {
                        return QVariant();
                    }
                    default:
                        break;
                }
                return _QSettingsPrivate::stringToVariant(value.toString());
            }
            default:
                break;
        }
        return {};
    }

    QJsonValue variantToJsonValue(const QVariant &value) {
        switch (value.metaType().id()) {
            // Primitive types
            case QMetaType::Bool: {
            }
            case QMetaType::Int:
            case QMetaType::UInt:
            case QMetaType::Double:
            case QMetaType::Short:
            case QMetaType::UShort:
            case QMetaType::Float: {
                return QJsonValue::fromVariant(value);
            }

            case QMetaType::LongLong:
            case QMetaType::Long: {
                qlonglong num = value.toLongLong();
                if (num <= (2LL << 50) && num >= -(2LL << 50)) {
                    return QJsonValue(double(num));
                }
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::LongLong);
                obj.insert(kKeyValueData, value.toString());
                return obj;
            }

            case QMetaType::ULongLong:
            case QMetaType::ULong: {
                qulonglong num = value.toULongLong();
                if (num <= (2ULL << 50)) {
                    return QJsonValue(double(num));
                }
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::ULongLong);
                obj.insert(kKeyValueData, value.toString());
                return obj;
            }

            // Simple json types
            case QMetaType::QString: {
                return value.toString();
            }
            case QMetaType::QJsonArray: {
                return value.toJsonArray();
            }

            // String list
            case QMetaType::QStringList: {
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QStringList);
                obj.insert(kKeyValueData, QJsonArray::fromStringList(value.toStringList()));
                return obj;
            }

            // ByteArray
            case QMetaType::QByteArray: {
                const auto &a = value.toByteArray();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QByteArray);
                obj.insert(kKeyValueData, QLatin1StringView(a.constData(), a.size()));
                return obj;
            }

            // Simple structure types
            case QMetaType::QRect: {
                const auto &r = value.toRect();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QRect);
                obj.insert(kKeyValueData, QJsonArray{
                                              r.x(),
                                              r.y(),
                                              r.width(),
                                              r.height(),
                                          });
                return obj;
            }
            case QMetaType::QRectF: {
                const auto &r = value.toRectF();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QRectF);
                obj.insert(kKeyValueData, QJsonArray{
                                              r.x(),
                                              r.y(),
                                              r.width(),
                                              r.height(),
                                          });
                return obj;
            }
            case QMetaType::QSize: {
                const auto &s = value.toSize();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QSize);
                obj.insert(kKeyValueData, QJsonArray{
                                              s.width(),
                                              s.height(),
                                          });
                return obj;
            }
            case QMetaType::QSizeF: {
                const auto &s = value.toSizeF();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QSizeF);
                obj.insert(kKeyValueData, QJsonArray{
                                              s.width(),
                                              s.height(),
                                          });
                return obj;
            }
            case QMetaType::QPoint: {
                const auto &p = value.toPoint();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QPoint);
                obj.insert(kKeyValueData, QJsonArray{
                                              p.x(),
                                              p.y(),
                                          });
                return obj;
            }
            case QMetaType::QPointF: {
                const auto &p = value.toPointF();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QPointF);
                obj.insert(kKeyValueData, QJsonArray{
                                              p.x(),
                                              p.y(),
                                          });
                return obj;
            }
            case QMetaType::QLine: {
                const auto &l = value.toLine();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QLine);
                obj.insert(kKeyValueData, QJsonArray{
                                              l.x1(),
                                              l.y1(),
                                              l.x2(),
                                              l.y2(),
                                          });
                return obj;
            }
            case QMetaType::QLineF: {
                const auto &l = value.toLineF();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QLineF);
                obj.insert(kKeyValueData, QJsonArray{
                                              l.x1(),
                                              l.y1(),
                                              l.x2(),
                                              l.y2(),
                                          });
                return obj;
            }

            // Variant container types
            case QMetaType::QVariantPair: {
                const auto &pair = value.value<QVariantPair>();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QVariantPair);
                obj.insert(kKeyValueData, QJsonArray{
                                              variantToJsonValue(pair.first),
                                              variantToJsonValue(pair.second),
                                          });
                return obj;
            }
            case QMetaType::QVariantList: {
                const auto &list = value.toList();
                QJsonArray containerArr;
                for (const auto &v : list) {
                    containerArr.append(variantToJsonValue(v));
                }

                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QVariantList);
                obj.insert(kKeyValueData, containerArr);
                return obj;
            }
            case QMetaType::QVariantMap: {
                const auto &map = value.toMap();
                QJsonObject containerObj;
                for (auto it = map.begin(); it != map.end(); ++it) {
                    containerObj.insert(it.key(), variantToJsonValue(it.value()));
                }
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QVariantMap);
                obj.insert(kKeyValueData, containerObj);
                return obj;
            }
            case QMetaType::QVariantHash: {
                const auto &hash = value.toHash();
                QJsonObject containerObj;
                for (auto it = hash.begin(); it != hash.end(); ++it) {
                    containerObj.insert(it.key(), variantToJsonValue(it.value()));
                }
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QVariantHash);
                obj.insert(kKeyValueData, containerObj);
                return obj;
            }

            // Complex json types
            case QMetaType::QJsonValue: {
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QJsonValue);
                obj.insert(kKeyValueData, value.toJsonValue());
                return obj;
            }
            case QMetaType::QJsonObject: {
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QJsonObject);
                obj.insert(kKeyValueData, value.toJsonObject());
                return obj;
            }
            case QMetaType::QJsonDocument: {
                const auto &doc = value.toJsonDocument();
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::QJsonDocument);
                if (doc.isObject()) {
                    obj.insert(kKeyValueData, doc.object());
                } else if (doc.isArray()) {
                    obj.insert(kKeyValueData, doc.array());
                } else {
                    obj.insert(kKeyValueData, QJsonValue::Null);
                }
                return obj;
            }

            // Unknown type
            case QMetaType::UnknownType: {
                QJsonObject obj;
                obj.insert(kKeyValueType, QMetaType::UnknownType);
                obj.insert(kKeyValueData, QJsonValue::Null);
                return obj;
            }
            default:
                break;
        }

        QJsonObject obj;
        obj.insert(kKeyValueType, value.metaType().id());
        obj.insert(kKeyValueData, _QSettingsPrivate::variantToString(value));
        return obj;
    }

    using SettingsKeys = QVarLengthArray<QStringView, 10 * sizeof(QStringView)>;

    void splitSettingsKeys(SettingsKeys &result, const QStringView &s) {
        qsizetype start = 0;
        qsizetype end = s.indexOf(kSeparator);
        while (end >= 0) {
            result.append(s.mid(start, end - start));
            start = end + 1;
            end = s.indexOf(kSeparator, start);
        }
        result.append(s.mid(start));
    }

    class Writer {
    private:
        struct NodeRef {
            int index;
            bool isLeaf;

            NodeRef(int index = 0, bool isLeaf = false) : index(index), isLeaf(isLeaf) {
            }
        };

        struct LeafNode {
            QString key;
            QVariant value;

            LeafNode(QString key = {}, QVariant value = {})
                : key(std::move(key)), value(std::move(value)){};
        };

        using NodeRefList = QVarLengthArray<NodeRef, 10 * sizeof(NodeRef)>;

        struct BranchNode {
            QString key;
            NodeRefList refs;

            BranchNode(QString key = {}, NodeRefList refs = {})
                : key(std::move(key)), refs(std::move(refs)){};
        };

        // Emulated heap
        QVector<LeafNode> leafs;
        QVector<BranchNode> branches;

        // Root node index in the emulated heap, normally = 0
        int rootIndex;

        inline auto allocLeaf(QStringView key, const QVariant &value) {
            int index = int(leafs.size());
            leafs.emplace_back(key.toString(), value);
            return index;
        }

        inline auto allocBranch(QStringView key) {
            int index = int(branches.size());
            branches.emplace_back(key.toString(), NodeRefList());
            return index;
        }

        // NOT USED
        void construct(const QJsonObject &input, int branchIndex) {
            for (auto it = input.begin(); it != input.end(); ++it) {
                const auto &key = it.key();
                const auto &value = it.value();
                if (value.isObject() && value[kKeyValueType] == QJsonValue::Undefined) {
                    construct(value.toObject(), findOrCreateBranch(key, branchIndex));
                    continue;
                }

                insert(branchIndex, key, jsonValueToVariant(it.value()));
            }
        }

        void construct(const QVariantMap &input, int branchIndex) {
            for (auto it = input.begin(); it != input.end(); ++it) {
                const auto &mergedKeys = it.key();
                SettingsKeys keys;
                splitSettingsKeys(keys, mergedKeys);
                if (keys.isEmpty()) {
                    continue;
                }

                const auto &value = it.value();

                // Find the desired branch
                int nextBranchIndex = branchIndex;
                qsizetype i = 0;
                qsizetype end = keys.size() - 1;
                for (; i < end; ++i) {
                    nextBranchIndex = findOrCreateBranch(keys[i], nextBranchIndex);
                }
                insert(nextBranchIndex, keys.back(), value);
            }
        }

        // Find the deeper branch with the given key, create new branch if necessary
        // Returns the index of the found or created branch
        int findOrCreateBranch(QStringView key, int branchIndex) {
            bool keyExists = false;
            auto pos = indexOf(branches[branchIndex].refs, key, &keyExists);
            if (keyExists) {
                const NodeRef &ref = branches[branchIndex].refs[pos];
                if (ref.isLeaf) {
                    int orgLeafIndex = ref.index;
                    int nextBranchIndex = allocBranch(key);

                    // Insert original leaf to the new branch with the reserved key
                    leafs[orgLeafIndex].key = kKeyValue;
                    branches[nextBranchIndex].refs.append({orgLeafIndex, true});

                    // Replace leaf with a new branch
                    // NOTE: Don't use "ref" here because "allocBranch" may cause "branches" to
                    // reallocate its storage, which invalidates "ref"
                    branches[branchIndex].refs[pos] = {nextBranchIndex, false};
                    branchIndex = nextBranchIndex;
                } else {
                    branchIndex = ref.index;
                }
            } else {
                int nextBranchIndex = allocBranch(key);

                // Insert new branch to the parent branch
                branches[branchIndex].refs.insert(pos, {nextBranchIndex, false});
                branchIndex = nextBranchIndex;
            }
            return branchIndex;
        }

        // Insert leaf to the given branch
        void insert(int branchIndex, QStringView key, const QVariant &value) {
            auto &refs = branches[branchIndex].refs;
            bool keyExists = false;
            auto pos = indexOf(refs, key, &keyExists);
            if (keyExists) {
                NodeRef &ref = refs[pos];
                if (ref.isLeaf) {
                    // Replace leaf with a new leaf
                    leafs[ref.index].value = value;
                } else {
                    // Insert leaf to the branch with the reserved key
                    NodeRefList &targetBranchRefs = branches[ref.index].refs;
                    auto pos1 = indexOf(targetBranchRefs, kKeyValue, &keyExists);
                    targetBranchRefs.insert(pos1, {allocLeaf(kKeyValue, value), true});
                }
            } else {
                // Insert new leaf to the parent branch
                refs.insert(pos, {allocLeaf(key, value), true});
            }
        }

        // Find insert position
        qsizetype indexOf(const NodeRefList &refs, const QStringView &key, bool *keyExists) const {
            const auto begin = refs.begin();
            const auto end = refs.end();
            const auto it = std::lower_bound(
                refs.begin(), refs.end(), key, [&](const NodeRef &e, const QStringView &key) {
                    return (e.isLeaf ? leafs[e.index].key : branches[e.index].key) < key;
                });

            *keyExists =
                (it != end) && (it->isLeaf ? leafs[it->index].key : branches[it->index].key) == key;
            return it - begin;
        }

        QJsonObject toJsonObjectImpl(const NodeRefList &refs) const {
            QJsonObject obj;
            for (const auto &ref : refs) {
                if (ref.isLeaf) {
                    const auto &leaf = leafs[ref.index];
                    obj.insert(leaf.key, variantToJsonValue(leaf.value));
                } else {
                    const auto &branch = branches[ref.index];
                    obj.insert(branch.key, toJsonObjectImpl(branch.refs));
                }
            }
            return obj;
        }

        void toVariantMapImpl(QVariantMap &result, const NodeRefList &branch,
                              QStringList &keys) const {
            for (const auto &ref : branch) {
                if (ref.isLeaf) {
                    auto mergedKeys = keys.join(kSeparator);
                    const auto &leaf = leafs[ref.index];
                    if (leaf.key == kKeyValue) {
                        result[mergedKeys] = leaf.value;
                    } else {
                        if (!mergedKeys.isEmpty()) {
                            mergedKeys.append(kSeparator);
                        }
                        result[mergedKeys + leaf.key] = leaf.value;
                    }
                } else {
                    const auto &branch = branches[ref.index];
                    keys << branch.key;
                    toVariantMapImpl(result, branch.refs, keys);
                    keys.removeLast();
                }
            }
        }

    public:
        explicit Writer(const QJsonObject &input) {
            rootIndex = allocBranch({});
            construct(input, rootIndex);
        }

        explicit Writer(const QVariantMap &input) {
            rootIndex = allocBranch({});
            construct(input, rootIndex);
        }

        QVariantMap toVariantMap() const {
            QVariantMap result;
            QStringList keys;
            toVariantMapImpl(result, branches[rootIndex].refs, keys);
            return result;
        }

        QJsonObject toJsonObject() const {
            return toJsonObjectImpl(branches[rootIndex].refs);
        }
    };

    class Reader {
    private:
        void toVariantMapImpl(const QJsonObject &input, QStringList &names,
                              QVariantMap &result) const {
            for (auto it = input.begin(); it != input.end(); ++it) {
                const auto &key = it.key();
                const auto &value = it.value();
                if (key == kKeyValue) {
                    result.insert(names.join(kSeparator), jsonValueToVariant(value));
                    continue;
                }

                if (value.isObject() && value[kKeyValueType] == QJsonValue::Undefined) {
                    names.append(key);
                    toVariantMapImpl(value.toObject(), names, result);
                    names.removeLast();
                    continue;
                }

                names.append(key);
                result.insert(names.join(kSeparator), jsonValueToVariant(it.value()));
                names.removeLast();
            }
        }

    public:
        explicit Reader(const QJsonObject &input) : input(input) {
        }

        QVariantMap toVariantMap() const {
            QVariantMap result;
            QStringList names;
            toVariantMapImpl(input, names, result);
            return result;
        }

        const QJsonObject &input;
    };

}

// INTERFACES
QString QJsonSettings::reservedKey(ReservedKey key) {
    switch (key) {
        case ReservedKey::Value:
            return kKeyValue;
        case ValueType:
            return kKeyValueType;
        case ValueData:
            return kKeyValueData;
    };
    return {};
}

bool QJsonSettings::read(QIODevice &dev, QSettings::SettingsMap &settings) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(dev.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    settings = Reader(doc.object()).toVariantMap();
    return true;
}

bool QJsonSettings::write(QIODevice &dev, const QSettings::SettingsMap &settings) {
    QJsonDocument doc;
    doc.setObject(Writer(settings).toJsonObject());
    dev.write(doc.toJson());
    return true;
}