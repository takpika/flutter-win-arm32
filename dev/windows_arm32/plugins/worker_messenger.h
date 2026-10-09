// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <flutter/binary_messenger.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace flutter::winrt::plugins {

// Keeps a package's original generated channel handlers on one worker, while
// all calls into Flutter's messenger run on its platform thread. Registration
// and Close are platform-thread operations. The owner remains alive for any
// handler already executing when the host shuts down.
class WorkerMessenger final : public BinaryMessenger {
 public:
  using Task = std::function<void()>;
  using PlatformPost = std::function<bool(Task)>;
  WorkerMessenger(
      BinaryMessenger* target, PlatformPost post, std::shared_ptr<void> owner,
      std::function<bool()> initialize = [] { return true; },
      Task uninitialize = [] {})
      : state_(std::make_shared<State>()) {
    if (!target || !post)
      throw std::invalid_argument("Invalid worker messenger");
    state_->target = target;
    state_->post = std::move(post);
    state_->owner = std::move(owner);
    std::promise<bool> ready;
    auto result = ready.get_future();
    std::thread([state = state_, initialize = std::move(initialize),
                 uninitialize = std::move(uninitialize),
                 ready = std::move(ready)]() mutable {
      bool initialized = false;
      try {
        initialized = initialize();
      } catch (...) {
      }
      ready.set_value(initialized);
      if (!initialized) return;
      for (;;) {
        Job job;
        {
          std::unique_lock<std::mutex> lock(state->mutex);
          state->condition.wait(
              lock, [&] { return state->closing || !state->jobs.empty(); });
          if (state->closing && state->jobs.empty()) break;
          job = std::move(state->jobs.front());
          state->jobs.pop_front();
        }
        try {
          job.run();
        } catch (...) {
          if (job.cancel) job.cancel();
        }
      }
      uninitialize();
    }).detach();
    if (!result.get())
      throw std::runtime_error("Native plugin worker initialization failed");
  }
  ~WorkerMessenger() override { Close(); }
  WorkerMessenger(const WorkerMessenger&) = delete;
  WorkerMessenger& operator=(const WorkerMessenger&) = delete;

  void Close() {
    auto state = state_;
    if (!state->active.exchange(false)) return;
    for (const auto& name : channels_)
      state->target->SetMessageHandler(name, nullptr);
    channels_.clear();
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->closing = true;
    }
    // Drain/cancel on the worker itself: package callbacks must never migrate
    // to the platform thread merely because shutdown has begun.
    state->condition.notify_one();
  }

  using Sender = std::function<bool(const std::string&,
                                    const std::vector<uint8_t>&, BinaryReply)>;
  using WorkerPost = std::function<bool(Task, Task)>;
  WorkerPost BindTaskPoster() const {
    std::weak_ptr<State> weak = state_;
    return [weak](Task task, Task canceled) {
      auto state = weak.lock();
      if (!state || !state->active) return false;
      return Enqueue(state, {[state, task = std::move(task), canceled] {
                              if (state->active) task();
                              else if (canceled) canceled();
                            },
                            std::move(canceled)});
    };
  }
  Sender BindSender() const {
    std::weak_ptr<State> weak = state_;
    return [weak](const std::string& channel, const std::vector<uint8_t>& bytes,
                  BinaryReply reply) {
      auto state = weak.lock();
      if (!state || !state->active) return false;
      SendMessage(state, channel, bytes.data(), bytes.size(), std::move(reply));
      return true;
    };
  }
  void Send(const std::string& channel, const uint8_t* bytes, size_t size,
            BinaryReply reply = nullptr) const override {
    SendMessage(state_, channel, bytes, size, std::move(reply));
  }

  void SetMessageHandler(const std::string& channel,
                         BinaryMessageHandler handler) override {
    auto state = state_;
    if (!state->active) throw std::logic_error("Worker messenger is closed");
    if (!handler) {
      channels_.erase(channel);
      state->target->SetMessageHandler(channel, nullptr);
      return;
    }
    channels_.insert(channel);
    state->target->SetMessageHandler(
        channel, [state, handler = std::move(handler)](
                     const uint8_t* data, size_t size, BinaryReply reply) {
          auto respond = [state, reply](const uint8_t* bytes, size_t count) {
            auto payload = Copy(bytes, count);
            // The reply itself carries the engine's weak lifetime guard.
            state->post([reply, payload = std::move(payload)] {
              reply(payload.empty() ? nullptr : payload.data(), payload.size());
            });
          };
          auto payload =
              std::make_shared<std::vector<uint8_t>>(Copy(data, size));
          Job job{[state, handler, payload, respond] {
                    if (!state->active) {
                      respond(nullptr, 0);
                      return;
                    }
                    handler(payload->empty() ? nullptr : payload->data(),
                            payload->size(), respond);
                  },
                  [respond] { respond(nullptr, 0); }};
          if (!Enqueue(state, job)) job.cancel();
        });
  }

 private:
  struct Job {
    Task run, cancel;
  };
  struct State {
    BinaryMessenger* target = nullptr;
    PlatformPost post;
    std::shared_ptr<void> owner;
    std::atomic<bool> active{true};
    std::mutex mutex;
    std::condition_variable condition;
    bool closing = false;
    std::deque<Job> jobs;
  };
  static void SendMessage(const std::shared_ptr<State>& state,
                          const std::string& channel, const uint8_t* bytes,
                          size_t size, BinaryReply reply) {
    auto payload = Copy(bytes, size);
    auto complete = [state, reply](std::vector<uint8_t> response) {
      if (!reply) return;
      auto owned = std::make_shared<std::vector<uint8_t>>(std::move(response));
      Job job{[state, reply, owned] {
                if (state->active)
                  reply(owned->empty() ? nullptr : owned->data(),
                        owned->size());
                else
                  reply(nullptr, 0);
              },
              [reply] { reply(nullptr, 0); }};
      Enqueue(state, job);
    };
    if (!state->active ||
        !state->post([state, channel, payload = std::move(payload), complete,
                      reply] {
          if (!state->active) {
            complete({});
            return;
          }
          state->target->Send(
              channel, payload.empty() ? nullptr : payload.data(),
              payload.size(),
              reply
                  ? BinaryReply([complete](const uint8_t* data, size_t count) {
                      complete(Copy(data, count));
                    })
                  : nullptr);
        }))
      complete({});
  }
  static std::vector<uint8_t> Copy(const uint8_t* data, size_t size) {
    if (!size) return {};
    if (!data) throw std::invalid_argument("Missing platform message payload");
    return {data, data + size};
  }
  static bool Enqueue(const std::shared_ptr<State>& state, const Job& job) {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->closing) return false;
      state->jobs.push_back(job);
    }
    state->condition.notify_one();
    return true;
  }
  std::shared_ptr<State> state_;
  std::set<std::string> channels_;
};
}  // namespace flutter::winrt::plugins
