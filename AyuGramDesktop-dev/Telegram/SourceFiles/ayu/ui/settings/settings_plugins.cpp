// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_plugins.h"

#include "lang_auto.h"
#include "ayu/plugins/plugin.h"
#include "ayu/plugins/plugin_manager.h"
#include "ayu/plugins/plugin_runtime.h"
#include "ayu/plugins/plugin_settings_store.h"
#include "ayu/ui/boxes/edit_mark_box.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_main.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "core/file_utilities.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "rpl/variable.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;

namespace {

namespace Plugins = ::AyuPlugins;

void FillPluginSettings(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		const QString &pluginId) {
	const auto plugin = Plugins::PluginManager::instance().find(pluginId);
	const auto store = Plugins::PluginManager::settingsStoreFor(pluginId);
	if (!plugin || !store) {
		return;
	}

	const auto items = plugin->createSettings();

	using Type = Plugins::PluginSettingItem::Type;
	for (const auto &item : items) {
		switch (item.type) {
		case Type::Header: {
			Ui::AddSubsectionTitle(container, rpl::single(item.text));
		} break;

		case Type::Divider: {
			if (item.text.isEmpty()) {
				Ui::AddDivider(container);
			} else {
				Ui::AddDividerText(container, rpl::single(item.text));
			}
		} break;

		case Type::Text: {
			AddButtonWithIcon(
				container,
				rpl::single(item.text),
				st::settingsButtonNoIcon);
		} break;

		case Type::Switch: {
			const auto key = item.key;
			const auto def = item.defaultBool;
			AddToggle(
				container,
				rpl::single(item.text),
				[=] { return store->getBool(key, def); },
				[=](bool value) { store->setBool(key, value); });
		} break;

		case Type::Input: {
			const auto key = item.key;
			const auto def = item.defaultString;
			const auto title = item.text;
			const auto label = container->lifetime()
				.make_state<rpl::variable<QString>>(
					store->getString(key, def));
			const auto button = AddButtonWithLabel(
				container,
				rpl::single(item.text),
				label->value(),
				st::settingsButtonNoIcon);
			button->setClickedCallback([=] {
				const auto current = store->getString(key, def);
				controller->show(Box<EditMarkBox>(
					rpl::single(title),
					current,
					def,
					[=](const QString &value) {
						store->setString(key, value);
						*label = value;
					}));
			});
		} break;

		case Type::Selector: {
			const auto key = item.key;
			const auto def = item.defaultInt;
			const auto options = item.items;
			const auto labelFor = [=](int idx) {
				return (idx >= 0 && idx < options.size())
					? options[idx]
					: QString();
			};
			const auto initial = std::clamp(
				store->getInt(key, def),
				0,
				std::max(0, int(options.size()) - 1));
			const auto label = container->lifetime()
				.make_state<rpl::variable<QString>>(labelFor(initial));
			const auto button = AddButtonWithLabel(
				container,
				rpl::single(item.text),
				label->value(),
				st::settingsButtonNoIcon);
			button->setClickedCallback([=] {
				if (options.isEmpty()) {
					return;
				}
				const auto current = std::clamp(
					store->getInt(key, def),
					0,
					int(options.size()) - 1);
				const auto next = (current + 1) % options.size();
				store->setInt(key, next);
				*label = labelFor(next);
			});
		} break;

		default: {
			AddButtonWithIcon(
				container,
				rpl::single(item.text.isEmpty()
					? tr::ayu_PluginSettingUnsupported(tr::now)
					: item.text),
				st::settingsButtonNoIcon);
		} break;
		}
	}
}

void ShowPluginConfigBox(
		not_null<Window::SessionController*> controller,
		const QString &pluginId,
		Fn<void()> onChanged) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto entries = Plugins::PluginManager::instance().list();
		auto found = std::optional<Plugins::PluginEntry>();
		for (const auto &entry : entries) {
			if (entry.metadata.id == pluginId) {
				found = entry;
				break;
			}
		}
		if (!found) {
			box->closeBox();
			return;
		}

		box->setTitle(rpl::single(found->metadata.name));

		const auto content = box->verticalLayout();

		Ui::AddSkip(content);
		AddToggle(
			content,
			tr::ayu_PluginEnabled(),
			[=] {
				return Plugins::PluginManager::instance()
					.isEnabled(pluginId);
			},
			[=](bool value) {
				auto &manager = Plugins::PluginManager::instance();
				if (value) {
					auto error = QString();
					if (!manager.enable(pluginId, &error) && !error.isEmpty()) {
						controller->showToast(error);
					}
				} else {
					manager.disable(pluginId);
				}
				if (onChanged) {
					onChanged();
				}
			});

		if (!found->metadata.version.isEmpty()) {
			Ui::AddDividerText(
				content,
				rpl::single(tr::ayu_PluginVersion(tr::now)
					+ u" "_q
					+ found->metadata.version));
		}

		if (Plugins::PluginManager::instance().isEnabled(pluginId)) {
			FillPluginSettings(content, controller, pluginId);
		}

		Ui::AddSkip(content);
		const auto uninstall = AddButtonWithIcon(
			content,
			tr::ayu_PluginUninstall(),
			st::settingsAttentionButton);
		uninstall->setClickedCallback([=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_PluginUninstallConfirm(tr::rich),
				.confirmed = [=](Fn<void()> &&close) {
					Plugins::PluginManager::instance().uninstall(pluginId);
					close();
					box->closeBox();
					if (onChanged) {
						onChanged();
					}
				},
				.confirmText = tr::lng_box_yes(),
			}));
		});

		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void BuildPluginList(SectionBuilder &builder) {
	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;
			const auto controller = wctx.controller;

			const auto rebuild
				= container->lifetime().make_state<Fn<void()>>();

			const auto wrap = container->add(
				object_ptr<Ui::VerticalLayout>(container));

			*rebuild = [=] {
				while (wrap->count()) {
					delete wrap->widgetAt(0);
				}
				const auto entries =
					Plugins::PluginManager::instance().list();
				if (entries.empty()) {
					Ui::AddDividerText(
						wrap,
						tr::ayu_PluginsEmpty());
					wrap->resizeToWidth(wrap->width());
					return;
				}
				for (const auto &entry : entries) {
					const auto id = entry.metadata.id;
					const auto button = AddButtonWithLabel(
						wrap,
						rpl::single(entry.metadata.name),
						rpl::single(entry.enabled
							? tr::ayu_PluginEnabled(tr::now)
							: tr::ayu_PluginDisabled(tr::now)),
						st::settingsButtonNoIcon);
					button->setClickedCallback([=] {
						ShowPluginConfigBox(controller, id, *rebuild);
					});
				}
				wrap->resizeToWidth(wrap->width());
			};
			(*rebuild)();
		}, [&](const SearchContext &sctx) {
			sctx.entries->push_back({
				.id = u"ayu/plugins/list"_q,
				.title = tr::ayu_CategoryPlugins(tr::now),
				.section = sctx.sectionId,
			});
		});
	});
}

void BuildPluginsActions(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addButton({
		.id = u"ayu/plugins/openFolder"_q,
		.title = tr::ayu_PluginsOpenFolder(),
		.icon = { &st::menuIconShowInFolder },
		.onClick = [=] {
			File::ShowInFolder(Plugins::PluginRuntime::pluginsDir());
		},
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_PluginsDescription());
}

const auto kMeta = BuildHelper({
	.id = AyuPlugins::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryPlugins,
	.icon = &st::menuIconChannel,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(tr::ayu_CategoryPlugins());
	BuildPluginList(builder);
	BuildPluginsActions(builder);
});

} // namespace

rpl::producer<QString> AyuPlugins::title() {
	return tr::ayu_CategoryPlugins();
}

AyuPlugins::AyuPlugins(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuPlugins::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuPluginsId() {
	return AyuPlugins::Id();
}

} // namespace Settings
