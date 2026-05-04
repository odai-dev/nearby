// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>

#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>

#include "absl/synchronization/mutex.h"
#include "internal/platform/implementation/device_info.h"
#include "internal/platform/implementation/linux/device_info.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {

namespace {

std::string GetEnvOrDefault(const char* key, std::string fallback) {
  const char* value = std::getenv(key);
  if (value == nullptr || *value == '\0') {
    return fallback;
  }
  return value;
}

std::string GetHomeDirectory() {
  return GetEnvOrDefault("HOME", "/tmp");
}

FilePath BuildPathFromBase(const std::string& base,
                           std::initializer_list<std::string> components) {
  std::filesystem::path path(base);
  for (const auto& component : components) {
    path /= component;
  }
  return FilePath(path.string());
}

std::string GetConfigHome() {
  return GetEnvOrDefault(
      "XDG_CONFIG_HOME",
      (std::filesystem::path(GetHomeDirectory()) / ".config").string());
}

std::string GetStateHome() {
  return GetEnvOrDefault(
      "XDG_STATE_HOME",
      (std::filesystem::path(GetHomeDirectory()) / ".local" / "state")
          .string());
}

std::string GetRuntimeBase() {
  const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
  if (runtime_dir != nullptr && *runtime_dir != '\0') {
    return runtime_dir;
  }

  const char* tmp_dir = std::getenv("TMPDIR");
  if (tmp_dir != nullptr && *tmp_dir != '\0') {
    return tmp_dir;
  }

  return "/tmp";
}

FilePath GetDownloadDirectory() {
  const char* xdg_download_dir = std::getenv("XDG_DOWNLOAD_DIR");
  if (xdg_download_dir != nullptr && *xdg_download_dir != '\0') {
    return FilePath(std::string(xdg_download_dir));
  }
  return BuildPathFromBase(GetHomeDirectory(), {"Downloads"});
}

}  // namespace

void CurrentUserSession::RegisterScreenLockedListener(
    absl::string_view listener_name,
    std::function<void(api::DeviceInfo::ScreenStatus)> callback) {
  absl::MutexLock l(&screen_lock_listeners_mutex_);
  screen_lock_listeners_[listener_name] = std::move(callback);
}

void CurrentUserSession::UnregisterScreenLockedListener(
    absl::string_view listener_name) {
  absl::MutexLock l(&screen_lock_listeners_mutex_);
  screen_lock_listeners_.erase(listener_name);
}

void CurrentUserSession::onLock() {
  absl::ReaderMutexLock l(&screen_lock_listeners_mutex_);
  for (auto &[_, callback] : screen_lock_listeners_) {
    callback(api::DeviceInfo::ScreenStatus::kLocked);
  }
}

void CurrentUserSession::onUnlock() {
  absl::ReaderMutexLock l(&screen_lock_listeners_mutex_);
  for (auto &[_, callback] : screen_lock_listeners_) {
    callback(api::DeviceInfo::ScreenStatus::kUnlocked);
  }
}

DeviceInfo::DeviceInfo(std::shared_ptr<sdbus::IConnection> system_bus)
    : system_bus_(std::move(system_bus)) {}

std::string LoginManager::GetCurrentSessionPath() {
  try {
    auto session_path = GetSessionByPID(static_cast<uint32_t>(::getpid()));
    if (!session_path.empty()) {
      return std::string(session_path);
    }
  } catch (const sdbus::Error& e) {
    LOG(WARNING) << __func__
                 << ": GetSessionByPID failed, falling back to other session "
                    "resolution strategies: "
                 << e.getName() << " - " << e.getMessage();
  }

  const char* session_id = std::getenv("XDG_SESSION_ID");
  if (session_id != nullptr && *session_id != '\0') {
    try {
      auto session_path = GetSession(std::string(session_id));
      if (!session_path.empty()) {
        return std::string(session_path);
      }
    } catch (const sdbus::Error& e) {
      LOG(WARNING) << __func__
                   << ": GetSession failed for XDG_SESSION_ID, falling back "
                      "to /org/freedesktop/login1/session/auto: "
                   << e.getName() << " - " << e.getMessage();
    }
  }

  return "/org/freedesktop/login1/session/auto";
}

std::optional<std::string> DeviceInfo::GetOsDeviceName() const {
  char hostname[256] = {};
  if (::gethostname(hostname, sizeof(hostname)) == 0 && hostname[0] != '\0') {
    return std::string(hostname);
  }

  const char* env_hostname = std::getenv("HOSTNAME");
  if (env_hostname != nullptr && *env_hostname != '\0') {
    return std::string(env_hostname);
  }

  return std::nullopt;
}

api::DeviceInfo::DeviceType DeviceInfo::GetDeviceType() const {
  return api::DeviceInfo::DeviceType::kLaptop;
}


std::optional<FilePath> DeviceInfo::GetDownloadPath() const {
  return GetDownloadDirectory();
}

std::optional<FilePath> DeviceInfo::GetLocalAppDataPath() const {
  return BuildPathFromBase(GetConfigHome(), {"Google Nearby"});
}

std::optional<FilePath> DeviceInfo::GetTemporaryPath() const {
  return BuildPathFromBase(GetRuntimeBase(), {"Google Nearby"});
}

std::optional<FilePath> DeviceInfo::GetLogPath() const {
  return BuildPathFromBase(GetStateHome(), {"Google Nearby", "logs"});
}

std::optional<FilePath> DeviceInfo::GetCrashDumpPath() const {
  return BuildPathFromBase(GetStateHome(), {"Google Nearby", "crashes"});
}

bool DeviceInfo::IsScreenLocked() const {
  return false;
}

bool DeviceInfo::PreventSleep() {
  return true;
}

bool DeviceInfo::AllowSleep() {
  return true;
}

}  // namespace linux
}  // namespace nearby
