// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/ui/boxes/plugin_info_box.h"

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace AyuPlugins {

// One row produced by a plugin's create_settings(). Mirrors the ExteraGram
// ui.settings widget dataclasses; widgets the desktop MVP cannot render yet
// (EditText, Custom) are surfaced as Unsupported so the page still builds.
struct PluginSettingItem {
	enum class Type {
		Header,
		Divider,
		Switch,
		Selector,
		Input,
		Text,
		Unsupported,
	};

	Type type = Type::Unsupported;
	QString key;
	QString text;
	QString subtext;
	QString icon;
	QStringList items;
	bool defaultBool = false;
	int defaultInt = 0;
	QString defaultString;
	bool accent = false;
	bool red = false;
};

// Result of dispatching on_send_message_hook to a plugin, decoded from the
// Python HookResult/HookStrategy.
struct SendMessageHookOutcome {
	enum class Strategy {
		Default,
		Cancel,
		Modify,
		ModifyFinal,
	};

	Strategy strategy = Strategy::Default;
	QString message;
};

// A single loaded .plugin. Wraps the Python module and the instantiated
// BasePlugin subclass; all CPython access lives in the .cpp behind the GIL.
class Plugin final {
public:
	Plugin(Ui::PluginMetadata metadata, QString filePath);
	~Plugin();

	Plugin(const Plugin &) = delete;
	Plugin &operator=(const Plugin &) = delete;

	[[nodiscard]] const Ui::PluginMetadata &metadata() const;
	[[nodiscard]] const QString &filePath() const;
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] QString lastError() const;

	// Imports/executes the plugin module and instantiates its BasePlugin
	// subclass. Returns false and sets lastError() on failure.
	bool load();
	void callOnLoad();
	void callOnUnload();
	void unload();

	[[nodiscard]] std::vector<PluginSettingItem> createSettings();

	// Runs the plugin's on_send_message_hook against the given text, returning
	// what the engine should do with the outgoing message.
	[[nodiscard]] SendMessageHookOutcome dispatchSendMessage(
		int account,
		const QString &text);

private:
	class Impl;
	std::unique_ptr<Impl> _impl;
};

} // namespace AyuPlugins
