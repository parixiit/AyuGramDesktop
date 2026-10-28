/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#include "ayu/ui/settings/settings_nav_widget.h"

#include "ayu/ui/settings/settings_main.h"
#include "boxes/language_box.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_active_sessions.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_local_storage.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_notifications.h"
#include "settings/sections/settings_premium.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/settings_power_saving.h"
#include "ui/boxes/peer_qr_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

namespace Ayu {

class NavButton final : public Ui::RippleButton {
public:
	NavButton(
		QWidget *parent,
		const style::icon *icon,
		const QString &text,
		Fn<void()> onClick);

	void setActive(bool active);
	[[nodiscard]] bool isActive() const {
		return _active;
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

private:
	const style::icon *_icon = nullptr;
	QString _text;
	bool _active = false;

};

NavButton::NavButton(
	QWidget *parent,
	const style::icon *icon,
	const QString &text,
	Fn<void()> onClick)
: RippleButton(parent, st::defaultRippleAnimation)
, _icon(icon)
, _text(text) {
	resize(width(), 42);
	setClickedCallback(std::move(onClick));
}

void NavButton::setActive(bool active) {
	if (_active != active) {
		_active = active;
		update();
	}
}

void NavButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

void NavButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto r = rect().marginsRemoved({ 8, 2, 8, 2 });
	if (_active || isOver()) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(r, 6, 6);
	}

	paintRipple(p, 0, 0);

	if (_active) {
		// Modern selection indicator
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		const auto pillHeight = 18;
		const auto pillY = (height() - pillHeight) / 2;
		p.drawRoundedRect(QRect(11, pillY, 3, pillHeight), 1.5, 1.5);
	}

	if (_icon) {
		const auto iconX = 22;
		const auto iconY = (height() - _icon->height()) / 2;
		if (_active) {
			_icon->paint(p, QPoint(iconX, iconY), width(), st::windowBgActive->c);
		} else {
			_icon->paint(p, QPoint(iconX, iconY), width());
		}
	}

	p.setFont(st::semiboldFont);
	p.setPen(_active ? st::windowBgActive : st::windowFg);
	const auto textX = 54;
	const auto textW = width() - textX - 16;
	p.drawText(
		QRect(textX, 0, textW, height()),
		Qt::AlignVCenter | Qt::AlignLeft,
		_text);
}

SettingsNavWidget::SettingsNavWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _user(controller->session().user()) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	_user->updateFull();

	setupTopBar();
	setupProfileCover();
	setupCategories();

	_controller->session().changes().peerUpdates(
		_user,
		Data::PeerUpdate::Flag::Photo | Data::PeerUpdate::Flag::Name
	) | rpl::on_next([=] {
		if (_name) {
			_name->setText(_user->name());
		}
		update();
	}, lifetime());
}

SettingsNavWidget::~SettingsNavWidget() = default;

void SettingsNavWidget::setupTopBar() {
	_topBar.create(this);
	_topBar->resize(width(), st::infoLayerTopBar.height);

	_backButton.create(_topBar.data(), st::infoLayerTopBar.back);
	_backButton->moveToLeft(
		4,
		(st::infoLayerTopBar.height - _backButton->height()) / 2);
	_backButton->setClickedCallback([=] {
		if (_closeCallback) {
			_closeCallback();
		}
	});

	_title.create(
		_topBar.data(),
		tr::lng_menu_settings(tr::now),
		st::infoLayerTopBar.title);
	const auto titleLeft = _backButton->x() + _backButton->width() + 8;
	_title->moveToLeft(
		titleLeft,
		(st::infoLayerTopBar.height - _title->height()) / 2);
}

void SettingsNavWidget::setupProfileCover() {
	_cover.create(this);
	_cover->resize(width(), 70);

	_userpic.create(
		_cover.data(),
		_controller,
		_user,
		Ui::UserpicButton::Role::OpenPhoto,
		Ui::UserpicButton::Source::PeerPhoto,
		st::mainMenuUserpic);
	_userpic->moveToLeft(16, 11);

	_name.create(_cover.data(), _user->name(), st::infoProfileCover.name);
	_name->moveToLeft(74, 14);

	const auto username = !_user->username().isEmpty()
		? ("@" + _user->username())
		: !_user->phone().isEmpty()
		? _user->phone()
		: tr::lng_status_online(tr::now);
	_status.create(_cover.data(), username, st::defaultFlatLabel);
	_status->moveToLeft(74, 38);

	_qrButton.create(_cover.data(), st::infoProfileLabeledButtonQr);
	_qrButton->moveToRight(16, 16);
	_qrButton->setClickedCallback([=, show = _controller->uiShow()] {
		Ui::DefaultShowFillPeerQrBoxCallback(show, _user);
	});
}

void SettingsNavWidget::setupCategories() {
	_scroll.create(this, st::defaultScrollArea);
	_content = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(_scroll.data()));

	const auto addCategory = [&](
			const style::icon *icon,
			const QString &text,
			Settings::Type type) {
		auto btn = _content->add(object_ptr<NavButton>(
			_content,
			icon,
			text,
			[=] {
				setActiveType(type);
				_controller->showSettings(
					type,
					Window::SectionShow::Way::ClearStack);
			}));
		btn->resize(btn->width(), 42);
		_typeButtons.push_back({ type, btn });
		_allButtons.push_back(btn);
		return btn;
	};

	const auto addAction = [&](
			const style::icon *icon,
			const QString &text,
			Fn<void()> action) {
		auto btn = _content->add(object_ptr<NavButton>(
			_content,
			icon,
			text,
			[=] {
				action();
			}));
		btn->resize(btn->width(), 42);
		_allButtons.push_back(btn);
		return btn;
	};

	addCategory(
		&st::menuIconPremium,
		tr::ayu_AyuPreferences(tr::now),
		Settings::AyuMain::Id());
	addCategory(
		&st::menuIconProfile,
		tr::lng_settings_my_account(tr::now),
		Settings::InformationId());
	addCategory(
		&st::menuIconPalette,
		tr::lng_settings_section_chat_settings(tr::now),
		Settings::ChatId());
	addCategory(
		&st::menuIconLock,
		tr::lng_settings_section_privacy(tr::now),
		Settings::PrivacySecurityId());
	addCategory(
		&st::menuIconNotifications,
		tr::lng_settings_section_notify(tr::now),
		Settings::NotificationsId());
	addCategory(
		&st::menuIconStats,
		tr::lng_local_storage_title(tr::now),
		Settings::LocalStorageId());
	addAction(
		&st::menuIconPowerUsage,
		tr::lng_settings_power_menu(tr::now),
		[=] {
			_controller->show(Box(
				::Settings::PowerSavingBox,
				PowerSaving::Flags()));
		});
	addCategory(
		&st::menuIconShowInFolder,
		tr::lng_settings_section_filters(tr::now),
		Settings::FoldersId());
	addCategory(
		&st::menuIconUnmute,
		tr::lng_settings_section_devices(tr::now),
		Settings::SessionsId());
	addAction(
		&st::menuIconTranslate,
		tr::lng_settings_language(tr::now),
		[=] {
			static auto Guard = base::binary_guard();
			Guard = LanguageBox::Show(_controller);
		});
	addCategory(
		&st::menuIconSettings,
		tr::lng_settings_advanced(tr::now),
		Settings::AdvancedId());

	Ui::AddDivider(_content);
	Ui::AddSkip(_content, 4);

	addCategory(
		&st::menuIconGiftPremium,
		tr::lng_premium_summary_title(tr::now),
		Settings::PremiumId());
	addAction(
		&st::menuIconFaq,
		tr::lng_settings_faq(tr::now),
		[=] {
			::Settings::OpenFaq(_controller);
		});

	Ui::AddSkip(_content, 12);
}

void SettingsNavWidget::setActiveType(Settings::Type type) {
	for (const auto &[btnType, btn] : _typeButtons) {
		if (btnType == type) {
			setActiveButton(btn);
			return;
		}
	}
}

void SettingsNavWidget::setActiveButton(NavButton *button) {
	if (_activeButton == button) {
		return;
	}
	_activeButton = button;
	for (const auto btn : _allButtons) {
		btn->setActive(btn == _activeButton);
	}
}

void SettingsNavWidget::setCloseCallback(Fn<void()> callback) {
	_closeCallback = std::move(callback);
}

void SettingsNavWidget::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	const auto topBarHeight = _topBar ? _topBar->height() : 0;
	const auto coverHeight = _cover ? _cover->height() : 0;
	if (_topBar) {
		_topBar->setGeometryToLeft(0, 0, width(), topBarHeight);
	}
	if (_cover) {
		_cover->setGeometryToLeft(0, topBarHeight, width(), coverHeight);
		if (_name) {
			_name->resizeToWidth(std::max(0, width() - 74 - 56));
		}
		if (_status) {
			_status->resizeToWidth(std::max(0, width() - 74 - 56));
		}
		if (_qrButton) {
			_qrButton->moveToRight(16, 16);
		}
	}
	if (_scroll) {
		const auto scrollY = topBarHeight + coverHeight;
		_scroll->setGeometryToLeft(0, scrollY, width(), height() - scrollY);
	}
}

void SettingsNavWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::windowBg);
}

} // namespace Ayu
