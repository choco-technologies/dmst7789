# dmst7789

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmst7789/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmst7789/actions/workflows/ci.yml)

dmst7789 DMOD library module.

## Description

TODO: describe what this module does.

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub. The checkout needs its submodules (dmod keeps its compression
library in one): `git clone --recursive`, or `git submodule update --init --recursive` in an existing one.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Testing

Tests are built automatically alongside the module (see `tests/`). Once built,
run them with `ctest`:

```bash
cd build
ctest --output-on-failure
```

`ctest` installs the test module's dependencies with `dmf-get` and then runs
it through `dmod_loader`. To run it manually instead:

```bash
export DMOD_DMF_DIR=$(pwd)/build/dmf
dmf-get install -d ${DMOD_DMF_DIR}/test_dmst7789-local.dmd -y
dmod_loader build/dmf/test_dmst7789.dmf
```

## Usage

<TBD>

This library module provides functions that can be used by other modules:

```c
#include "dmst7789.h"
```

## API

| Function | Description |
|----------|-------------|
| `dmst7789_create()` | Create a new `dmst7789_t` instance. |
| `dmst7789_destroy()` | Destroy an instance created by `_create()`. |
| `dmst7789_is_valid()` | Check whether a handle is a valid instance. |

See [include/dmst7789.h](include/dmst7789.h) for the full
declarations and [docs/api-reference.md](docs/api-reference.md) for the
complete reference.

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Complete API documentation

View documentation using `dmf-man dmst7789`.
## Project Structure

```
dmst7789/
├── docs/              # Documentation (markdown format)
├── include/           # Public headers
│   └── dmst7789.h
├── src/
│   └── dmst7789.c
├── tests/
│   ├── CMakeLists.txt
│   └── dmst7789_test.c
├── CMakeLists.txt
├── Makefile
├── dmst7789.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT
