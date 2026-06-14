// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/plugins/plugin.h"

#include "ayu/plugins/plugin_runtime.h"

#include <QFile>

#ifndef AYUGRAM_DISABLE_PLUGINS

#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace AyuPlugins {
namespace {

QString PyErrorText(const py::error_already_set &e) {
	return QString::fromUtf8(e.what());
}

PluginSettingItem::Type KindToType(const std::string &kind) {
	if (kind == "header") {
		return PluginSettingItem::Type::Header;
	} else if (kind == "divider") {
		return PluginSettingItem::Type::Divider;
	} else if (kind == "switch") {
		return PluginSettingItem::Type::Switch;
	} else if (kind == "selector") {
		return PluginSettingItem::Type::Selector;
	} else if (kind == "input") {
		return PluginSettingItem::Type::Input;
	} else if (kind == "text") {
		return PluginSettingItem::Type::Text;
	}
	return PluginSettingItem::Type::Unsupported;
}

QString AttrString(const py::object &obj, const char *name) {
	if (!py::hasattr(obj, name)) {
		return {};
	}
	const auto value = obj.attr(name);
	if (value.is_none()) {
		return {};
	}
	try {
		return QString::fromStdString(py::str(value).cast<std::string>());
	} catch (...) {
		return {};
	}
}

bool AttrBool(const py::object &obj, const char *name, bool def) {
	if (!py::hasattr(obj, name)) {
		return def;
	}
	try {
		const auto value = obj.attr(name);
		return value.is_none() ? def : value.cast<bool>();
	} catch (...) {
		return def;
	}
}

int AttrInt(const py::object &obj, const char *name, int def) {
	if (!py::hasattr(obj, name)) {
		return def;
	}
	try {
		const auto value = obj.attr(name);
		return value.is_none() ? def : value.cast<int>();
	} catch (...) {
		return def;
	}
}

} // namespace

class Plugin::Impl {
public:
	Ui::PluginMetadata metadata;
	QString filePath;
	bool loaded = false;
	QString error;

	py::object module;
	py::object instance;

	[[nodiscard]] bool hasSendHook() const {
		if (!instance) {
			return false;
		}
		try {
			return py::hasattr(instance, "_send_hook_registered")
				&& instance.attr("_send_hook_registered").cast<bool>();
		} catch (...) {
			return false;
		}
	}
};

Plugin::Plugin(Ui::PluginMetadata metadata, QString filePath)
	: _impl(std::make_unique<Impl>()) {
	_impl->metadata = std::move(metadata);
	_impl->filePath = std::move(filePath);
}

Plugin::~Plugin() {
	unload();
}

const Ui::PluginMetadata &Plugin::metadata() const {
	return _impl->metadata;
}

const QString &Plugin::filePath() const {
	return _impl->filePath;
}

bool Plugin::loaded() const {
	return _impl->loaded;
}

QString Plugin::lastError() const {
	return _impl->error;
}

bool Plugin::load() {
	auto file = QFile(_impl->filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		_impl->error = u"Cannot read plugin file."_q;
		return false;
	}
	const auto source = QString::fromUtf8(file.readAll());
	file.close();

	const auto moduleName = u"ayu_plugin_"_q + _impl->metadata.id;

	py::gil_scoped_acquire gil;
	try {
		auto types = py::module_::import("types");
		auto module = types.attr("ModuleType")(moduleName.toStdString());
		module.attr("__file__") = _impl->filePath.toStdString();

		auto basePlugin = py::module_::import("base_plugin");
		basePlugin.attr("_set_current_plugin_id")(
			_impl->metadata.id.toStdString());

		auto globals = module.attr("__dict__");
		py::exec(source.toStdString(), globals);

		auto pluginClass = py::object();
		const auto basePluginClass = basePlugin.attr("BasePlugin");
		for (auto item : globals) {
			auto value = globals[item];
			if (!py::isinstance<py::type>(value)) {
				continue;
			}
			if (value.is(basePluginClass)) {
				continue;
			}
			try {
				if (PyObject_IsSubclass(
						value.ptr(),
						basePluginClass.ptr()) == 1) {
					pluginClass = py::reinterpret_borrow<py::object>(value);
					break;
				}
			} catch (...) {
			}
		}

		if (!pluginClass) {
			_impl->error = u"No BasePlugin subclass found."_q;
			return false;
		}

		_impl->module = module;
		_impl->instance = pluginClass();
		try {
			_impl->instance.attr("id") = _impl->metadata.id.toStdString();
		} catch (...) {
		}
		_impl->loaded = true;
		_impl->error.clear();
		return true;
	} catch (const py::error_already_set &e) {
		_impl->error = PyErrorText(e);
		return false;
	} catch (const std::exception &e) {
		_impl->error = QString::fromUtf8(e.what());
		return false;
	}
}

void Plugin::callOnLoad() {
	if (!_impl->instance) {
		return;
	}
	py::gil_scoped_acquire gil;
	try {
		_impl->instance.attr("on_plugin_load")();
	} catch (const py::error_already_set &e) {
		LOG(("AyuPlugins: on_plugin_load failed for '%1': %2"
		).arg(_impl->metadata.id, PyErrorText(e)));
	}
}

void Plugin::callOnUnload() {
	if (!_impl->instance) {
		return;
	}
	py::gil_scoped_acquire gil;
	try {
		_impl->instance.attr("on_plugin_unload")();
	} catch (const py::error_already_set &e) {
		LOG(("AyuPlugins: on_plugin_unload failed for '%1': %2"
		).arg(_impl->metadata.id, PyErrorText(e)));
	}
}

void Plugin::unload() {
	if (!_impl->instance && !_impl->module) {
		return;
	}
	py::gil_scoped_acquire gil;
	_impl->instance = py::object();
	_impl->module = py::object();
	_impl->loaded = false;
}

std::vector<PluginSettingItem> Plugin::createSettings() {
	auto result = std::vector<PluginSettingItem>();
	if (!_impl->instance) {
		return result;
	}
	py::gil_scoped_acquire gil;
	try {
		auto items = _impl->instance.attr("create_settings")();
		for (auto handle : items) {
			auto obj = py::reinterpret_borrow<py::object>(handle);
			auto item = PluginSettingItem();
			const auto kind = py::hasattr(obj, "_kind")
				? obj.attr("_kind").cast<std::string>()
				: std::string("unsupported");
			item.type = KindToType(kind);
			item.key = AttrString(obj, "key");
			item.text = AttrString(obj, "text");
			item.subtext = AttrString(obj, "subtext");
			item.icon = AttrString(obj, "icon");
			item.accent = AttrBool(obj, "accent", false);
			item.red = AttrBool(obj, "red", false);

			if (item.type == PluginSettingItem::Type::Switch) {
				item.defaultBool = AttrBool(obj, "default", false);
			} else if (item.type == PluginSettingItem::Type::Selector) {
				item.defaultInt = AttrInt(obj, "default", 0);
				if (py::hasattr(obj, "items")) {
					try {
						for (auto entry : obj.attr("items")) {
							item.items.append(QString::fromStdString(
								py::str(entry).cast<std::string>()));
						}
					} catch (...) {
					}
				}
			} else if (item.type == PluginSettingItem::Type::Input) {
				item.defaultString = AttrString(obj, "default");
			}
			result.push_back(std::move(item));
		}
	} catch (const py::error_already_set &e) {
		LOG(("AyuPlugins: create_settings failed for '%1': %2"
		).arg(_impl->metadata.id, PyErrorText(e)));
	}
	return result;
}

SendMessageHookOutcome Plugin::dispatchSendMessage(
		int account,
		const QString &text) {
	auto outcome = SendMessageHookOutcome();
	if (!_impl->instance || !_impl->hasSendHook()) {
		return outcome;
	}

	py::gil_scoped_acquire gil;
	try {
		auto basePlugin = py::module_::import("base_plugin");
		auto params = basePlugin.attr("_SendParams")(
			account,
			text.toStdString());
		auto result = _impl->instance.attr("on_send_message_hook")(
			account,
			params);
		if (result.is_none()) {
			return outcome;
		}

		auto strategyEnum = basePlugin.attr("HookStrategy");
		auto strategy = result.attr("strategy");
		using Strategy = SendMessageHookOutcome::Strategy;
		if (strategy.is(strategyEnum.attr("CANCEL"))) {
			outcome.strategy = Strategy::Cancel;
		} else if (strategy.is(strategyEnum.attr("MODIFY"))
			|| strategy.is(strategyEnum.attr("MODIFY_FINAL"))) {
			outcome.strategy = strategy.is(strategyEnum.attr("MODIFY_FINAL"))
				? Strategy::ModifyFinal
				: Strategy::Modify;
			outcome.message = text;
			if (py::hasattr(result, "message")
				&& !result.attr("message").is_none()) {
				outcome.message = QString::fromStdString(
					result.attr("message").cast<std::string>());
			} else {
				auto resultParams = result.attr("params");
				auto source = resultParams.is_none() ? params : resultParams;
				if (py::hasattr(source, "message")
					&& !source.attr("message").is_none()) {
					outcome.message = QString::fromStdString(
						source.attr("message").cast<std::string>());
				}
			}
		}
	} catch (const py::error_already_set &e) {
		LOG(("AyuPlugins: on_send_message_hook failed for '%1': %2"
		).arg(_impl->metadata.id, PyErrorText(e)));
	}
	return outcome;
}

} // namespace AyuPlugins

#else // AYUGRAM_DISABLE_PLUGINS

namespace AyuPlugins {

class Plugin::Impl {
public:
	Ui::PluginMetadata metadata;
	QString filePath;
};

Plugin::Plugin(Ui::PluginMetadata metadata, QString filePath)
	: _impl(std::make_unique<Impl>()) {
	_impl->metadata = std::move(metadata);
	_impl->filePath = std::move(filePath);
}

Plugin::~Plugin() = default;

const Ui::PluginMetadata &Plugin::metadata() const {
	return _impl->metadata;
}

const QString &Plugin::filePath() const {
	return _impl->filePath;
}

bool Plugin::loaded() const {
	return false;
}

QString Plugin::lastError() const {
	return u"Plugin runtime is not available in this build."_q;
}

bool Plugin::load() {
	return false;
}

void Plugin::callOnLoad() {
}

void Plugin::callOnUnload() {
}

void Plugin::unload() {
}

std::vector<PluginSettingItem> Plugin::createSettings() {
	return {};
}

SendMessageHookOutcome Plugin::dispatchSendMessage(
		int account,
		const QString &text) {
	return {};
}

} // namespace AyuPlugins

#endif // AYUGRAM_DISABLE_PLUGINS
