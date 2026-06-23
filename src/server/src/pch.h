#pragma once

#include <QGuiApplication>
#include <QObject>
#include <QString>
#include <QDebug>
#include <QTimer>
#include <QVariant>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonDocument>
#include <QUrl>
#include <QColor>
#include <QtConcurrent/QtConcurrent>
#ifndef Q_OS_WIN
#include <QDBusConnection>
#include <QDBusInterface>
#endif
#include <QEvent>
#include <QSqlQuery>
#include <QFutureWatcher>

#include <memory>
#include <vector>
#include <optional>
#include <variant>
#include <unordered_map>
#include <filesystem>
#include <ranges>
#include <string>
