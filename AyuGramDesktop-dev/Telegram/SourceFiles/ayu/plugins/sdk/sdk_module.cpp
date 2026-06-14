// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/plugins/sdk/sdk_module.h"

#include "ayu/plugins/plugin_runtime.h"

#include <QDir>
#include <QFile>

#ifndef AYUGRAM_DISABLE_PLUGINS

#include "ayu/plugins/plugin_manager.h"
#include "ayu/plugins/plugin_settings_store.h"
#include "ayu/ui/toasts.h"
#include "ayu/utils/telegram_helpers.h"

#include "apiwrap.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "api/api_common.h"
#include "api/api_text_entities.h"

#include <QClipboard>
#include <QGuiApplication>

#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace AyuPlugins {
namespace {

void BridgeLog(const std::string &pluginId, const std::string &text) {
	LOG(("[plugin:%1] %2"
	).arg(QString::fromStdString(pluginId), QString::fromStdString(text)));
}

std::string BridgeGetSetting(
		const std::string &pluginId,
		const std::string &key,
		const std::string &defaultJson) {
	if (const auto store = PluginManager::settingsStoreFor(
			QString::fromStdString(pluginId))) {
		return store->getRaw(key, defaultJson);
	}
	return defaultJson;
}

void BridgeSetSetting(
		const std::string &pluginId,
		const std::string &key,
		const std::string &valueJson) {
	if (const auto store = PluginManager::settingsStoreFor(
			QString::fromStdString(pluginId))) {
		store->setRaw(key, valueJson);
	}
}

std::string BridgeExportSettings(const std::string &pluginId) {
	if (const auto store = PluginManager::settingsStoreFor(
			QString::fromStdString(pluginId))) {
		return store->exportAll();
	}
	return "{}";
}

void BridgeImportSettings(
		const std::string &pluginId,
		const std::string &json) {
	if (const auto store = PluginManager::settingsStoreFor(
			QString::fromStdString(pluginId))) {
		store->importAll(json);
	}
}

void BridgeToast(const std::string &text, bool isError) {
	const auto message = QString::fromStdString(text);
	dispatchToMainThread([message, isError] {
		auto config = Ui::Toast::Config{ .text = { message } };
		Ui::Toast::Show(config);
	});
}

void BridgeCopyToClipboard(const std::string &text) {
	const auto message = QString::fromStdString(text);
	dispatchToMainThread([message] {
		if (const auto clipboard = QGuiApplication::clipboard()) {
			clipboard->setText(message);
		}
	});
}

bool BridgeSendText(std::int64_t peerId, const std::string &text) {
	const auto message = QString::fromStdString(text);
	const auto raw = static_cast<ID>(peerId);
	dispatchToMainThread([raw, message] {
		const auto peer = getPeerFromDialogId(raw);
		if (!peer) {
			return;
		}
		const auto history = peer->owner().history(peer);
		auto toSend = Api::MessageToSend(
			Api::SendAction(history));
		toSend.textWithTags = TextWithTags{ message };
		peer->session().api().sendMessage(std::move(toSend));
	});
	return true;
}

std::int64_t BridgeCurrentUserId() {
	try {
		const auto session = currentSession();
		return static_cast<std::int64_t>(session->userId().bare);
	} catch (...) {
		return 0;
	}
}

} // namespace
} // namespace AyuPlugins

PYBIND11_EMBEDDED_MODULE(_ayusdk, m) {
	using namespace AyuPlugins;

	m.doc() = "AyuGram native bridge for the plugin SDK.";

	m.def("log", &BridgeLog);
	m.def("get_setting", &BridgeGetSetting);
	m.def("set_setting", &BridgeSetSetting);
	m.def("export_settings", &BridgeExportSettings);
	m.def("import_settings", &BridgeImportSettings);
	m.def("toast", [](const std::string &text) { BridgeToast(text, false); });
	m.def("toast_error",
		[](const std::string &text) { BridgeToast(text, true); });
	m.def("copy_to_clipboard", &BridgeCopyToClipboard);
	m.def("send_text", &BridgeSendText);
	m.def("current_user_id", &BridgeCurrentUserId);
}

#endif // !AYUGRAM_DISABLE_PLUGINS

namespace AyuPlugins {
namespace {

struct SdkFile {
	const char *relativePath;
	const char *contents;
};

constexpr auto kBasePluginPy = R"PYSDK(
"""ExteraGram-compatible base_plugin shim for AyuGram Desktop.

Implements the parts of the mobile SDK supported by the desktop port. APIs that
have no desktop equivalent yet are present so imports succeed, but raise
PluginUnsupportedError when actually called.
"""
import enum
import json
import _ayusdk

_CURRENT_PLUGIN_ID = None


def _set_current_plugin_id(plugin_id):
    global _CURRENT_PLUGIN_ID
    _CURRENT_PLUGIN_ID = plugin_id


class PluginUnsupportedError(NotImplementedError):
    """Raised when a plugin calls an API not supported on AyuGram Desktop."""


class AppEvent(enum.Enum):
    START = "start"
    STOP = "stop"
    PAUSE = "pause"
    RESUME = "resume"


class HookStrategy(enum.Enum):
    DEFAULT = 0
    CANCEL = 1
    MODIFY = 2
    MODIFY_FINAL = 3


class HookResult:
    def __init__(self, strategy=HookStrategy.DEFAULT, params=None,
                 request=None, response=None, update=None, updates=None,
                 message=None):
        self.strategy = strategy
        self.params = params
        self.request = request
        self.response = response
        self.update = update
        self.updates = updates
        self.message = message


class _SendParams:
    """Mirror of SendMessagesHelper.SendMessageParams used by the send hook."""
    def __init__(self, account, message):
        self.account = account
        self.message = message
        self.peer = None


class BasePlugin:
    def __init__(self):
        self.id = getattr(type(self), "__id__", None) or _CURRENT_PLUGIN_ID
        self._send_hook_registered = False
        self._priority = 0

    # -- lifecycle -----------------------------------------------------------
    def on_plugin_load(self):
        pass

    def on_plugin_unload(self):
        pass

    def on_app_event(self, event_type):
        pass

    # -- settings ------------------------------------------------------------
    def create_settings(self):
        return []

    def get_setting(self, key, default=None):
        raw = _ayusdk.get_setting(self.id, key, json.dumps(default))
        try:
            return json.loads(raw)
        except Exception:
            return default

    def set_setting(self, key, value, reload_settings=False):
        _ayusdk.set_setting(self.id, key, json.dumps(value))

    def export_settings(self):
        try:
            return json.loads(_ayusdk.export_settings(self.id))
        except Exception:
            return {}

    def import_settings(self, settings, reload_settings=True):
        _ayusdk.import_settings(self.id, json.dumps(settings))

    # -- logging -------------------------------------------------------------
    def log(self, *args):
        text = " ".join(str(a) for a in args)
        _ayusdk.log(self.id or "?", text)

    # -- hook registration ---------------------------------------------------
    def add_on_send_message_hook(self, priority=0):
        self._send_hook_registered = True
        self._priority = priority

    def add_hook(self, name, match_substring=False, priority=0):
        raise PluginUnsupportedError(
            "add_hook(request/update interception) is not supported on "
            "AyuGram Desktop yet.")

    # -- menu items (not supported yet) --------------------------------------
    def add_menu_item(self, data):
        raise PluginUnsupportedError(
            "Menu items are not supported on AyuGram Desktop yet.")

    def remove_menu_item(self, item_id):
        pass

    # -- hook handlers (overridden by plugins) -------------------------------
    def on_send_message_hook(self, account, params):
        return HookResult()

    def pre_request_hook(self, request_name, account, request):
        return HookResult()

    def post_request_hook(self, request_name, account, response, error):
        return HookResult()

    def on_update_hook(self, update_name, account, update):
        return HookResult()

    def on_updates_hook(self, container_name, account, updates):
        return HookResult()
)PYSDK";

constexpr auto kClientUtilsPy = R"PYSDK(
"""ExteraGram-compatible client_utils shim (desktop subset)."""
import _ayusdk
from base_plugin import PluginUnsupportedError


def send_text(peer_id, text, replyToMsg=None, parse_mode=None):
    return _ayusdk.send_text(int(peer_id), str(text))


def send_message(params, parse_mode=None):
    peer = params.get("peer") if isinstance(params, dict) else None
    message = params.get("message") if isinstance(params, dict) else None
    if peer is None or message is None:
        raise ValueError("send_message requires 'peer' and 'message'.")
    return _ayusdk.send_text(int(peer), str(message))


class _UserConfig:
    def getClientUserId(self):
        return _ayusdk.current_user_id()

    def getCurrentUser(self):
        raise PluginUnsupportedError(
            "get_user_config().getCurrentUser() is not supported on AyuGram "
            "Desktop yet; use getClientUserId().")


def get_user_config():
    return _UserConfig()


def _unsupported(name):
    def _f(*args, **kwargs):
        raise PluginUnsupportedError(
            name + " is not supported on AyuGram Desktop yet.")
    return _f


send_photo = _unsupported("send_photo")
send_document = _unsupported("send_document")
send_video = _unsupported("send_video")
send_audio = _unsupported("send_audio")
edit_message = _unsupported("edit_message")
send_request = _unsupported("send_request")
run_on_queue = _unsupported("run_on_queue")
get_last_fragment = _unsupported("get_last_fragment")
get_messages_controller = _unsupported("get_messages_controller")
get_send_messages_helper = _unsupported("get_send_messages_helper")
get_connections_manager = _unsupported("get_connections_manager")
get_account_instance = _unsupported("get_account_instance")
)PYSDK";

constexpr auto kAndroidUtilsPy = R"PYSDK(
"""ExteraGram-compatible android_utils shim (desktop subset)."""
import _ayusdk
from base_plugin import PluginUnsupportedError


def run_on_ui_thread(func, delay=0):
    # On desktop there is no separate UI thread the SDK can post to from here;
    # plugin code already runs on the main thread during hook dispatch, so we
    # invoke directly. delay is ignored in the MVP.
    return func()


def log(data):
    _ayusdk.log("android_utils", str(data))


def copy_to_clipboard(text):
    _ayusdk.copy_to_clipboard(str(text))


def _wrapper(name):
    def _f(*args, **kwargs):
        raise PluginUnsupportedError(
            name + " (Android listener wrapper) is not supported on AyuGram "
            "Desktop.")
    return _f


R = _wrapper("R")
OnClickListener = _wrapper("OnClickListener")
OnLongClickListener = _wrapper("OnLongClickListener")
)PYSDK";

constexpr auto kUiInitPy = R"PYSDK(
)PYSDK";

constexpr auto kUiSettingsPy = R"PYSDK(
"""ExteraGram-compatible ui.settings widgets.

These are plain data holders; the C++ settings page reads their attributes to
build native rows. Widgets the desktop MVP cannot render yet (EditText, Custom)
still construct so create_settings() never fails.
"""


class _Item:
    _kind = "unsupported"

    def __init__(self, **kwargs):
        for key, value in kwargs.items():
            setattr(self, key, value)


class Header(_Item):
    _kind = "header"

    def __init__(self, text="", **kwargs):
        super().__init__(text=text, **kwargs)


class Divider(_Item):
    _kind = "divider"

    def __init__(self, text="", **kwargs):
        super().__init__(text=text, **kwargs)


class Switch(_Item):
    _kind = "switch"

    def __init__(self, key="", text="", default=False, subtext="", icon="",
                 on_change=None, on_long_click=None, link_alias="", **kwargs):
        super().__init__(key=key, text=text, default=default, subtext=subtext,
                         icon=icon, on_change=on_change,
                         on_long_click=on_long_click, link_alias=link_alias,
                         **kwargs)


class Selector(_Item):
    _kind = "selector"

    def __init__(self, key="", text="", default=0, items=None, icon="",
                 on_change=None, on_long_click=None, link_alias="", **kwargs):
        super().__init__(key=key, text=text, default=default,
                         items=items or [], icon=icon, on_change=on_change,
                         on_long_click=on_long_click, link_alias=link_alias,
                         **kwargs)


class Input(_Item):
    _kind = "input"

    def __init__(self, key="", text="", default="", subtext="", icon="",
                 on_change=None, on_long_click=None, link_alias="", **kwargs):
        super().__init__(key=key, text=text, default=default, subtext=subtext,
                         icon=icon, on_change=on_change,
                         on_long_click=on_long_click, link_alias=link_alias,
                         **kwargs)


class Text(_Item):
    _kind = "text"

    def __init__(self, text="", subtext="", icon="", accent=False, red=False,
                 on_click=None, on_long_click=None, create_sub_fragment=None,
                 link_alias="", **kwargs):
        super().__init__(text=text, subtext=subtext, icon=icon, accent=accent,
                         red=red, on_click=on_click,
                         on_long_click=on_long_click,
                         create_sub_fragment=create_sub_fragment,
                         link_alias=link_alias, **kwargs)


class EditText(_Item):
    _kind = "edittext"

    def __init__(self, key="", hint="", default="", multiline=False,
                 max_length=0, mask="", on_change=None, **kwargs):
        super().__init__(key=key, hint=hint, default=default,
                         multiline=multiline, max_length=max_length, mask=mask,
                         on_change=on_change, **kwargs)


class Custom(_Item):
    _kind = "custom"

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
)PYSDK";

constexpr auto kUiBulletinPy = R"PYSDK(
"""ExteraGram-compatible ui.bulletin shim mapped onto AyuGram toasts."""
import _ayusdk

DURATION_SHORT = 1500
DURATION_LONG = 2750
DURATION_PROLONG = 5000


class BulletinHelper:
    @staticmethod
    def show_info(message, fragment=None):
        _ayusdk.toast(str(message))

    @staticmethod
    def show_success(message, fragment=None):
        _ayusdk.toast(str(message))

    @staticmethod
    def show_error(message, fragment=None):
        _ayusdk.toast_error(str(message))

    @staticmethod
    def show_simple(text, icon_res_id=None, fragment=None):
        _ayusdk.toast(str(text))

    @staticmethod
    def show_two_line(title, subtitle, icon_res_id=None, fragment=None):
        _ayusdk.toast(str(title) + " - " + str(subtitle))

    @staticmethod
    def show_copied_to_clipboard(message=None, fragment=None):
        _ayusdk.toast(str(message) if message else "Copied to clipboard")
)PYSDK";

constexpr auto kHookUtilsPy = R"PYSDK(
"""Stub for hook_utils: Java reflection has no desktop equivalent."""
from base_plugin import PluginUnsupportedError


def __getattr__(name):
    def _f(*args, **kwargs):
        raise PluginUnsupportedError(
            "hook_utils." + name + " (Java reflection) is not available on "
            "AyuGram Desktop.")
    return _f
)PYSDK";

const SdkFile kSdkFiles[] = {
	{ "base_plugin.py", kBasePluginPy },
	{ "client_utils.py", kClientUtilsPy },
	{ "android_utils.py", kAndroidUtilsPy },
	{ "hook_utils.py", kHookUtilsPy },
	{ "ui/__init__.py", kUiInitPy },
	{ "ui/settings.py", kUiSettingsPy },
	{ "ui/bulletin.py", kUiBulletinPy },
};

void WriteFileIfChanged(const QString &path, const QByteArray &contents) {
	auto existing = QFile(path);
	if (existing.open(QIODevice::ReadOnly)) {
		const auto current = existing.readAll();
		existing.close();
		if (current == contents) {
			return;
		}
	}
	auto file = QFile(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(contents);
		file.close();
	}
}

} // namespace

void DeploySdkPackage() {
	const auto root = PluginRuntime::sdkDir();
	auto dir = QDir(root);
	dir.mkpath(u"."_q);
	dir.mkpath(u"ui"_q);

	for (const auto &entry : kSdkFiles) {
		const auto path = root + QString::fromUtf8(entry.relativePath);
		WriteFileIfChanged(path, QByteArray(entry.contents));
	}
}

} // namespace AyuPlugins
