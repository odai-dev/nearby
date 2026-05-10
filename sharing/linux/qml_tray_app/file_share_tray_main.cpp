#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QList>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QSettings>
#include <QStringList>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QUrl>

#include <fcntl.h>
#include <unistd.h>

#include "file_share_tray_controller.h"
#include "notification_manager.h"

namespace {

constexpr char kDefaultLogPath[] = "/tmp/nearby_qml_file_tray.log";

bool EnsureLogDirectory(const QString& file_path) {
  const QFileInfo file_info(file_path);
  QDir directory = file_info.absoluteDir();
  if (directory.exists()) {
    return true;
  }
  return directory.mkpath(QStringLiteral("."));
}

bool RedirectStdStreamsToFile(const QString& file_path) {
  const QByteArray encoded_path = QFile::encodeName(file_path);
  const int fd = ::open(encoded_path.constData(),
                        O_CREAT | O_APPEND | O_WRONLY,
                        0644);
  if (fd < 0) {
    return false;
  }

  const bool redirected_stdout = ::dup2(fd, STDOUT_FILENO) >= 0;
  const bool redirected_stderr = ::dup2(fd, STDERR_FILENO) >= 0;
  ::close(fd);
  return redirected_stdout && redirected_stderr;
}

QString ResolveConfiguredLogPath() {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  const QString configured_path =
      settings.value(QStringLiteral("logPath"),
                     QString::fromLatin1(kDefaultLogPath))
          .toString()
          .trimmed();
  if (configured_path.isEmpty()) {
    return QString::fromLatin1(kDefaultLogPath);
  }
  return configured_path;
}

void RedirectProcessLogsToConfiguredPath() {
  QString log_path = ResolveConfiguredLogPath();
  if (EnsureLogDirectory(log_path) && RedirectStdStreamsToFile(log_path)) {
    return;
  }

  const QString fallback_path = QString::fromLatin1(kDefaultLogPath);
  if (log_path == fallback_path) {
    return;
  }
  if (!EnsureLogDirectory(fallback_path)) {
    return;
  }
  RedirectStdStreamsToFile(fallback_path);
}

QIcon BuildTintedSymbolicIcon(const QString& source, const QColor& color) {
  QIcon source_icon(source);
  if (source_icon.isNull()) {
    return QIcon();
  }

  QIcon tinted_icon;
  for (int size : {16, 18, 20, 22, 24, 32}) {
    QPixmap pixmap = source_icon.pixmap(size, size);
    if (pixmap.isNull()) {
      continue;
    }

    QPixmap tinted(pixmap.size());
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();

    tinted_icon.addPixmap(tinted);
  }

  return tinted_icon;
}

void LogQmlWarnings(const QList<QQmlError>& warnings) {
  for (const auto& warning : warnings) {
    qWarning().noquote() << warning.toString();
  }
}

QStringList LocalFilesFromUrls(const QList<QUrl>& urls) {
  QStringList files;
  for (const QUrl& url : urls) {
    const QString file = url.toLocalFile();
    if (!file.isEmpty()) {
      files.append(file);
    }
  }
  return files;
}

bool ShouldStartHidden(const QStringList& arguments) {
  return arguments.contains(QStringLiteral("--start-hidden"));
}

}  // namespace

int main(int argc, char* argv[]) {
  // Use portal for file picker but don't force the whole platform theme
  // to avoid breaking the system dark mode detection.
  qputenv("QT_USE_PORTAL", "1");

  RedirectProcessLogsToConfiguredPath();

  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  QGuiApplication::setDesktopFileName(QStringLiteral("nearby-file-share"));
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/nearby-linux-desktop.png")));
  const bool start_hidden = ShouldStartHidden(app.arguments());

  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    qWarning() << "System tray is unavailable. The app will keep running, "
                  "but tray interactions may not work in this session.";
  }

  FileShareTrayController controller;

  QQmlApplicationEngine engine;
  QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                   [](const QList<QQmlError>& warnings) {
                     LogQmlWarnings(warnings);
                   });
  engine.rootContext()->setContextProperty("fileShareController", &controller);
  engine.rootContext()->setContextProperty("startHidden", start_hidden);
  engine.load(QUrl(QStringLiteral("qrc:/qml/FileShareTray.qml")));
  if (engine.rootObjects().isEmpty()) {
    qCritical() << "Failed to load FileShareTray.qml. Check the log for missing "
                   "Qt runtime components or QML import errors.";
    return 1;
  }


  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (window == nullptr) {
    qCritical() << "QML loaded, but the root object is not a QQuickWindow.";
    return 1;
  }
  window->setIcon(app.windowIcon());

  const auto resolve_tray_icon = [&app]() {
    QIcon tray_icon(QStringLiteral(":/icons/nearby-linux-desktop.png"));
    if (tray_icon.isNull()) {
      tray_icon = app.windowIcon();
    }
    if (tray_icon.isNull()) {
      const QColor symbolic_color = app.palette().color(QPalette::WindowText);
      tray_icon = BuildTintedSymbolicIcon(
          QStringLiteral(":/icons/tray_icon-symbolic.svg"), symbolic_color);
    }
    if (tray_icon.isNull()) {
      tray_icon = QIcon(QStringLiteral(":/icons/tray_icon.png"));
    }
    if (tray_icon.isNull()) {
      tray_icon = QIcon::fromTheme(QStringLiteral("network-wireless-symbolic"));
    }
    return tray_icon;
  };

  QSystemTrayIcon tray(resolve_tray_icon());
  tray.setToolTip(QStringLiteral("Nearby File Tray"));
  NotificationManager notification_manager(&tray, &app);

  QMenu tray_menu;
  QAction* send_action = tray_menu.addAction(QStringLiteral("Send"));
  QAction* receive_action = tray_menu.addAction(QStringLiteral("Receive"));
  tray_menu.addSeparator();
  QAction* show_action = tray_menu.addAction(QStringLiteral("Show"));
  QAction* hide_action = tray_menu.addAction(QStringLiteral("Hide"));
  tray_menu.addSeparator();
  QAction* quit_action = tray_menu.addAction(QStringLiteral("Quit"));

  QObject::connect(send_action, &QAction::triggered, window,
                   [&controller, window]() {
                     const QList<QUrl> urls = QFileDialog::getOpenFileUrls(
                         nullptr, QStringLiteral("Select files to send"),
                         QUrl::fromLocalFile(QDir::homePath()), QStringLiteral("All Files (*)"));
                     if (urls.isEmpty()) {
                       return;
                     }
                     const QStringList files = LocalFilesFromUrls(urls);
                     if (files.isEmpty()) {
                       return;
                     }
                     controller.switchToSendModeWithFiles(files);
                     window->show();
                     window->raise();
                     window->requestActivate();
                   });

  QObject::connect(receive_action, &QAction::triggered,
                   [&controller, window]() {
                     controller.switchToReceiveMode();
                     window->show();
                     window->raise();
                     window->requestActivate();
                   });

  QObject::connect(show_action, &QAction::triggered, window, [window]() {
    window->show();
    window->raise();
    window->requestActivate();
  });

  QObject::connect(hide_action, &QAction::triggered, window, [window]() {
    window->hide();
  });

  QObject::connect(quit_action, &QAction::triggered, &app,
                   [&controller, &app]() {
                     controller.stop();
                     app.quit();
                   });

  QObject::connect(&tray, &QSystemTrayIcon::activated, window,
                   [window](QSystemTrayIcon::ActivationReason reason) {
                     if (reason != QSystemTrayIcon::Trigger &&
                         reason != QSystemTrayIcon::DoubleClick) {
                       return;
                     }
                     if (window->isVisible()) {
                       window->hide();
                     } else {
                       window->show();
                       window->raise();
                       window->requestActivate();
                     }
                   });

  QObject::connect(&controller, &FileShareTrayController::requestFilePicker,
                   [&controller, window]() {
                     // Using URLs can sometimes trigger the portal more reliably in Qt 6
                     const QList<QUrl> urls = QFileDialog::getOpenFileUrls(
                         nullptr, QStringLiteral("Select files to send"),
                         QUrl::fromLocalFile(QDir::homePath()), QStringLiteral("All Files (*)"));
                     const QStringList files = LocalFilesFromUrls(urls);
                     if (!files.isEmpty()) {
                       controller.switchToSendModeWithFiles(files);
                     }
                   });

  QObject::connect(&controller, &FileShareTrayController::requestTrayMessage,
                   &notification_manager, &NotificationManager::ShowNotification);
  QObject::connect(&controller,
                   &FileShareTrayController::requestCopyLinkTrayMessage,
                   &notification_manager,
                   [&notification_manager](const QString& title,
                                           const QString& body,
                                           const QString& link) {
                     notification_manager.ShowCopyableNotification(
                         title, body, link, QStringLiteral("Copy link"));
                   });

  QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
                   [&controller]() { controller.stop(); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &tray,
                   [&tray, &resolve_tray_icon](Qt::ColorScheme) {
                     tray.setIcon(resolve_tray_icon());
                   });
#endif

  tray.setContextMenu(&tray_menu);
  tray.show();

  controller.start();
  //controller.
  controller.switchToReceiveMode();

  return app.exec();
}
