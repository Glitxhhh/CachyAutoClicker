#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QVersionNumber>

#include "portal_shortcut.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#ifndef CAC_VERSION
#define CAC_VERSION "0.0.0"   // CMake sets this (the release workflow passes the release version)
#endif

// ---------------------------------------------------------------------------------------------------------
// Finds the latest GitHub release and installs it over the running binary.
//
// The binary is replaced by renaming the new file over the old one, which is fine while the old one is running
// (it keeps its inode). The new copy is then started with "--wait-for <pid>" so it waits for this one to exit.
// Releases aren't code-signed, so the trust is "whatever Glitxhhh/CachyAutoClicker publishes on GitHub over
// HTTPS"; when GitHub reports a SHA-256 digest for the asset it's checked as well.
// ---------------------------------------------------------------------------------------------------------

struct UpdateInfo {
    QString tag;
    QVersionNumber version;
    QString release_url;
    QString download_url;
    qint64 size = 0;
    QString sha256;   // empty when GitHub doesn't report one
};

class AppUpdater : public QObject {
    Q_OBJECT
public:
    explicit AppUpdater(QObject *parent = nullptr) : QObject(parent) {}

    static QString currentVersionText() { return QStringLiteral(CAC_VERSION); }
    static QVersionNumber currentVersion() { return QVersionNumber::fromString(currentVersionText()); }
    static QString releasesPage() { return QStringLiteral("https://github.com/Glitxhhh/CachyAutoClicker/releases"); }

    void check(bool manual) {
        QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/Glitxhhh/CachyAutoClicker/releases/latest")));
        request.setRawHeader("Accept", "application/vnd.github+json");
        decorate(request);
        QNetworkReply *reply = net.get(request);

        connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                emit checkFinished(manual, false, UpdateInfo(), QString(), reply->errorString());
                return;
            }

            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            QString tag = root.value("tag_name").toString();
            const QVersionNumber latest = QVersionNumber::fromString(tag.startsWith('v') || tag.startsWith('V') ? tag.mid(1) : tag);
            if (latest.isNull()) {
                emit checkFinished(manual, false, UpdateInfo(), tag, QString("Couldn't read the release version '%1'.").arg(tag));
                return;
            }
            if (latest <= currentVersion()) {
                emit checkFinished(manual, false, UpdateInfo(), tag, QString());
                return;
            }

            // The release carries the plain "CachyAutoClicker" binary, which is what the README tells people to download.
            const QJsonArray assets = root.value("assets").toArray();
            for (const QJsonValue &value : assets) {
                const QJsonObject asset = value.toObject();
                if (asset.value("name").toString() != QLatin1String("CachyAutoClicker")) continue;

                UpdateInfo info;
                info.tag = tag;
                info.version = latest;
                info.release_url = root.value("html_url").toString(releasesPage());
                info.download_url = asset.value("browser_download_url").toString();
                info.size = static_cast<qint64>(asset.value("size").toDouble());
                const QString digest = asset.value("digest").toString();
                if (digest.startsWith("sha256:", Qt::CaseInsensitive)) info.sha256 = digest.mid(7);
                emit checkFinished(manual, true, info, tag, QString());
                return;
            }
            emit checkFinished(manual, false, UpdateInfo(), tag, QString());   // newer release, but no binary on it
        });
    }

    void install(const UpdateInfo &info) {
        const QUrl url(info.download_url);
        if (url.scheme() != QLatin1String("https")) {
            emit installFinished(false, "The release has no HTTPS download.");
            return;
        }

        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        decorate(request);
        QNetworkReply *reply = net.get(request);

        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            if (total > 0) emit progress(static_cast<int>(received * 100 / total));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, info]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                emit installFinished(false, QString("Download failed: %1").arg(reply->errorString()));
                return;
            }

            // Redirects are followed automatically; only accept the file if it ended up on GitHub's own hosts.
            const QString host = reply->url().host();
            if (host != QLatin1String("github.com") && !host.endsWith(".github.com") && !host.endsWith(".githubusercontent.com")) {
                emit installFinished(false, QString("The download was redirected to an unexpected host (%1).").arg(host));
                return;
            }

            const QByteArray data = reply->readAll();
            if (info.size > 0 && data.size() != info.size) {
                emit installFinished(false, QString("The download was cut short (%1 of %2 bytes).").arg(data.size()).arg(info.size));
                return;
            }
            if (!info.sha256.isEmpty()
                && QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().compare(info.sha256.toLatin1(), Qt::CaseInsensitive) != 0) {
                emit installFinished(false, "The download doesn't match the checksum GitHub reports for it.");
                return;
            }
            // Refuse anything that isn't a plausible executable.
            if (data.size() < 100000 || !data.startsWith(QByteArray("\x7f" "ELF", 4))) {
                emit installFinished(false, "The downloaded file isn't a valid program for this system.");
                return;
            }

            const QString error = writeOverRunningBinary(data);
            emit installFinished(error.isEmpty(), error);
        });
    }

    // Replaces the binary on disk; returns an error text, or an empty string on success.
    QString writeOverRunningBinary(const QByteArray &data) {
        const QString exe = currentExePath();
        if (exe.isEmpty()) return "Can't work out where the app is installed.";

        const QString staged = exe + ".new";
        QFile file(staged);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QString("Can't write to %1 (%2). Download the release yourself instead.").arg(QFileInfo(exe).absolutePath(), file.errorString());
        file.write(data);
        file.close();
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                            QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);

        // rename(2) replaces the file atomically, even while the old one is running.
        if (std::rename(staged.toLocal8Bit().constData(), exe.toLocal8Bit().constData()) != 0) {
            const QString why = QString::fromLocal8Bit(std::strerror(errno));
            QFile::remove(staged);
            return QString("Couldn't replace the app (%1).").arg(why);
        }
        return QString();
    }

    // For the "--wait-for <pid>" a freshly installed copy is started with.
    static void waitForExit(long pid) {
        for (int waited = 0; waited < 15000 && ::kill(static_cast<pid_t>(pid), 0) == 0; waited += 100)
            ::usleep(100000);
    }

    // Deletes a stray "*.new" from an update that didn't finish.
    static void cleanupAfterUpdate() {
        if (!currentExePath().isEmpty()) QFile::remove(currentExePath() + ".new");
    }

signals:
    // update_available false with an empty error means "you're on the latest"
    void checkFinished(bool manual, bool update_available, const UpdateInfo &info, const QString &latest_tag, const QString &error);
    void progress(int percent);
    // ok: the new binary is in place (message empty); otherwise message says what went wrong
    void installFinished(bool ok, const QString &message);

private:
    QNetworkAccessManager net;

    void decorate(QNetworkRequest &request) const {
        // A plain client name: no contact details in the User-Agent.
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CachyAutoClicker/") + currentVersionText());
        request.setTransferTimeout(30000);
    }
};
