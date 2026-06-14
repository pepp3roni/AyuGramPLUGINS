// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/plugins/plugin_manager.h"

#include "ayu/plugins/plugin.h"
#include "ayu/plugins/plugin_runtime.h"
#include "ayu/plugins/plugin_settings_store.h"
#include "ayu/libs/json.hpp"

#include "api/api_common.h"

#include <fstream>
#include <map>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace AyuPlugins {
namespace {

QString enabledSetPath() {
	return PluginRuntime::pluginsDir() + u"enabled.json"_q;
}

std::map<QString, std::unique_ptr<PluginSettingsStore>> &storeRegistry() {
	static auto registry =
		std::map<QString, std::unique_ptr<PluginSettingsStore>>();
	return registry;
}

} // namespace

PluginManager &PluginManager::instance() {
	static auto manager = PluginManager();
	return manager;
}

PluginManager::PluginManager() = default;
PluginManager::~PluginManager() = default;

PluginSettingsStore *PluginManager::settingsStoreFor(const QString &id) {
	if (id.isEmpty()) {
		return nullptr;
	}
	auto &registry = storeRegistry();
	const auto it = registry.find(id);
	if (it != registry.end()) {
		return it->second.get();
	}
	auto store = std::make_unique<PluginSettingsStore>(id);
	const auto raw = store.get();
	registry.emplace(id, std::move(store));
	return raw;
}

void PluginManager::init() {
	if (_initialized) {
		return;
	}
	_initialized = true;

	if (!PluginRuntime::instance().init()) {
		LOG(("AyuPlugins: runtime unavailable, plugin system disabled."));
		return;
	}

	loadEnabledSet();
	scan();

	for (const auto &loaded : _plugins) {
		if (!loaded->enabled) {
			continue;
		}
		auto plugin = std::make_unique<Plugin>(
			loaded->metadata,
			loaded->filePath);
		if (plugin->load()) {
			plugin->callOnLoad();
			loaded->plugin = std::move(plugin);
			loaded->loaded = true;
			_anyLoaded = true;
		} else {
			loaded->error = plugin->lastError();
			LOG(("AyuPlugins: failed to load '%1': %2"
			).arg(loaded->metadata.id, loaded->error));
		}
	}
}

void PluginManager::scan() {
	const auto dir = QDir(PluginRuntime::pluginsDir());
	const auto files = dir.entryInfoList(
		QStringList{ u"*.plugin"_q },
		QDir::Files);
	for (const auto &info : files) {
		auto file = QFile(info.absoluteFilePath());
		if (!file.open(QIODevice::ReadOnly)) {
			continue;
		}
		const auto data = file.readAll();
		file.close();

		auto metadata = Ui::ParsePluginMetadata(data);
		if (metadata.id.isEmpty() || metadata.name.isEmpty()) {
			continue;
		}
		if (findLoaded(metadata.id)) {
			continue;
		}
		auto entry = std::make_unique<Loaded>();
		entry->enabled = _enabledIds.contains(metadata.id);
		entry->filePath = info.absoluteFilePath();
		entry->metadata = std::move(metadata);
		_plugins.push_back(std::move(entry));
	}
}

QString PluginManager::installFromFile(
		const QString &sourcePath,
		QString *error) {
	const auto setError = [&](const QString &message) {
		if (error) {
			*error = message;
		}
		return QString();
	};

	auto source = QFile(sourcePath);
	if (!source.open(QIODevice::ReadOnly)) {
		return setError(u"Cannot open plugin file."_q);
	}
	const auto data = source.readAll();
	source.close();

	auto metadata = Ui::ParsePluginMetadata(data);
	if (metadata.id.isEmpty() || metadata.name.isEmpty()) {
		return setError(u"Invalid plugin metadata."_q);
	}

	if (!_initialized) {
		init();
	}

	const auto destination = PluginRuntime::pluginsDir()
		+ metadata.id
		+ u".plugin"_q;
	if (QFileInfo::exists(destination)) {
		QFile::remove(destination);
	}
	auto dest = QFile(destination);
	if (!dest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return setError(u"Cannot write to plugins directory."_q);
	}
	dest.write(data);
	dest.close();

	if (const auto existing = findLoaded(metadata.id)) {
		existing->filePath = destination;
		existing->metadata = metadata;
	} else {
		auto entry = std::make_unique<Loaded>();
		entry->filePath = destination;
		entry->metadata = metadata;
		entry->enabled = false;
		_plugins.push_back(std::move(entry));
	}

	return metadata.id;
}

bool PluginManager::enable(const QString &id, QString *error) {
	const auto loaded = findLoaded(id);
	if (!loaded) {
		if (error) {
			*error = u"Unknown plugin."_q;
		}
		return false;
	}
	if (loaded->plugin) {
		loaded->enabled = true;
		_enabledIds.insert(id);
		saveEnabledSet();
		return true;
	}
	if (!PluginRuntime::instance().init()) {
		if (error) {
			*error = u"Plugin runtime is unavailable."_q;
		}
		return false;
	}

	auto plugin = std::make_unique<Plugin>(loaded->metadata, loaded->filePath);
	if (!plugin->load()) {
		loaded->error = plugin->lastError();
		if (error) {
			*error = loaded->error;
		}
		return false;
	}
	plugin->callOnLoad();
	loaded->plugin = std::move(plugin);
	loaded->loaded = true;
	loaded->enabled = true;
	loaded->error.clear();
	_anyLoaded = true;
	_enabledIds.insert(id);
	saveEnabledSet();
	return true;
}

void PluginManager::disable(const QString &id) {
	const auto loaded = findLoaded(id);
	if (!loaded) {
		return;
	}
	if (loaded->plugin) {
		loaded->plugin->callOnUnload();
		loaded->plugin->unload();
		loaded->plugin.reset();
	}
	loaded->loaded = false;
	loaded->enabled = false;
	_enabledIds.erase(id);
	saveEnabledSet();

	_anyLoaded = false;
	for (const auto &entry : _plugins) {
		if (entry->plugin) {
			_anyLoaded = true;
			break;
		}
	}
}

bool PluginManager::isEnabled(const QString &id) const {
	return _enabledIds.contains(id);
}

void PluginManager::uninstall(const QString &id) {
	disable(id);
	for (auto it = _plugins.begin(); it != _plugins.end(); ++it) {
		if ((*it)->metadata.id == id) {
			if (!(*it)->filePath.isEmpty()) {
				QFile::remove((*it)->filePath);
			}
			_plugins.erase(it);
			break;
		}
	}
}

std::vector<PluginEntry> PluginManager::list() const {
	auto result = std::vector<PluginEntry>();
	result.reserve(_plugins.size());
	for (const auto &loaded : _plugins) {
		result.push_back(PluginEntry{
			.metadata = loaded->metadata,
			.filePath = loaded->filePath,
			.enabled = loaded->enabled,
			.loaded = loaded->loaded,
			.error = loaded->error,
		});
	}
	return result;
}

Plugin *PluginManager::find(const QString &id) const {
	const auto loaded = findLoaded(id);
	return loaded ? loaded->plugin.get() : nullptr;
}

bool PluginManager::dispatchOnSendMessage(Api::MessageToSend &message) {
	if (!_anyLoaded) {
		return true;
	}

	const auto account = 0;
	auto text = message.textWithTags.text;

	for (const auto &loaded : _plugins) {
		if (!loaded->plugin || !loaded->enabled) {
			continue;
		}
		const auto outcome = loaded->plugin->dispatchSendMessage(account, text);
		using Strategy = SendMessageHookOutcome::Strategy;
		if (outcome.strategy == Strategy::Cancel) {
			return false;
		} else if (outcome.strategy == Strategy::Modify
			|| outcome.strategy == Strategy::ModifyFinal) {
			if (outcome.message != text) {
				text = outcome.message;
				message.textWithTags = TextWithTags{ text };
			}
			if (outcome.strategy == Strategy::ModifyFinal) {
				break;
			}
		}
	}
	return true;
}

void PluginManager::loadEnabledSet() {
	_enabledIds.clear();
	auto stream = std::ifstream(enabledSetPath().toStdString());
	if (!stream.good()) {
		return;
	}
	try {
		const auto data = nlohmann::json::parse(stream);
		if (!data.is_array()) {
			return;
		}
		for (const auto &item : data) {
			if (item.is_string()) {
				_enabledIds.insert(
					QString::fromStdString(item.get<std::string>()));
			}
		}
	} catch (...) {
	}
}

void PluginManager::saveEnabledSet() const {
	auto array = nlohmann::json::array();
	for (const auto &id : _enabledIds) {
		array.push_back(id.toStdString());
	}
	auto stream = std::ofstream(
		enabledSetPath().toStdString(),
		std::ios::trunc);
	if (stream.good()) {
		stream << array.dump(4);
	}
}

PluginManager::Loaded *PluginManager::findLoaded(const QString &id) {
	for (const auto &loaded : _plugins) {
		if (loaded->metadata.id == id) {
			return loaded.get();
		}
	}
	return nullptr;
}

const PluginManager::Loaded *PluginManager::findLoaded(
		const QString &id) const {
	for (const auto &loaded : _plugins) {
		if (loaded->metadata.id == id) {
			return loaded.get();
		}
	}
	return nullptr;
}

} // namespace AyuPlugins
