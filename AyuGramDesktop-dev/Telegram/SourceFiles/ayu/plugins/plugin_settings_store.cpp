// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/plugins/plugin_settings_store.h"

#include "ayu/plugins/plugin_runtime.h"

#include <fstream>
#include <QFile>

namespace AyuPlugins {
namespace {

QString settingsPathFor(const QString &pluginId) {
	return PluginRuntime::pluginsDir() + pluginId + u".settings.json"_q;
}

} // namespace

PluginSettingsStore::PluginSettingsStore(QString pluginId)
	: _pluginId(std::move(pluginId))
	, _path(settingsPathFor(_pluginId))
	, _data(nlohmann::json::object()) {
	load();
}

void PluginSettingsStore::load() {
	auto stream = std::ifstream(_path.toStdString());
	if (!stream.good()) {
		_data = nlohmann::json::object();
		return;
	}
	try {
		_data = nlohmann::json::parse(stream, nullptr, true, true);
		if (!_data.is_object()) {
			_data = nlohmann::json::object();
		}
	} catch (...) {
		_data = nlohmann::json::object();
	}
}

void PluginSettingsStore::save() const {
	auto stream = std::ofstream(_path.toStdString(), std::ios::trunc);
	if (!stream.good()) {
		return;
	}
	stream << _data.dump(4);
}

std::string PluginSettingsStore::getRaw(
		const std::string &key,
		const std::string &defaultJson) const {
	const auto it = _data.find(key);
	if (it == _data.end()) {
		return defaultJson;
	}
	return it->dump();
}

void PluginSettingsStore::setRaw(
		const std::string &key,
		const std::string &valueJson) {
	try {
		_data[key] = nlohmann::json::parse(valueJson);
	} catch (...) {
		_data[key] = valueJson;
	}
	save();
}

std::string PluginSettingsStore::exportAll() const {
	return _data.dump();
}

void PluginSettingsStore::importAll(const std::string &json) {
	try {
		auto parsed = nlohmann::json::parse(json);
		if (parsed.is_object()) {
			_data = std::move(parsed);
			save();
		}
	} catch (...) {
	}
}

bool PluginSettingsStore::getBool(const QString &key, bool def) const {
	const auto it = _data.find(key.toStdString());
	if (it == _data.end() || !it->is_boolean()) {
		return def;
	}
	return it->get<bool>();
}

void PluginSettingsStore::setBool(const QString &key, bool value) {
	_data[key.toStdString()] = value;
	save();
}

int PluginSettingsStore::getInt(const QString &key, int def) const {
	const auto it = _data.find(key.toStdString());
	if (it == _data.end() || !it->is_number_integer()) {
		return def;
	}
	return it->get<int>();
}

void PluginSettingsStore::setInt(const QString &key, int value) {
	_data[key.toStdString()] = value;
	save();
}

QString PluginSettingsStore::getString(
		const QString &key,
		const QString &def) const {
	const auto it = _data.find(key.toStdString());
	if (it == _data.end() || !it->is_string()) {
		return def;
	}
	return QString::fromStdString(it->get<std::string>());
}

void PluginSettingsStore::setString(const QString &key, const QString &value) {
	_data[key.toStdString()] = value.toStdString();
	save();
}

} // namespace AyuPlugins
