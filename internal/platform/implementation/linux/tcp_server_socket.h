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

#ifndef PLATFORM_IMPL_LINUX_TCP_SERVER_SOCKET_H_
#define PLATFORM_IMPL_LINUX_TCP_SERVER_SOCKET_H_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <optional>

#include <sdbus-c++/Types.h>

#include "internal/platform/exception.h"
#include "internal/platform/implementation/linux/stream.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {

inline void ConfigureHighSpeedTcpSocket(int sock) {
  if (sock < 0) return;

  int nodelay = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

#ifdef TCP_QUICKACK
  int quickack = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
#endif

  int buf_size = 4 * 1024 * 1024;  // 4MB buffer
  setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
  setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));

  int keepalive = 1;
  setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
}

class TCPSocket {
 public:
  explicit TCPSocket(sdbus::UnixFd fd)
      : closed_(false),
        fd_(std::make_shared<sdbus::UnixFd>(std::move(fd))),
        output_stream_(fd_),
        input_stream_(fd_) {}

  explicit TCPSocket(std::shared_ptr<sdbus::UnixFd> fd)
      : closed_(false),
        fd_(std::move(fd)),
        output_stream_(fd_),
        input_stream_(fd_) {}

  static std::optional<TCPSocket> Connect(const std::string& ip_address,
                                          int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      LOG(ERROR) << __func__
                 << ": Error opening socket: " << std::strerror(errno);
      return std::nullopt;
    }

    ConfigureHighSpeedTcpSocket(sock);

    LOG(INFO) << __func__ << ": Connecting to " << ip_address << ":"
              << port;
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = inet_addr(ip_address.c_str());
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    auto ret =
        connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret < 0) {
      LOG(ERROR) << __func__ << ": Error connecting to socket: "
                 << std::strerror(errno);
      close(sock);
      return std::nullopt;
    }

    return TCPSocket(sdbus::UnixFd(sock, sdbus::adopt_fd));
  }

  InputStream& GetInputStream() { return input_stream_; }
  OutputStream& GetOutputStream() { return output_stream_; }

  Exception Close() {
    if (closed_) return {Exception::kSuccess};

    closed_ = true;
    if (fd_ && fd_->isValid()) {
      ::shutdown(fd_->get(), SHUT_RDWR);
    }
    input_stream_.Close();
    output_stream_.Close();
    fd_.reset();

    return {Exception::kSuccess};
  }

 private:
  bool closed_;
  std::shared_ptr<sdbus::UnixFd> fd_;
  OutputStream output_stream_;
  InputStream input_stream_;
};

class TCPServerSocket {
 public:
  explicit TCPServerSocket(int fd) : fd_(fd) {}

  static std::optional<TCPServerSocket> Listen(
      std::optional<const std::reference_wrapper<std::string>> ip_address,
      int port) {
    auto sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      LOG(ERROR) << __func__
                         << ": Error opening socket: " << std::strerror(errno);
      return std::nullopt;
    }

    int reuse_addr = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr,
                   sizeof(reuse_addr)) < 0) {
      LOG(ERROR) << __func__ << ": Error setting SO_REUSEADDR: "
                 << std::strerror(errno);
      close(sock);
      return std::nullopt;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (ip_address.has_value())
      addr.sin_addr.s_addr = inet_addr(ip_address->get().c_str());
    else
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    auto ret =
        bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret < 0) {
      LOG(ERROR) << __func__ << ": Error binding to socket: "
                         << std::strerror(errno);
      close(sock);
      return std::nullopt;
    }

    ret = listen(sock, SOMAXCONN);
    if (ret < 0) {
      LOG(ERROR) << __func__ << ": Error listening on socket: "
                         << std::strerror(errno);
      close(sock);
      return std::nullopt;
    }

    return TCPServerSocket(sock);
  }
  std::optional<TCPSocket> Accept() {
    int server_fd = fd_.get();
    if (server_fd < 0) {
      return std::nullopt;
    }

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    auto conn =
        accept(server_fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    if (conn < 0) {
      if (errno == EBADF || errno == EINVAL || errno == ENOTSOCK) {
        VLOG(1) << __func__
                << ": Server socket closed while waiting for Accept().";
        return std::nullopt;
      }
      LOG(ERROR) << __func__
                         << ": Error accepting incoming connections on socket "
                         << server_fd << ": " << std::strerror(errno);
      return std::nullopt;
    }

    ConfigureHighSpeedTcpSocket(conn);

    return TCPSocket(sdbus::UnixFd(conn));
  };

  Exception Close() {
    int fd = fd_.release();
    if (fd < 0) {
      return {Exception::kSuccess};
    }
    if (shutdown(fd, SHUT_RDWR) < 0 && errno != ENOTCONN && errno != EINVAL &&
        errno != EBADF) {
      VLOG(1) << __func__ << ": shutdown(" << fd
              << ") failed: " << std::strerror(errno);
    }
    auto ret = close(fd);
    if (ret < 0) {
      if (errno == EBADF) return {Exception::kSuccess};
      LOG(ERROR) << __func__ << ": Error closing socket " << fd << ": "
                         << std::strerror(errno);
      return {Exception::kFailed};
    }

    return {Exception::kSuccess};
  };

  int GetPort() const {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    auto ret =
        getsockname(fd_.get(), reinterpret_cast<struct sockaddr*>(&sin), &len);
    if (ret < 0) {
      LOG(ERROR) << __func__
                         << ": Error getting information for socket "
                         << fd_.get() << ": " << std::strerror(errno);
      return 0;
    }

    return ntohs(sin.sin_port);
  }

 private:
  sdbus::UnixFd fd_;
};
}  // namespace linux
}  // namespace nearby

#endif
