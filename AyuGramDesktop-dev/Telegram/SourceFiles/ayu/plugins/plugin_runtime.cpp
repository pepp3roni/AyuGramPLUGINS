// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/plugins/plugin_runtime.h"

#include "ayu/plugins/sdk/sdk_module.h"

#include <QDir>
#include <QStandardPaths>

#ifndef AYUGRAM_DISABLE_PLUGINS
#include <pybind11/embed.h>
namespace py = pybind11;
#endif

namespace AyuPlugins {
namespace {

QString workingDir() {
	const auto current = QDir::currentPath();
	return current.endsWith('/') ? current : (current + u"/"_q);
}

} // namespace

PluginRuntime &PluginRuntime::instance() {
	static auto runtime = PluginRuntime();
	return runtime;
}

PluginRuntime::PluginRuntime() = default;
PluginRuntime::~PluginRuntime() = default;

QString PluginRuntime::pluginsDir() {
	const auto path = workingDir() + u"tdata/plugins/"_q;
	QDir().mkpath(path);
	return path;
}

QString PluginRuntime::sdkDir() {
	const auto path = workingDir() + u"tdata/plugins/_sdk/"_q;
	QDir().mkpath(path);
	return path;
}

bool PluginRuntime::available() const {
	return _available;
}

bool PluginRuntime::initialized() const {
	return _initialized;
}

#ifndef AYUGRAM_DISABLE_PLUGINS

bool PluginRuntime::init() {
	if (_initialized) {
		return _available;
	}
	_initialized = true;

	try {
		DeploySdkPackage();

		py::initialize_interpreter();

		// initialize_interpreter() leaves the GIL held by this (main) thread.
		// We perform setup while it is held, then release it for the lifetime
		// of the process via a never-destroyed gil_scoped_release, so that the
		// gil_scoped_acquire blocks used during hook dispatch can re-acquire
		// it from the main thread later.
		auto sys = py::module_::import("sys");
		auto path = sys.attr("path");
		path.attr("insert")(0, sdkDir().toStdString());
		path.attr("insert")(0, pluginsDir().toStdString());

		py::module_::import("base_plugin");

		_available = true;
		LOG(("AyuPlugins: CPython runtime initialized."));
	} catch (const std::exception &e) {
		_available = false;
		LOG(("AyuPlugins: failed to initialize runtime: %1"
		).arg(QString::fromUtf8(e.what())));
	}

	if (_available) {
		static auto nogil = py::gil_scoped_release();
		(void)nogil;
	}
	return _available;
}

#else // AYUGRAM_DISABLE_PLUGINS

bool PluginRuntime::init() {
	if (_initialized) {
		return _available;
	}
	_initialized = true;
	_available = false;
	LOG(("AyuPlugins: built without CPython; plugin system disabled."));
	return false;
}

#endif // AYUGRAM_DISABLE_PLUGINS

} // namespace AyuPlugins
