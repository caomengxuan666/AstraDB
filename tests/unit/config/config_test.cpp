#include <gtest/gtest.h>

#include "astra/base/config.hpp"
#include "astra/server/server.hpp"

namespace {

constexpr const char* kRocksDBAllInConfig = R"(
[server]
host = "127.0.0.1"
port = 6389
thread_count = 3
use_per_worker_io = true
use_so_reuseport = false

[database]
num_databases = 4
num_shards = 2

[storage]
mode = "rocksdb"
enable_rocksdb_cold_data = false
enable_compression = false
compression_type = "none"

[storage.rocksdb_mode]
data_dir = "./data/all-in-rocksdb"
cache_size = 12345
write_buffer_size = 67890
enable_wal = true
create_if_missing = true
max_open_files = 77

[rocksdb]
enabled = true
data_dir = "./data/cold-rocksdb"

[acl]
enabled = true
default_user = "alice"
default_password = "secret"

[metrics]
enabled = true
bind_addr = "127.0.0.1"
port = 19100
stats_frequency_seconds = 5

[cluster]
enabled = true
node_id = "node-a"
bind_addr = "127.0.0.1"
gossip_port = 17946
shard_count = 64
seeds = ["127.0.0.1:17947"]
)";

constexpr const char* kRedisModeCanonicalConfig = R"(
[storage]
mode = "redis"

[storage.redis_mode]
rdb_enabled = false
rdb_path = "./data/canonical.rdb"
rdb_auto_save = true
rdb_save_interval = 120
aof_enabled = true
aof_path = "./data/canonical.aof"
aof_sync_everysec = false

[aof]
enabled = false
path = "./data/legacy.aof"
sync_everysec = true

[rdb]
enabled = true
path = "./data/legacy.rdb"
auto_save = false
save_interval = 300
)";

}  // namespace

TEST(ConfigTest, LoadFromStringParsesUnifiedStorageAndAcl) {
  auto config = astra::base::ServerConfig::LoadFromString(kRocksDBAllInConfig);

  EXPECT_EQ(config.host, "127.0.0.1");
  EXPECT_EQ(config.port, 6389);
  EXPECT_EQ(config.thread_count, 3);
  EXPECT_TRUE(config.use_per_worker_io);
  EXPECT_FALSE(config.use_so_reuseport);

  EXPECT_EQ(config.storage.mode, astra::base::StorageMode::kRocksDB);
  EXPECT_FALSE(config.storage.enable_rocksdb_cold_data);
  EXPECT_FALSE(config.storage.enable_compression);
  EXPECT_EQ(config.storage.compression_type, "none");
  EXPECT_EQ(config.storage.rocksdb_mode.data_dir, "./data/all-in-rocksdb");
  EXPECT_EQ(config.storage.rocksdb_mode.cache_size, 12345);
  EXPECT_EQ(config.storage.rocksdb_mode.write_buffer_size, 67890);
  EXPECT_EQ(config.storage.rocksdb_mode.max_open_files, 77);

  EXPECT_TRUE(config.rocksdb.enabled);
  EXPECT_EQ(config.rocksdb.data_dir, "./data/cold-rocksdb");

  EXPECT_TRUE(config.acl.enabled);
  EXPECT_EQ(config.acl.default_user, "alice");
  EXPECT_EQ(config.acl.default_password, "secret");

  EXPECT_TRUE(config.metrics.enabled);
  EXPECT_EQ(config.metrics.bind_addr, "127.0.0.1");
  EXPECT_EQ(config.metrics.port, 19100);
  EXPECT_EQ(config.metrics.stats_frequency_seconds, 5);
}

TEST(ConfigTest, NoSharingServerConfigCopiesDerivedRuntimeFields) {
  auto base_config =
      astra::base::ServerConfig::LoadFromString(kRocksDBAllInConfig);
  auto server_config =
      astra::server::ServerConfig::FromBaseConfig(base_config);

  EXPECT_EQ(server_config.storage.mode, astra::base::StorageMode::kRocksDB);
  EXPECT_EQ(server_config.storage.rocksdb_mode.data_dir,
            "./data/all-in-rocksdb");

  EXPECT_TRUE(server_config.acl_enabled);
  EXPECT_EQ(server_config.acl_default_user, "alice");
  EXPECT_EQ(server_config.acl_default_password, "secret");

  EXPECT_TRUE(server_config.metrics_enabled);
  EXPECT_EQ(server_config.metrics_bind_addr, "127.0.0.1");
  EXPECT_EQ(server_config.metrics_port, 19100);

  EXPECT_TRUE(server_config.cluster_enabled);
  EXPECT_EQ(server_config.cluster_node_id, "node-a");
  EXPECT_EQ(server_config.cluster_bind_addr, "127.0.0.1");
  EXPECT_EQ(server_config.cluster_gossip_port, 17946);
  EXPECT_EQ(server_config.cluster_shard_count, 64);
  ASSERT_EQ(server_config.cluster_seeds.size(), 1);
  EXPECT_EQ(server_config.cluster_seeds[0], "127.0.0.1:17947");
}

TEST(ConfigTest, StorageRedisModeOverridesLegacyAofAndRdbWhenExplicit) {
  auto config =
      astra::base::ServerConfig::LoadFromString(kRedisModeCanonicalConfig);

  EXPECT_EQ(config.storage.mode, astra::base::StorageMode::kRedis);

  EXPECT_FALSE(config.rdb.enabled);
  EXPECT_EQ(config.rdb.path, "./data/canonical.rdb");
  EXPECT_TRUE(config.rdb.auto_save);
  EXPECT_EQ(config.rdb.save_interval, 120);

  EXPECT_TRUE(config.aof.enabled);
  EXPECT_EQ(config.aof.path, "./data/canonical.aof");
  EXPECT_FALSE(config.aof.sync_everysec);
}
