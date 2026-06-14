// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/libs/json.hpp"

#include <QString>
#include <string>

namespace AyuPlugins {

// Per-plugin persistent key/value store backed by
// <pluginsDir>/<id>.settings.json. A single instance per plugin id is shared
// between the Python bridge (raw JSON-encoded access) and the C++ settings UI
// (typed helpers), so both observe the same values without reloading.
class PluginSettingsStore final {
public:
	explicit PluginSettingsStore(QString pluginId);

	[[nodiscard]] std::string getRaw(
		const std::string &key,
		const std::string &defaultJson) const;
	void setRaw(const std::string &key, const std::string &valueJson);

	[[nodiscard]] std::string exportAll() const;
	void importAll(const std::string &json);

	[[nodiscard]] bool getBool(const QString &key, bool def) const;
	void setBool(const QString &key, bool value);

	[[nodiscard]] int getInt(const QString &key, int def) const;
	void setInt(const QString &key, int value);

	[[nodiscard]] QString getString(const QString &key, const QString &def) const;
	void setString(const QString &key, const QString &value);

private:
	void load();
	void save() const;

	QString _pluginId;
	QString _path;
	nlohmann::json _data;
};

} // namespace AyuPlugins
