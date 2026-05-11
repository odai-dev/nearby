#include "file_share_tray_controller.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QFile>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

namespace {

using StatusCode = NearbySharingApi::StatusCode;

struct FakeServiceState {
  explicit FakeServiceState(QString device_name) : device_name(std::move(device_name)) {}

  QString device_name;
  NearbySharingApi::Listener listener;
  NearbySharingApi::DiagnosticInfo diagnostics;
  std::string qr_code_url = "nearby://test";

  StatusCode start_send_status = StatusCode::kOk;
  StatusCode stop_send_status = StatusCode::kStatusAlreadyStopped;
  StatusCode start_receive_status = StatusCode::kOk;
  StatusCode stop_receive_status = StatusCode::kStatusAlreadyStopped;
  StatusCode send_files_status = StatusCode::kOk;
  StatusCode send_text_status = StatusCode::kOk;
  StatusCode send_url_status = StatusCode::kOk;
  StatusCode accept_status = StatusCode::kOk;
  StatusCode reject_status = StatusCode::kOk;
  StatusCode cancel_status = StatusCode::kOk;
  StatusCode set_receive_folder_status = StatusCode::kOk;
  StatusCode shutdown_status = StatusCode::kOk;
  std::string receive_folder;

  bool defer_start_send = false;
  bool defer_stop_send = false;
  bool defer_start_receive = false;
  bool defer_stop_receive = false;
  std::function<void(StatusCode)> start_send_callback;
  std::function<void(StatusCode)> stop_send_callback;
  std::function<void(StatusCode)> start_receive_callback;
  std::function<void(StatusCode)> stop_receive_callback;

  int set_listener_calls = 0;
  int start_send_calls = 0;
  int stop_send_calls = 0;
  int start_receive_calls = 0;
  int stop_receive_calls = 0;
  int send_files_calls = 0;
  int send_text_calls = 0;
  int send_url_calls = 0;
  int accept_calls = 0;
  int reject_calls = 0;
  int cancel_calls = 0;
  int set_receive_folder_calls = 0;
  int shutdown_calls = 0;
  int set_hotspot_calls = 0;
  qlonglong last_send_target_id = 0;
  qlonglong last_accept_target_id = 0;
  qlonglong last_reject_target_id = 0;
  qlonglong last_cancel_target_id = 0;
  std::vector<std::string> last_sent_files;
  std::string last_sent_text;
  std::string last_sent_url;
  std::string last_receive_folder;
};

class FakeSharingService final : public NearbySharingServiceInterface {
 public:
  explicit FakeSharingService(std::shared_ptr<FakeServiceState> state)
      : state_(std::move(state)) {}

  void SetListener(NearbySharingApi::Listener listener) override {
    ++state_->set_listener_calls;
    state_->listener = std::move(listener);
  }

  void StartSendMode(std::function<void(StatusCode)> callback) override {
    ++state_->start_send_calls;
    FinishOrDefer(state_->defer_start_send, state_->start_send_status,
                  &state_->start_send_callback, std::move(callback));
  }

  void StopSendMode(std::function<void(StatusCode)> callback) override {
    ++state_->stop_send_calls;
    FinishOrDefer(state_->defer_stop_send, state_->stop_send_status,
                  &state_->stop_send_callback, std::move(callback));
  }

  void StartReceiveMode(std::function<void(StatusCode)> callback) override {
    ++state_->start_receive_calls;
    FinishOrDefer(state_->defer_start_receive, state_->start_receive_status,
                  &state_->start_receive_callback, std::move(callback));
  }

  void StopReceiveMode(std::function<void(StatusCode)> callback) override {
    ++state_->stop_receive_calls;
    FinishOrDefer(state_->defer_stop_receive, state_->stop_receive_status,
                  &state_->stop_receive_callback, std::move(callback));
  }

  void SendFiles(qlonglong share_target_id,
                 const std::vector<std::string>& file_paths,
                 std::function<void(StatusCode)> callback) override {
    ++state_->send_files_calls;
    state_->last_send_target_id = share_target_id;
    state_->last_sent_files = file_paths;
    if (callback) {
      callback(state_->send_files_status);
    }
  }

  void SendText(qlonglong share_target_id, const std::string& text,
                std::function<void(StatusCode)> callback) override {
    ++state_->send_text_calls;
    state_->last_send_target_id = share_target_id;
    state_->last_sent_text = text;
    if (callback) {
      callback(state_->send_text_status);
    }
  }

  void SendUrl(qlonglong share_target_id, const std::string& url,
               std::function<void(StatusCode)> callback) override {
    ++state_->send_url_calls;
    state_->last_send_target_id = share_target_id;
    state_->last_sent_url = url;
    if (callback) {
      callback(state_->send_url_status);
    }
  }

  void Accept(qlonglong share_target_id,
              std::function<void(StatusCode)> callback) override {
    ++state_->accept_calls;
    state_->last_accept_target_id = share_target_id;
    if (callback) {
      callback(state_->accept_status);
    }
  }

  void Reject(qlonglong share_target_id,
              std::function<void(StatusCode)> callback) override {
    ++state_->reject_calls;
    state_->last_reject_target_id = share_target_id;
    if (callback) {
      callback(state_->reject_status);
    }
  }

  void Cancel(qlonglong share_target_id,
              std::function<void(StatusCode)> callback) override {
    ++state_->cancel_calls;
    state_->last_cancel_target_id = share_target_id;
    if (callback) {
      callback(state_->cancel_status);
    }
  }

  void Set5GhzHotspotEnabled(bool) override { ++state_->set_hotspot_calls; }

  void Shutdown(std::function<void(StatusCode)> callback) override {
    ++state_->shutdown_calls;
    if (callback) {
      callback(state_->shutdown_status);
    }
  }

  std::string GetQrCodeUrl() const override { return state_->qr_code_url; }

  NearbySharingApi::DiagnosticInfo GetDiagnostics() const override {
    return state_->diagnostics;
  }

  std::string GetReceiveFolder() const override {
    return state_->receive_folder;
  }

  void SetReceiveFolder(
      const std::string& folder_path,
      std::function<void(StatusCode)> callback) override {
    ++state_->set_receive_folder_calls;
    state_->last_receive_folder = folder_path;
    state_->receive_folder = folder_path;
    if (callback) {
      callback(state_->set_receive_folder_status);
    }
  }

 private:
  static void FinishOrDefer(bool defer, StatusCode status,
                            std::function<void(StatusCode)>* saved_callback,
                            std::function<void(StatusCode)> callback) {
    if (defer) {
      *saved_callback = std::move(callback);
      return;
    }
    if (callback) {
      callback(status);
    }
  }

  std::shared_ptr<FakeServiceState> state_;
};

bool WriteTextFile(const QString& path, const QByteArray& data) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  return file.write(data) == data.size();
}

QVariantMap FirstTransfer(const FileShareTrayController& controller) {
  return controller.transfers().first().toMap();
}

}  // namespace

class FileShareTrayControllerTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    settings_dir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(settings_dir_->isValid());
    qputenv("XDG_CONFIG_HOME", settings_dir_->path().toUtf8());
  }

  void init() {
    services_.clear();
    start_receive_statuses_.clear();
    defer_start_receive_values_.clear();
    QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
    settings.clear();
    settings.sync();
  }

  void freshInstallDefaultDisablesAutoAccept() {
    auto controller = CreateController();

    QVERIFY(!controller->autoAcceptIncoming());
  }

  void startStartsReceiveModeByDefault() {
    auto controller = CreateController();
    auto service = services_.back();

    controller->start();

    QTRY_COMPARE(service->stop_send_calls, 1);
    QCOMPARE(service->start_receive_calls, 1);
    QCOMPARE(controller->mode(), QStringLiteral("Receive"));
    QVERIFY(controller->running());
    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StartReceiveMode: Ok"));
  }

  void startReceiveFailureReportsFailureAndStopsRunning() {
    start_receive_statuses_.push_back(StatusCode::kNoAvailableConnectionMedium);
    auto controller = CreateController();
    auto service = services_.back();
    QSignalSpy tray_messages(
        controller.get(), &FileShareTrayController::requestTrayMessage);

    controller->start();

    QTRY_COMPARE(service->start_receive_calls, 1);
    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StartReceiveMode: NoAvailableConnectionMedium"));
    QVERIFY(!controller->running());
    QCOMPARE(controller->mode(), QStringLiteral("Receive"));
    QCOMPARE(tray_messages.count(), 1);
    QCOMPARE(tray_messages.first().at(0).toString(),
             QStringLiteral("Receive mode failed"));
    QVERIFY(tray_messages.first()
                .at(1)
                .toString()
                .contains(QStringLiteral("NoAvailableConnectionMedium")));
  }

  void startStartsSendModeWhenFilesArePending() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file_path = dir.filePath(QStringLiteral("hello.txt"));
    QVERIFY(WriteTextFile(file_path, "hello"));

    auto controller = CreateController();
    auto service = services_.back();

    controller->switchToSendModeWithFiles({file_path});
    QCOMPARE(controller->pendingSendFileCount(), 1);
    controller->start();

    QTRY_COMPARE(service->stop_receive_calls, 1);
    QCOMPARE(service->start_send_calls, 1);
    QCOMPARE(controller->mode(), QStringLiteral("Send"));
    QVERIFY(controller->running());
    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StartSendMode: Ok"));
  }

  void invalidFileSelectionDoesNotSend() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing_file = dir.filePath(QStringLiteral("missing.txt"));

    auto controller = CreateController();
    auto service = services_.back();
    QSignalSpy tray_messages(
        controller.get(), &FileShareTrayController::requestTrayMessage);

    controller->switchToSendModeWithFiles({missing_file});

    QCOMPARE(controller->statusMessage(),
             QStringLiteral("Selected file is not valid"));
    QCOMPARE(service->send_files_calls, 0);
    QCOMPARE(service->start_send_calls, 0);
    QCOMPARE(tray_messages.count(), 1);
    const QList<QVariant> message = tray_messages.takeFirst();
    QCOMPARE(message.at(0).toString(), QStringLiteral("Send canceled"));
    QVERIFY(message.at(1).toString().contains(QStringLiteral("does not exist")));
  }

  void sendModeStopReceiveFailureRestoresReceiveMode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file_path = dir.filePath(QStringLiteral("hello.txt"));
    QVERIFY(WriteTextFile(file_path, "hello"));

    auto controller = CreateController();
    auto service = services_.back();
    controller->start();
    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StartReceiveMode: Ok"));

    service->stop_receive_status = StatusCode::kError;
    controller->switchToSendModeWithFiles({file_path});

    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StopReceiveMode failed: Error"));
    QCOMPARE(controller->mode(), QStringLiteral("Receive"));
    QVERIFY(!controller->running());
    QCOMPARE(service->start_send_calls, 0);
  }

  void sendModeStartFailureRestoresReceiveMode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file_path = dir.filePath(QStringLiteral("hello.txt"));
    QVERIFY(WriteTextFile(file_path, "hello"));

    auto controller = CreateController();
    auto service = services_.back();
    controller->start();
    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StartReceiveMode: Ok"));

    service->stop_receive_status = StatusCode::kOk;
    service->start_send_status = StatusCode::kNoAvailableConnectionMedium;
    QSignalSpy tray_messages(
        controller.get(), &FileShareTrayController::requestTrayMessage);

    controller->switchToSendModeWithFiles({file_path});

    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StartSendMode: NoAvailableConnectionMedium"));
    QCOMPARE(controller->mode(), QStringLiteral("Receive"));
    QVERIFY(!controller->running());
    QCOMPARE(service->start_send_calls, 1);
    QVERIFY(tray_messages.count() >= 2);
    QCOMPARE(tray_messages.last().at(0).toString(),
             QStringLiteral("Send mode failed"));
    QVERIFY(tray_messages.last()
                .at(1)
                .toString()
                .contains(QStringLiteral("NoAvailableConnectionMedium")));
  }

  void stopClearsTransfersOnlyAfterCallbacksReturn() {
    auto controller = CreateController();
    auto service = services_.back();
    controller->start();
    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StartReceiveMode: Ok"));

    NearbySharingApi::TransferUpdateInfo update;
    update.share_target_id = 7;
    update.device_name = "Laptop";
    update.status = NearbySharingApi::TransferStatus::kInProgress;
    update.first_file_name = "hello.txt";
    update.total_attachments = 1;
    service->listener.transfer_update_cb(update);
    QTRY_COMPARE(controller->transfers().size(), 1);

    service->defer_stop_receive = true;
    service->stop_receive_status = StatusCode::kOk;

    controller->stop();

    QCOMPARE(service->stop_send_calls, 2);
    QCOMPARE(service->stop_receive_calls, 1);
    QVERIFY(!controller->running());
    QCOMPARE(controller->transfers().size(), 1);

    QVERIFY(service->stop_receive_callback);
    service->stop_receive_callback(StatusCode::kOk);

    QTRY_COMPARE(controller->transfers().size(), 0);
    QCOMPARE(controller->statusMessage(), QStringLiteral("Stopped"));
  }

  void staleStartReceiveCallbackAfterStopIsIgnored() {
    defer_start_receive_values_.push_back(true);
    auto controller = CreateController();
    auto service = services_.back();

    controller->start();
    QCOMPARE(service->start_receive_calls, 1);
    QVERIFY(service->start_receive_callback);

    controller->stop();
    QTRY_COMPARE(controller->statusMessage(), QStringLiteral("Stopped"));

    service->start_receive_callback(StatusCode::kOk);
    QCoreApplication::processEvents();

    QCOMPARE(controller->statusMessage(), QStringLiteral("Stopped"));
    QVERIFY(!controller->running());
  }

  void staleStartReceiveCallbackAfterRenameIsIgnored() {
    defer_start_receive_values_.push_back(true);
    defer_start_receive_values_.push_back(false);
    start_receive_statuses_.push_back(StatusCode::kOk);
    start_receive_statuses_.push_back(StatusCode::kError);

    auto controller = CreateController();
    auto old_service = services_.back();
    controller->start();
    QCOMPARE(old_service->start_receive_calls, 1);
    QVERIFY(old_service->start_receive_callback);

    controller->setDeviceName(QStringLiteral("Renamed Device"));
    QCOMPARE(services_.size(), static_cast<size_t>(2));
    auto new_service = services_.back();
    QTRY_COMPARE(new_service->start_receive_calls, 1);
    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StartReceiveMode: Error"));

    old_service->start_receive_callback(StatusCode::kOk);
    QCoreApplication::processEvents();

    QCOMPARE(controller->statusMessage(),
             QStringLiteral("StartReceiveMode: Error"));
  }

  void cancelCallsApiAndMarksTransferCancelled() {
    auto controller = CreateController();
    auto service = services_.back();
    controller->start();
    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("StartReceiveMode: Ok"));

    NearbySharingApi::ShareTargetInfo target;
    target.id = 42;
    target.device_name = "Laptop";
    service->listener.target_discovered_cb(target);
    QTRY_COMPARE(controller->discoveredTargets().size(), 1);

    NearbySharingApi::TransferUpdateInfo update;
    update.share_target_id = 42;
    update.device_name = "Laptop";
    update.status = NearbySharingApi::TransferStatus::kInProgress;
    update.progress = 0.25f;
    update.first_file_name = "hello.txt";
    update.total_attachments = 1;
    service->listener.transfer_update_cb(update);
    QTRY_COMPARE(controller->transfers().size(), 1);

    controller->cancelTransfer(42);

    QTRY_COMPARE(service->cancel_calls, 1);
    QCOMPARE(service->last_cancel_target_id, 42);
    QTRY_COMPARE(controller->statusMessage(),
                 QStringLiteral("Transfer cancelled"));
    QCOMPARE(FirstTransfer(*controller).value(QStringLiteral("status")).toString(),
             QStringLiteral("Cancelled"));
  }

  void selfShareTargetIsFilteredFromReceivers() {
    auto controller = CreateController();
    auto service = services_.back();

    NearbySharingApi::ShareTargetInfo target;
    target.id = 101;
    target.device_name = "My other device";
    target.for_self_share = true;
    service->listener.target_discovered_cb(target);

    QCoreApplication::processEvents();
    QCOMPARE(controller->discoveredTargets().size(), 0);
  }

  void localEchoTargetIsFilteredFromReceivers() {
    auto controller = CreateController();
    auto service = services_.back();

    NearbySharingApi::ShareTargetInfo target;
    target.id = 102;
    target.device_name = controller->deviceName().toStdString();
    service->listener.target_discovered_cb(target);

    QCoreApplication::processEvents();
    QCOMPARE(controller->discoveredTargets().size(), 0);
  }

  void disabledPhoneTargetAppearsButCannotBeSelected() {
    auto controller = CreateController();
    auto service = services_.back();

    NearbySharingApi::ShareTargetInfo target;
    target.id = 103;
    target.device_name = "Nearby Android device";
    target.receive_disabled = true;
    target.is_actionable = false;
    target.status_reason = "Set phone Quick Share visibility to Everyone";
    service->listener.target_discovered_cb(target);
    QTRY_COMPARE(controller->discoveredTargets().size(), 1);

    const QVariantMap row = controller->discoveredTargets().first().toMap();
    QCOMPARE(row.value(QStringLiteral("isActionable")).toBool(), false);
    QCOMPARE(row.value(QStringLiteral("statusReason")).toString(),
             QStringLiteral("Set phone Quick Share visibility to Everyone"));
    QCOMPARE(controller->statusMessage(),
             QStringLiteral("Phone detected, but not visible to everyone"));

    controller->switchToSendModeWithText(QStringLiteral("hello"));
    controller->sendPendingFilesToTarget(103);

    QCOMPARE(service->send_text_calls, 0);
    QCOMPARE(controller->statusMessage(),
             QStringLiteral("Set phone Quick Share visibility to Everyone"));
  }

  void disabledPhoneTargetIsReplacedByActionableTarget() {
    auto controller = CreateController();
    auto service = services_.back();

    NearbySharingApi::ShareTargetInfo target;
    target.id = 104;
    target.device_name = "Nearby Android device";
    target.receive_disabled = true;
    target.is_actionable = false;
    target.status_reason = "Set phone Quick Share visibility to Everyone";
    service->listener.target_discovered_cb(target);
    QTRY_COMPARE(controller->discoveredTargets().size(), 1);

    target.device_name = "Pixel 6 Pro";
    target.receive_disabled = false;
    target.is_actionable = true;
    target.status_reason.clear();
    service->listener.target_discovered_cb(target);
    QTRY_COMPARE(controller->discoveredTargets().size(), 1);
    QTRY_COMPARE(controller->discoveredTargets()
                     .first()
                     .toMap()
                     .value(QStringLiteral("name"))
                     .toString(),
                 QStringLiteral("Pixel 6 Pro"));

    const QVariantMap row = controller->discoveredTargets().first().toMap();
    QCOMPARE(row.value(QStringLiteral("isActionable")).toBool(), true);
    QCOMPARE(row.value(QStringLiteral("statusReason")).toString(), QString());
  }

  void clipboardTextPreparesTextSend() {
    auto controller = CreateController();
    QClipboard* clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard != nullptr);
    clipboard->setText(QStringLiteral("hello from clipboard"));

    controller->prepareSendFromClipboard();

    QCOMPARE(controller->pendingSendKind(), QStringLiteral("text"));
    QCOMPARE(controller->pendingSendText(), QStringLiteral("hello from clipboard"));
    QCOMPARE(controller->pendingSendFileCount(), 1);
  }

  void clipboardUrlPreparesLinkSend() {
    auto controller = CreateController();
    QClipboard* clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard != nullptr);
    clipboard->setText(QStringLiteral("https://example.com/path"));

    controller->prepareSendFromClipboard();

    QCOMPARE(controller->pendingSendKind(), QStringLiteral("link"));
    QCOMPARE(controller->pendingSendText(), QStringLiteral("https://example.com/path"));
  }

  void clipboardFileUrlsPrepareFileSend() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file_path = dir.filePath(QStringLiteral("hello.txt"));
    QVERIFY(WriteTextFile(file_path, "hello"));

    auto controller = CreateController();
    auto* mime_data = new QMimeData();
    mime_data->setUrls({QUrl::fromLocalFile(file_path)});
    QClipboard* clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard != nullptr);
    clipboard->setMimeData(mime_data);

    controller->prepareSendFromClipboard();

    QCOMPARE(controller->pendingSendKind(), QStringLiteral("files"));
    QCOMPARE(controller->pendingSendFilePaths(), QStringList{file_path});
  }

  void sendsPreparedTextAndLinkToTarget() {
    auto controller = CreateController();
    auto service = services_.back();

    controller->switchToSendModeWithText(QStringLiteral("hello"));
    controller->sendPendingFilesToTarget(42);
    QTRY_COMPARE(service->send_text_calls, 1);
    QCOMPARE(service->last_send_target_id, 42);
    QCOMPARE(QString::fromStdString(service->last_sent_text),
             QStringLiteral("hello"));

    controller->switchToSendModeWithLink(QStringLiteral("https://example.com"));
    controller->sendPendingFilesToTarget(43);
    QTRY_COMPARE(service->send_url_calls, 1);
    QCOMPARE(service->last_send_target_id, 43);
    QCOMPARE(QString::fromStdString(service->last_sent_url),
             QStringLiteral("https://example.com"));
  }

  void retryFailedOutgoingTextResendsOriginalPayload() {
    auto controller = CreateController();
    auto service = services_.back();
    service->send_text_status = StatusCode::kError;

    controller->switchToSendModeWithText(QStringLiteral("try again"));
    controller->sendPendingFilesToTarget(42);
    QTRY_COMPARE(controller->transfers().size(), 1);
    QTRY_COMPARE(FirstTransfer(*controller).value(QStringLiteral("status")).toString(),
                 QStringLiteral("Failed"));
    QCOMPARE(FirstTransfer(*controller).value(QStringLiteral("canRetry")).toBool(),
             true);
    QCOMPARE(controller->pendingSendFileCount(), 0);

    service->send_text_status = StatusCode::kOk;
    controller->retryTransfer(42);

    QTRY_COMPARE(service->send_text_calls, 2);
    QCOMPARE(QString::fromStdString(service->last_sent_text),
             QStringLiteral("try again"));
    QCOMPARE(FirstTransfer(*controller).value(QStringLiteral("status")).toString(),
             QStringLiteral("Queued"));
  }

  void retryUnavailableForIncomingFailure() {
    auto controller = CreateController();
    auto service = services_.back();

    NearbySharingApi::TransferUpdateInfo update;
    update.share_target_id = 77;
    update.device_name = "Phone";
    update.is_incoming = true;
    update.status = NearbySharingApi::TransferStatus::kFailed;
    update.first_file_name = "hello.txt";
    update.total_attachments = 1;
    service->listener.transfer_update_cb(update);
    QTRY_COMPARE(controller->transfers().size(), 1);

    controller->retryTransfer(77);

    QCOMPARE(service->send_files_calls, 0);
    QCOMPARE(service->send_text_calls, 0);
    QCOMPARE(service->send_url_calls, 0);
    QCOMPARE(controller->statusMessage(), QStringLiteral("Retry is not available"));
  }

  void receiveFolderSettingPersistsAndReportsInvalidFolders() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto controller = CreateController();
    auto service = services_.back();

    controller->setReceiveFolder(dir.path());

    QTRY_COMPARE(service->set_receive_folder_calls, 1);
    QTRY_COMPARE(controller->receiveFolder(), dir.path());
    QCOMPARE(QString::fromStdString(service->last_receive_folder), dir.path());

    const QString missing_folder = dir.filePath(QStringLiteral("missing"));
    controller->setReceiveFolder(missing_folder);

    QCOMPARE(service->set_receive_folder_calls, 1);
    QCOMPARE(controller->statusMessage(),
             QStringLiteral("Receive folder is not valid"));
  }

  void incomingConfirmationEmitsNotificationPromptAndHandlersAcceptReject() {
    auto controller = CreateController();
    auto service = services_.back();
    QSignalSpy actionable_messages(
        controller.get(), &FileShareTrayController::requestActionableTrayMessage);
    QSignalSpy confirmation_prompts(
        controller.get(),
        &FileShareTrayController::requestIncomingConfirmationPrompt);

    NearbySharingApi::TransferUpdateInfo update;
    update.share_target_id = 88;
    update.device_name = "Phone";
    update.is_incoming = true;
    update.status = NearbySharingApi::TransferStatus::kAwaitingLocalConfirmation;
    update.first_file_name = "hello.txt";
    update.total_attachments = 1;
    service->listener.transfer_update_cb(update);

    QTRY_COMPARE(actionable_messages.count(), 1);
    QTRY_COMPARE(confirmation_prompts.count(), 1);
    QCOMPARE(confirmation_prompts.at(0).at(2).toLongLong(), 88);

    controller->handleNotificationAction(QStringLiteral("accept"), 88, {});
    QTRY_COMPARE(service->accept_calls, 1);
    QCOMPARE(service->last_accept_target_id, 88);

    controller->handleNotificationAction(QStringLiteral("reject"), 88, {});
    QTRY_COMPARE(service->reject_calls, 1);
    QCOMPARE(service->last_reject_target_id, 88);
  }

  void incomingConfirmationAutoAcceptSkipsUserPrompt() {
    auto controller = CreateController();
    auto service = services_.back();
    controller->setAutoAcceptIncoming(true);
    QSignalSpy actionable_messages(
        controller.get(), &FileShareTrayController::requestActionableTrayMessage);
    QSignalSpy confirmation_prompts(
        controller.get(),
        &FileShareTrayController::requestIncomingConfirmationPrompt);

    NearbySharingApi::TransferUpdateInfo update;
    update.share_target_id = 88;
    update.device_name = "Phone";
    update.is_incoming = true;
    update.status = NearbySharingApi::TransferStatus::kAwaitingLocalConfirmation;
    update.first_file_name = "hello.txt";
    update.total_attachments = 1;
    service->listener.transfer_update_cb(update);

    QCOMPARE(actionable_messages.count(), 0);
    QCOMPARE(confirmation_prompts.count(), 0);
    QTRY_COMPARE(service->accept_calls, 1);
    QCOMPARE(service->last_accept_target_id, 88);
  }

  void targetRemovalPreservesCompletedTransferContext() {
    FileShareState state;
    state.AddOrUpdateTarget(99, QStringLiteral("Laptop"), false);
    state.AddOrUpdateTransfer(99, QStringLiteral("Laptop"),
                              QStringLiteral("Complete"), 1.0, 10, 10, 0,
                              QStringLiteral("Wi-Fi"),
                              QStringLiteral("outgoing"),
                              QStringLiteral("hello.txt"),
                              QStringLiteral("/tmp/hello.txt"), 1, 1);

    state.RemoveTarget(99);

    QVERIFY(!state.HasTarget(99));
    QCOMPARE(state.GetTargetName(99), QStringLiteral("Laptop"));
    QCOMPARE(state.transfers().size(), 1);
    const QVariantMap transfer = state.transfers().first().toMap();
    QCOMPARE(transfer.value(QStringLiteral("targetName")).toString(),
             QStringLiteral("Laptop"));
    QCOMPARE(transfer.value(QStringLiteral("status")).toString(),
             QStringLiteral("Complete"));
  }

 private:
  std::unique_ptr<FileShareTrayController> CreateController() {
    FileShareTrayController::ServiceFactory factory =
        [this](const QString& device_name) {
          auto state = std::make_shared<FakeServiceState>(device_name);
          if (!start_receive_statuses_.empty()) {
            state->start_receive_status = start_receive_statuses_.front();
            start_receive_statuses_.pop_front();
          }
          if (!defer_start_receive_values_.empty()) {
            state->defer_start_receive = defer_start_receive_values_.front();
            defer_start_receive_values_.pop_front();
          }
          services_.push_back(state);
          return std::make_unique<FakeSharingService>(state);
        };
    return std::make_unique<FileShareTrayController>(std::move(factory));
  }

  std::unique_ptr<QTemporaryDir> settings_dir_;
  std::vector<std::shared_ptr<FakeServiceState>> services_;
  std::deque<StatusCode> start_receive_statuses_;
  std::deque<bool> defer_start_receive_values_;
};

QTEST_MAIN(FileShareTrayControllerTest)
#include "file_share_tray_controller_test.moc"
