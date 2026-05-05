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

#include "internal/platform/implementation/linux/tcp_server_socket.h"

#include <chrono>
#include <future>
#include <thread>

#include "gtest/gtest.h"

namespace nearby::linux {
namespace {

TEST(TcpServerSocketTest, ListenCloseListenOnSamePortSucceeds) {
  auto first = TCPServerSocket::Listen(std::nullopt, /*port=*/0);
  ASSERT_TRUE(first.has_value());
  int port = first->GetPort();
  ASSERT_GT(port, 0);

  EXPECT_TRUE(first->Close().Ok());

  auto second = TCPServerSocket::Listen(std::nullopt, port);
  ASSERT_TRUE(second.has_value());
  EXPECT_TRUE(second->Close().Ok());
}

TEST(TcpServerSocketTest, CloseIsIdempotent) {
  auto server = TCPServerSocket::Listen(std::nullopt, /*port=*/0);
  ASSERT_TRUE(server.has_value());

  EXPECT_TRUE(server->Close().Ok());
  EXPECT_TRUE(server->Close().Ok());
}

TEST(TcpServerSocketTest, AcceptReturnsNulloptWhenSocketIsClosed) {
  auto server = TCPServerSocket::Listen(std::nullopt, /*port=*/0);
  ASSERT_TRUE(server.has_value());

  std::promise<bool> accepted;
  std::future<bool> accepted_future = accepted.get_future();
  std::thread accept_thread([&server, &accepted]() {
    auto socket = server->Accept();
    accepted.set_value(socket.has_value());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_TRUE(server->Close().Ok());

  accept_thread.join();
  EXPECT_FALSE(accepted_future.get());
}

}  // namespace
}  // namespace nearby::linux
