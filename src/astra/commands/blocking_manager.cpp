// Copyright 2026 AstraDB Project
// Licensed under the Apache License, Version 2.0

#include "blocking_manager.hpp"

#include <mutex>

#include "astra/base/logging.hpp"
#include "astra/network/connection.hpp"
#include "astra/protocol/resp/resp_builder.hpp"

namespace astra::commands {

BlockingManager::BlockingManager(asio::io_context& io_context)
    : io_context_(io_context) {}

void BlockingManager::AddBlockedClient(const std::string& key,
                                       BlockedClient client) {
  // Debug: log the key parameter
  ASTRADB_LOG_DEBUG("AddBlockedClient called with key='{}', client_id={}", key,
                    client.client_id);

  // Create timer for this client
  auto timer = std::make_unique<asio::steady_timer>(io_context_);

  if (client.timeout_seconds > 0) {
    timer->expires_after(std::chrono::milliseconds(
        static_cast<int64_t>(client.timeout_seconds * 1000)));

    // Set timer callback
    timer->async_wait(
        [this, client_id = client.client_id](const asio::error_code& ec) {
          if (!ec) {
            // Timeout expired, return nil response
            HandleTimeout(client_id);
          }
        });
  }

  // Add to timer map
  {
    std::unique_lock<std::shared_mutex> lock(timers_mutex_);
    timeout_timers_[client.client_id] = std::move(timer);
  }

  // Add to wait queue
  {
    std::unique_lock<std::shared_mutex> lock(wait_queues_mutex_);
    wait_queues_[key].push_back(std::move(client));
  }

  ASTRADB_LOG_DEBUG("Added blocked client {} for key {} with timeout {}s",
                    client.client_id, key, client.timeout_seconds);
}

void BlockingManager::RemoveBlockedClient(uint64_t client_id) {
  // Remove from timer map
  {
    std::unique_lock<std::shared_mutex> lock(timers_mutex_);
    auto timer_it = timeout_timers_.find(client_id);
    if (timer_it != timeout_timers_.end()) {
      // Cancel timer
      asio::error_code ec;
      timer_it->second->cancel(ec);
      if (ec) {
        ASTRADB_LOG_WARN("Failed to cancel timer for client {}: {}", client_id,
                         ec.message());
      }
      timeout_timers_.erase(timer_it);
    }
  }

  // Remove from wait queues
  {
    std::unique_lock<std::shared_mutex> lock(wait_queues_mutex_);
    for (auto& [key, queue] : wait_queues_) {
      auto it = std::remove_if(queue.begin(), queue.end(),
                               [client_id](const BlockedClient& c) {
                                 return c.client_id == client_id;
                               });
      if (it != queue.end()) {
        queue.erase(it, queue.end());
        ASTRADB_LOG_DEBUG("Removed blocked client {} from queue for key {}",
                          client_id, key);
        break;
      }
    }
  }
}

void BlockingManager::WakeUpBlockedClients(const std::string& key) {
  std::unique_lock<std::shared_mutex> lock(wait_queues_mutex_);

  auto it = wait_queues_.find(key);
  if (it == wait_queues_.end() || it->second.empty()) {
    return;
  }

  // Wake up the first waiting client (FIFO)
  auto client = std::move(it->second.front());
  it->second.pop_front();

  // Clean up empty queues
  if (it->second.empty()) {
    wait_queues_.erase(it);
  }

  // Cancel timeout timer
  {
    std::unique_lock<std::shared_mutex> timer_lock(timers_mutex_);
    auto timer_it = timeout_timers_.find(client.client_id);
    if (timer_it != timeout_timers_.end()) {
      asio::error_code ec;
      timer_it->second->cancel(ec);
      timeout_timers_.erase(timer_it);
    }
  }

  // Unlock before processing to avoid deadlock
  lock.unlock();

  // Process the blocked command with new data
  ProcessBlockedCommand(client);
}

void BlockingManager::CleanExpiredRequests() {
  // This method is called periodically to clean up any stale entries
  // Most cleanup happens via timeout callbacks, but this ensures safety

  std::unique_lock<std::shared_mutex> lock(wait_queues_mutex_);
  auto now = std::chrono::steady_clock::now();

  for (auto& [key, queue] : wait_queues_) {
    auto it = queue.begin();
    while (it != queue.end()) {
      auto elapsed =
          std::chrono::duration<double>(now - it->start_time).count();
      if (elapsed > it->timeout_seconds && it->timeout_seconds > 0) {
        ASTRADB_LOG_DEBUG("Cleaning up expired blocked client {} for key {}",
                          it->client_id, key);

        // Remove from timer map
        {
          std::unique_lock<std::shared_mutex> timer_lock(timers_mutex_);
          timeout_timers_.erase(it->client_id);
        }

        // Send nil response via connection
        if (it->connection) {
          auto result = it->callback(astra::protocol::RespValue(
              astra::protocol::RespType::kNullBulkString));
          auto builder = astra::protocol::RespBuilder::Build(result);
          it->connection->Send(builder);
        }

        it = queue.erase(it);
      } else {
        ++it;
      }
    }
  }
}

size_t BlockingManager::GetBlockedClientCount() const {
  std::shared_lock<std::shared_mutex> lock(wait_queues_mutex_);
  size_t count = 0;
  for (const auto& [key, queue] : wait_queues_) {
    count += queue.size();
  }
  return count;
}

bool BlockingManager::IsClientBlocked(uint64_t client_id) const {
  std::shared_lock<std::shared_mutex> lock(wait_queues_mutex_);
  for (const auto& [key, queue] : wait_queues_) {
    for (const auto& client : queue) {
      if (client.client_id == client_id) {
        return true;
      }
    }
  }
  return false;
}

void BlockingManager::HandleTimeout(uint64_t client_id) {
  ASTRADB_LOG_DEBUG("Timeout for blocked client {}", client_id);

  // Remove from wait queues
  std::function<void(astra::protocol::RespValue)> callback;
  astra::network::Connection* connection = nullptr;
  {
    std::unique_lock<std::shared_mutex> lock(wait_queues_mutex_);
    for (auto& [key, queue] : wait_queues_) {
      auto it = std::find_if(queue.begin(), queue.end(),
                             [client_id](const BlockedClient& c) {
                               return c.client_id == client_id;
                             });
      if (it != queue.end()) {
        callback = it->callback;
        connection = it->connection;
        queue.erase(it);

        // Clean up empty queues
        if (queue.empty()) {
          wait_queues_.erase(key);
        }
        break;
      }
    }
  }

  // Remove from timer map
  {
    std::unique_lock<std::shared_mutex> lock(timers_mutex_);
    timeout_timers_.erase(client_id);
  }

  // Send nil response via connection
  if (connection) {
    ASTRADB_LOG_DEBUG("HandleTimeout: Sending nil response via connection");
    auto builder = astra::protocol::RespBuilder::Build(
        astra::protocol::RespValue(astra::protocol::RespType::kNullBulkString));
    connection->Send(builder);
  } else {
    ASTRADB_LOG_ERROR("HandleTimeout: Connection is null for client {}",
                      client_id);
  }
}

void BlockingManager::ProcessBlockedCommand(const BlockedClient& client) {
  ASTRADB_LOG_DEBUG("Processing blocked command for client {} on key {}",
                    client.client_id, client.key);

  // The callback will execute the command with new data and return the result
  if (client.callback) {
    auto result = client.callback(
        astra::protocol::RespValue(astra::protocol::RespType::kNull));

    // Send result via connection
    if (client.connection) {
      ASTRADB_LOG_DEBUG(
          "ProcessBlockedCommand: Sending response via connection");
      auto builder = astra::protocol::RespBuilder::Build(result);
      client.connection->Send(builder);
    } else {
      ASTRADB_LOG_ERROR(
          "ProcessBlockedCommand: Connection is null for client {}",
          client.client_id);
    }
  }
}

}  // namespace astra::commands
