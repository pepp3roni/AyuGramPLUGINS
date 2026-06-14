// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QString>

namespace AyuPlugins {

// Owns the embedded CPython interpreter for the plugin system.
//
// A single global instance is created lazily through instance(). When the
// project is built with AYUGRAM_DISABLE_PLUGINS (no CPython available), every
// method becomes an inert no-op and available() returns false, so callers can
// be written unconditionally.
class PluginRuntime final {
public:
	[[nodiscard]] static PluginRuntime &instance();

	PluginRuntime(const PluginRuntime &) = delete;
	PluginRuntime &operator=(const PluginRuntime &) = delete;

	// Initializes the interpreter, registers the embedded SDK modules and
	// extends sys.path with the bundled SDK package and the user plugins
	// directory. Safe to call more than once; only the first call has effect.
	// Returns true once the interpreter is usable.
	bool init();

	[[nodiscard]] bool available() const;
	[[nodiscard]] bool initialized() const;

	// Absolute path to the writable directory that stores user .plugin files
	// and their <id>.settings.json. Created on first use.
	[[nodiscard]] static QString pluginsDir();

	// Absolute path to the bundled read-only SDK python package directory that
	// is placed on sys.path (ExteraGram-compatible shims live here).
	[[nodiscard]] static QString sdkDir();

private:
	PluginRuntime();
	~PluginRuntime();

	bool _initialized = false;
	bool _available = false;
};

} // namespace AyuPlugins
