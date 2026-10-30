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
namespace Window::Theme { [[nodiscard]] bool IsNightMode(); }
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_emoji_status_panel.h"
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
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_dialogs.h"
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

	int resizeGetHeight(int newWidth) override {
		resize(newWidth, 42);
		return 42;
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
	resize(width(), 40);
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

class NavSeparator final : public Ui::RpWidget {
public:
	NavSeparator(QWidget *parent, int marginX = 16, int marginY = 4)
	: RpWidget(parent)
	, _marginX(marginX)
	, _marginY(marginY) {
		resize(width(), _marginY * 2 + 1);
	}
protected:
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		const auto color = Window::Theme::IsNightMode()
			? QColor(255, 255, 255, 22)
			: QColor(0, 0, 0, 20);
		p.fillRect(QRect(_marginX, _marginY, width() - 2 * _marginX, 1), color);
	}
private:
	int _marginX;
	int _marginY;
};

void NavButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto r = rect().marginsRemoved({ 6, 2, 6, 2 });
	if (_active || isOver()) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(r, 4, 4);
	}

	paintRipple(p, 0, 0);

	if (_active) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		const auto pillHeight = 16;
		const auto pillY = (height() - pillHeight) / 2;
		p.drawRoundedRect(QRect(9, pillY, 3, pillHeight), 1.5, 1.5);
	}

	const auto softFg = anim::color(st::windowFg->c, st::windowSubTextFg->c, 0.20);
	const auto fg = _active
		? st::windowBgActive->c
		: isOver()
		? st::windowFg->c
		: softFg;

	if (_icon) {
		const auto iconX = 20;
		const auto iconY = (height() - _icon->height()) / 2;
		_icon->paint(p, QPoint(iconX, iconY), width(), fg);
	}

	p.setFont(st::normalFont);
	p.setPen(fg);
	const auto textX = 64;
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
		Data::PeerUpdate::Flag::Photo
		| Data::PeerUpdate::Flag::Name
		| Data::PeerUpdate::Flag::EmojiStatus
	) | rpl::on_next([=] {
		if (_name) {
			_name->setText(_user->name());
			updateCoverGeometry(width());
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
	_cover->resize(width(), 74);

	_userpic.create(
		_cover.data(),
		_controller,
		_user,
		Ui::UserpicButton::Role::OpenPhoto,
		Ui::UserpicButton::Source::PeerPhoto,
		st::mainMenuUserpic);
	_userpic->moveToLeft(16, 12);

	_name.create(_cover.data(), _user->name(), st::infoProfileCover.name);
	_name->moveToLeft(88, 11);

	const auto username = !_user->username().isEmpty()
		? ("@" + _user->username())
		: !_user->phone().isEmpty()
		? _user->phone()
		: tr::lng_status_online(tr::now);
	_status.create(_cover.data(), username, st::infoProfileStatus);
	_status->moveToLeft(88, 40);

	_qrButton.create(_cover.data(), st::infoProfileLabeledButtonQr);
	_qrButton->moveToRight(16, 16);
	_qrButton->setClickedCallback([=, show = _controller->uiShow()] {
		Ui::DefaultShowFillPeerQrBoxCallback(show, _user);
	});

	_emojiStatusPanel = std::make_unique<Info::Profile::EmojiStatusPanel>();
	_badge = std::make_unique<Info::Profile::Badge>(
		_cover.data(),
		st::settingsCoverBadge,
		&_controller->session(),
		Info::Profile::BadgeContentForPeer(_user),
		_emojiStatusPanel.get(),
		[=] {
			return _controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer);
		},
		0,
		Info::Profile::BadgeType::Premium | Info::Profile::BadgeType::Verified);

	_badge->setPremiumClickCallback([=] {
		if (const auto widget = _badge->widget()) {
			_emojiStatusPanel->show(
				_controller,
				widget,
				_badge->sizeTag());
		}
	});

	_badge->updated() | rpl::on_next([=] {
		updateCoverGeometry(width());
	}, lifetime());

	updateCoverGeometry(width());
}

void SettingsNavWidget::updateCoverGeometry(int newWidth) {
	if (!_cover || !_name) {
		return;
	}
	const auto nameLeft = 88;
	const auto nameTop = 13;
	const auto qrWidth = (_qrButton && !_qrButton->isHidden())
		? (_qrButton->width() + 16)
		: 0;
	auto nameWidth = newWidth - nameLeft - 16 - qrWidth;
	if (const auto badgeWidget = _badge ? _badge->widget() : nullptr) {
		nameWidth -= st::infoVerifiedCheckPosition.x() + badgeWidget->width();
	}
	nameWidth = std::max(0, nameWidth);

	_name->resizeToNaturalWidth(nameWidth);
	_name->moveToLeft(nameLeft, nameTop, newWidth);

	if (_badge && _badge->widget()) {
		const auto badgeLeft = nameLeft + _name->width();
		const auto badgeTop = nameTop;
		const auto badgeBottom = nameTop + _name->height();
		_badge->move(badgeLeft, badgeTop, badgeBottom);
	}
	if (_status) {
		_status->resizeToWidth(std::max(0, newWidth - nameLeft - 16 - qrWidth));
	}
	if (_qrButton) {
		_qrButton->moveToRight(16, 16);
	}
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
				if (_activeType == type) {
					return;
				}
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

	_content->add(object_ptr<NavSeparator>(_content, 16, 4));

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
	addCategory(
		&st::menuIconPowerUsage,
		tr::lng_settings_power_menu(tr::now),
		Settings::PowerSavingId());
	addCategory(
		&st::menuIconShowInFolder,
		tr::lng_settings_section_filters(tr::now),
		Settings::FoldersId());
	addCategory(
		&st::menuIconUnmute,
		tr::lng_settings_section_devices(tr::now),
		Settings::SessionsId());
	addCategory(
		&st::menuIconTranslate,
		tr::lng_settings_language(tr::now),
		Settings::LanguageId());
	addCategory(
		&st::menuIconSettings,
		tr::lng_settings_advanced(tr::now),
		Settings::AdvancedId());

	_content->add(object_ptr<NavSeparator>(_content, 16, 4));

	addCategory(
		&st::menuIconGiftPremium,
		tr::lng_premium_summary_title(tr::now),
		Settings::PremiumId());

	_content->add(object_ptr<NavSeparator>(_content, 16, 4));

	addAction(
		&st::menuIconFaq,
		tr::lng_settings_faq(tr::now),
		[=] {
			::Settings::OpenFaq(_controller);
		});

	Ui::AddSkip(_content, 12);
	_content->resizeToWidth(width());
	_scroll->show();
	_content->show();
}

void SettingsNavWidget::setActiveType(Settings::Type type) {
	_activeType = type;
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
		updateCoverGeometry(width());
	}
	if (_scroll) {
		const auto scrollY = topBarHeight + coverHeight;
		_scroll->setGeometryToLeft(0, scrollY, width(), height() - scrollY);
		if (_content) {
			_content->resizeToWidth(width());
		}
	}
}

void SettingsNavWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::windowBg);
	const auto borderColor = Window::Theme::IsNightMode()
		? QColor(255, 255, 255, 22)
		: QColor(0, 0, 0, 20);
	p.fillRect(QRect(width() - 1, 0, 1, height()), borderColor);
}

} // namespace Ayu
