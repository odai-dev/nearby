// Copyright 2024 Google LLC
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

#include "internal/platform/implementation/linux/ble_l2cap_socket.h"

#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "internal/platform/implementation/crypto.h"
#include "internal/platform/logging.h"
#include "proto/mediums/ble_frames.pb.h"

namespace nearby {
namespace linux {

BleL2capInputStream::~BleL2capInputStream() {
  Close();
}

ExceptionOr<ByteArray> BleL2capInputStream::Read(std::int64_t size) {
  if (size <= 0) {
    return ExceptionOr<ByteArray>(ByteArray(std::string()));
  }
  if (!fd_raw_ || !fd_raw_->isValid()) {
    return {Exception::kIo};
  }

  std::vector<char> buffer(size);
  ssize_t rcvd = 0;

  while (rcvd < size) {
    pollfd pfd{};
    pfd.fd = fd_raw_->get();
    pfd.events = POLLIN;

    int r = poll(&pfd, 1, 1000);
    if (!fd_raw_ || !fd_raw_->isValid()) {
      return {Exception::kIo};
    }

    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {Exception::kIo};
    }

    if (r == 0) {
      // Timeout tick, continue
      continue;
    }

    if (pfd.revents & (POLLERR | POLLNVAL)) {
      return {Exception::kIo};
    }

    if (pfd.revents & (POLLIN | POLLHUP)) {
      auto bytes_read = recv(fd_raw_->get(), buffer.data() + rcvd, size - rcvd, 0);
      if (bytes_read > 0) {
        rcvd += bytes_read;
      } else if (bytes_read == 0) {
        // EOF / disconnected
        if (rcvd == 0) {
          return ExceptionOr<ByteArray>(ByteArray(std::string()));
        }
        break;
      } else {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        }
        return {Exception::kIo};
      }
    }
  }

  buffer.resize(rcvd);
  return ExceptionOr<ByteArray>{ByteArray(std::string(buffer.begin(), buffer.end()))};
}

Exception BleL2capInputStream::Close() {
  if (!fd_raw_ || !fd_raw_->isValid()) return {Exception::kSuccess};
  ::shutdown(fd_raw_->get(), SHUT_RDWR);
  fd_raw_->reset();
  return {Exception::kSuccess};
}
BleL2capOutputStream::~BleL2capOutputStream() {
  Close();
}

Exception BleL2capOutputStream::Write(absl::string_view data) {
  if (!fd_raw_ || !fd_raw_->isValid()) {
    return {Exception::kIo};
  }

  ssize_t sent = 0;

  while (sent < data.size()) {
    pollfd pfd{};
    pfd.fd = fd_raw_->get();
    pfd.events = POLLOUT;

    int r = poll(&pfd, 1, 1000);
    if (!fd_raw_ || !fd_raw_->isValid()) {
      return {Exception::kIo};
    }

    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {Exception::kIo};
    }

    if (r == 0) {
      continue;
    }

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      return {Exception::kIo};
    }

    if (pfd.revents & POLLOUT) {
      auto written = send(fd_raw_->get(), data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
      if (written < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        }
        return {Exception::kIo};
      }
      sent += written;
    }
  }
  return {Exception::kSuccess};
}

Exception BleL2capOutputStream::Close() {
  if (!fd_raw_ || !fd_raw_->isValid()) return {Exception::kSuccess};
  ::shutdown(fd_raw_->get(), SHUT_RDWR);
  fd_raw_->reset();
  return {Exception::kSuccess};
}

BleL2capSocket::BleL2capSocket(int fd,
                               api::ble::BlePeripheral::UniqueId peripheral_id,
                               std::string service_id)
    : fd_(std::make_shared<sdbus::UnixFd>(fd)),
      input_stream_(std::make_unique<BleL2capInputStream>(fd_)),
      output_stream_(std::make_unique<BleL2capOutputStream>(fd_)),
      peripheral_id_(peripheral_id) {}

BleL2capSocket::~BleL2capSocket() {
  Close();
}

Exception BleL2capSocket::Close() {
  if (!fd_ || !fd_->isValid()) return {Exception::kIo};
  ::shutdown(fd_->get(), SHUT_RDWR);
  fd_->reset();
  closed_ = true;
  return {Exception::kSuccess};
}

void BleL2capSocket::SetCloseNotifier(absl::AnyInvocable<void()> notifier) {}

bool BleL2capSocket::IsClosed() const {
  return closed_;
}

}  // namespace linux
}  // namespace nearby
