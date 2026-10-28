/*
 * This is the source code of AyuGram for Desktop.
 *
 * We do not and cannot prevent the use of our code,
 * but be respectful and credit the original author.
 *
 * Copyright @Radolyn, 2026
 */
#include "ayu/ui/text_selection_flyout.h"

#include "ayu/ayu_settings.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/widgets/fields/input_field.h"
#include "styles/style_basic.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QTextCursor>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>

namespace Ayu {
namespace {

constexpr auto kFlyoutHeight = 36;
constexpr auto kBtnWidth = 32;
constexpr auto kBtnHeight = 30;
constexpr auto kFlyoutPadding = 4;
constexpr auto kAnimDuration = 140;

} // namespace

TextSelectionFlyout::TextSelectionFlyout(
	QWidget *parent,
	not_null<Ui::InputField*> field)
: RpWidget(parent)
, _field(field) {
	setFocusPolicy(Qt::NoFocus);
	setAttribute(Qt::WA_NoMousePropagation);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	setMouseTracking(true);

	setupButtons();
	hide();

	const auto raw = _field->rawTextEdit().get();
	QObject::connect(
		raw,
		&QTextEdit::selectionChanged,
		[=] { updateState(); });
	QObject::connect(
		raw,
		&QTextEdit::cursorPositionChanged,
		[=] { updateState(); });

	_field->scrollTop().changes(
	) | rpl::on_next([=] {
		if (!isHidden()) {
			updatePosition();
		}
	}, lifetime());

	_field->geometryValue(
	) | rpl::on_next([=] {
		if (!isHidden()) {
			updatePosition();
		}
	}, lifetime());

	_field->focusedChanges(
	) | rpl::on_next([=](bool focused) {
		if (!focused) {
			crl::on_main(this, [=] {
				if (!_field->hasFocus() && !underMouse()) {
					hideFast();
				}
			});
		}
	}, lifetime());
}

TextSelectionFlyout::~TextSelectionFlyout() = default;

void TextSelectionFlyout::setupButtons() {
	_buttons.clear();

	const auto types = {
		std::tuple{ ButtonType::Bold, u"B"_q, tr::lng_menu_formatting_bold(tr::now) },
		std::tuple{ ButtonType::Italic, u"I"_q, tr::lng_menu_formatting_italic(tr::now) },
		std::tuple{ ButtonType::Underline, u"U"_q, tr::lng_menu_formatting_underline(tr::now) },
		std::tuple{ ButtonType::Strikethrough, u"S"_q, tr::lng_menu_formatting_strike_out(tr::now) },
		std::tuple{ ButtonType::Quote, QString::fromUtf8("”"), tr::lng_menu_formatting_blockquote(tr::now) },
		std::tuple{ ButtonType::Monospace, u"</>"_q, tr::lng_menu_formatting_monospace(tr::now) },
		std::tuple{ ButtonType::Spoiler, QString(), tr::lng_menu_formatting_spoiler(tr::now) },
		std::tuple{ ButtonType::Link, QString(), tr::lng_menu_formatting_link_create(tr::now) },
	};

	auto currentX = kFlyoutPadding;
	for (const auto &[type, text, tip] : types) {
		if (type == ButtonType::Link) {
			currentX += 6; // gap for separator
		}
		const auto r = QRect(
			currentX,
			(kFlyoutHeight - kBtnHeight) / 2,
			kBtnWidth,
			kBtnHeight);
		_buttons.push_back({ type, r, text, tip, false });
		currentX += kBtnWidth + 2;
	}

	resize(currentX - 2 + kFlyoutPadding, kFlyoutHeight);
}

int TextSelectionFlyout::buttonIndexAt(QPoint pos) const {
	for (auto i = 0; i < int(_buttons.size()); ++i) {
		if (_buttons[i].rect.contains(pos)) {
			return i;
		}
	}
	return -1;
}

void TextSelectionFlyout::mouseMoveEvent(QMouseEvent *e) {
	const auto idx = buttonIndexAt(e->pos());
	if (_hoveredIndex != idx) {
		_hoveredIndex = idx;
		if (idx >= 0 && idx < int(_buttons.size())) {
			setToolTip(_buttons[idx].tooltip);
		} else {
			setToolTip(QString());
		}
		update();
	}
}

void TextSelectionFlyout::leaveEventHook(QEvent *e) {
	RpWidget::leaveEventHook(e);
	if (_hoveredIndex != -1) {
		_hoveredIndex = -1;
		setToolTip(QString());
		update();
	}
}

void TextSelectionFlyout::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto idx = buttonIndexAt(e->pos());
	if (idx >= 0 && idx < int(_buttons.size())) {
		executeButton(_buttons[idx].type);
	}
}

void TextSelectionFlyout::executeButton(ButtonType type) {
	const auto raw = _field->rawTextEdit().get();
	auto key = std::optional<Qt::Key>();
	auto modifiers = Qt::KeyboardModifiers(Qt::NoModifier);

	switch (type) {
	case ButtonType::Bold:
		key = Qt::Key_B;
		modifiers = Qt::ControlModifier;
		break;
	case ButtonType::Italic:
		key = Qt::Key_I;
		modifiers = Qt::ControlModifier;
		break;
	case ButtonType::Underline:
		key = Qt::Key_U;
		modifiers = Qt::ControlModifier;
		break;
	case ButtonType::Strikethrough:
		key = Qt::Key_X;
		modifiers = Qt::ControlModifier | Qt::ShiftModifier;
		break;
	case ButtonType::Quote:
		key = Qt::Key_Period;
		modifiers = Qt::ControlModifier | Qt::ShiftModifier;
		break;
	case ButtonType::Monospace:
		key = Qt::Key_M;
		modifiers = Qt::ControlModifier | Qt::ShiftModifier;
		break;
	case ButtonType::Spoiler:
		key = Qt::Key_P;
		modifiers = Qt::ControlModifier | Qt::ShiftModifier;
		break;
	case ButtonType::Link:
		key = Qt::Key_K;
		modifiers = Qt::ControlModifier;
		break;
	}

	if (key) {
		QApplication::postEvent(
			raw,
			new QKeyEvent(QEvent::KeyPress, *key, modifiers));
		QApplication::postEvent(
			raw,
			new QKeyEvent(QEvent::KeyRelease, *key, modifiers));
		crl::on_main(this, [=] {
			updateState();
		});
	}
}

void TextSelectionFlyout::updateState() {
	if (!AyuSettings::getInstance().textSelectionFlyout()) {
		hideFast();
		return;
	}

	const auto raw = _field->rawTextEdit().get();
	const auto cursor = raw->textCursor();
	if (!cursor.hasSelection() || cursor.selectedText().trimmed().isEmpty()) {
		if (!_hiding && !isHidden()) {
			hideAnimated();
		}
		return;
	}

	const auto textWithTags = _field->getTextWithTagsPart(
		cursor.selectionStart(),
		cursor.selectionEnd());
	bool isBold = false;
	bool isItalic = false;
	bool isUnderline = false;
	bool isStrikeOut = false;
	bool isCode = false;
	bool isSpoiler = false;
	bool isBlockquote = false;

	for (const auto &tag : textWithTags.tags) {
		if (tag.id == Ui::InputField::kTagBold) {
			isBold = true;
		} else if (tag.id == Ui::InputField::kTagItalic) {
			isItalic = true;
		} else if (tag.id == Ui::InputField::kTagUnderline) {
			isUnderline = true;
		} else if (tag.id == Ui::InputField::kTagStrikeOut) {
			isStrikeOut = true;
		} else if (tag.id == Ui::InputField::kTagCode || tag.id == Ui::InputField::kTagPre) {
			isCode = true;
		} else if (tag.id == Ui::InputField::kTagSpoiler) {
			isSpoiler = true;
		} else if (tag.id == Ui::InputField::kTagBlockquote || tag.id == Ui::InputField::kTagBlockquoteCollapsed) {
			isBlockquote = true;
		}
	}

	for (auto &btn : _buttons) {
		switch (btn.type) {
		case ButtonType::Bold: btn.active = isBold; break;
		case ButtonType::Italic: btn.active = isItalic; break;
		case ButtonType::Underline: btn.active = isUnderline; break;
		case ButtonType::Strikethrough: btn.active = isStrikeOut; break;
		case ButtonType::Quote: btn.active = isBlockquote; break;
		case ButtonType::Monospace: btn.active = isCode; break;
		case ButtonType::Spoiler: btn.active = isSpoiler; break;
		case ButtonType::Link: btn.active = false; break;
		}
	}

	updatePosition();
	showAnimated();
	update();
}

void TextSelectionFlyout::updatePosition() {
	const auto raw = _field->rawTextEdit().get();
	const auto cursorRect = raw->cursorRect();
	const auto globalCursor = raw->viewport()->mapToGlobal(cursorRect.topLeft());
	const auto parent = parentWidget();
	if (!parent) {
		return;
	}
	const auto localCursor = parent->mapFromGlobal(globalCursor);

	const auto flyoutWidth = width();
	const auto flyoutHeight = height();

	auto targetX = localCursor.x() - (flyoutWidth / 2);
	targetX = std::clamp(targetX, 8, parent->width() - flyoutWidth - 8);

	auto targetY = localCursor.y() - flyoutHeight - 8;
	if (targetY < 8) {
		targetY = localCursor.y() + cursorRect.height() + 8;
	}
	targetY = std::clamp(targetY, 8, parent->height() - flyoutHeight - 8);

	move(targetX, targetY);
	raise();
}

void TextSelectionFlyout::showAnimated() {
	if (!isHidden() && !_hiding) {
		return;
	}
	_hiding = false;
	show();
	raise();
	_opacityAnimation.start(
		[=] {
			update();
		},
		0.,
		1.,
		kAnimDuration);
}

void TextSelectionFlyout::hideAnimated() {
	if (isHidden() || _hiding) {
		return;
	}
	_hiding = true;
	_opacityAnimation.start(
		[=] {
			update();
			if (!_opacityAnimation.animating() && _hiding) {
				hide();
				_hiding = false;
			}
		},
		1.,
		0.,
		kAnimDuration);
}

void TextSelectionFlyout::hideFast() {
	_opacityAnimation.stop();
	_hiding = false;
	hide();
}

void TextSelectionFlyout::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const auto opacity = _opacityAnimation.value(_hiding ? 0. : 1.);
	p.setOpacity(opacity);

	const auto r = rect().marginsRemoved({ 1, 1, 1, 1 });
	p.setPen(st::windowBgOver);
	p.setBrush(st::windowBg);
	p.drawRoundedRect(r, 8, 8);

	// Separator before Link button
	if (_buttons.size() >= 2) {
		const auto linkBtn = _buttons.back();
		const auto sepX = linkBtn.rect.x() - 4;
		p.setPen(st::windowBgOver);
		p.drawLine(sepX, 7, sepX, height() - 7);
	}

	for (auto i = 0; i < int(_buttons.size()); ++i) {
		const auto &btn = _buttons[i];
		const auto isHovered = (i == _hoveredIndex);
		const auto innerRect = btn.rect.marginsRemoved({ 2, 2, 2, 2 });

		if (btn.active) {
			p.setPen(Qt::NoPen);
			auto activeBg = st::windowBgActive->c;
			activeBg.setAlpha(45);
			p.setBrush(activeBg);
			p.drawRoundedRect(innerRect, 6, 6);
		} else if (isHovered) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(innerRect, 6, 6);
		}

		p.setPen(btn.active ? st::windowBgActive : st::windowFg);

		if (btn.type == ButtonType::Spoiler) {
			const auto iconPos = QPoint(
				btn.rect.x() + (btn.rect.width() - st::menuIconSpoiler.width()) / 2,
				btn.rect.y() + (btn.rect.height() - st::menuIconSpoiler.height()) / 2);
			if (btn.active) {
				st::menuIconSpoiler.paint(p, iconPos, width(), st::windowBgActive->c);
			} else {
				st::menuIconSpoiler.paint(p, iconPos, width());
			}
		} else if (btn.type == ButtonType::Link) {
			const auto iconPos = QPoint(
				btn.rect.x() + (btn.rect.width() - st::menuIconLink.width()) / 2,
				btn.rect.y() + (btn.rect.height() - st::menuIconLink.height()) / 2);
			if (btn.active) {
				st::menuIconLink.paint(p, iconPos, width(), st::windowBgActive->c);
			} else {
				st::menuIconLink.paint(p, iconPos, width());
			}
		} else if (btn.type == ButtonType::Monospace) {
			auto font = st::semiboldFont->f;
			font.setFamily(u"monospace"_q);
			font.setBold(true);
			p.setFont(font);
			p.drawText(btn.rect, Qt::AlignCenter, btn.text);
		} else if (btn.type == ButtonType::Quote) {
			auto font = st::semiboldFont->f;
			font.setPointSize(font.pointSize() + 3);
			font.setBold(true);
			p.setFont(font);
			p.drawText(btn.rect.translated(0, -1), Qt::AlignCenter, btn.text);
		} else if (btn.type == ButtonType::Italic) {
			auto font = st::semiboldFont->f;
			font.setItalic(true);
			p.setFont(font);
			p.drawText(btn.rect, Qt::AlignCenter, btn.text);
		} else if (btn.type == ButtonType::Underline) {
			p.setFont(st::semiboldFont);
			p.drawText(btn.rect, Qt::AlignCenter, btn.text);
			const auto textWidth = st::semiboldFont->width(btn.text);
			const auto lineX = btn.rect.x() + (btn.rect.width() - textWidth) / 2;
			const auto lineY = btn.rect.y() + (btn.rect.height() + st::semiboldFont->height) / 2 - 2;
			p.drawLine(lineX, lineY, lineX + textWidth, lineY);
		} else if (btn.type == ButtonType::Strikethrough) {
			p.setFont(st::semiboldFont);
			p.drawText(btn.rect, Qt::AlignCenter, btn.text);
			const auto textWidth = st::semiboldFont->width(btn.text);
			const auto lineX = btn.rect.x() + (btn.rect.width() - textWidth) / 2;
			const auto lineY = btn.rect.y() + btn.rect.height() / 2;
			p.drawLine(lineX - 1, lineY, lineX + textWidth + 1, lineY);
		} else {
			p.setFont(st::semiboldFont);
			p.drawText(btn.rect, Qt::AlignCenter, btn.text);
		}
	}
}

} // namespace Ayu
