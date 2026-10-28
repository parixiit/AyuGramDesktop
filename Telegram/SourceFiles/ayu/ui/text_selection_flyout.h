/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

#include <vector>

namespace Ui {
class InputField;
} // namespace Ui

namespace Ayu {

class TextSelectionFlyout final : public Ui::RpWidget {
public:
	TextSelectionFlyout(
		QWidget *parent,
		not_null<Ui::InputField*> field);
	~TextSelectionFlyout();

	void showAnimated();
	void hideAnimated();
	void hideFast();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *e) override;

private:
	enum class ButtonType {
		Bold,
		Italic,
		Underline,
		Strikethrough,
		Quote,
		Monospace,
		Spoiler,
		Link,
	};

	struct Button {
		ButtonType type;
		QRect rect;
		QString text;
		bool active = false;
	};

	void setupButtons();
	void updateState();
	void updatePosition();
	void executeButton(ButtonType type);
	[[nodiscard]] int buttonIndexAt(QPoint pos) const;

	const not_null<Ui::InputField*> _field;
	std::vector<Button> _buttons;
	int _hoveredIndex = -1;
	bool _hiding = false;
	Ui::Animations::Simple _opacityAnimation;

};

} // namespace Ayu
