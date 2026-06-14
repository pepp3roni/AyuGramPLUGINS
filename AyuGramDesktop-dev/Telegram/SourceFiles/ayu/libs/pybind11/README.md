# Vendored pybind11

This directory holds the [pybind11](https://github.com/pybind/pybind11) headers
used by AyuGram's plugin system (`Telegram/SourceFiles/ayu/plugins/`) to embed
CPython 3.11 and expose the ExteraGram-compatible SDK modules to `.plugin`
files.

pybind11 is header-only. Place its `include/` tree here so that

```
Telegram/SourceFiles/ayu/libs/pybind11/include/pybind11/embed.h
```

resolves. `Telegram/cmake/td_python.cmake` adds this `include/` directory to the
`ayugram::python` interface target and links `Python3::Python`.

## Provisioning

Either:

* add pybind11 as a git submodule pointing here, or
* copy a tagged pybind11 release's `include/` directory into this folder.

Recommended version: pybind11 >= 2.12 (CPython 3.11 support).

If neither pybind11 nor an embeddable CPython 3.11 is available at configure
time, `td_python.cmake` defines `AYUGRAM_DISABLE_PLUGINS` and the plugin sources
compile to inert stubs, so the app still builds.
