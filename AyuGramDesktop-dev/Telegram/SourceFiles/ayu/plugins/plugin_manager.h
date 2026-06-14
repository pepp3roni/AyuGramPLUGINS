// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/ui/boxes/plugin_info_box.h"

#include <QString>
#include <memory>
#include <set>
#include <vector>

namespace Api {
struct MessageToSend;
} // namespace Api

namespace AyuPlugins {

class Plugin;
class PluginSettingsStore;

struct PluginEntry {
	Ui::PluginMetadata metadata;
	QString filePath;
	bool enabled = false;
	bool loaded = false;
	QString error;
};

// Singleton that discovers, installs, enables/disables and dispatches hooks to
// .plugin files. Enabled-plugin ids are persisted to
// <pluginsDir>/enabled.json. All Python interaction is funnelled through the
// owned Plugin objects, which serialize access behind the GIL.
class PluginManager final {
public:
	[[nodiscard]] static PluginManager &instance();

	PluginManager(const PluginManager &) = delete;
	PluginManager &operator=(const PluginManager &) = delete;

	// Initializes the runtime, scans the plugins directory and loads every
	// plugin whose id is in the persisted enabled set. Called once from
	// AyuInfra::init().
	void init();

	// Copies a .plugin file into the plugins directory. Returns the parsed
	// metadata id on success, or an empty string on failure (sets *error).
	QString installFromFile(const QString &sourcePath, QString *error = nullptr);

	bool enable(const QString &id, QString *error = nullptr);
	void disable(const QString &id);
	[[nodiscard]] bool isEnabled(const QString &id) const;

	void uninstall(const QString &id);

	[[nodiscard]] std::vector<PluginEntry> list() const;
	[[nodiscard]] Plugin *find(const QString &id) const;

	// Returns the settings store for a plugin id, or nullptr if unknown. Used
	// by the native SDK bridge to back get_setting/set_setting.
	[[nodiscard]] static PluginSettingsStore *settingsStoreFor(
		const QString &id);

	// Runs every enabled plugin's on_send_message_hook over the outgoing text.
	// Mutates message.textWithTags.text on MODIFY and returns false when the
	// send must be cancelled. Fast no-op when no plugins are loaded.
	bool dispatchOnSendMessage(Api::MessageToSend &message);

private:
	PluginManager();
	~PluginManager();

	struct Loaded {
		Ui::PluginMetadata metadata;
		QString filePath;
		std::unique_ptr<Plugin> plugin;
		bool enabled = false;
		QString error;
	};

	void scan();
	void loadEnabledSet();
	void saveEnabledSet() const;
	Loaded *findLoaded(const QString &id);
	[[nodiscard]] const Loaded *findLoaded(const QString &id) const;

	bool _initialized = false;
	bool _anyLoaded = false;
	std::set<QString> _enabledIds;
	std::vector<std::unique_ptr<Loaded>> _plugins;
};

} // namespace AyuPlugins
