#include "native_file_dialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

NativeFileDialog::NativeFileDialog(QObject* parent) : QObject(parent) {}

NativeFileDialog::~NativeFileDialog() = default;

void NativeFileDialog::pickFiles(
    QWindow* parent_window,
    const QString& title,
    const QString& initial_folder,
    bool multiple,
    std::function<void(const QStringList&)> on_selected) {
  auto* dialog = new NativeFileDialog();
  dialog->startFilePicker(parent_window, title, initial_folder,
                          /*directory=*/false, multiple, std::move(on_selected));
}

void NativeFileDialog::pickFolder(
    QWindow* parent_window,
    const QString& title,
    const QString& initial_folder,
    std::function<void(const QString&)> on_selected) {
  auto* dialog = new NativeFileDialog();
  dialog->startFilePicker(
      parent_window, title, initial_folder, /*directory=*/true,
      /*multiple=*/false,
      [on_selected = std::move(on_selected)](const QStringList& files) {
        if (!files.isEmpty()) {
          on_selected(files.first());
        }
      });
}

void NativeFileDialog::startFilePicker(
    QWindow* parent_window,
    const QString& title,
    const QString& initial_folder,
    bool directory,
    bool multiple,
    std::function<void(const QStringList&)> on_selected) {
  callback_ = std::move(on_selected);

  const QString desktop =
      qEnvironmentVariable("XDG_CURRENT_DESKTOP").toUpper();
  const bool is_kde = desktop.contains(QStringLiteral("KDE"));

  if (is_kde) {
    if (tryLaunchKDialog(title, initial_folder, directory, multiple)) return;
    if (tryLaunchZenity(title, initial_folder, directory, multiple)) return;
  } else {
    if (tryLaunchZenity(title, initial_folder, directory, multiple)) return;
    if (tryLaunchKDialog(title, initial_folder, directory, multiple)) return;
  }

  fallbackToQtDialog(parent_window, title, initial_folder, directory, multiple);
}

bool NativeFileDialog::tryLaunchZenity(const QString& title,
                                       const QString& initial_folder,
                                       bool directory,
                                       bool multiple) {
  const QString zenity =
      QStandardPaths::findExecutable(QStringLiteral("zenity"));
  if (zenity.isEmpty()) {
    return false;
  }

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
    QString folder = initial_folder.trimmed();
    if (!folder.endsWith(QLatin1Char('/'))) {
      folder.append(QLatin1Char('/'));
    }
    args << QStringLiteral("--filename=%1").arg(folder);
  }

  connect(
      process,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
      [this, process](int exit_code, QProcess::ExitStatus status) {
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
        if (callback_ && !results.isEmpty()) {
          callback_(results);
        }
        deleteLater();
      });

  process->start(zenity, args);
  return true;
}

bool NativeFileDialog::tryLaunchKDialog(const QString& title,
                                        const QString& initial_folder,
                                        bool directory,
                                        bool multiple) {
  const QString kdialog =
      QStandardPaths::findExecutable(QStringLiteral("kdialog"));
  if (kdialog.isEmpty()) {
    return false;
  }

  const QString folder =
      initial_folder.isEmpty() ? QDir::homePath() : initial_folder;

  auto* process = new QProcess(this);
  QStringList args;
  if (directory) {
    args << QStringLiteral("--getexistingdirectory") << folder
         << QStringLiteral("--title") << title;
  } else if (multiple) {
    args << QStringLiteral("--getopenfilename") << folder
         << QStringLiteral("*") << QStringLiteral("--multiple")
         << QStringLiteral("--separate-output") << QStringLiteral("--title")
         << title;
  } else {
    args << QStringLiteral("--getopenfilename") << folder
         << QStringLiteral("*") << QStringLiteral("--title") << title;
  }

  connect(
      process,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
      [this, process](int exit_code, QProcess::ExitStatus status) {
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
        if (callback_ && !results.isEmpty()) {
          callback_(results);
        }
        deleteLater();
      });

  process->start(kdialog, args);
  return true;
}

void NativeFileDialog::fallbackToQtDialog(QWindow* parent_window,
                                          const QString& title,
                                          const QString& initial_folder,
                                          bool directory,
                                          bool multiple) {
  Q_UNUSED(parent_window);
  if (directory) {
    const QString folder = QFileDialog::getExistingDirectory(
        nullptr, title,
        initial_folder.isEmpty() ? QDir::homePath() : initial_folder,
        QFileDialog::ShowDirsOnly | QFileDialog::ReadOnly);
    if (!folder.isEmpty() && callback_) {
      callback_(QStringList{folder});
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
    if (!files.isEmpty() && callback_) {
      callback_(files);
    }
  }
  deleteLater();
}
