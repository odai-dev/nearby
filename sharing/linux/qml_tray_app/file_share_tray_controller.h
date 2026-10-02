#ifndef SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_TRAY_CONTROLLER_H_
#define SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_TRAY_CONTROLLER_H_

#include <QObject>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "file_share_state.h"
#include <sharing/linux/nearby_sharing_api.h>

using NearbySharingApi = nearby::sharing::NearbySharingApi;

class NearbySharingServiceInterface {
 public:
  virtual ~NearbySharingServiceInterface() = default;

  virtual void SetListener(NearbySharingApi::Listener listener) = 0;
  virtual void StartSendMode(
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void StopSendMode(
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void StartReceiveMode(
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void StopReceiveMode(
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void SendFiles(
      qlonglong share_target_id, const std::vector<std::string>& file_paths,
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void SendText(
      qlonglong share_target_id, const std::string& text,
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void SendUrl(
      qlonglong share_target_id, const std::string& url,
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void Accept(
      qlonglong share_target_id,
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void Reject(
      qlonglong share_target_id,
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void Cancel(
      qlonglong share_target_id,
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual void Set5GhzHotspotEnabled(bool enabled) = 0;
  virtual void Shutdown(
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
  virtual std::string GetQrCodeUrl() const = 0;
  virtual NearbySharingApi::DiagnosticInfo GetDiagnostics() const = 0;
  virtual std::string GetReceiveFolder() const = 0;
  virtual void SetReceiveFolder(
      const std::string& folder_path,
      std::function<void(NearbySharingApi::StatusCode)> callback) = 0;
};

class FileShareTrayController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
  Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(QString pendingSendFileName READ pendingSendFileName NOTIFY pendingSendFileNameChanged)
  Q_PROPERTY(QString pendingSendFilePath READ pendingSendFilePath NOTIFY pendingSendFilePathChanged)
  Q_PROPERTY(QStringList pendingSendFileNames READ pendingSendFileNames NOTIFY pendingSendFilesChanged)
  Q_PROPERTY(QStringList pendingSendFilePaths READ pendingSendFilePaths NOTIFY pendingSendFilesChanged)
  Q_PROPERTY(int pendingSendFileCount READ pendingSendFileCount NOTIFY pendingSendFilesChanged)
  Q_PROPERTY(QString pendingSendSummary READ pendingSendSummary NOTIFY pendingSendFilesChanged)
  Q_PROPERTY(QString pendingSendKind READ pendingSendKind NOTIFY pendingSendFilesChanged)
  Q_PROPERTY(QString pendingSendText READ pendingSendText NOTIFY pendingSendFilesChanged)
  Q_PROPERTY(QVariantList discoveredTargets READ discoveredTargets NOTIFY discoveredTargetsChanged)
  Q_PROPERTY(QVariantList transfers READ transfers NOTIFY transfersChanged)
  Q_PROPERTY(bool autoAcceptIncoming READ autoAcceptIncoming WRITE setAutoAcceptIncoming NOTIFY autoAcceptIncomingChanged)
  Q_PROPERTY(bool enable5GhzHotspot READ enable5GhzHotspot WRITE setEnable5GhzHotspot NOTIFY enable5GhzHotspotChanged)
  Q_PROPERTY(bool startOnLogin READ startOnLogin WRITE setStartOnLogin NOTIFY startOnLoginChanged)
  Q_PROPERTY(QString diagnosticsSummary READ diagnosticsSummary NOTIFY diagnosticsChanged)
  Q_PROPERTY(QString qrCodeUrl READ qrCodeUrl NOTIFY qrCodeUrlChanged)
  Q_PROPERTY(QStringList qrCodeRows READ qrCodeRows NOTIFY qrCodeChanged)
  Q_PROPERTY(int qrCodeSize READ qrCodeSize NOTIFY qrCodeChanged)
  Q_PROPERTY(QString logPath READ logPath WRITE setLogPath NOTIFY logPathChanged)
  Q_PROPERTY(QString receiveFolder READ receiveFolder WRITE setReceiveFolder NOTIFY receiveFolderChanged)

 public:
  using ServiceFactory =
      std::function<std::unique_ptr<NearbySharingServiceInterface>(
          const QString& device_name)>;

  explicit FileShareTrayController(QObject* parent = nullptr);
  FileShareTrayController(ServiceFactory service_factory,
                          QObject* parent = nullptr);
  ~FileShareTrayController() override;

  // Property accessors
  QString mode() const { return state_.mode(); }
  QString deviceName() const { return state_.deviceName(); }
  QString statusMessage() const { return state_.statusMessage(); }
  bool running() const { return state_.running(); }
  QString pendingSendFileName() const { return state_.pendingSendFileName(); }
  QString pendingSendFilePath() const { return state_.pendingSendFilePath(); }
  QStringList pendingSendFileNames() const { return state_.pendingSendFileNames(); }
  QStringList pendingSendFilePaths() const { return state_.pendingSendFilePaths(); }
  int pendingSendFileCount() const { return state_.pendingSendFileCount(); }
  QString pendingSendSummary() const { return state_.pendingSendSummary(); }
  QString pendingSendKind() const { return state_.pendingSendKind(); }
  QString pendingSendText() const { return state_.pendingSendText(); }
  QVariantList discoveredTargets() const { return state_.discoveredTargets(); }
  QVariantList transfers() const { return state_.transfers(); }
  bool autoAcceptIncoming() const { return state_.autoAcceptIncoming(); }
  bool enable5GhzHotspot() const { return state_.enable5GhzHotspot(); }
  bool startOnLogin() const { return state_.startOnLogin(); }
  QString diagnosticsSummary() const { return diagnostics_summary_; }
  QString qrCodeUrl() const { return state_.qrCodeUrl(); }
  QStringList qrCodeRows() const { return state_.qrCodeRows(); }
  int qrCodeSize() const { return state_.qrCodeSize(); }
  QString logPath() const { return state_.logPath(); }
  QString receiveFolder() const { return receive_folder_; }

  // Public methods
  void setDeviceName(const QString& device_name);
  void setAutoAcceptIncoming(bool enabled);
  void setEnable5GhzHotspot(bool enabled);
  void setStartOnLogin(bool enabled);
  void setLogPath(const QString& path);
  void setReceiveFolder(const QString& path);

  Q_INVOKABLE void start();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void switchToReceiveMode();
  Q_INVOKABLE void switchToSendModeWithFile(const QString& file_path);
  Q_INVOKABLE void switchToSendModeWithFiles(const QStringList& file_paths);
  Q_INVOKABLE void switchToSendModeWithUrls(const QVariantList& urls);
  Q_INVOKABLE void switchToSendModeWithText(const QString& text);
  Q_INVOKABLE void switchToSendModeWithLink(const QString& url);
  Q_INVOKABLE void prepareSendFromClipboard();
  Q_INVOKABLE void prepareDroppedText(const QString& text);
  Q_INVOKABLE void sendPendingFileToTarget(qlonglong share_target_id);
  Q_INVOKABLE void sendPendingFilesToTarget(qlonglong share_target_id);
  Q_INVOKABLE void retryTransfer(qlonglong share_target_id);
  Q_INVOKABLE void copyTextToClipboard(const QString& text);
  Q_INVOKABLE void openFileLocation(const QString& file_path);
  Q_INVOKABLE void clearTransfers();
  Q_INVOKABLE void dismissTransfer(qlonglong share_target_id);
  Q_INVOKABLE void hideToTray();
  Q_INVOKABLE void acceptTransfer(qlonglong share_target_id);
  Q_INVOKABLE void rejectTransfer(qlonglong share_target_id);
  Q_INVOKABLE void cancelTransfer(qlonglong share_target_id);
  Q_INVOKABLE void openFilePicker();
  Q_INVOKABLE void chooseReceiveFolder();
  Q_INVOKABLE void resetReceiveFolder();
  Q_INVOKABLE void handleNotificationAction(const QString& action_key,
                                            qlonglong share_target_id,
                                            const QString& file_path);

 signals:
  void modeChanged();
  void deviceNameChanged();
  void statusMessageChanged();
  void runningChanged();
  void pendingSendFileNameChanged();
  void pendingSendFilePathChanged();
  void pendingSendFilesChanged();
  void discoveredTargetsChanged();
  void transfersChanged();
  void autoAcceptIncomingChanged();
  void enable5GhzHotspotChanged();
  void startOnLoginChanged();
  void diagnosticsChanged();
  void qrCodeUrlChanged();
  void qrCodeChanged();
  void logPathChanged();
  void receiveFolderChanged();
  void requestFilePicker();
  void requestReceiveFolderPicker(const QString& current_folder);

  void requestTrayMessage(const QString& title, const QString& body);
  void requestCopyLinkTrayMessage(const QString& title, const QString& body,
                                   const QString& link);
  void requestActionableTrayMessage(const QString& title, const QString& body,
                                    qlonglong share_target_id,
                                    const QString& file_path,
                                    const QVariantList& actions);
  void requestIncomingConfirmationPrompt(const QString& title,
                                         const QString& body,
                                         qlonglong share_target_id);

 private:
  void initializeService();
  void attachServiceListeners();
  void loadSettings();
  void saveSettings() const;
  bool applyAutostartSetting(bool enabled, QString* error_message) const;
  uint64_t nextOperationGeneration();
  bool isCurrentOperation(uint64_t generation) const;
  bool isCurrentService(uint64_t generation) const;
  void refreshDiagnostics();
  void refreshReceiveFolder();
  void emitDiagnosticsWarningIfNeeded(const QString& operation_name);
  QString diagnosticWarningsToSummary(
      const NearbySharingApi::DiagnosticInfo& diagnostics) const;
  void updateQrCodeData();
  bool normalizeFileSelection(const QStringList& file_paths, QStringList* normalized_paths,
                              QStringList* file_names,
                              QString* error_message) const;
  QStringList localPathsFromUrlValues(const QVariantList& urls) const;
  bool prepareSendText(const QString& text, const QString& kind,
                       QString* error_message);
  bool isHttpOrHttpsUrl(const QString& text) const;
  std::vector<std::string> pendingSendFilePathsForApi() const;
  void sendPendingContentToTarget(qlonglong share_target_id);
  void sendPreparedContentToTarget(qlonglong share_target_id,
                                   const QString& target_name,
                                   const QString& content_kind,
                                   const QString& content_text,
                                   const QStringList& file_paths,
                                   const QStringList& file_names);
  void clearPendingSendState();
  void emitPendingSendStateChanged();
  QString transferFileSummary(const NearbySharingApi::TransferUpdateInfo& update) const;

  void startSendMode();
  void startReceiveMode();
  void finishStopOperation(uint64_t generation);
  
  void updateTargetFromInfo(const NearbySharingApi::ShareTargetInfo& info);
  bool shouldHideDiscoveredTarget(
      const NearbySharingApi::ShareTargetInfo& info,
      const QString& target_name) const;
  void handleTransferUpdate(const NearbySharingApi::TransferUpdateInfo& update);
  void handleTransferComplete(const NearbySharingApi::TransferUpdateInfo& update);
  void handleIncomingTransferComplete(const NearbySharingApi::TransferUpdateInfo& update,
                                      const QString& name, bool success);
  void handleOutgoingTransferComplete(const NearbySharingApi::TransferUpdateInfo& update,
                                      const QString& name, bool success);
  
  void setStatus(const QString& status);
  void notifyStateChange(const QString& property);

  ServiceFactory service_factory_;
  std::unique_ptr<NearbySharingServiceInterface> service_;
  FileShareState state_;
  QString diagnostics_summary_;
  QString receive_folder_;
  uint64_t service_generation_ = 0;
  uint64_t operation_generation_ = 0;
  bool stopping_ = false;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_TRAY_CONTROLLER_H_
