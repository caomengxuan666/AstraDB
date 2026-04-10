// ==============================================================================
// Configuration Implementation
// ==============================================================================
// License: Apache 2.0
// ==============================================================================

#include "config.hpp"

#include <fstream>
#include <iostream>

namespace astra::base {

ServerConfig ServerConfig::LoadFromFile(const std::string& config_file) {
  ServerConfig config;

  try {
    auto data = toml::parse_file(config_file);

    // Network
    if (data["server"]) {
      auto server = *data["server"].as_table();
      if (server["host"]) {
        config.host = server["host"].value_or<std::string>("0.0.0.0");
      }
      if (server["port"]) {
        config.port = server["port"].value_or<uint16_t>(6379);
      }
      if (server["max_connections"]) {
        config.max_connections =
            server["max_connections"].value_or<size_t>(10000);
      }
      if (server["thread_count"]) {
        config.thread_count = server["thread_count"].value_or<size_t>(0);
      }
      if (server["use_per_worker_io"]) {
        config.use_per_worker_io =
            server["use_per_worker_io"].value_or<bool>(false);
      }
      if (server["use_so_reuseport"]) {
        config.use_so_reuseport =
            server["use_so_reuseport"].value_or<bool>(true);
      }
    }

    // Database
    if (data["database"]) {
      auto db = *data["database"].as_table();
      if (db["num_databases"]) {
        config.num_databases = db["num_databases"].value_or<size_t>(16);
      }
      if (db["num_shards"]) {
        config.num_shards = db["num_shards"].value_or<size_t>(16);
      }
    }

    // Logging
    if (data["logging"]) {
      auto logging = *data["logging"].as_table();
      if (logging["level"]) {
        config.log_level = logging["level"].value_or<std::string>("info");
      }
      if (logging["file"]) {
        config.log_file = logging["file"].value_or<std::string>("astradb.log");
      }
      if (logging["async"]) {
        config.log_async = logging["async"].value_or<bool>(true);
      }
      if (logging["queue_size"]) {
        config.log_queue_size = logging["queue_size"].value_or<size_t>(8192);
      }
    }

    // Performance
    if (data["performance"]) {
      auto perf = *data["performance"].as_table();
      if (perf["enable_pipeline"]) {
        config.enable_pipeline = perf["enable_pipeline"].value_or<bool>(true);
      }
      if (perf["enable_compression"]) {
        config.enable_compression =
            perf["enable_compression"].value_or<bool>(false);
      }
    }

    // Async / Coroutine
    if (data["async"]) {
      auto async = *data["async"].as_table();
      if (async["use_async_commands"]) {
        config.use_async_commands =
            async["use_async_commands"].value_or<bool>(true);
      }
    }

    // Storage configuration (NEW: unified storage mode)
    if (data["storage"]) {
      auto storage = *data["storage"].as_table();

      // Parse storage mode
      if (storage["mode"]) {
        std::string mode_str = storage["mode"].value_or<std::string>("redis");
        if (mode_str == "redis" || mode_str == "Redis") {
          config.storage.mode = StorageMode::kRedis;
        } else if (mode_str == "rocksdb" || mode_str == "RocksDB") {
          config.storage.mode = StorageMode::kRocksDB;
        }
      }

      // Common settings
      config.storage.enable_rocksdb_cold_data =
          storage["enable_rocksdb_cold_data"].value_or<bool>(true);
      config.storage.enable_compression =
          storage["enable_compression"].value_or<bool>(true);
      config.storage.compression_type =
          storage["compression_type"].value_or<std::string>("zlib");

      // Redis mode settings
      if (storage["redis_mode"]) {
        auto redis_mode = *storage["redis_mode"].as_table();
        config.storage.redis_mode.rdb_enabled =
            redis_mode["rdb_enabled"].value_or<bool>(true);
        config.storage.redis_mode.rdb_path =
            redis_mode["rdb_path"].value_or<std::string>("./data/dump.rdb");
        config.storage.redis_mode.rdb_auto_save =
            redis_mode["rdb_auto_save"].value_or<bool>(false);
        config.storage.redis_mode.rdb_save_interval =
            redis_mode["rdb_save_interval"].value_or<int>(300);
        config.storage.redis_mode.aof_enabled =
            redis_mode["aof_enabled"].value_or<bool>(false);
        config.storage.redis_mode.aof_path =
            redis_mode["aof_path"].value_or<std::string>(
                "./data/aof/appendonly.aof");
        config.storage.redis_mode.aof_sync_everysec =
            redis_mode["aof_sync_everysec"].value_or<bool>(true);
      }

      // RocksDB mode settings
      if (storage["rocksdb_mode"]) {
        auto rocksdb_mode = *storage["rocksdb_mode"].as_table();
        config.storage.rocksdb_mode.data_dir =
            rocksdb_mode["data_dir"].value_or<std::string>("./data/rocksdb");
        config.storage.rocksdb_mode.cache_size =
            rocksdb_mode["cache_size"].value_or<size_t>(256 * 1024 * 1024);
        config.storage.rocksdb_mode.write_buffer_size =
            rocksdb_mode["write_buffer_size"].value_or<size_t>(64 * 1024 *
                                                               1024);
        config.storage.rocksdb_mode.enable_wal =
            rocksdb_mode["enable_wal"].value_or<bool>(true);
        config.storage.rocksdb_mode.create_if_missing =
            rocksdb_mode["create_if_missing"].value_or<bool>(true);
        config.storage.rocksdb_mode.max_open_files =
            rocksdb_mode["max_open_files"].value_or<int>(-1);
        config.storage.rocksdb_mode.max_total_wal_size =
            rocksdb_mode["max_total_wal_size"].value_or<size_t>(100 * 1024 *
                                                                1024);
      }
    }

    // Persistence (Legacy configuration - for backward compatibility)
    if (data["persistence"]) {
      auto persistence = *data["persistence"].as_table();
      config.persistence.enabled = persistence["enabled"].value_or<bool>(false);
      config.persistence.data_dir =
          persistence["data_dir"].value_or<std::string>("./data/astradb");
      config.persistence.write_buffer_size =
          persistence["write_buffer_size"].value_or<size_t>(4 * 1024 * 1024);
      config.persistence.cache_size =
          persistence["cache_size"].value_or<size_t>(256 * 1024 * 1024);
      config.persistence.sync_writes =
          persistence["sync_writes"].value_or<bool>(false);
    }

    // RocksDB for cold data storage
    if (data["rocksdb"]) {
      auto rocksdb = *data["rocksdb"].as_table();
      config.rocksdb.enabled = rocksdb["enabled"].value_or<bool>(false);
      config.rocksdb.data_dir =
          rocksdb["data_dir"].value_or<std::string>("./data/rocksdb");
      config.rocksdb.enable_wal = rocksdb["enable_wal"].value_or<bool>(true);
      config.rocksdb.cache_size =
          rocksdb["cache_size"].value_or<size_t>(256 * 1024 * 1024);
      config.rocksdb.create_if_missing =
          rocksdb["create_if_missing"].value_or<bool>(true);
    }

    // AOF (NO SHARING architecture)
    if (data["aof"]) {
      auto aof = *data["aof"].as_table();
      config.aof.enabled = aof["enabled"].value_or<bool>(false);
      config.aof.path =
          aof["path"].value_or<std::string>("./data/aof/appendonly.aof");
      config.aof.sync_everysec = aof["sync_everysec"].value_or<bool>(true);
    }

    // RDB (NO SHARING architecture)
    if (data["rdb"]) {
      auto rdb = *data["rdb"].as_table();
      config.rdb.enabled = rdb["enabled"].value_or<bool>(true);
      config.rdb.path = rdb["path"].value_or<std::string>("./data/dump.rdb");
      config.rdb.auto_save = rdb["auto_save"].value_or<bool>(false);
      config.rdb.save_interval = rdb["save_interval"].value_or<int>(300);
    }

    // Cluster
    if (data["cluster"]) {
      auto cluster = *data["cluster"].as_table();
      config.cluster.enabled = cluster["enabled"].value_or<bool>(false);
      config.cluster.node_id = cluster["node_id"].value_or<std::string>("");
      config.cluster.bind_addr =
          cluster["bind_addr"].value_or<std::string>("0.0.0.0");
      config.cluster.gossip_port =
          cluster["gossip_port"].value_or<uint16_t>(7946);
      config.cluster.shard_count =
          cluster["shard_count"].value_or<uint32_t>(256);
      config.cluster.use_tcp = cluster["use_tcp"].value_or<bool>(false);

      // Parse seed nodes array
      if (cluster["seeds"]) {
        auto seeds = cluster["seeds"].as_array();
        if (seeds) {
          for (const auto& seed : *seeds) {
            if (auto seed_str = seed.value<std::string>()) {
              config.cluster.seeds.push_back(*seed_str);
            }
          }
        }
      }
    }

    // Memory (Eviction policy)
    if (data["memory"]) {
      auto memory = *data["memory"].as_table();
      if (memory["max_memory"]) {
        config.memory.max_memory = memory["max_memory"].value_or<uint64_t>(0);
      }
      if (memory["eviction_policy"]) {
        config.memory.eviction_policy =
            memory["eviction_policy"].value_or<std::string>("noeviction");
      }
      if (memory["eviction_threshold"]) {
        config.memory.eviction_threshold =
            memory["eviction_threshold"].value_or<double>(0.9);
      }
      if (memory["eviction_samples"]) {
        config.memory.eviction_samples =
            memory["eviction_samples"].value_or<uint32_t>(5);
      }
      if (memory["enable_tracking"]) {
        config.memory.enable_tracking =
            memory["enable_tracking"].value_or<bool>(true);
      }
    }

    // Replication
    if (data["replication"]) {
      auto replication = *data["replication"].as_table();
      if (replication["enabled"]) {
        config.replication.enabled =
            replication["enabled"].value_or<bool>(false);
      }
      if (replication["role"]) {
        config.replication.role =
            replication["role"].value_or<std::string>("master");
      }
      if (replication["master_host"]) {
        config.replication.master_host =
            replication["master_host"].value_or<std::string>("127.0.0.1");
      }
      if (replication["master_port"]) {
        config.replication.master_port =
            replication["master_port"].value_or<uint16_t>(6379);
      }
      if (replication["master_auth"]) {
        config.replication.master_auth =
            replication["master_auth"].value_or<std::string>("");
      }
      if (replication["read_only"]) {
        config.replication.read_only =
            replication["read_only"].value_or<bool>(false);
      }
      if (replication["repl_backlog_size"]) {
        config.replication.repl_backlog_size =
            replication["repl_backlog_size"].value_or<uint64_t>(1 * 1024 *
                                                                1024);
      }
      if (replication["repl_timeout"]) {
        config.replication.repl_timeout =
            replication["repl_timeout"].value_or<uint32_t>(60);
      }
    }

    // Metrics
    if (data["metrics"]) {
      auto metrics = *data["metrics"].as_table();
      config.metrics.enabled = metrics["enabled"].value_or<bool>(false);
      config.metrics.bind_addr =
          metrics["bind_addr"].value_or<std::string>("0.0.0.0");
      config.metrics.port = metrics["port"].value_or<uint16_t>(9100);
      std::cout << "Config loaded: metrics.enabled = " << config.metrics.enabled
                << ", port = " << config.metrics.port << std::endl;
    } else {
      std::cout
          << "Config: [metrics] section not found, using defaults (disabled)"
          << std::endl;
    }

  } catch (const std::exception& e) {
    std::cerr << "Failed to load config from " << config_file << ": "
              << e.what() << std::endl;
    std::cerr << "Using default configuration" << std::endl;
  }

  return config;
}

ServerConfig ServerConfig::LoadFromString(const std::string& config_str) {
  ServerConfig config;

  try {
    auto data = toml::parse(config_str);

    // Same parsing logic as LoadFromFile
    // Network
    if (data["server"]) {
      auto server = *data["server"].as_table();
      config.host = server["host"].value_or<std::string>("0.0.0.0");
      config.port = server["port"].value_or<uint16_t>(6379);
      config.max_connections =
          server["max_connections"].value_or<size_t>(10000);
      config.thread_count = server["thread_count"].value_or<size_t>(0);
    }

    // Database
    if (data["database"]) {
      auto db = *data["database"].as_table();
      config.num_databases = db["num_databases"].value_or<size_t>(16);
      config.num_shards = db["num_shards"].value_or<size_t>(16);
    }

    // Logging
    if (data["logging"]) {
      auto logging = *data["logging"].as_table();
      config.log_level = logging["level"].value_or<std::string>("info");
      config.log_file = logging["file"].value_or<std::string>("astradb.log");
      config.log_async = logging["async"].value_or<bool>(true);
      config.log_queue_size = logging["queue_size"].value_or<size_t>(8192);
    }

    // Async / Coroutine
    if (data["async"]) {
      auto async = *data["async"].as_table();
      if (async["use_async_commands"]) {
        config.use_async_commands =
            async["use_async_commands"].value_or<bool>(true);
      }
    }

    // Persistence
    if (data["persistence"]) {
      auto persistence = *data["persistence"].as_table();
      config.persistence.enabled = persistence["enabled"].value_or<bool>(false);
      config.persistence.data_dir =
          persistence["data_dir"].value_or<std::string>("./data/astradb");
    }

    // Cluster
    if (data["cluster"]) {
      auto cluster = *data["cluster"].as_table();
      config.cluster.enabled = cluster["enabled"].value_or<bool>(false);
      config.cluster.node_id = cluster["node_id"].value_or<std::string>("");
    }

  } catch (const std::exception& e) {
    std::cerr << "Failed to parse config string: " << e.what() << std::endl;
  }

  return config;
}

}  // namespace astra::base
