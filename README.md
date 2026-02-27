# AstraDB

A C++ project initialized with CMakeHub.

## Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
./AstraDB
```

## Adding Modules

Use the CMakeHub CLI to add modules:

```bash
# List available modules
cmakehub list

# Search for modules
cmakehub search sanitizer

# Add a module
cmakehub use sanitizers --append CMakeLists.txt

# Check module compatibility
cmakehub check sanitizers
```

## Features

- CMake 3.20+
- C++20 standard
- CMakeHub integration
- Testing enabled

## License

Licensed under the Apache License, Version 2.0. See the [LICENSE](LICENSE) file for details.
