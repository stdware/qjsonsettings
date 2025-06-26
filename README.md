# QJsonSettings

Qt QSettings in JSON format.

## Quick Start

Source code:

```cpp
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
```

Output settings file:

```json
{
    "a": 1,
    "b": {
        "c": true
    },
    "d": {
        "e": {
            "f": "Hello World!"
        }
    },
    "foo": {
        "$value": "foo",
        "bar": "foo/bar"
    }
}
```

### Reserved Keys

- `$value`: If the current key has subkeys, its value is stored in the `$type` property
- `$type`/`$data`: If the value is of a complex type, it is stored using a json object in the following format:
    ```json
    {
        "$type": "<qt metatype id>",
        "$data": "<serialized data>"
    }
    ```