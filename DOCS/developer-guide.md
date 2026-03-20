# AstraDB Developer Guide

This guide provides information for developers working on AstraDB.

## Configuration Files

AstraDB uses two types of configuration files:

### Production Configuration (`astradb.toml`)

This is the default configuration file used in production environments. It is installed with the package to `/etc/astradb/astradb.toml`.

**Key settings for production:**
- Log level: `warn` (minimal logging for production)
- Optimized for performance and stability
- Included in version control and package installation

### Development Configuration (`astradb-dev.toml`)

This is an optional configuration file for development. It is **not** included in version control.

**Key settings for development:**
- Log level: `trace` (detailed debugging information)
- Development-friendly settings
- Created by developers for local development

## Memory Configuration

AstraDB supports Redis-compatible memory limits and eviction policies. Configure these in the `[memory]` section:

```toml
[memory]
max_memory = 1073741824        # 1GB (0 = no limit)
eviction_policy = "2q"         # Recommended: 2Q algorithm
eviction_threshold = 0.9       # Trigger eviction at 90% of max_memory
eviction_samples = 5           # Number of samples for LRU/LFU
enable_tracking = true         # Enable memory tracking
```

### Supported Eviction Policies

| Policy | Description | Recommended |
|--------|-------------|-------------|
| `noeviction` | No eviction, return error on OOM | No |
| `allkeys-lru` | Evict any key using LRU | No |
| `volatile-lru` | Evict keys with TTL using LRU | No |
| `allkeys-lfu` | Evict any key using LFU | No |
| `volatile-lfu` | Evict keys with TTL using LFU | No |
| `allkeys-random` | Evict any key randomly | No |
| `volatile-random` | Evict keys with TTL randomly | No |
| `volatile-ttl` | Evict keys with smallest TTL | No |
| **`2q`** | **Dragonfly-style 2Q algorithm** | **Yes** |

For detailed information about eviction strategies, see [Eviction Strategy Optimization](./eviction-strategy-optimization.md).

## Setting Up Development Environment

### 1. Clone the Repository

```bash
git clone https://github.com/your-username/AstraDB.git
cd AstraDB
```

### 2. Create Development Configuration

Copy the development configuration template:

```bash
cp astradb-dev-example.toml astradb-dev.toml
```

The `astradb-dev.toml` file is ignored by Git (see `.gitignore`), so you can customize it without worrying about accidentally committing development settings.

### 3. Build the Project

```bash
# Using CMake presets
cmake --preset linux-debug-clang
cmake --build build-linux-debug-clang -j$(nproc)

# Or manually
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

### 4. Run AstraDB

```bash
# With development configuration (automatically detected)
./build-linux-debug-clang/bin/astradb

# Or explicitly specify configuration
./build-linux-debug-clang/bin/astradb --config astradb-dev.toml
```

## Configuration File Priority

AstraDB automatically selects the appropriate configuration file:

1. If you explicitly specify a config file with `--config`, that file is used
2. If you don't specify a config file (default behavior):
   - First, it checks for `astradb-dev.toml` (development config)
   - If not found, it uses `astradb.toml` (production config)

This allows you to:
- Keep `astradb-dev.toml` in your development directory for local development
- Use the default `astradb.toml` for production deployment
- Never accidentally commit development settings to version control

## Customizing Development Configuration

Edit `astradb-dev.toml` to suit your development needs:

```toml
[logging]
# Set to 'trace' for maximum detail during debugging
level = "trace"

[server]
# Use a different port for local development
port = 6380
```

## Testing

Run tests to verify your changes:

```bash
cd build-linux-debug-clang
ctest --output-on-failure
```

## Debugging

### Using GDB

```bash
gdb --args ./build-linux-debug-clang/bin/astradb --config astradb-dev.toml
```

### Attaching to Running Process

See the main repository documentation for detailed debugging instructions.

## Coding Standards

- Follow the existing code style (see `.clang-format`)
- Use C++23 features where appropriate
- Write tests for new functionality
- Update documentation as needed

## Contributing

1. Create a feature branch from `develop`
2. Make your changes
3. Test thoroughly
4. Submit a pull request to `develop`

## Useful Commands

```bash
# Format code
clang-format -i src/**/*.cpp include/**/*.hpp

# Run static analysis
clang-tidy src/**/*.cpp

# Clean build directory
rm -rf build-*
```

## Additional Resources

- [AstraDB Design Document](../AstraDB_DESIGN.md)
- [Performance Guide](../PERFORMANCE.md)
- [README](../README.md)