#include "native_file_dialog.h"

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

NativeFileDialog::NativeFileDialog(QObject* parent) : QObject(parent) {}

NativeFileDialog::~NativeFileDialog() {
  if (!request_path_.isEmpty()) {
    QDBusConnection::sessionBus().disconnect(
        QStringLiteral("org.freedesktop.portal.Desktop"), request_path_,
        QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"), this,
        SLOT(onPortalResponse(uint, QVariantMap)));
  }
}

void NativeFileDialog::pickFiles(
    QWindow* parent_window,
    const QString& title,
    const QString& initial_folder,
    bool multiple,
    std::function<void(const QStringList&)> on_selected) {
  auto* dialog = new NativeFileDialog();
  dialog->startOpenFilePortal(parent_window, title, initial_folder,
                              /*directory=*/false, multiple, std::move(on_selected));
}

void NativeFileDialog::pickFolder(
    QWindow* parent_window,
    const QString& title,
    const QString& initial_folder,
    std::function<void(const QString&)> on_selected) {
  auto* dialog = new NativeFileDialog();
  dialog->startOpenFilePortal(
      parent_window, title, initial_folder, /*directory=*/true,
      /*multiple=*/false,
      [on_selected = std::move(on_selected)](const QStringList& files) {
        if (!files.isEmpty()) {
          on_selected(files.first());
        }
      });
}

void NativeFileDialog::startOpenFilePortal(
    QWindow* parent_window,
    const QString& title,
    const QString& initial_folder,
    bool directory,
    bool multiple,
    std::function<void(const QStringList&)> on_selected) {
  callback_ = std::move(on_selected);

  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    fallbackToProcessOrQt(parent_window, title, initial_folder, directory,
                          multiple, callback_);
    deleteLater();
    return;
  }

  const QString token =
      QStringLiteral("qs_%1").arg(QUuid::createUuid().toString(QUuid::Id128));

  QVariantMap options;
  options[QStringLiteral("handle_token")] = token;
  options[QStringLiteral("multiple")] = multiple;
  options[QStringLiteral("directory")] = directory;
  options[QStringLiteral("modal")] = true;

  if (!initial_folder.trimmed().isEmpty()) {
    QByteArray folder_bytes = initial_folder.toUtf8();
    folder_bytes.append('\0');
    options[QStringLiteral("current_folder")] = folder_bytes;
  }

  QString parent_handle;
  if (parent_window != nullptr && parent_window->winId() != 0) {
    parent_handle = QStringLiteral("x11:%1").arg(parent_window->winId(), 0, 16);
  }

  QDBusMessage msg = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.portal.Desktop"),
      QStringLiteral("/org/freedesktop/portal/desktop"),
      QStringLiteral("org.freedesktop.portal.FileChooser"),
      QStringLiteral("OpenFile"));
  msg << parent_handle << title << options;

  QDBusPendingCall async_call = bus.asyncCall(msg);
  auto* watcher = new QDBusPendingCallWatcher(async_call, this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, parent_window, title, initial_folder, directory, multiple,
           token](QDBusPendingCallWatcher* self) {
            self->deleteLater();
            QDBusPendingReply<QDBusObjectPath> reply = *self;
            if (reply.isError()) {
              fallbackToProcessOrQt(parent_window, title, initial_folder,
                                    directory, multiple, callback_);
              deleteLater();
              return;
            }

            request_path_ = reply.value().path();
            if (request_path_.isEmpty()) {
              fallbackToProcessOrQt(parent_window, title, initial_folder,
                                    directory, multiple, callback_);
              deleteLater();
              return;
            }

            bool connected = QDBusConnection::sessionBus().connect(
                QStringLiteral("org.freedesktop.portal.Desktop"), request_path_,
                QStringLiteral("org.freedesktop.portal.Request"),
                QStringLiteral("Response"), this,
                SLOT(onPortalResponse(uint, QVariantMap)));

            if (!connected) {
              fallbackToProcessOrQt(parent_window, title, initial_folder,
                                    directory, multiple, callback_);
              deleteLater();
            }
          });
}

void NativeFileDialog::onPortalResponse(uint response,
                                        const QVariantMap& results) {
  QStringList selected_files;

  if (response == 0) {
    QStringList uris;
    const QVariant uris_val = results.value(QStringLiteral("uris"));

    if (uris_val.canConvert<QStringList>()) {
      uris = uris_val.toStringList();
    } else if (uris_val.canConvert<QVariantList>()) {
      const auto list = uris_val.toList();
      for (const auto& item : list) {
        uris.append(item.toString());
      }
    } else if (uris_val.userType() == qMetaTypeId<QDBusArgument>()) {
      const QDBusArgument arg = uris_val.value<QDBusArgument>();
      arg >> uris;
    }

    for (const QString& uri : uris) {
      if (uri.trimmed().isEmpty()) continue;
      QString local_path = QUrl(uri).toLocalFile();
      if (local_path.isEmpty()) {
        if (uri.startsWith(QStringLiteral("file://"))) {
          local_path = QUrl::fromEncoded(uri.toUtf8()).toLocalFile();
        } else {
          local_path = uri;
        }
      }
      if (!local_path.isEmpty() && QFileInfo::exists(local_path)) {
        selected_files.append(local_path);
      }
    }
  }

  if (callback_ && !selected_files.isEmpty()) {
    callback_(selected_files);
  }

  deleteLater();
}

void NativeFileDialog::fallbackToProcessOrQt(
    QWindow* parent_window,
    const QString& title,
    const QString& initial_folder,
    bool directory,
    bool multiple,
    std::function<void(const QStringList&)> on_selected) {
  const QString zenity = QStandardPaths::findExecutable(QStringLiteral("zenity"));
  if (!zenity.isEmpty()) {
    auto* process = new QProcess(this);
    QStringList args;
    args << QStringLiteral("--file-selection");
    args << QStringLiteral("--title=%1").arg(title);
    if (directory) {
      args << QStringLiteral("--directory");
    }
    if (multiple) {
      args << QStringLiteral("--multiple") << QStringLiteral("--separator=\n");
    }
    if (!initial_folder.trimmed().isEmpty()) {
      args << QStringLiteral("--filename=%1/").arg(initial_folder);
    }

    connect(
        process,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [process, on_selected = std::move(on_selected)](
            int exit_code, QProcess::ExitStatus status) {
          QStringList results;
          if (status == QProcess::NormalExit && exit_code == 0) {
            const QString output =
                QString::fromUtf8(process->readAllStandardOutput()).trimmed();
            if (!output.isEmpty()) {
              const QStringList lines =
                  output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
              for (const QString& line : lines) {
                const QString trimmed = line.trimmed();
                if (!trimmed.isEmpty() && QFileInfo::exists(trimmed)) {
                  results.append(trimmed);
                }
              }
            }
          }
          process->deleteLater();
          if (on_selected && !results.isEmpty()) {
            on_selected(results);
          }
        });

    process->start(zenity, args);
    return;
  }

  // Fallback to QFileDialog
  if (directory) {
    const QString folder = QFileDialog::getExistingDirectory(
        nullptr, title,
        initial_folder.isEmpty() ? QDir::homePath() : initial_folder,
        QFileDialog::ShowDirsOnly | QFileDialog::ReadOnly);
    if (!folder.isEmpty() && on_selected) {
      on_selected(QStringList{folder});
    }
  } else {
    const QList<QUrl> urls = QFileDialog::getOpenFileUrls(
        nullptr, title,
        QUrl::fromLocalFile(
            initial_folder.isEmpty() ? QDir::homePath() : initial_folder),
        QStringLiteral("All Files (*)"), nullptr, QFileDialog::ReadOnly);
    QStringList files;
    for (const QUrl& url : urls) {
      const QString path = url.isLocalFile() ? url.toLocalFile() : url.path();
      if (!path.isEmpty() && QFileInfo::exists(path)) {
        files.append(path);
      }
    }
    if (!files.isEmpty() && on_selected) {
      on_selected(files);
    }
  }
}
