// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../sdk_operation_reply.h"

#include <cassert>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using flutter::winrt::plugins::firebase_messaging::SdkOperationReply;
using Bridge = SdkOperationReply;

static void CompletionAndReleaseBeforeStartReturns() {
  auto owner = std::make_shared<int>(1);
  std::weak_ptr<int> retained = owner;
  bool delivered = false;
  std::vector<uint8_t> posted;
  const auto status = Bridge::Start(
      [&](Bridge::Context* context) noexcept -> Bridge::Status {
        std::thread worker([&] {
          std::vector<uint8_t> borrowed{0x61, 0, 0xe6, 0x97, 0xa5};
          Bridge::Complete(context, 0, borrowed.data(), borrowed.size());
          borrowed.assign(borrowed.size(), 0);
          Bridge::Release(context);
        });
        worker.join();
        assert(!retained.expired());  // Caller still executes SDK module code.
        return 0;
      },
      [&](Bridge::Status result, Bridge::Bytes bytes) {
        assert(result == 0);
        delivered = true;
        posted = std::move(bytes);
      },
      std::move(owner));
  assert(status == 0 && delivered && retained.expired());
  assert((posted == std::vector<uint8_t>{0x61, 0, 0xe6, 0x97, 0xa5}));
}

static void DelayedCompletionAfterEngineTeardown() {
  struct Engine {
    unsigned replies = 0;
  };
  auto engine = std::make_shared<Engine>();
  std::weak_ptr<Engine> weak_engine = engine;
  auto owner = std::make_shared<int>(1);
  std::weak_ptr<int> retained = owner;
  Bridge::Context* pending = nullptr;
  bool native_callback_ran = false;
  assert(Bridge::Start(
             [&](Bridge::Context* context) noexcept -> Bridge::Status {
               pending = context;
               return 0;
             },
             [&, weak_engine](Bridge::Status result, Bridge::Bytes bytes) {
               native_callback_ran = true;
               assert(result == 0 && bytes.size() == 3);
               if (auto active = weak_engine.lock()) active->replies++;
             },
             std::move(owner)) == 0);
  engine.reset();
  assert(weak_engine.expired() && !retained.expired());
  std::thread worker([&] {
    const uint8_t borrowed[] = {1, 2, 3};
    Bridge::Complete(pending, 0, borrowed, sizeof(borrowed));
    assert(!retained.expired());  // Completion does not release native state.
    Bridge::Release(pending);
  });
  worker.join();
  assert(native_callback_ran && retained.expired());
}

static void CopyPrecedesPostedReplyAndSdkWipe() {
  Bridge::Context* pending = nullptr;
  std::function<void()> platform_task;
  Bridge::Bytes received;
  Bridge::Start(
      [&](Bridge::Context* context) noexcept -> Bridge::Status {
        pending = context;
        return 0;
      },
      [&](Bridge::Status status, Bridge::Bytes bytes) {
        assert(status == 0);
        platform_task = [&, bytes = std::move(bytes)] { received = bytes; };
      },
      {});
  std::vector<uint8_t> borrowed{3, 4, 5, 6};
  Bridge::Complete(pending, 0, borrowed.data(), borrowed.size());
  borrowed.assign(borrowed.size(), 0);
  Bridge::Release(pending);
  platform_task();
  assert((received == Bridge::Bytes{3, 4, 5, 6}));
}

static void RejectedStartAndNonSuccessPayloads() {
  for (Bridge::Status rejected : {Bridge::Status(1), Bridge::Status(-1)}) {
    auto owner = std::make_shared<int>(1);
    std::weak_ptr<int> retained = owner;
    unsigned replies = 0;
    assert(Bridge::Start(
               [rejected](Bridge::Context*) noexcept { return rejected; },
               [&](Bridge::Status status, Bridge::Bytes bytes) {
                 ++replies;
                 assert(status == rejected && bytes.empty());
               },
               std::move(owner)) == rejected);
    assert(replies == 1 && retained.expired());
  }
  for (Bridge::Status result : {Bridge::Status(0), Bridge::Status(-1),
                               static_cast<Bridge::Status>(UINT32_C(0x800704c7))}) {
    unsigned replies = 0;
    Bridge::Start(
        [result](Bridge::Context* context) noexcept -> Bridge::Status {
          const uint8_t token[] = {7, 8};
          Bridge::Complete(context, result, result == 0 ? nullptr : token,
                           result == 0 ? 0 : sizeof(token));
          Bridge::Release(context);
          return 0;
        },
        [&](Bridge::Status status, Bridge::Bytes bytes) {
          ++replies;
          assert(status == result && bytes.empty());
        },
        {});
    assert(replies == 1);
  }
}

static void CallbackCannotThrowAcrossCAbi() {
  unsigned replies = 0;
  Bridge::Start(
      [](Bridge::Context* context) noexcept -> Bridge::Status {
        Bridge::Complete(context, 0, nullptr, 5);
        Bridge::Release(context);
        return 0;
      },
      [&](Bridge::Status status, Bridge::Bytes bytes) {
        ++replies;
        assert(status == static_cast<Bridge::Status>(UINT32_C(0x80070057)));
        assert(bytes.empty());
        throw std::runtime_error("closed platform dispatcher");
      },
      {});
  assert(replies == 1);
}

int main() {
  CompletionAndReleaseBeforeStartReturns();
  DelayedCompletionAfterEngineTeardown();
  CopyPrecedesPostedReplyAndSdkWipe();
  RejectedStartAndNonSuccessPayloads();
  CallbackCannotThrowAcrossCAbi();
}
