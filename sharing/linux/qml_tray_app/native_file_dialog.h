#ifndef NEARBY_SHARING_LINUX_QML_TRAY_APP_NATIVE_FILE_DIALOG_H_
#define NEARBY_SHARING_LINUX_QML_TRAY_APP_NATIVE_FILE_DIALOG_H_

#include <functional>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QWindow>
#include <QVariantMap>

class NativeFileDialog : public QObject {
  Q_OBJECT

 public:
  explicit NativeFileDialog(QObject* parent = nullptr);
  ~NativeFileDialog() override;

  static void pickFiles(QWindow* parent_window,
                        const QString& title,
                        const QString& initial_folder,
                        bool multiple,
                        std::function<void(const QStringList&)> on_selected);

  static void pickFolder(QWindow* parent_window,
                         const QString& title,
                         const QString& initial_folder,
                         std::function<void(const QString&)> on_selected);

 private:
  void startOpenFilePortal(QWindow* parent_window,
                           const QString& title,
                           const QString& initial_folder,
                           bool directory,
                           bool multiple,
                           std::function<void(const QStringList&)> on_selected);

  void fallbackToProcessOrQt(QWindow* parent_window,
                             const QString& title,
                             const QString& initial_folder,
                             bool directory,
                             bool multiple,
                             std::function<void(const QStringList&)> on_selected);

 private Q_SLOTS:
  void onPortalResponse(uint response, const QVariantMap& results);

 private:
  QString request_path_;
  std::function<void(const QStringList&)> callback_;
};

#endif  // NEARBY_SHARING_LINUX_QML_TRAY_APP_NATIVE_FILE_DIALOG_H_
