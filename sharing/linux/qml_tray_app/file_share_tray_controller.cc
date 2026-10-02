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
#include <QMimeData>
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

constexpr char kAutostartFileName[] = "quick-share.desktop";

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

  void SendText(
      qlonglong share_target_id, const std::string& text,
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.SendText(share_target_id, text, std::move(callback));
  }

  void SendUrl(
      qlonglong share_target_id, const std::string& url,
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.SendUrl(share_target_id, url, std::move(callback));
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

  std::string GetReceiveFolder() const override {
    return api_.GetReceiveFolder();
  }

  void SetReceiveFolder(
      const std::string& folder_path,
      std::function<void(NearbySharingApi::StatusCode)> callback) override {
    api_.SetReceiveFolder(folder_path, std::move(callback));
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
             "Name=Quick Share\n"
             "GenericName=Quick Share\n"
             "Comment=Share files with nearby devices using Quick Share\n"
             "Exec=%1 --start-hidden\n"
             "Icon=quick-share\n"
             "Categories=Utility;Network;FileTransfer;\n"
             "Keywords=quick;quick share;share;file;nearby;nearby share;transfer;\n"
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
  if (!receive_folder_.isEmpty()) {
    service_->SetReceiveFolder(receive_folder_.toStdString(),
                               [](NearbySharingApi::StatusCode) {});
  }
  refreshDiagnostics();
  refreshReceiveFolder();
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
      path = url.toLocalFile().trimmed();
    } else if (url.scheme().isEmpty()) {
      path = value.toString().trimmed();
    }

    if (!path.isEmpty()) {
      paths.append(path);
    }
  }
  paths.removeDuplicates();
  return paths;
}

bool FileShareTrayController::isHttpOrHttpsUrl(const QString& text) const {
  const QUrl url(text.trimmed());
  return url.isValid() && !url.host().isEmpty() &&
         (url.scheme() == QStringLiteral("http") ||
          url.scheme() == QStringLiteral("https"));
}

bool FileShareTrayController::prepareSendText(const QString& text,
                                              const QString& kind,
                                              QString* error_message) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("No text was selected.");
    }
    return false;
  }

  const bool is_link = kind == QStringLiteral("link");
  const NearbySharingApi::StatusCode validation_status =
      is_link ? NearbySharingApi::ValidateSendUrl(trimmed.toStdString())
              : NearbySharingApi::ValidateSendText(trimmed.toStdString());
  if (validation_status != NearbySharingApi::StatusCode::kOk) {
    if (error_message != nullptr) {
      *error_message =
          is_link ? QStringLiteral("Links must start with http:// or https://.")
                  : QStringLiteral("Text cannot be empty.");
    }
    return false;
  }

  const QString summary = is_link ? trimmed : QStringLiteral("Text");
  state_.SetPendingSendText(kind, trimmed, summary, 0);
  emitPendingSendStateChanged();
  return true;
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
  if (!update.text_attachments.empty()) {
    for (const auto& text : update.text_attachments) {
      const QString body = StringUtils::TrimmedFromStdString(text.text_body);
      if (text.type == NearbySharingApi::TextAttachmentType::kUrl &&
          !body.isEmpty()) {
        return body;
      }
      const QString title = StringUtils::TrimmedFromStdString(text.text_title);
      if (!title.isEmpty()) {
        return title;
      }
      if (!body.isEmpty()) {
        return QStringLiteral("Text");
      }
    }
  }

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

  if (shouldHideDiscoveredTarget(info, name)) {
    qInfo() << "self target filtered" << name << info.id;
    state_.RemoveTarget(info.id);
    emit discoveredTargetsChanged();
    return;
  }

  const QString status_reason =
      StringUtils::TrimmedFromStdString(info.status_reason);
  const bool is_actionable =
      info.is_actionable && !info.receive_disabled && status_reason.isEmpty();
  if (!is_actionable) {
    qInfo() << "unavailable target shown" << name << info.id << status_reason;
    if (!status_reason.isEmpty()) {
      setStatus(QStringLiteral("Phone detected, but not visible to everyone"));
    }
  }

  state_.AddOrUpdateTarget(info.id, name, info.is_incoming, is_actionable,
                           status_reason);
  emit discoveredTargetsChanged();
}

bool FileShareTrayController::shouldHideDiscoveredTarget(
    const NearbySharingApi::ShareTargetInfo& info,
    const QString& target_name) const {
  if (info.for_self_share) {
    return true;
  }
  if (info.is_incoming || info.receive_disabled ||
      state_.HasActiveTransferForTarget(info.id)) {
    return false;
  }

  const QString local_name = state_.deviceName().trimmed();
  const QString host_name = QSysInfo::machineHostName().trimmed();
  return (!local_name.isEmpty() && target_name == local_name) ||
         (!host_name.isEmpty() && target_name == host_name);
}

void FileShareTrayController::handleTransferUpdate(
    const NearbySharingApi::TransferUpdateInfo& update) {
  const QVariantMap previous_transfer =
      state_.TransferForTarget(update.share_target_id);
  const QString previous_status =
      previous_transfer.value(QStringLiteral("status")).toString();
  const QString target_name = StringUtils::TrimmedFromStdString(update.device_name);
  if (!target_name.isEmpty()) {
    state_.AddOrUpdateTarget(update.share_target_id, target_name, update.is_incoming);
  }

  const QString name = state_.GetTargetName(update.share_target_id);
  const QString status = StatusMapper::TransferStatusToString(update.status);
  const QString direction =
      update.is_incoming ? QStringLiteral("incoming") : QStringLiteral("outgoing");

  const QString file_name = transferFileSummary(update);
  const QString content_kind =
      !update.is_incoming && state_.pendingSendTargetId() == update.share_target_id
          ? state_.pendingSendKind()
          : QString();
  const QString content_text =
      !update.is_incoming && state_.pendingSendTargetId() == update.share_target_id
          ? state_.pendingSendText()
          : QString();
  const QStringList file_paths =
      !update.is_incoming && state_.pendingSendTargetId() == update.share_target_id
          ? state_.pendingSendFilePaths()
          : QStringList();

  state_.AddOrUpdateTransfer(update.share_target_id, name, status, update.progress,
                             update.transferred_bytes, update.total_bytes,
                             update.transfer_speed, StringUtils::FromStdString(update.connection_medium),
                             direction, file_name,
                             StringUtils::FromStdString(update.first_file_path),
                             update.total_attachments,
                             update.transferred_attachments, content_kind,
                             content_text, file_paths);
  emit transfersChanged();

  setStatus(QStringLiteral("%1 (%2)").arg(status, name));

  if (previous_status != status &&
      update.status ==
          NearbySharingApi::TransferStatus::kAwaitingLocalConfirmation &&
      update.is_incoming && !state_.autoAcceptIncoming()) {
    const QString title = QStringLiteral("Incoming transfer");
    const QString body =
        QStringLiteral("%1 wants to share %2").arg(name, file_name);
    QVariantList actions;
    actions.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("accept")},
                               {QStringLiteral("label"), QStringLiteral("Accept")}});
    actions.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("reject")},
                               {QStringLiteral("label"), QStringLiteral("Reject")}});
    emit requestActionableTrayMessage(title, body, update.share_target_id,
                                      QString(), actions);
    emit requestIncomingConfirmationPrompt(title, body, update.share_target_id);
  } else if (previous_status != status && !update.is_incoming &&
             !StatusMapper::IsFinalTransferStatus(update.status) &&
             status != QStringLiteral("Unknown")) {
    QVariantList actions;
    actions.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("cancel")},
                               {QStringLiteral("label"), QStringLiteral("Cancel")}});
    emit requestActionableTrayMessage(
        QStringLiteral("Sending"),
        QStringLiteral("%1 to %2").arg(file_name, name),
        update.share_target_id, QString(), actions);
  }

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
  const QString file_path = StringUtils::FromStdString(update.first_file_path);
  if (!file_path.isEmpty()) {
    QVariantList actions;
    actions.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("open")},
                               {QStringLiteral("label"), QStringLiteral("Open folder")}});
    emit requestActionableTrayMessage(
        update.total_attachments > 1 ? QStringLiteral("Files received")
                                     : QStringLiteral("File received"),
        QStringLiteral("%1 from %2").arg(file_name, name),
        update.share_target_id, file_path, actions);
  }
}

void FileShareTrayController::handleOutgoingTransferComplete(
    const NearbySharingApi::TransferUpdateInfo& update, const QString& name,
    bool success) {
  const QString file_name = transferFileSummary(update);

  if (success) {
    emit requestTrayMessage(QStringLiteral("Send complete"),
                            QStringLiteral("%1 sent to %2").arg(file_name, name));
  } else {
    const QString body =
        QStringLiteral("%1 failed to send to %2").arg(file_name, name);
    emit requestTrayMessage(QStringLiteral("Send failed"), body);
    QVariantList actions;
    actions.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("retry")},
                               {QStringLiteral("label"), QStringLiteral("Retry")}});
    emit requestActionableTrayMessage(QStringLiteral("Send failed"), body,
                                      update.share_target_id, QString(),
                                      actions);
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

  receive_folder_ =
      settings.value(QStringLiteral("receiveFolder")).toString().trimmed();
}

void FileShareTrayController::saveSettings() const {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  settings.setValue(QStringLiteral("deviceName"), state_.deviceName());
  settings.setValue(QStringLiteral("autoAcceptIncoming"), state_.autoAcceptIncoming());
  settings.setValue(QStringLiteral("enable5GhzHotspot"),
                    state_.enable5GhzHotspot());
  settings.setValue(QStringLiteral("startOnLogin"), state_.startOnLogin());
  settings.setValue(QStringLiteral("logPath"), state_.logPath());
  settings.setValue(QStringLiteral("receiveFolder"), receive_folder_);
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

void FileShareTrayController::setReceiveFolder(const QString& path) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty()) {
    resetReceiveFolder();
    return;
  }
  if (trimmed == receive_folder_) {
    return;
  }

  const NearbySharingApi::StatusCode validation_status =
      NearbySharingApi::ValidateReceiveFolder(trimmed.toStdString());
  if (validation_status != NearbySharingApi::StatusCode::kOk) {
    setStatus(QStringLiteral("Receive folder is not valid"));
    emit requestTrayMessage(
        QStringLiteral("Receive folder failed"),
        QStringLiteral("%1 must be an existing writable folder.")
            .arg(QDir::toNativeSeparators(trimmed)));
    emit receiveFolderChanged();
    return;
  }

  if (!service_) {
    receive_folder_ = trimmed;
    saveSettings();
    emit receiveFolderChanged();
    return;
  }

  service_->SetReceiveFolder(
      trimmed.toStdString(),
      [this, trimmed](NearbySharingApi::StatusCode status) {
        QMetaObject::invokeMethod(
            this,
            [this, trimmed, status]() {
              if (status != NearbySharingApi::StatusCode::kOk) {
                setStatus(QStringLiteral("Receive folder failed: %1")
                              .arg(StatusMapper::ApiStatusToString(status)));
                emit requestTrayMessage(
                    QStringLiteral("Receive folder failed"),
                    QStringLiteral("Could not set receive folder: %1")
                        .arg(StatusMapper::ApiStatusToString(status)));
                emit receiveFolderChanged();
                return;
              }
              receive_folder_ = trimmed;
              saveSettings();
              emit receiveFolderChanged();
              setStatus(QStringLiteral("Receive folder updated"));
            },
            Qt::QueuedConnection);
      });
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

void FileShareTrayController::refreshReceiveFolder() {
  if (!service_) {
    return;
  }
  const QString folder =
      QString::fromStdString(service_->GetReceiveFolder()).trimmed();
  if (folder.isEmpty() || folder == receive_folder_) {
    return;
  }
  receive_folder_ = folder;
  emit receiveFolderChanged();
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

void FileShareTrayController::switchToSendModeWithText(const QString& text) {
  if (state_.running() && state_.HasActiveTransfers()) {
    setStatus(QStringLiteral("Cannot switch mode while transfer is active"));
    emit requestTrayMessage(
        QStringLiteral("Transfer in progress"),
        QStringLiteral("Wait for the current transfer to complete."));
    return;
  }

  QString error_message;
  if (!prepareSendText(text, QStringLiteral("text"), &error_message)) {
    setStatus(QStringLiteral("Selected text is not valid"));
    emit requestTrayMessage(QStringLiteral("Send canceled"),
                            error_message.isEmpty()
                                ? QStringLiteral("Please choose text to send.")
                                : error_message);
    return;
  }

  if (state_.running()) {
    startSendMode();
    state_.SetMode(QStringLiteral("Send"));
    emit modeChanged();
  }

  setStatus(QStringLiteral("Discovery started. Choose a nearby device."));
  emit requestTrayMessage(
      QStringLiteral("Send mode"),
      QStringLiteral("Text ready. Choose a nearby device to send."));
}

void FileShareTrayController::switchToSendModeWithLink(const QString& url) {
  if (state_.running() && state_.HasActiveTransfers()) {
    setStatus(QStringLiteral("Cannot switch mode while transfer is active"));
    emit requestTrayMessage(
        QStringLiteral("Transfer in progress"),
        QStringLiteral("Wait for the current transfer to complete."));
    return;
  }

  QString error_message;
  if (!prepareSendText(url, QStringLiteral("link"), &error_message)) {
    setStatus(QStringLiteral("Selected link is not valid"));
    emit requestTrayMessage(QStringLiteral("Send canceled"),
                            error_message.isEmpty()
                                ? QStringLiteral("Please choose a valid link.")
                                : error_message);
    return;
  }

  if (state_.running()) {
    startSendMode();
    state_.SetMode(QStringLiteral("Send"));
    emit modeChanged();
  }

  setStatus(QStringLiteral("Discovery started. Choose a nearby device."));
  emit requestTrayMessage(
      QStringLiteral("Send mode"),
      QStringLiteral("Link ready. Choose a nearby device to send."));
}

void FileShareTrayController::switchToSendModeWithUrls(
    const QVariantList& urls) {
  const QStringList files = localPathsFromUrlValues(urls);
  if (!files.isEmpty()) {
    switchToSendModeWithFiles(files);
    return;
  }
  for (const QVariant& value : urls) {
    QString text;
    if (value.canConvert<QUrl>()) {
      const QUrl url = value.toUrl();
      if (url.isValid() && (url.scheme() == QStringLiteral("http") ||
                            url.scheme() == QStringLiteral("https"))) {
        text = url.toString().trimmed();
      }
    }
    if (text.isEmpty()) {
      text = value.toString().trimmed();
    }
    if (isHttpOrHttpsUrl(text)) {
      switchToSendModeWithLink(text);
      return;
    }
  }
  setStatus(QStringLiteral("Drop did not include shareable content"));
  emit requestTrayMessage(QStringLiteral("Send canceled"),
                          QStringLiteral("Drop files, text, or an http/https link."));
}

void FileShareTrayController::prepareSendFromClipboard() {
  const QClipboard* clipboard = QGuiApplication::clipboard();
  if (clipboard == nullptr || clipboard->mimeData() == nullptr) {
    emit requestTrayMessage(QStringLiteral("Paste failed"),
                            QStringLiteral("Clipboard is not available."));
    return;
  }
  const QMimeData* mime_data = clipboard->mimeData();
  if (mime_data->hasUrls()) {
    QVariantList url_values;
    for (const QUrl& url : mime_data->urls()) {
      url_values.append(url);
    }
    switchToSendModeWithUrls(url_values);
    return;
  }
  const QString text = mime_data->text().trimmed();
  if (isHttpOrHttpsUrl(text)) {
    switchToSendModeWithLink(text);
  } else {
    switchToSendModeWithText(text);
  }
}

void FileShareTrayController::prepareDroppedText(const QString& text) {
  const QString trimmed = text.trimmed();
  if (isHttpOrHttpsUrl(trimmed)) {
    switchToSendModeWithLink(trimmed);
  } else {
    switchToSendModeWithText(trimmed);
  }
}

void FileShareTrayController::sendPendingFileToTarget(qlonglong share_target_id) {
  sendPendingContentToTarget(share_target_id);
}

void FileShareTrayController::sendPendingFilesToTarget(
    qlonglong share_target_id) {
  sendPendingContentToTarget(share_target_id);
}

void FileShareTrayController::sendPendingContentToTarget(
    qlonglong share_target_id) {
  if (share_target_id <= 0) {
    return;
  }
  if (state_.HasTarget(share_target_id) &&
      !state_.IsTargetActionable(share_target_id)) {
    const QVariantMap target = [&]() {
      for (const QVariant& value : state_.discoveredTargets()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toLongLong() == share_target_id) {
          return row;
        }
      }
      return QVariantMap{};
    }();
    const QString reason =
        target.value(QStringLiteral("statusReason")).toString().trimmed();
    setStatus(reason.isEmpty() ? QStringLiteral("Receiver is not available")
                               : reason);
    emit requestTrayMessage(
        QStringLiteral("Receiver unavailable"),
        reason.isEmpty()
            ? QStringLiteral("Choose another nearby device.")
            : reason);
    return;
  }
  if (!service_) {
    setStatus(QStringLiteral("Sharing service is not available"));
    emit requestTrayMessage(QStringLiteral("Send failed"),
                            QStringLiteral("Sharing service is not available."));
    return;
  }

  QStringList normalized_paths = state_.pendingSendFilePaths();
  QStringList file_names = state_.pendingSendFileNames();
  QString content_text = state_.pendingSendText();
  const QString content_kind = state_.pendingSendKind();

  if (content_kind == QStringLiteral("files")) {
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
  } else if (content_kind == QStringLiteral("text")) {
    if (NearbySharingApi::ValidateSendText(content_text.toStdString()) !=
        NearbySharingApi::StatusCode::kOk) {
      setStatus(QStringLiteral("Selected text is not available"));
      emit requestTrayMessage(QStringLiteral("Send failed"),
                              QStringLiteral("Text cannot be empty."));
      return;
    }
  } else if (content_kind == QStringLiteral("link")) {
    if (NearbySharingApi::ValidateSendUrl(content_text.toStdString()) !=
        NearbySharingApi::StatusCode::kOk) {
      setStatus(QStringLiteral("Selected link is not available"));
      emit requestTrayMessage(
          QStringLiteral("Send failed"),
          QStringLiteral("Links must start with http:// or https://."));
      return;
    }
  } else {
    setStatus(QStringLiteral("Nothing selected to send"));
    emit requestTrayMessage(QStringLiteral("Send failed"),
                            QStringLiteral("Choose files, text, or a link first."));
    return;
  }

  const QString target_name = state_.GetTargetName(share_target_id);
  sendPreparedContentToTarget(share_target_id, target_name, content_kind,
                              content_text, normalized_paths, file_names);
}

void FileShareTrayController::sendPreparedContentToTarget(
    qlonglong share_target_id, const QString& target_name,
    const QString& content_kind, const QString& content_text,
    const QStringList& file_paths, const QStringList& file_names) {
  if (!service_) {
    setStatus(QStringLiteral("Sharing service is not available"));
    emit requestTrayMessage(QStringLiteral("Send failed"),
                            QStringLiteral("Sharing service is not available."));
    return;
  }

  if (content_kind == QStringLiteral("files")) {
    state_.SetPendingSendFiles(file_paths, file_names, share_target_id);
  } else {
    const QString summary =
        content_kind == QStringLiteral("link") ? content_text : QStringLiteral("Text");
    state_.SetPendingSendText(content_kind, content_text, summary, share_target_id);
  }
  emitPendingSendStateChanged();
  const QString summary = state_.pendingSendSummary();
  const QString first_path = state_.pendingSendFilePath();
  const int attachment_count = state_.pendingSendFileCount();

  state_.AddOrUpdateTransfer(share_target_id, target_name, QStringLiteral("Queued"),
                             0.0, 0, 0, 0, QStringLiteral("Unknown"),
                             QStringLiteral("outgoing"), summary, first_path,
                             attachment_count, 0, content_kind, content_text,
                             file_paths);
  emit transfersChanged();

  const uint64_t generation = operation_generation_;
  const auto callback =
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
                                         state_.pendingSendFileCount(), 0,
                                         state_.pendingSendKind(),
                                         state_.pendingSendText(),
                                         state_.pendingSendFilePaths());
              emit transfersChanged();
              clearPendingSendState();
            },
            Qt::QueuedConnection);
      };

  if (content_kind == QStringLiteral("files")) {
    service_->SendFiles(share_target_id, pendingSendFilePathsForApi(), callback);
  } else if (content_kind == QStringLiteral("link")) {
    service_->SendUrl(share_target_id, content_text.toStdString(), callback);
  } else {
    service_->SendText(share_target_id, content_text.toStdString(), callback);
  }
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

void FileShareTrayController::retryTransfer(qlonglong share_target_id) {
  const QVariantMap transfer = state_.TransferForTarget(share_target_id);
  if (transfer.isEmpty() || !transfer.value(QStringLiteral("canRetry")).toBool()) {
    setStatus(QStringLiteral("Retry is not available"));
    emit requestTrayMessage(
        QStringLiteral("Retry unavailable"),
        QStringLiteral("Only failed outgoing transfers can be retried."));
    return;
  }

  const QString content_kind =
      transfer.value(QStringLiteral("contentKind")).toString();
  const QString content_text =
      transfer.value(QStringLiteral("contentText")).toString();
  const QStringList file_paths =
      transfer.value(QStringLiteral("filePaths")).toStringList();
  const QString target_name = state_.GetTargetName(share_target_id);

  if (content_kind == QStringLiteral("files")) {
    QStringList normalized_paths;
    QStringList file_names;
    QString error_message;
    if (!normalizeFileSelection(file_paths, &normalized_paths, &file_names,
                                &error_message)) {
      setStatus(QStringLiteral("Retry failed: selected files are unavailable"));
      emit requestTrayMessage(
          QStringLiteral("Retry failed"),
          error_message.isEmpty()
              ? QStringLiteral("The original files are no longer available.")
              : error_message);
      return;
    }
    sendPreparedContentToTarget(share_target_id, target_name, content_kind,
                                QString(), normalized_paths, file_names);
    return;
  }

  if (content_kind == QStringLiteral("link") &&
      NearbySharingApi::ValidateSendUrl(content_text.toStdString()) !=
          NearbySharingApi::StatusCode::kOk) {
    setStatus(QStringLiteral("Retry failed: link is invalid"));
    emit requestTrayMessage(QStringLiteral("Retry failed"),
                            QStringLiteral("The original link is no longer valid."));
    return;
  }
  if (content_kind == QStringLiteral("text") &&
      NearbySharingApi::ValidateSendText(content_text.toStdString()) !=
          NearbySharingApi::StatusCode::kOk) {
    setStatus(QStringLiteral("Retry failed: text is empty"));
    emit requestTrayMessage(QStringLiteral("Retry failed"),
                            QStringLiteral("The original text is empty."));
    return;
  }
  if (content_kind != QStringLiteral("text") &&
      content_kind != QStringLiteral("link")) {
    setStatus(QStringLiteral("Retry failed: content is unavailable"));
    emit requestTrayMessage(
        QStringLiteral("Retry failed"),
        QStringLiteral("The original transfer details are no longer available."));
    return;
  }

  sendPreparedContentToTarget(share_target_id, target_name, content_kind,
                              content_text, {}, {});
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

void FileShareTrayController::chooseReceiveFolder() {
  emit requestReceiveFolderPicker(receive_folder_);
}

void FileShareTrayController::resetReceiveFolder() {
  const QString default_folder =
      QString::fromStdString(NearbySharingApi::DefaultReceiveFolder());
  setReceiveFolder(default_folder);
}

void FileShareTrayController::handleNotificationAction(
    const QString& action_key, qlonglong share_target_id,
    const QString& file_path) {
  if (action_key == QStringLiteral("accept")) {
    acceptTransfer(share_target_id);
  } else if (action_key == QStringLiteral("reject")) {
    rejectTransfer(share_target_id);
  } else if (action_key == QStringLiteral("cancel")) {
    cancelTransfer(share_target_id);
  } else if (action_key == QStringLiteral("retry")) {
    retryTransfer(share_target_id);
  } else if (action_key == QStringLiteral("open")) {
    openFileLocation(file_path);
  }
}

void FileShareTrayController::clearTransfers() {
  state_.ClearAll();
  emit discoveredTargetsChanged();
  emit transfersChanged();
}

void FileShareTrayController::dismissTransfer(qlonglong share_target_id) {
  state_.RemoveTransfer(share_target_id);
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
  } else if (property == QStringLiteral("receiveFolder")) {
    emit receiveFolderChanged();
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
  const QVariantMap existing = state_.TransferForTarget(share_target_id);
  const QString direction = existing.value(QStringLiteral("direction"),
                                           QStringLiteral("outgoing")).toString();
  const QString file_name =
      existing.value(QStringLiteral("fileName"), state_.pendingSendSummary()).toString();
  const QString file_path =
      existing.value(QStringLiteral("filePath"), state_.pendingSendFilePath()).toString();
  state_.AddOrUpdateTransfer(share_target_id, target_name,
                             QStringLiteral("Cancelling"), 0.0, 0, 0, 0,
                             QStringLiteral("Unknown"), direction, file_name,
                             file_path,
                             state_.pendingSendFileCount(), 0);
  emit transfersChanged();

  const uint64_t generation = operation_generation_;
  service_->Cancel(share_target_id, [this, share_target_id, generation,
                                     direction, file_name, file_path](
                                        NearbySharingApi::StatusCode status) {
    QMetaObject::invokeMethod(
        this,
        [this, share_target_id, generation, status, direction, file_name,
         file_path]() {
          if (!isCurrentOperation(generation)) {
            return;
          }
          const QString target_name = state_.GetTargetName(share_target_id);
          if (status == NearbySharingApi::StatusCode::kOk) {
            state_.AddOrUpdateTransfer(
                share_target_id, target_name, QStringLiteral("Cancelled"), 1.0,
                0, 0, 0, QStringLiteral("Unknown"), direction, file_name,
                file_path,
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
