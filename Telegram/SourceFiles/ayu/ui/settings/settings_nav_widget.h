/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#pragma once

#include "ui/rp_widget.h"
#include "ui/widgets/scroll_area.h"
#include "settings/settings_type.h"

#include <vector>

namespace style {
struct icon;
} // namespace style

namespace Ui {
class FlatLabel;
class IconButton;
class UserpicButton;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Ayu {

class NavButton;

class SettingsNavWidget final : public Ui::RpWidget {
public:
	SettingsNavWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~SettingsNavWidget();

	void setActiveType(Settings::Type type);
	void setCloseCallback(Fn<void()> callback);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void setupTopBar();
	void setupProfileCover();
	void setupCategories();
	void setActiveButton(NavButton *button);

	const not_null<Window::SessionController*> _controller;
	not_null<UserData*> _user;
	Fn<void()> _closeCallback;

	object_ptr<Ui::RpWidget> _topBar = { nullptr };
	object_ptr<Ui::IconButton> _backButton = { nullptr };
	object_ptr<Ui::FlatLabel> _title = { nullptr };

	object_ptr<Ui::RpWidget> _cover = { nullptr };
	object_ptr<Ui::UserpicButton> _userpic = { nullptr };
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	object_ptr<Ui::IconButton> _qrButton = { nullptr };

	object_ptr<Ui::ScrollArea> _scroll = { nullptr };
	Ui::VerticalLayout *_content = nullptr;

	std::vector<std::pair<Settings::Type, NavButton*>> _typeButtons;
	std::vector<NavButton*> _allButtons;
	NavButton *_activeButton = nullptr;

};

} // namespace Ayu
