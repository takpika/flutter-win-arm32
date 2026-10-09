// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../plugins/worker_messenger.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <map>

using flutter::BinaryReply;
using flutter::winrt::plugins::WorkerMessenger;
using namespace std::chrono_literals;

class Platform : public flutter::BinaryMessenger {
 public:
  const std::thread::id thread = std::this_thread::get_id();
  std::map<std::string, flutter::BinaryMessageHandler> handlers;
  mutable BinaryReply outgoing;
  mutable std::vector<uint8_t> sent;
  mutable std::string sent_channel;
  bool Post(WorkerMessenger::Task task) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      tasks.push_back(std::move(task));
    }
    condition.notify_one();
    return true;
  }
  void Send(const std::string& channel, const uint8_t* data, size_t size,
            BinaryReply reply) const override {
    assert(std::this_thread::get_id() == thread);
    sent_channel = channel;
    sent.assign(data, data + size);
    outgoing = std::move(reply);
  }
  void SetMessageHandler(const std::string& channel,
                         flutter::BinaryMessageHandler handler) override {
    assert(std::this_thread::get_id() == thread);
    if (handler)
      handlers[channel] = std::move(handler);
    else
      handlers.erase(channel);
  }
  void Emit(const std::string& channel, const std::vector<uint8_t>& bytes,
            BinaryReply reply) {
    auto handler = handlers.at(channel);
    handler(bytes.data(), bytes.size(), std::move(reply));
  }
  void PumpUntil(const std::function<bool()>& done) {
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done()) {
      WorkerMessenger::Task task;
      {
        std::unique_lock<std::mutex> lock(mutex);
        assert(condition.wait_until(lock, deadline,
                                    [&] { return !tasks.empty(); }));
        task = std::move(tasks.front());
        tasks.pop_front();
      }
      task();
    }
  }

 private:
  std::mutex mutex;
  std::condition_variable condition;
  std::deque<WorkerMessenger::Task> tasks;
};

struct Owner {
  explicit Owner(std::promise<void>& signal) : signal(signal) {}
  ~Owner() { signal.set_value(); }
  std::promise<void>& signal;
};

int main() {
  Platform platform;
  std::promise<void> owner_destroyed;
  auto released = owner_destroyed.get_future();
  auto owner = std::make_shared<Owner>(owner_destroyed);
  std::weak_ptr<Owner> weak = owner;
  std::thread::id worker_thread;
  std::promise<void> cleaned;
  auto cleanup = cleaned.get_future();
  auto messenger = std::make_unique<WorkerMessenger>(
      &platform, [&](auto task) { return platform.Post(std::move(task)); },
      owner,
      [&] {
        worker_thread = std::this_thread::get_id();
        return true;
      },
      [&] { cleaned.set_value(); });
  assert(worker_thread != platform.thread);
  std::promise<void> proceed;
  auto gate = proceed.get_future().share();
  std::vector<uint8_t> received;
  messenger->SetMessageHandler(
      "core", [&](const uint8_t* data, size_t size, BinaryReply reply) {
        assert(std::this_thread::get_id() == worker_thread);
        gate.wait();
        assert(size == 1);
        received.push_back(data[0]);
        reply(data, size);
      });
  int responses = 0;
  for (uint8_t i = 1; i <= 3; ++i) {
    std::vector<uint8_t> bytes{i};
    platform.Emit("core", bytes, [&, i](const uint8_t* data, size_t size) {
      assert(std::this_thread::get_id() == platform.thread);
      assert(size == 1 && data[0] == i);
      ++responses;
    });
    bytes[0] = 99;
  }
  proceed.set_value();
  platform.PumpUntil([&] { return responses == 3; });
  assert(received == std::vector<uint8_t>({1, 2, 3}));

  // Native exceptions must complete a request as failure, without killing the
  // worker or returning a fake successful method result.
  messenger->SetMessageHandler("throws",
                               [](const uint8_t*, size_t, BinaryReply) {
                                 throw std::runtime_error("synthetic failure");
                               });
  platform.Emit("throws", {}, [&](const uint8_t* data, size_t size) {
    assert(!data && !size);
    ++responses;
  });
  platform.PumpUntil([&] { return responses == 4; });

  std::promise<void> outbound_done;
  auto outbound = outbound_done.get_future();
  std::vector<uint8_t> outgoing{4, 5};
  auto send = messenger->BindSender();
  auto post = messenger->BindTaskPoster();
  std::promise<void> posted;
  auto posted_result = posted.get_future();
  assert(post(
      [&] {
        assert(std::this_thread::get_id() == worker_thread);
        posted.set_value();
      },
      [] { assert(false); }));
  assert(posted_result.wait_for(5s) == std::future_status::ready);
  assert(send("outgoing", outgoing, [&](const uint8_t* data, size_t size) {
    assert(std::this_thread::get_id() == worker_thread);
    assert(size == 2 && data[0] == 7 && data[1] == 8);
    outbound_done.set_value();
  }));
  outgoing[0] = 99;
  platform.PumpUntil([&] { return static_cast<bool>(platform.outgoing); });
  assert(platform.sent == std::vector<uint8_t>({4, 5}));
  auto callback = std::move(platform.outgoing);
  uint8_t answer[] = {7, 8};
  callback(answer, sizeof(answer));
  callback = nullptr;
  answer[0] = 99;
  assert(outbound.wait_for(5s) == std::future_status::ready);

  std::promise<void> entered, finish;
  auto entered_future = entered.get_future();
  auto finish_future = finish.get_future().share();
  messenger->SetMessageHandler(
      "slow", [&, finish_future](const uint8_t*, size_t, BinaryReply reply) {
        assert(std::this_thread::get_id() == worker_thread);
        entered.set_value();
        finish_future.wait();
        assert(!weak.expired());
        reply(nullptr, 0);
      });
  platform.Emit("slow", {}, [&](const uint8_t*, size_t) { ++responses; });
  assert(entered_future.wait_for(5s) == std::future_status::ready);
  std::promise<void> canceled;
  auto canceled_result = canceled.get_future();
  assert(post([] { assert(false); },
              [&] {
                assert(std::this_thread::get_id() == worker_thread);
                canceled.set_value();
              }));
  // This queued request must not invoke a disposed package API after Close.
  platform.Emit("core", {6}, [&](const uint8_t* data, size_t size) {
    assert(!data && !size);
    ++responses;
  });
  owner.reset();
  messenger->Close();
  messenger.reset();
  assert(!send("after-close", {}, nullptr));
  assert(!post([] { assert(false); }, [] { assert(false); }));
  assert(!weak.expired() && platform.handlers.empty());
  finish.set_value();
  platform.PumpUntil([&] { return responses == 6; });
  assert(cleanup.wait_for(5s) == std::future_status::ready);
  assert(canceled_result.wait_for(5s) == std::future_status::ready);
  // State may remain alive until the final canceled task leaves the worker.
  for (int i = 0; i < 100 && !weak.expired(); ++i) std::this_thread::yield();
  assert(received == std::vector<uint8_t>({1, 2, 3}));
  bool failed_initialization = false;
  try {
    WorkerMessenger unavailable(
        &platform, [&](auto task) { return platform.Post(std::move(task)); },
        {}, [] { return false; });
  } catch (const std::runtime_error&) {
    failed_initialization = true;
  }
  assert(failed_initialization && platform.handlers.empty());
  std::cout
      << "Worker messenger: original handlers, serialization, owned payloads, "
         "thread affinity, failure and shutdown lifetime PASS\n";
}
