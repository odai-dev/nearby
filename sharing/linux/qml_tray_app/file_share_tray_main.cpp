#include <QAction>
#include <QAbstractButton>
#include <QAbstractSocket>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QPushButton>
#include <QQuickWindow>
#include <QSettings>
#include <QStringList>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QUrl>

#include <fcntl.h>
#include <memory>
#include <unistd.h>

#include "file_share_tray_controller.h"
#include "notification_manager.h"

namespace {

constexpr char kDefaultLogPath[] = "/tmp/nearby_qml_file_tray.log";
constexpr char kInstanceServerName[] = "nearby-qml-file-tray-app";

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

bool NotifyExistingInstance() {
  QLocalSocket socket;
  socket.connectToServer(QString::fromLatin1(kInstanceServerName),
                         QIODevice::WriteOnly);
  if (!socket.waitForConnected(250)) {
    return false;
  }

  socket.write("show");
  socket.flush();
  socket.waitForBytesWritten(250);
  socket.disconnectFromServer();
  return true;
}

std::unique_ptr<QLocalServer> CreateSingleInstanceServer() {
  auto server = std::make_unique<QLocalServer>();
  const QString server_name = QString::fromLatin1(kInstanceServerName);
  if (server->listen(server_name)) {
    return server;
  }

  if (server->serverError() == QAbstractSocket::AddressInUseError) {
    QLocalServer::removeServer(server_name);
    if (server->listen(server_name)) {
      return server;
    }
  }

  qWarning().noquote()
      << "Could not create single-instance server:" << server->errorString();
  return nullptr;
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
  if (NotifyExistingInstance()) {
    return 0;
  }

  std::unique_ptr<QLocalServer> single_instance_server =
      CreateSingleInstanceServer();
  const bool tray_available = QSystemTrayIcon::isSystemTrayAvailable();
  const bool start_hidden = ShouldStartHidden(app.arguments()) && tray_available;

  if (!tray_available) {
    qWarning() << "System tray is unavailable. Starting with the main window "
                  "visible so the app remains reachable.";
  }

  FileShareTrayController controller;

  QQmlApplicationEngine engine;
  QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                   [](const QList<QQmlError>& warnings) {
                     LogQmlWarnings(warnings);
                   });
  engine.rootContext()->setContextProperty("fileShareController", &controller);
  engine.rootContext()->setContextProperty("startHidden", start_hidden);
  engine.rootContext()->setContextProperty("trayAvailable", tray_available);
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

  const auto showAndActivateWindow = [window]() {
    window->show();
    window->raise();
    window->requestActivate();
  };

  if (single_instance_server != nullptr) {
    QObject::connect(single_instance_server.get(), &QLocalServer::newConnection,
                     window, [server = single_instance_server.get(),
                              showAndActivateWindow]() {
                       while (QLocalSocket* socket =
                                  server->nextPendingConnection()) {
                         socket->readAll();
                         socket->disconnectFromServer();
                         socket->deleteLater();
                       }
                       showAndActivateWindow();
                     });
  }

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
  hide_action->setEnabled(tray_available);
  tray_menu.addSeparator();
  QAction* quit_action = tray_menu.addAction(QStringLiteral("Quit"));

  QObject::connect(send_action, &QAction::triggered, window,
                   [&controller, showAndActivateWindow]() {
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
                     showAndActivateWindow();
                   });

  QObject::connect(receive_action, &QAction::triggered,
                   [&controller, showAndActivateWindow]() {
                     controller.switchToReceiveMode();
                     showAndActivateWindow();
                   });

  QObject::connect(show_action, &QAction::triggered, window,
                   showAndActivateWindow);

  QObject::connect(hide_action, &QAction::triggered, window, [window]() {
    window->hide();
  });

  QObject::connect(quit_action, &QAction::triggered, &app,
                   [&controller, &app]() {
                     controller.stop();
                     app.quit();
                   });

  QObject::connect(&tray, &QSystemTrayIcon::activated, window,
                   [window, showAndActivateWindow](
                       QSystemTrayIcon::ActivationReason reason) {
                     if (reason != QSystemTrayIcon::Trigger &&
                         reason != QSystemTrayIcon::DoubleClick) {
                       return;
                     }
                     if (window->isVisible()) {
                       window->hide();
                     } else {
                       showAndActivateWindow();
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
  QObject::connect(&controller,
                   &FileShareTrayController::requestReceiveFolderPicker,
                   [&controller](const QString& current_folder) {
                     const QString initial_folder =
                         current_folder.trimmed().isEmpty()
                             ? QDir::homePath()
                             : current_folder;
                     const QString folder = QFileDialog::getExistingDirectory(
                         nullptr, QStringLiteral("Select receive folder"),
                         initial_folder);
                     if (!folder.isEmpty()) {
                       controller.setReceiveFolder(folder);
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
  QObject::connect(&controller,
                   &FileShareTrayController::requestActionableTrayMessage,
                   &notification_manager,
                   &NotificationManager::ShowActionableNotification);
  QObject::connect(&notification_manager,
                   &NotificationManager::notificationActionRequested,
                   &controller,
                   &FileShareTrayController::handleNotificationAction);
  QObject::connect(&controller,
                   &FileShareTrayController::requestIncomingConfirmationPrompt,
                   window,
                   [&controller, window, &app, showAndActivateWindow](
                       const QString& title, const QString& body,
                       qlonglong share_target_id) {
                     if (window->isVisible()) {
                       showAndActivateWindow();
                       return;
                     }

                     auto* message_box = new QMessageBox(
                         QMessageBox::Question, title, body,
                         QMessageBox::NoButton);
                     message_box->setAttribute(Qt::WA_DeleteOnClose);
                     message_box->setTextFormat(Qt::PlainText);
                     message_box->setWindowFlag(Qt::WindowStaysOnTopHint);
                     if (!app.windowIcon().isNull()) {
                       message_box->setWindowIcon(app.windowIcon());
                     }

                     QAbstractButton* reject_button =
                         message_box->addButton(QStringLiteral("Reject"),
                                                QMessageBox::RejectRole);
                     QAbstractButton* accept_button =
                         message_box->addButton(QStringLiteral("Accept"),
                                                QMessageBox::AcceptRole);
                     QObject::connect(
                         message_box, &QMessageBox::buttonClicked,
                         message_box,
                         [&controller, message_box, accept_button,
                          reject_button, share_target_id](QAbstractButton* button) {
                           if (button == accept_button) {
                             controller.acceptTransfer(share_target_id);
                           } else if (button == reject_button) {
                             controller.rejectTransfer(share_target_id);
                           }
                           message_box->close();
                         });
                     message_box->show();
                     message_box->raise();
                     message_box->activateWindow();
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
  if (tray_available) {
    tray.show();
  } else {
    showAndActivateWindow();
  }

  controller.start();

  return app.exec();
}
