#include "file_share_tray_controller.h"

#include <memory>
#include <utility>
#include <vector>

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include "string_utils.h"
#include "status_mapper.h"
#include "qr_code_generator.h"

namespace {

constexpr char kAutostartFileName[] = "nearby-file-share.desktop";

class NearbySharingApiService final : public NearbySharingServiceInterface {
 public:
  explicit NearbySharingApiService(const QString& device_name)
      : api_(device_name.toStdString()) {}

  void SetListener(NearbySharingApi::Listener listener) override {
    api_.SetListener(std::move(listener));
  }

  void StartSendMode(
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.StartSendMode(std::move(callback));
  }

  void StopSendMode(
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.StopSendMode(std::move(callback));
  }

  void StartReceiveMode(
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.StartReceiveMode(std::move(callback));
  }

  void StopReceiveMode(
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.StopReceiveMode(std::move(callback));
  }

  void SendFiles(
      qlonglong share_target_id, const std::vector<std::string>& file_paths,
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.SendFiles(share_target_id, file_paths, std::move(callback));
  }

  void Accept(
      qlonglong share_target_id,
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.Accept(share_target_id, std::move(callback));
  }

  void Reject(
      qlonglong share_target_id,
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.Reject(share_target_id, std::move(callback));
  }

  void Cancel(
      qlonglong share_target_id,
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.Cancel(share_target_id, std::move(callback));
  }

  void Set5GhzHotspotEnabled(bool enabled) override {
    api_.Set5GhzHotspotEnabled(enabled);
  }

  void Shutdown(
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.Shutdown(std::move(callback));
  }

  std::string GetQrCodeUrl() const override { return api_.GetQrCodeUrl(); }

  NearbySharingApi::DiagnosticInfo GetDiagnostics() const override {
    return api_.GetDiagnostics();
  }

 private:
  NearbySharingApi api_;
};

std::unique_ptr<NearbySharingServiceInterface> CreateNearbySharingService(
    const QString& device_name) {
  return std::make_unique<NearbySharingApiService>(device_name);
}

QString AutostartDirectoryPath() {
  const QString config_path =
      QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  if (config_path.isEmpty()) {
    return QDir::home().filePath(QStringLiteral(".config/autostart"));
  }
  return QDir(config_path).filePath(QStringLiteral("autostart"));
}

QString AutostartFilePath() {
  return QDir(AutostartDirectoryPath())
      .filePath(QString::fromLatin1(kAutostartFileName));
}

QString EscapeDesktopExecArgument(const QString& argument) {
  QString escaped = argument;
  escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
  escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
  escaped.replace(QStringLiteral("`"), QStringLiteral("\\`"));
  escaped.replace(QStringLiteral("$"), QStringLiteral("\\$"));
  return QStringLiteral("\"%1\"").arg(escaped);
}

QString AutostartDesktopEntry() {
  const QString executable =
      QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
  return QStringLiteral(
             "[Desktop Entry]\n"
             "Type=Application\n"
             "Name=Nearby File Share\n"
             "Comment=Share files with nearby devices using Nearby Connections\n"
             "Exec=%1 --start-hidden\n"
             "Icon=nearby-file-share\n"
             "Categories=Utility;Network;FileTransfer;\n"
             "Keywords=share;file;nearby;transfer;\n"
             "StartupNotify=false\n"
             "Terminal=false\n"
             "X-GNOME-Autostart-enabled=true\n")
      .arg(EscapeDesktopExecArgument(executable));
}

}  // namespace

FileShareTrayController::FileShareTrayController(QObject* parent)
    : FileShareTrayController(CreateNearbySharingService, parent) {}

FileShareTrayController::FileShareTrayController(ServiceFactory service_factory,
                                                 QObject* parent)
    : QObject(parent), service_factory_(std::move(service_factory)) {
  const QString host = QSysInfo::machineHostName().trimmed();
  if (!host.isEmpty()) {
    state_.SetDeviceName(host);
  }

  loadSettings();
  initializeService();
}

FileShareTrayController::~FileShareTrayController() {
  if (service_) {
    ++operation_generation_;
    ++service_generation_;
    service_->SetListener({});
    service_->StopSendMode([](NearbySharingApi::StatusCode) {});
    service_->StopReceiveMode([](NearbySharingApi::StatusCode) {});
    service_->Shutdown([](NearbySharingApi::StatusCode) {});
  }
}

void FileShareTrayController::initializeService() {
  ++service_generation_;
  service_ = service_factory_(state_.deviceName());
  if (!service_) {
    refreshDiagnostics();
    setStatus(QStringLiteral("Sharing service is not available"));
    emit requestTrayMessage(QStringLiteral("Sharing unavailable"),
                            QStringLiteral("Could not create sharing service."));
    return;
  }
  service_->Set5GhzHotspotEnabled(state_.enable5GhzHotspot());
  refreshDiagnostics();
  state_.SetQrCodeData(QString::fromStdString(service_->GetQrCodeUrl()), {}, 0);
  updateQrCodeData();
  emit qrCodeUrlChanged();
  emit qrCodeChanged();
  attachServiceListeners();
  // service_ ->StartFastInitiationScanning([](auto a)
  // {
  //   std::cout << "Probably fine";
  // });
}

void FileShareTrayController::updateQrCodeData() {
  const auto qr_data = QrCodeGenerator::GenerateQrCode(state_.qrCodeUrl());
  state_.SetQrCodeData(state_.qrCodeUrl(), qr_data.rows, qr_data.size);
  emit qrCodeChanged();
}

bool FileShareTrayController::normalizeFileSelection(
    const QStringList& file_paths, QStringList* normalized_paths,
    QStringList* file_names, QString* error_message) const {
  normalized_paths->clear();
  file_names->clear();

  if (file_paths.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("No files were selected.");
    }
    return false;
  }

  for (const QString& file_path : file_paths) {
    const QString trimmed_path = file_path.trimmed();
    if (trimmed_path.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("One selected file path was empty.");
      }
      return false;
    }

    QFileInfo info(trimmed_path);
    if (!info.exists()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("%1 does not exist.")
                             .arg(QDir::toNativeSeparators(trimmed_path));
      }
      return false;
    }
    if (info.isDir()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("%1 is a folder. Folder sharing is not supported yet.")
                             .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
      }
      return false;
    }
    if (!info.isFile()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("%1 is not a regular file.")
                             .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
      }
      return false;
    }
    if (!info.isReadable()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("%1 is not readable.")
                             .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
      }
      return false;
    }
    if (info.size() <= 0) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("%1 is empty. Empty files cannot be sent.")
                             .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
      }
      return false;
    }

    normalized_paths->append(info.absoluteFilePath());
    file_names->append(info.fileName());
  }

  return !normalized_paths->isEmpty();
}

QStringList FileShareTrayController::localPathsFromUrlValues(
    const QVariantList& urls) const {
  QStringList paths;
  for (const QVariant& value : urls) {
    QUrl url = value.toUrl();
    if (!url.isValid() || url.isEmpty()) {
      url = QUrl(value.toString());
    }

    QString path;
    if (url.isLocalFile()) {
      path = url.toLocalFile();
    } else if (url.scheme().isEmpty()) {
      path = value.toString();
    }

    if (!path.isEmpty()) {
      paths.append(path);
    }
  }
  return paths;
}

std::vector<std::string> FileShareTrayController::pendingSendFilePathsForApi()
    const {
  std::vector<std::string> paths;
  paths.reserve(static_cast<size_t>(state_.pendingSendFileCount()));
  for (const QString& path : state_.pendingSendFilePaths()) {
    paths.push_back(path.toStdString());
  }
  return paths;
}

void FileShareTrayController::emitPendingSendStateChanged() {
  emit pendingSendFilePathChanged();
  emit pendingSendFileNameChanged();
  emit pendingSendFilesChanged();
}

void FileShareTrayController::clearPendingSendState() {
  state_.ClearPendingSendFile();
  emitPendingSendStateChanged();
}

QString FileShareTrayController::transferFileSummary(
    const NearbySharingApi::TransferUpdateInfo& update) const {
  if (update.total_attachments > 1) {
    return QStringLiteral("%1 files").arg(update.total_attachments);
  }

  QString file_name = StringUtils::FromStdString(update.first_file_name);
  if (file_name.isEmpty() && !update.is_incoming &&
      state_.pendingSendTargetId() == update.share_target_id &&
      !state_.pendingSendSummary().isEmpty()) {
    file_name = state_.pendingSendSummary();
  }

  return file_name.isEmpty() ? QStringLiteral("file") : file_name;
}

void FileShareTrayController::attachServiceListeners() {
  NearbySharingApi::Listener listener;
  const uint64_t service_generation = service_generation_;

  listener.target_discovered_cb = [this, service_generation](const NearbySharingApi::ShareTargetInfo& info) {
    const uint64_t generation = service_generation;
    QMetaObject::invokeMethod(this, [this, info, generation]() {
                                if (isCurrentService(generation)) {
                                  updateTargetFromInfo(info);
                                }
                              },
                              Qt::QueuedConnection);
  };

  listener.target_updated_cb = [this, service_generation](const NearbySharingApi::ShareTargetInfo& info) {
    const uint64_t generation = service_generation;
    QMetaObject::invokeMethod(this, [this, info, generation]() {
                                if (isCurrentService(generation)) {
                                  updateTargetFromInfo(info);
                                }
                              },
                              Qt::QueuedConnection);
  };

  listener.target_lost_cb = [this, service_generation](int64_t share_target_id) {
    const uint64_t generation = service_generation;
    QMetaObject::invokeMethod(
        this, [this, share_target_id, generation]() {
          if (!isCurrentService(generation)) {
            return;
          }
          state_.RemoveTarget(share_target_id);
          emit discoveredTargetsChanged();
        },
        Qt::QueuedConnection);
  };

  listener.transfer_update_cb = [this, service_generation](const NearbySharingApi::TransferUpdateInfo& update) {
    const uint64_t generation = service_generation;
    QMetaObject::invokeMethod(this, [this, update, generation]() {
                                if (isCurrentService(generation)) {
                                  handleTransferUpdate(update);
                                }
                              },
                              Qt::QueuedConnection);
  };

  service_->SetListener(std::move(listener));
}

void FileShareTrayController::updateTargetFromInfo(
    const NearbySharingApi::ShareTargetInfo& info) {
  const QString name = StringUtils::TrimmedOrFallback(
      StringUtils::FromStdString(info.device_name),
      QStringLiteral("Unknown device"));
  state_.AddOrUpdateTarget(info.id, name, info.is_incoming);
  emit discoveredTargetsChanged();
}

void FileShareTrayController::handleTransferUpdate(
    const NearbySharingApi::TransferUpdateInfo& update) {
  const QString target_name = StringUtils::TrimmedFromStdString(update.device_name);
  if (!target_name.isEmpty()) {
    state_.AddOrUpdateTarget(update.share_target_id, target_name, update.is_incoming);
  }

  const QString name = state_.GetTargetName(update.share_target_id);
  const QString status = StatusMapper::TransferStatusToString(update.status);
  const QString direction =
      update.is_incoming ? QStringLiteral("incoming") : QStringLiteral("outgoing");

  const QString file_name = transferFileSummary(update);

  state_.AddOrUpdateTransfer(update.share_target_id, name, status, update.progress,
                             update.transferred_bytes, update.total_bytes,
                             update.transfer_speed, StringUtils::FromStdString(update.connection_medium),
                             direction, file_name,
                             StringUtils::FromStdString(update.first_file_path),
                             update.total_attachments,
                             update.transferred_attachments);
  emit transfersChanged();

  setStatus(QStringLiteral("%1 (%2)").arg(status, name));

  // Auto-accept incoming transfers if enabled
  if (update.status == NearbySharingApi::TransferStatus::kAwaitingLocalConfirmation &&
      state_.autoAcceptIncoming()) {
    service_->Accept(update.share_target_id, [](NearbySharingApi::StatusCode) {});
  }

  // Handle final transfer status
  if (StatusMapper::IsFinalTransferStatus(update.status)) {
    handleTransferComplete(update);
  }
}

void FileShareTrayController::handleTransferComplete(
    const NearbySharingApi::TransferUpdateInfo& update) {
  const bool success = update.status == NearbySharingApi::TransferStatus::kComplete;
  const QString name = state_.GetTargetName(update.share_target_id);

  if (update.is_incoming) {
    handleIncomingTransferComplete(update, name, success);
  } else {
    handleOutgoingTransferComplete(update, name, success);
  }

  // Cleanup pending send state
  if (state_.pendingSendTargetId() == update.share_target_id) {
    clearPendingSendState();

    // Auto-switch to receive mode after successful send
    if (!update.is_incoming && success) {
      switchToReceiveMode();
    }
  }

  // Deferred target removal
  if (state_.IsPendingTargetRemoval(update.share_target_id)) {
    QTimer::singleShot(1400, this, [this, target_id = update.share_target_id]() {
      if (state_.IsPendingTargetRemoval(target_id)) {
        state_.RemoveTarget(target_id);
        emit discoveredTargetsChanged();
      }
    });
  }
}

void FileShareTrayController::handleIncomingTransferComplete(
    const NearbySharingApi::TransferUpdateInfo& update, const QString& name,
    bool success) {
  if (!success) {
    emit requestTrayMessage(
        QStringLiteral("Receive failed"),
        QStringLiteral("Transfer from %1 failed").arg(name));
    return;
  }

  const QString file_name = transferFileSummary(update);

  // Check for received URL
  for (const auto& text : update.text_attachments) {
    if (text.type == NearbySharingApi::TextAttachmentType::kUrl) {
      const QString link = StringUtils::TrimmedFromStdString(text.text_body);
      if (!link.isEmpty()) {
        emit requestCopyLinkTrayMessage(QStringLiteral("Link received"),
                                        QStringLiteral("%1 from %2").arg(link, name),
                                        link);
        return;
      }
    }
  }

  // Check for received text
  if (!update.text_attachments.empty()) {
    const QString text_summary = [&]() {
      for (const auto& text : update.text_attachments) {
        const QString title = StringUtils::TrimmedFromStdString(text.text_title);
        if (!title.isEmpty()) return title;
        const QString body = StringUtils::TrimmedFromStdString(text.text_body);
        if (!body.isEmpty()) return body;
      }
      return QStringLiteral("Text");
    }();
    emit requestTrayMessage(QStringLiteral("Text received"),
                            QStringLiteral("%1 from %2").arg(text_summary, name));
    return;
  }

  emit requestTrayMessage(update.total_attachments > 1
                              ? QStringLiteral("Files received")
                              : QStringLiteral("File received"),
                          QStringLiteral("%1 from %2").arg(file_name, name));
}

void FileShareTrayController::handleOutgoingTransferComplete(
    const NearbySharingApi::TransferUpdateInfo& update, const QString& name,
    bool success) {
  const QString file_name = transferFileSummary(update);

  if (success) {
    emit requestTrayMessage(QStringLiteral("Send complete"),
                            QStringLiteral("%1 sent to %2").arg(file_name, name));
  } else {
    emit requestTrayMessage(QStringLiteral("Send failed"),
                            QStringLiteral("%1 failed to send to %2").arg(file_name, name));
  }
}

void FileShareTrayController::loadSettings() {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));

  const QString stored_device_name =
      settings.value(QStringLiteral("deviceName"), state_.deviceName())
          .toString()
          .trimmed();
  if (!stored_device_name.isEmpty()) {
    state_.SetDeviceName(stored_device_name);
  }

  const bool stored_auto_accept =
      settings.value(QStringLiteral("autoAcceptIncoming"), false).toBool();
  state_.SetAutoAcceptIncoming(stored_auto_accept);

  const bool stored_enable_5ghz_hotspot =
      settings.value(QStringLiteral("enable5GhzHotspot"), true).toBool();
  state_.SetEnable5GhzHotspot(stored_enable_5ghz_hotspot);

  const bool stored_start_on_login =
      settings.value(QStringLiteral("startOnLogin"), false).toBool();
  state_.SetStartOnLogin(stored_start_on_login);

  const QString stored_log_path =
      settings.value(QStringLiteral("logPath"), QStringLiteral("/tmp/nearby_qml_file_tray.log"))
          .toString()
          .trimmed();
  if (!stored_log_path.isEmpty()) {
    state_.SetLogPath(stored_log_path);
  }
}

void FileShareTrayController::saveSettings() const {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  settings.setValue(QStringLiteral("deviceName"), state_.deviceName());
  settings.setValue(QStringLiteral("autoAcceptIncoming"), state_.autoAcceptIncoming());
  settings.setValue(QStringLiteral("enable5GhzHotspot"),
                    state_.enable5GhzHotspot());
  settings.setValue(QStringLiteral("startOnLogin"), state_.startOnLogin());
  settings.setValue(QStringLiteral("logPath"), state_.logPath());
}

void FileShareTrayController::setDeviceName(const QString& device_name) {
  const QString trimmed = device_name.trimmed();
  if (trimmed.isEmpty() || trimmed == state_.deviceName()) {
    return;
  }

  const bool should_restart = state_.running();
  state_.SetDeviceName(trimmed);
  saveSettings();
  emit deviceNameChanged();

  if (service_) {
    nextOperationGeneration();
    ++service_generation_;
    stopping_ = false;
    service_->SetListener({});
    service_->StopSendMode([](NearbySharingApi::StatusCode) {});
    service_->StopReceiveMode([](NearbySharingApi::StatusCode) {});
    service_->Shutdown([](NearbySharingApi::StatusCode) {});
    service_.reset();
  }

  if (should_restart) {
    state_.SetRunning(false);
    emit runningChanged();
  }

  state_.ClearAll();
  emit discoveredTargetsChanged();
  emit transfersChanged();

  initializeService();
  if (should_restart) {
    start();
  }
}

void FileShareTrayController::setAutoAcceptIncoming(bool enabled) {
  if (enabled == state_.autoAcceptIncoming()) {
    return;
  }
  state_.SetAutoAcceptIncoming(enabled);
  saveSettings();
  emit autoAcceptIncomingChanged();
}

void FileShareTrayController::setEnable5GhzHotspot(bool enabled) {
  if (enabled == state_.enable5GhzHotspot()) {
    return;
  }
  state_.SetEnable5GhzHotspot(enabled);
  if (service_) {
    service_->Set5GhzHotspotEnabled(enabled);
  }
  saveSettings();
  emit enable5GhzHotspotChanged();
}

void FileShareTrayController::setStartOnLogin(bool enabled) {
  if (enabled == state_.startOnLogin()) {
    return;
  }

  QString error_message;
  if (!applyAutostartSetting(enabled, &error_message)) {
    emit startOnLoginChanged();
    emit requestTrayMessage(
        QStringLiteral("Startup setting failed"),
        error_message.isEmpty()
            ? QStringLiteral("Could not update the login startup shortcut.")
            : error_message);
    return;
  }

  state_.SetStartOnLogin(enabled);
  saveSettings();
  emit startOnLoginChanged();

}

void FileShareTrayController::setLogPath(const QString& path) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty() || trimmed == state_.logPath()) {
    return;
  }
  state_.SetLogPath(trimmed);
  saveSettings();
  emit logPathChanged();
}

bool FileShareTrayController::applyAutostartSetting(
    bool enabled, QString* error_message) const {
  const QString autostart_file_path = AutostartFilePath();

  if (!enabled) {
    if (!QFile::exists(autostart_file_path)) {
      return true;
    }
    if (QFile::remove(autostart_file_path)) {
      return true;
    }
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not remove %1.")
                           .arg(autostart_file_path);
    }
    return false;
  }

  QDir autostart_dir(AutostartDirectoryPath());
  if (!autostart_dir.exists() &&
      !autostart_dir.mkpath(QStringLiteral("."))) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not create %1.")
                           .arg(autostart_dir.absolutePath());
    }
    return false;
  }

  QSaveFile file(autostart_file_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not write %1.")
                           .arg(autostart_file_path);
    }
    return false;
  }

  const QByteArray contents = AutostartDesktopEntry().toUtf8();
  if (file.write(contents) != contents.size()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not write %1.")
                           .arg(autostart_file_path);
    }
    return false;
  }

  if (!file.commit()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not save %1.")
                           .arg(autostart_file_path);
    }
    return false;
  }

  return true;
}

uint64_t FileShareTrayController::nextOperationGeneration() {
  return ++operation_generation_;
}

bool FileShareTrayController::isCurrentOperation(uint64_t generation) const {
  return generation == operation_generation_ && !stopping_;
}

bool FileShareTrayController::isCurrentService(uint64_t generation) const {
  return generation == service_generation_ && !stopping_;
}

QString FileShareTrayController::diagnosticWarningsToSummary(
    const NearbySharingApi::DiagnosticInfo& diagnostics) const {
  QStringList warnings;
  warnings.reserve(static_cast<int>(diagnostics.warnings.size()));
  for (const std::string& warning : diagnostics.warnings) {
    warnings.append(QString::fromStdString(warning));
  }
  return warnings.join(QStringLiteral(" "));
}

void FileShareTrayController::refreshDiagnostics() {
  if (!service_) {
    if (!diagnostics_summary_.isEmpty()) {
      diagnostics_summary_.clear();
      emit diagnosticsChanged();
    }
    return;
  }

  const QString summary = diagnosticWarningsToSummary(service_->GetDiagnostics());
  if (summary == diagnostics_summary_) {
    return;
  }
  diagnostics_summary_ = summary;
  emit diagnosticsChanged();
}

void FileShareTrayController::emitDiagnosticsWarningIfNeeded(
    const QString& operation_name) {
  refreshDiagnostics();
  if (diagnostics_summary_.isEmpty()) {
    return;
  }

  emit requestTrayMessage(
      QStringLiteral("Sharing may be limited"),
      QStringLiteral("%1 requested, but: %2").arg(operation_name, diagnostics_summary_));
}

void FileShareTrayController::start() {
  if (state_.running()) {
    return;
  }
  if (!service_) {
    initializeService();
  }

  stopping_ = false;
  state_.SetRunning(true);
  emit runningChanged();

  if (state_.pendingSendFileCount() > 0) {
    startSendMode();
    state_.SetMode(QStringLiteral("Send"));
  } else {
    startReceiveMode();
    state_.SetMode(QStringLiteral("Receive"));
  }
  emit modeChanged();
}

void FileShareTrayController::stop() {
  if (!state_.running() && !stopping_) {
    return;
  }

  const uint64_t generation = nextOperationGeneration();
  stopping_ = true;
  state_.SetRunning(false);
  emit runningChanged();

  if (!service_) {
    finishStopOperation(generation);
    return;
  }

  auto pending_callbacks = std::make_shared<int>(2);
  auto finished = std::make_shared<bool>(false);
  auto finish_once = [this, generation, pending_callbacks, finished]() {
    if (*finished) {
      return;
    }
    --(*pending_callbacks);
    if (*pending_callbacks > 0) {
      return;
    }
    *finished = true;
    finishStopOperation(generation);
  };

  service_->StopSendMode([this, generation, finish_once](NearbySharingApi::StatusCode status) {
    QMetaObject::invokeMethod(
        this,
        [this, generation, status, finish_once]() {
          if (generation == operation_generation_ &&
              status != NearbySharingApi::StatusCode::kOk &&
              status != NearbySharingApi::StatusCode::kStatusAlreadyStopped) {
            setStatus(QStringLiteral("StopSendMode failed: %1")
                          .arg(StatusMapper::ApiStatusToString(status)));
          }
          finish_once();
        },
        Qt::QueuedConnection);
  });
  service_->StopReceiveMode(
      [this, generation, finish_once](NearbySharingApi::StatusCode status) {
        QMetaObject::invokeMethod(
            this,
            [this, generation, status, finish_once]() {
              if (generation == operation_generation_ &&
                  status != NearbySharingApi::StatusCode::kOk &&
                  status != NearbySharingApi::StatusCode::kStatusAlreadyStopped) {
                setStatus(QStringLiteral("StopReceiveMode failed: %1")
                              .arg(StatusMapper::ApiStatusToString(status)));
              }
              finish_once();
            },
            Qt::QueuedConnection);
      });

  QTimer::singleShot(1500, this, [this, generation, finished]() {
    if (*finished || generation != operation_generation_) {
      return;
    }
    *finished = true;
    finishStopOperation(generation);
  });
}

void FileShareTrayController::finishStopOperation(uint64_t generation) {
  if (generation != operation_generation_) {
    return;
  }
  if (state_.pendingSendFileCount() > 0) {
    clearPendingSendState();
  }
  state_.ClearAll();
  emit discoveredTargetsChanged();
  emit transfersChanged();

  setStatus(QStringLiteral("Stopped"));
  stopping_ = false;
}

void FileShareTrayController::startSendMode() {
  if (!service_) {
    setStatus(QStringLiteral("Sharing service is not available"));
    emit requestTrayMessage(QStringLiteral("Send mode failed"),
                            QStringLiteral("Sharing service is not available."));
    return;
  }

  const uint64_t generation = nextOperationGeneration();
  stopping_ = false;
  emitDiagnosticsWarningIfNeeded(QStringLiteral("Send mode"));
  service_->StopReceiveMode([this, generation](NearbySharingApi::StatusCode status) {
    if (!isCurrentOperation(generation)) {
      return;
    }
    if (status == NearbySharingApi::StatusCode::kOk ||
        status == NearbySharingApi::StatusCode::kStatusAlreadyStopped) {
      service_->StartSendMode([this, generation](NearbySharingApi::StatusCode status) {
        QMetaObject::invokeMethod(
            this,
            [this, generation, status]() {
              if (!isCurrentOperation(generation)) {
                return;
              }
              setStatus(QStringLiteral("StartSendMode: %1")
                            .arg(StatusMapper::ApiStatusToString(status)));
              if (status != NearbySharingApi::StatusCode::kOk) {
                state_.SetRunning(false);
                emit runningChanged();
                if (state_.mode() != QStringLiteral("Receive")) {
                  state_.SetMode(QStringLiteral("Receive"));
                  emit modeChanged();
                }
                emit requestTrayMessage(
                    QStringLiteral("Send mode failed"),
                    QStringLiteral("Could not start send mode: %1")
                        .arg(StatusMapper::ApiStatusToString(status)));
              }
            },
            Qt::QueuedConnection);
      });
      return;
    }

    QMetaObject::invokeMethod(
        this,
        [this, generation, status]() {
          if (!isCurrentOperation(generation)) {
            return;
          }
          setStatus(QStringLiteral("StopReceiveMode failed: %1")
                        .arg(StatusMapper::ApiStatusToString(status)));
          emit requestTrayMessage(
              QStringLiteral("Send mode failed"),
              QStringLiteral("Could not stop receive mode: %1")
                  .arg(StatusMapper::ApiStatusToString(status)));
          state_.SetRunning(false);
          emit runningChanged();
          if (state_.mode() != QStringLiteral("Receive")) {
            state_.SetMode(QStringLiteral("Receive"));
            emit modeChanged();
          }
        },
        Qt::QueuedConnection);
  });
}

void FileShareTrayController::startReceiveMode() {
  if (!service_) {
    setStatus(QStringLiteral("Sharing service is not available"));
    emit requestTrayMessage(QStringLiteral("Receive mode failed"),
                            QStringLiteral("Sharing service is not available."));
    return;
  }

  const uint64_t generation = nextOperationGeneration();
  stopping_ = false;
  emitDiagnosticsWarningIfNeeded(QStringLiteral("Receive mode"));
  service_->StopSendMode([this, generation](NearbySharingApi::StatusCode status) {
    if (!isCurrentOperation(generation)) {
      return;
    }
    if (status == NearbySharingApi::StatusCode::kOk ||
        status == NearbySharingApi::StatusCode::kStatusAlreadyStopped) {
      service_->StartReceiveMode([this, generation](NearbySharingApi::StatusCode status) {
        QMetaObject::invokeMethod(
            this,
            [this, generation, status]() {
              if (!isCurrentOperation(generation)) {
                return;
              }
              setStatus(QStringLiteral("StartReceiveMode: %1")
                            .arg(StatusMapper::ApiStatusToString(status)));
              if (status != NearbySharingApi::StatusCode::kOk) {
                state_.SetRunning(false);
                emit runningChanged();
                emit requestTrayMessage(
                    QStringLiteral("Receive mode failed"),
                    QStringLiteral("Could not start receive mode: %1")
                        .arg(StatusMapper::ApiStatusToString(status)));
              }
            },
            Qt::QueuedConnection);
      });
      return;
    }

    QMetaObject::invokeMethod(
        this,
        [this, generation, status]() {
          if (!isCurrentOperation(generation)) {
            return;
          }
          setStatus(QStringLiteral("StopSendMode failed: %1")
                        .arg(StatusMapper::ApiStatusToString(status)));
          emit requestTrayMessage(
              QStringLiteral("Receive mode failed"),
              QStringLiteral("Could not stop send mode: %1")
                  .arg(StatusMapper::ApiStatusToString(status)));
          state_.SetRunning(false);
          emit runningChanged();
          if (state_.mode() != QStringLiteral("Send")) {
            state_.SetMode(QStringLiteral("Send"));
            emit modeChanged();
          }
        },
        Qt::QueuedConnection);
  });
}

void FileShareTrayController::switchToReceiveMode() {
  if (state_.running() && state_.HasActiveTransfers()) {
    setStatus(QStringLiteral("Cannot switch mode while transfer is active"));
    emit requestTrayMessage(
        QStringLiteral("Transfer in progress"),
        QStringLiteral("Wait for the current transfer to complete."));
    return;
  }

  if (state_.pendingSendFileCount() > 0) {
    clearPendingSendState();
  }

  if (state_.running()) {
    startReceiveMode();
    state_.SetMode(QStringLiteral("Receive"));
    emit modeChanged();
  }
}

void FileShareTrayController::switchToSendModeWithFile(const QString& file_path) {
  switchToSendModeWithFiles(QStringList{file_path});
}

void FileShareTrayController::switchToSendModeWithFiles(
    const QStringList& file_paths) {
  QStringList normalized_paths;
  QStringList file_names;
  QString error_message;
  if (!normalizeFileSelection(file_paths, &normalized_paths, &file_names,
                              &error_message)) {
    setStatus(QStringLiteral("Selected file is not valid"));
    emit requestTrayMessage(QStringLiteral("Send canceled"),
                            error_message.isEmpty()
                                ? QStringLiteral("Please choose valid files.")
                                : error_message);
    return;
  }

  if (state_.running() && state_.HasActiveTransfers()) {
    setStatus(QStringLiteral("Cannot switch mode while transfer is active"));
    emit requestTrayMessage(
        QStringLiteral("Transfer in progress"),
        QStringLiteral("Wait for the current transfer to complete."));
    return;
  }

  state_.SetPendingSendFiles(normalized_paths, file_names, 0);
  emitPendingSendStateChanged();

  if (state_.running()) {
    startSendMode();
    state_.SetMode(QStringLiteral("Send"));
    emit modeChanged();
  }

  setStatus(QStringLiteral("Discovery started. Choose a nearby device."));
  emit requestTrayMessage(
      QStringLiteral("Send mode"),
      QStringLiteral("Selected %1. Choose a nearby device to send.")
          .arg(state_.pendingSendSummary()));
}

void FileShareTrayController::switchToSendModeWithUrls(
    const QVariantList& urls) {
  switchToSendModeWithFiles(localPathsFromUrlValues(urls));
}

void FileShareTrayController::sendPendingFileToTarget(qlonglong share_target_id) {
  sendPendingFilesToTarget(share_target_id);
}

void FileShareTrayController::sendPendingFilesToTarget(
    qlonglong share_target_id) {
  if (share_target_id <= 0) {
    return;
  }
  if (!service_) {
    setStatus(QStringLiteral("Sharing service is not available"));
    emit requestTrayMessage(QStringLiteral("Send failed"),
                            QStringLiteral("Sharing service is not available."));
    return;
  }

  QStringList normalized_paths;
  QStringList file_names;
  QString error_message;
  if (!normalizeFileSelection(state_.pendingSendFilePaths(), &normalized_paths,
                              &file_names, &error_message)) {
    setStatus(QStringLiteral("Selected file is not available"));
    emit requestTrayMessage(QStringLiteral("Send failed"),
                            error_message.isEmpty()
                                ? QStringLiteral("Selected files are not available.")
                                : error_message);
    return;
  }

  const QString target_name = state_.GetTargetName(share_target_id);
  state_.SetPendingSendFiles(normalized_paths, file_names, share_target_id);
  emitPendingSendStateChanged();
  const QString summary = state_.pendingSendSummary();
  const QString first_path = state_.pendingSendFilePath();
  const int file_count = state_.pendingSendFileCount();

  state_.AddOrUpdateTransfer(share_target_id, target_name, QStringLiteral("Queued"),
                             0.0, 0, 0, 0, QStringLiteral("Unknown"),
                             QStringLiteral("outgoing"), summary, first_path,
                             file_count, 0);
  emit transfersChanged();

  const uint64_t generation = operation_generation_;
  service_->SendFiles(
      share_target_id, pendingSendFilePathsForApi(),
      [this, share_target_id, generation](NearbySharingApi::StatusCode status) {
        QMetaObject::invokeMethod(
            this,
            [this, share_target_id, generation, status]() {
              if (!isCurrentOperation(generation)) {
                return;
              }
              const QString target_name = state_.GetTargetName(share_target_id);
              if (status == NearbySharingApi::StatusCode::kOk) {
                setStatus(QStringLiteral("Sending %1 to %2")
                              .arg(state_.pendingSendSummary(), target_name));
                return;
              }

              emit requestTrayMessage(
                  QStringLiteral("Send failed"),
                  QStringLiteral("Could not send to %1").arg(target_name));

              state_.AddOrUpdateTransfer(share_target_id, target_name,
                                         QStringLiteral("Failed"), 0.0, 0, 0, 0,
                                         QStringLiteral("Unknown"),
                                         QStringLiteral("outgoing"),
                                         state_.pendingSendSummary(),
                                         state_.pendingSendFilePath(),
                                         state_.pendingSendFileCount(), 0);
              emit transfersChanged();
              clearPendingSendState();
            },
            Qt::QueuedConnection);
      });
}

void FileShareTrayController::copyTextToClipboard(const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }

  QClipboard* clipboard = QGuiApplication::clipboard();
  if (clipboard == nullptr) {
    emit requestTrayMessage(QStringLiteral("Copy failed"),
                            QStringLiteral("Clipboard is not available."));
    return;
  }

  clipboard->setText(trimmed, QClipboard::Clipboard);
  setStatus(QStringLiteral("Connection URL copied to clipboard"));
  emit requestTrayMessage(QStringLiteral("URL copied"),
                          QStringLiteral("Link copied to clipboard."));
}

void FileShareTrayController::openFileLocation(const QString& file_path) {
  const QString trimmed = file_path.trimmed();
  if (trimmed.isEmpty()) {
    emit requestTrayMessage(QStringLiteral("Open location failed"),
                            QStringLiteral("No received file location is available."));
    return;
  }

  QFileInfo info(trimmed);
  QString target_path;
  if (info.exists() && info.isFile()) {
    // Open the containing folder so the file is visible in the user's file
    // manager regardless of the desktop environment.
    target_path = info.absolutePath();
  } else if (info.exists() && info.isDir()) {
    target_path = info.absoluteFilePath();
  } else {
    // Some transfer updates can outlive the exact file entry we saw earlier;
    // fall back to the parent directory when it still exists.
    const QFileInfo parent_info(info.absolutePath());
    if (parent_info.exists() && parent_info.isDir()) {
      target_path = parent_info.absoluteFilePath();
    }
  }

  if (target_path.isEmpty()) {
    emit requestTrayMessage(QStringLiteral("Open location failed"),
                            QStringLiteral("The file location is no longer available."));
    return;
  }

  const bool opened =
      QDesktopServices::openUrl(QUrl::fromLocalFile(target_path));
  if (!opened) {
    emit requestTrayMessage(QStringLiteral("Open location failed"),
                            QStringLiteral("Could not open the file location."));
  }
}

void FileShareTrayController::clearTransfers() {
  state_.ClearAll();
  emit discoveredTargetsChanged();
  emit transfersChanged();
}

void FileShareTrayController::hideToTray() {
  // This is handled by the main window, but can be extended here if needed
}

void FileShareTrayController::setStatus(const QString& status) {
  if (status == state_.statusMessage()) {
    return;
  }
  state_.SetStatusMessage(status);
  emit statusMessageChanged();
}

void FileShareTrayController::notifyStateChange(const QString& property) {
  if (property == QStringLiteral("mode")) {
    emit modeChanged();
  } else if (property == QStringLiteral("deviceName")) {
    emit deviceNameChanged();
  } else if (property == QStringLiteral("statusMessage")) {
    emit statusMessageChanged();
  } else if (property == QStringLiteral("running")) {
    emit runningChanged();
  } else if (property == QStringLiteral("autoAcceptIncoming")) {
    emit autoAcceptIncomingChanged();
  } else if (property == QStringLiteral("enable5GhzHotspot")) {
    emit enable5GhzHotspotChanged();
  } else if (property == QStringLiteral("startOnLogin")) {
    emit startOnLoginChanged();
  } else if (property == QStringLiteral("diagnostics")) {
    emit diagnosticsChanged();
  } else if (property == QStringLiteral("discoveredTargets")) {
    emit discoveredTargetsChanged();
  } else if (property == QStringLiteral("transfers")) {
    emit transfersChanged();
  } else if (property == QStringLiteral("pendingSendFiles")) {
    emitPendingSendStateChanged();
  }
}
void FileShareTrayController::acceptTransfer(qlonglong share_target_id) {
  if (service_) {
    const uint64_t generation = operation_generation_;
    service_->Accept(share_target_id, [this, generation](NearbySharingApi::StatusCode status) {
      QMetaObject::invokeMethod(
          this,
          [this, generation, status]() {
            if (!isCurrentOperation(generation) ||
                status == NearbySharingApi::StatusCode::kOk) {
              return;
            }
            setStatus(QStringLiteral("Accept failed: %1")
                          .arg(StatusMapper::ApiStatusToString(status)));
            emit requestTrayMessage(
                QStringLiteral("Accept failed"),
                QStringLiteral("Could not accept transfer: %1")
                    .arg(StatusMapper::ApiStatusToString(status)));
          },
          Qt::QueuedConnection);
    });
  }
}

void FileShareTrayController::rejectTransfer(qlonglong share_target_id) {
  if (service_) {
    const uint64_t generation = operation_generation_;
    service_->Reject(share_target_id, [this, generation](NearbySharingApi::StatusCode status) {
      QMetaObject::invokeMethod(
          this,
          [this, generation, status]() {
            if (!isCurrentOperation(generation) ||
                status == NearbySharingApi::StatusCode::kOk) {
              return;
            }
            setStatus(QStringLiteral("Reject failed: %1")
                          .arg(StatusMapper::ApiStatusToString(status)));
            emit requestTrayMessage(
                QStringLiteral("Reject failed"),
                QStringLiteral("Could not reject transfer: %1")
                    .arg(StatusMapper::ApiStatusToString(status)));
          },
          Qt::QueuedConnection);
    });
  }
}

void FileShareTrayController::cancelTransfer(qlonglong share_target_id) {
  if (!service_ || share_target_id <= 0) {
    return;
  }

  const QString target_name = state_.GetTargetName(share_target_id);
  state_.AddOrUpdateTransfer(share_target_id, target_name,
                             QStringLiteral("Cancelling"), 0.0, 0, 0, 0,
                             QStringLiteral("Unknown"),
                             QStringLiteral("outgoing"),
                             state_.pendingSendSummary(),
                             state_.pendingSendFilePath(),
                             state_.pendingSendFileCount(), 0);
  emit transfersChanged();

  const uint64_t generation = operation_generation_;
  service_->Cancel(share_target_id, [this, share_target_id, generation](
                                        NearbySharingApi::StatusCode status) {
    QMetaObject::invokeMethod(
        this,
        [this, share_target_id, generation, status]() {
          if (!isCurrentOperation(generation)) {
            return;
          }
          const QString target_name = state_.GetTargetName(share_target_id);
          if (status == NearbySharingApi::StatusCode::kOk) {
            state_.AddOrUpdateTransfer(
                share_target_id, target_name, QStringLiteral("Cancelled"), 1.0,
                0, 0, 0, QStringLiteral("Unknown"), QStringLiteral("outgoing"),
                state_.pendingSendSummary(), state_.pendingSendFilePath(),
                state_.pendingSendFileCount(), 0);
            emit transfersChanged();
            if (state_.pendingSendTargetId() == share_target_id) {
              clearPendingSendState();
            }
            setStatus(QStringLiteral("Transfer cancelled"));
            return;
          }

          setStatus(QStringLiteral("Cancel failed: %1")
                        .arg(StatusMapper::ApiStatusToString(status)));
          emit requestTrayMessage(
              QStringLiteral("Cancel failed"),
              QStringLiteral("Could not cancel transfer: %1")
                  .arg(StatusMapper::ApiStatusToString(status)));
        },
        Qt::QueuedConnection);
  });
}

void FileShareTrayController::openFilePicker() {
  emit requestFilePicker();
}
