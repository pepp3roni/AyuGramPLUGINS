// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

// Registration of the native `_ayusdk` CPython module that backs the
// ExteraGram-compatible Python SDK shims (base_plugin, client_utils,
// android_utils, ui.bulletin). The pure-Python shim layer lives in
// PluginRuntime::sdkDir() and calls into the functions registered here.
//
// This header intentionally exposes no pybind11 types so it can be included
// from translation units built without CPython.

namespace AyuPlugins {

// Writes the bundled Python SDK package (base_plugin.py, client_utils.py,
// android_utils.py, ui/*.py, stubs) into PluginRuntime::sdkDir(). Idempotent;
// overwrites on every startup so the shipped SDK always matches the binary.
void DeploySdkPackage();

} // namespace AyuPlugins

#ifndef AYUGRAM_DISABLE_PLUGINS
// Declares the embedded module initializer. Defined in sdk_module.cpp via
// PYBIND11_EMBEDDED_MODULE; called by PluginRuntime before Py_Initialize.
#endif
