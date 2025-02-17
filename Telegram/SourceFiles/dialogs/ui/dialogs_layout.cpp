/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_layout.h"

#include "data/data_abstract_structure.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "dialogs/dialogs_list.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "storage/localstorage.h"
#include "ui/empty_userpic.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/unread_badge.h"
#include "ui/ui_utility.h"
#include "core/ui_integration.h"
#include "lang/lang_keys.h"
#include "support/support_helper.h"
#include "main/main_session.h"
#include "history/view/history_view_send_action.h"
#include "history/view/history_view_item_preview.h"
#include "history/history_unread_things.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"

namespace Dialogs::Ui {
namespace {

// Show all dates that are in the last 20 hours in time format.
constexpr int kRecentlyInSeconds = 20 * 3600;
const auto kPsaBadgePrefix = "cloud_lng_badge_psa_";

[[nodiscard]] bool ShowUserBotIcon(not_null<UserData*> user) {
	return user->isBot() && !user->isSupport() && !user->isRepliesChat();
}

[[nodiscard]] bool ShowSendActionInDialogs(History *history) {
	return history
		&& (!history->peer->isUser()
			|| history->peer->asUser()->onlineTill > 0);
}

void PaintRowTopRight(Painter &p, const QString &text, QRect &rectForName, bool active, bool selected) {
	const auto width = st::dialogsDateFont->width(text);
	rectForName.setWidth(rectForName.width() - width - st::dialogsDateSkip);
	p.setFont(st::dialogsDateFont);
	p.setPen(active ? st::dialogsDateFgActive : (selected ? st::dialogsDateFgOver : st::dialogsDateFg));
	p.drawText(rectForName.left() + rectForName.width() + st::dialogsDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, text);
}

void PaintRowDate(Painter &p, QDateTime date, QRect &rectForName, bool active, bool selected) {
	const auto now = QDateTime::currentDateTime();
	const auto &lastTime = date;
	const auto nowDate = now.date();
	const auto lastDate = lastTime.date();

	const auto dt = [&] {
		const auto wasSameDay = (lastDate == nowDate);
		const auto wasRecently = qAbs(lastTime.secsTo(now)) < kRecentlyInSeconds;
		if (wasSameDay || wasRecently) {
			return lastTime.toString(cTimeFormat());
		} else if (lastDate.year() == nowDate.year()
			&& lastDate.weekNumber() == nowDate.weekNumber()) {
			return langDayOfWeek(lastDate);
		} else {
			return lastDate.toString(cDateFormat());
		}
	}();
	PaintRowTopRight(p, dt, rectForName, active, selected);
}

void PaintNarrowCounter(
		Painter &p,
		bool displayUnreadCounter,
		bool displayUnreadMark,
		bool displayMentionBadge,
		bool displayReactionBadge,
		int unreadCount,
		bool selected,
		bool active,
		bool unreadMuted,
		bool mentionOrReactionMuted) {
	auto skipBeforeMention = 0;
	if (displayUnreadCounter || displayUnreadMark) {
		const auto counter = (unreadCount > 0)
			? QString::number(unreadCount)
			: QString();
		const auto allowDigits = (displayMentionBadge
			|| displayReactionBadge)
			? 1
			: 3;
		const auto unreadRight = st::dialogsPadding.x()
			+ st::dialogsPhotoSize;
		const auto unreadTop = st::dialogsPadding.y()
			+ st::dialogsPhotoSize
			- st::dialogsUnreadHeight;

		UnreadBadgeStyle st;
		st.active = active;
		st.selected = selected;
		st.muted = unreadMuted;
		const auto badge = PaintUnreadBadge(
			p,
			counter,
			unreadRight,
			unreadTop,
			st,
			allowDigits);
		skipBeforeMention += badge.width() + st.padding;
	}
	if (displayMentionBadge || displayReactionBadge) {
		const auto counter = QString();
		const auto unreadRight = st::dialogsPadding.x()
			+ st::dialogsPhotoSize
			- skipBeforeMention;
		const auto unreadTop = st::dialogsPadding.y()
			+ st::dialogsPhotoSize
			- st::dialogsUnreadHeight;

		UnreadBadgeStyle st;
		st.sizeId = displayMentionBadge
			? UnreadBadgeInDialogs
			: UnreadBadgeReactionInDialogs;
		st.active = active;
		st.selected = selected;
		st.muted = mentionOrReactionMuted;
		st.padding = 0;
		st.textTop = 0;
		const auto badge = PaintUnreadBadge(
			p,
			counter,
			unreadRight,
			unreadTop,
			st);
		(displayMentionBadge
			? (st.active
				? st::dialogsUnreadMentionActive
				: st.selected
				? st::dialogsUnreadMentionOver
				: st::dialogsUnreadMention)
			: (st.active
				? st::dialogsUnreadReactionActive
				: st.selected
				? st::dialogsUnreadReactionOver
				: st::dialogsUnreadReaction)).paintInCenter(p, badge);
	}
}

int PaintWideCounter(
		Painter &p,
		int texttop,
		int availableWidth,
		int fullWidth,
		bool displayUnreadCounter,
		bool displayUnreadMark,
		bool displayMentionBadge,
		bool displayReactionBadge,
		bool displayPinnedIcon,
		int unreadCount,
		bool active,
		bool selected,
		bool unreadMuted,
		bool mentionOrReactionMuted) {
	const auto initial = availableWidth;
	if (displayUnreadCounter || displayUnreadMark) {
		const auto counter = (unreadCount > 0)
			? QString::number(unreadCount)
			: QString();
		const auto unreadRight = fullWidth
			- st::dialogsPadding.x();
		const auto unreadTop = texttop
			+ st::dialogsTextFont->ascent
			- st::dialogsUnreadFont->ascent
			- (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;

		UnreadBadgeStyle st;
		st.active = active;
		st.selected = selected;
		st.muted = unreadMuted;
		const auto badge = PaintUnreadBadge(
			p,
			counter,
			unreadRight,
			unreadTop,
			st);
		availableWidth -= badge.width() + st.padding;
	} else if (displayPinnedIcon) {
		const auto &icon = active
			? st::dialogsPinnedIconActive
			: selected
			? st::dialogsPinnedIconOver
			: st::dialogsPinnedIcon;
		icon.paint(
			p,
			fullWidth - st::dialogsPadding.x() - icon.width(),
			texttop,
			fullWidth);
		availableWidth -= icon.width() + st::dialogsUnreadPadding;
	}
	if (displayMentionBadge || displayReactionBadge) {
		const auto counter = QString();
		const auto unreadRight = fullWidth
			- st::dialogsPadding.x()
			- (initial - availableWidth);
		const auto unreadTop = texttop
			+ st::dialogsTextFont->ascent
			- st::dialogsUnreadFont->ascent
			- (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;

		UnreadBadgeStyle st;
		st.sizeId = displayMentionBadge
			? UnreadBadgeInDialogs
			: UnreadBadgeReactionInDialogs;
		st.active = active;
		st.selected = selected;
		st.muted = mentionOrReactionMuted;
		st.padding = 0;
		st.textTop = 0;
		const auto badge = PaintUnreadBadge(
			p,
			counter,
			unreadRight,
			unreadTop,
			st);
		(displayMentionBadge
			? (st.active
				? st::dialogsUnreadMentionActive
				: st.selected
				? st::dialogsUnreadMentionOver
				: st::dialogsUnreadMention)
			: (st.active
				? st::dialogsUnreadReactionActive
				: st.selected
				? st::dialogsUnreadReactionOver
				: st::dialogsUnreadReaction)).paintInCenter(p, badge);
		availableWidth -= badge.width()
			+ st.padding
			+ st::dialogsUnreadPadding;
	}
	return availableWidth;
}

void PaintListEntryText(
		Painter &p,
		QRect rect,
		bool active,
		bool selected,
		not_null<const Row*> row) {
	if (rect.isEmpty()) {
		return;
	}
	row->validateListEntryCache();
	const auto &palette = row->folder()
		? (active
			? st::dialogsTextPaletteArchiveActive
			: selected
			? st::dialogsTextPaletteArchiveOver
			: st::dialogsTextPaletteArchive)
		: (active
			? st::dialogsTextPaletteActive
			: selected
			? st::dialogsTextPaletteOver
			: st::dialogsTextPalette);
	const auto &color = active
		? st::dialogsTextFgActive
		: selected
		? st::dialogsTextFgOver
		: st::dialogsTextFg;
	p.setTextPalette(palette);
	p.setFont(st::dialogsTextFont);
	p.setPen(color);
	row->listEntryCache().drawElided(
		p,
		rect.left(),
		rect.top(),
		rect.width(),
		rect.height() / st::dialogsTextFont->height);
	p.restoreTextPalette();
}

enum class Flag {
	Active           = 0x01,
	Selected         = 0x02,
	SearchResult     = 0x04,
	SavedMessages    = 0x08,
	RepliesMessages  = 0x10,
	AllowUserOnline  = 0x20,
	VideoPaused      = 0x40,
};
inline constexpr bool is_flag_type(Flag) { return true; }

template <typename PaintItemCallback, typename PaintCounterCallback>
void paintRow(
		Painter &p,
		not_null<const BasicRow*> row,
		not_null<Entry*> entry,
		Dialogs::Key chat,
		VideoUserpic *videoUserpic,
		FilterId filterId,
		PeerData *from,
		Ui::PeerBadge &fromBadge,
		Fn<void()> customEmojiRepaint,
		const Ui::Text::String &fromName,
		const HiddenSenderInfo *hiddenSenderInfo,
		HistoryItem *item,
		const Data::Draft *draft,
		QDateTime date,
		int fullWidth,
		base::flags<Flag> flags,
		crl::time ms,
		PaintItemCallback &&paintItemCallback,
		PaintCounterCallback &&paintCounterCallback) {
	const auto supportMode = entry->session().supportMode();
	if (supportMode) {
		draft = nullptr;
	}

	auto active = (flags & Flag::Active);
	auto selected = (flags & Flag::Selected);
	auto fullRect = QRect(0, 0, fullWidth, st::dialogsRowHeight);
	auto bg = active
		? st::dialogsBgActive
		: (selected
			? st::dialogsBgOver
			: st::dialogsBg);
	auto ripple = active
		? st::dialogsRippleBgActive
		: st::dialogsRippleBg;
	p.fillRect(fullRect, bg);
	row->paintRipple(p, 0, 0, fullWidth, &ripple->c);

	const auto history = chat.history();

	if (flags & Flag::SavedMessages) {
		EmptyUserpic::PaintSavedMessages(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	} else if (flags & Flag::RepliesMessages) {
		EmptyUserpic::PaintRepliesMessages(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	} else if (from) {
		row->paintUserpic(
			p,
			from,
			videoUserpic,
			(flags & Flag::AllowUserOnline) ? history : nullptr,
			ms,
			active,
			fullWidth,
			(flags & Flag::VideoPaused));
	} else if (hiddenSenderInfo) {
		hiddenSenderInfo->emptyUserpic.paint(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	} else {
		entry->paintUserpicLeft(
			p,
			row->userpicView(),
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	}

	auto nameleft = st::dialogsPadding.x()
		+ st::dialogsPhotoSize
		+ st::dialogsPhotoPadding;
	if (fullWidth <= nameleft) {
		if (!draft && item && !item->isEmpty()) {
			paintCounterCallback();
		}
		return;
	}

	auto namewidth = fullWidth - nameleft - st::dialogsPadding.x();
	auto rectForName = QRect(
		nameleft,
		st::dialogsPadding.y() + st::dialogsNameTop,
		namewidth,
		st::msgNameFont->height);

	const auto promoted = (history && history->useTopPromotion())
		&& !(flags & Flag::SearchResult);
	if (promoted) {
		const auto type = history->topPromotionType();
		const auto custom = type.isEmpty()
			? QString()
			: Lang::GetNonDefaultValue(kPsaBadgePrefix + type.toUtf8());
		const auto text = type.isEmpty()
			? tr::lng_proxy_sponsor(tr::now)
			: custom.isEmpty()
			? tr::lng_badge_psa_default(tr::now)
			: custom;
		PaintRowTopRight(p, text, rectForName, active, selected);
	} else if (from) {
		if (const auto chatTypeIcon = ChatTypeIcon(from, active, selected)) {
			chatTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
			rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
		}
	}
	auto texttop = st::dialogsPadding.y()
		+ st::msgNameFont->height
		+ st::dialogsSkip;
	if (promoted && !history->topPromotionMessage().isEmpty()) {
		auto availableWidth = namewidth;
		p.setFont(st::dialogsTextFont);
		if (history->cloudDraftTextCache.isEmpty()) {
			history->cloudDraftTextCache.setText(
				st::dialogsTextStyle,
				history->topPromotionMessage(),
				DialogTextOptions());
		}
		p.setPen(active ? st::dialogsTextFgActive : (selected ? st::dialogsTextFgOver : st::dialogsTextFg));
		history->cloudDraftTextCache.drawElided(p, nameleft, texttop, availableWidth, 1);
	} else if (draft
		|| (supportMode
			&& entry->session().supportHelper().isOccupiedBySomeone(history))) {
		if (!promoted) {
			PaintRowDate(p, date, rectForName, active, selected);
		}

		auto availableWidth = namewidth;
		if (entry->isPinnedDialog(filterId) && (filterId || !entry->fixedOnTopIndex())) {
			auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
			icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		p.setFont(st::dialogsTextFont);
		auto &color = active ? st::dialogsTextFgServiceActive : (selected ? st::dialogsTextFgServiceOver : st::dialogsTextFgService);
		if (!ShowSendActionInDialogs(history)
			|| !history->sendActionPainter()->paint(p, nameleft, texttop, availableWidth, fullWidth, color, ms)) {
			if (history->cloudDraftTextCache.isEmpty()) {
				using namespace TextUtilities;
				auto draftWrapped = Text::PlainLink(
					tr::lng_dialogs_text_from_wrapped(
						tr::now,
						lt_from,
						tr::lng_from_draft(tr::now)));
				auto draftText = supportMode
					? Text::PlainLink(
						Support::ChatOccupiedString(history))
					: tr::lng_dialogs_text_with_from(
						tr::now,
						lt_from_part,
						draftWrapped,
						lt_message,
						DialogsPreviewText({
							.text = draft->textWithTags.text,
							.entities = ConvertTextTagsToEntities(
								draft->textWithTags.tags),
						}),
						Text::WithEntities);
				const auto context = Core::MarkedTextContext{
					.session = &history->session(),
					.customEmojiRepaint = customEmojiRepaint,
				};
				history->cloudDraftTextCache.setMarkedText(
					st::dialogsTextStyle,
					draftText,
					DialogTextOptions(),
					context);
			}
			p.setPen(active ? st::dialogsTextFgActive : (selected ? st::dialogsTextFgOver : st::dialogsTextFg));
			if (supportMode) {
				p.setTextPalette(active ? st::dialogsTextPaletteTakenActive : (selected ? st::dialogsTextPaletteTakenOver : st::dialogsTextPaletteTaken));
			} else {
				p.setTextPalette(active ? st::dialogsTextPaletteDraftActive : (selected ? st::dialogsTextPaletteDraftOver : st::dialogsTextPaletteDraft));
			}
			history->cloudDraftTextCache.drawElided(p, nameleft, texttop, availableWidth, 1);
			p.restoreTextPalette();
		}
	} else if (!item) {
		auto availableWidth = namewidth;
		if (entry->isPinnedDialog(filterId) && (filterId || !entry->fixedOnTopIndex())) {
			auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
			icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		auto &color = active ? st::dialogsTextFgServiceActive : (selected ? st::dialogsTextFgServiceOver : st::dialogsTextFgService);
		p.setFont(st::dialogsTextFont);
		if (!ShowSendActionInDialogs(history)
			|| !history->sendActionPainter()->paint(p, nameleft, texttop, availableWidth, fullWidth, color, ms)) {
			// Empty history
		}
	} else if (!item->isEmpty()) {
		if (history && !promoted) {
			PaintRowDate(p, date, rectForName, active, selected);
		}

		paintItemCallback(nameleft, namewidth);
	} else if (entry->isPinnedDialog(filterId) && (filterId || !entry->fixedOnTopIndex())) {
		auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
		icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
	}
	auto sendStateIcon = [&]() -> const style::icon* {
		if (draft) {
			if (draft->saveRequestId) {
				return &(active
					? st::dialogsSendingIconActive
					: (selected
						? st::dialogsSendingIconOver
						: st::dialogsSendingIcon));
			}
		} else if (item && !item->isEmpty() && item->needCheck()) {
			if (!item->isSending() && !item->hasFailed()) {
				if (item->unread()) {
					return &(active
						? st::dialogsSentIconActive
						: (selected
							? st::dialogsSentIconOver
							: st::dialogsSentIcon));
				}
				return &(active
					? st::dialogsReceivedIconActive
					: (selected
						? st::dialogsReceivedIconOver
						: st::dialogsReceivedIcon));
			}
			return &(active
				? st::dialogsSendingIconActive
				: (selected
					? st::dialogsSendingIconOver
					: st::dialogsSendingIcon));
		}
		return nullptr;
	}();
	if (sendStateIcon && history) {
		rectForName.setWidth(rectForName.width() - st::dialogsSendStateSkip);
		sendStateIcon->paint(p, rectForName.topLeft() + QPoint(rectForName.width(), 0), fullWidth);
	}

	p.setFont(st::msgNameFont);
	if (flags & (Flag::SavedMessages | Flag::RepliesMessages)) {
		auto text = (flags & Flag::SavedMessages)
			? tr::lng_saved_messages(tr::now)
			: tr::lng_replies_messages(tr::now);
		const auto textWidth = st::msgNameFont->width(text);
		if (textWidth > rectForName.width()) {
			text = st::msgNameFont->elided(text, rectForName.width());
		}
		p.setPen(active
			? st::dialogsNameFgActive
			: selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		p.drawTextLeft(rectForName.left(), rectForName.top(), fullWidth, text);
	} else if (from) {
		if (history && !(flags & Flag::SearchResult)) {
			const auto badgeWidth = fromBadge.drawGetWidth(
				p,
				rectForName,
				fromName.maxWidth(),
				fullWidth,
				{
					.peer = from,
					.verified = (active
						? &st::dialogsVerifiedIconActive
						: selected
						? &st::dialogsVerifiedIconOver
						: &st::dialogsVerifiedIcon),
					.premium = (active
						? &st::dialogsPremiumIconActive
						: selected
						? &st::dialogsPremiumIconOver
						: &st::dialogsPremiumIcon),
					.scam = (active
						? &st::dialogsScamFgActive
						: selected
						? &st::dialogsScamFgOver
						: &st::dialogsScamFg),
					.premiumFg = (active
						? &st::dialogsVerifiedIconBgActive
						: selected
						? &st::dialogsVerifiedIconBgOver
						: &st::dialogsVerifiedIconBg),
					.preview = (active
						? st::dialogsScamFgActive
						: selected
						? st::windowBgRipple
						: st::windowBgOver)->c,
					.customEmojiRepaint = customEmojiRepaint,
					.now = ms,
					.paused = bool(flags & Flag::VideoPaused),
				});
			rectForName.setWidth(rectForName.width() - badgeWidth);
		}
		p.setPen(active
			? st::dialogsNameFgActive
			: selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		fromName.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	} else if (hiddenSenderInfo) {
		p.setPen(active
			? st::dialogsNameFgActive
			: selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		hiddenSenderInfo->nameText().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	} else {
		p.setPen(active
			? st::dialogsNameFgActive
			: selected
			? st::dialogsArchiveFgOver
			: st::dialogsArchiveFg);
		auto text = entry->chatListName(); // TODO feed name with emoji
		auto textWidth = st::msgNameFont->width(text);
		if (textWidth > rectForName.width()) {
			text = st::msgNameFont->elided(text, rectForName.width());
		}
		p.drawTextLeft(rectForName.left(), rectForName.top(), fullWidth, text);
	}
}

struct UnreadBadgeSizeData {
	QImage circle;
	QPixmap left[6], right[6];
};
class UnreadBadgeStyleData : public Data::AbstractStructure {
public:
	UnreadBadgeStyleData();

	UnreadBadgeSizeData sizes[UnreadBadgeSizesCount];
	style::color bg[6] = {
		st::dialogsUnreadBg,
		st::dialogsUnreadBgOver,
		st::dialogsUnreadBgActive,
		st::dialogsUnreadBgMuted,
		st::dialogsUnreadBgMutedOver,
		st::dialogsUnreadBgMutedActive
	};
	style::color reactionBg[6] = {
		st::dialogsDraftFg,
		st::dialogsDraftFgOver,
		st::dialogsDraftFgActive,
		st::dialogsUnreadBgMuted,
		st::dialogsUnreadBgMutedOver,
		st::dialogsUnreadBgMutedActive
	};
	rpl::lifetime lifetime;
};
Data::GlobalStructurePointer<UnreadBadgeStyleData> unreadBadgeStyle;

UnreadBadgeStyleData::UnreadBadgeStyleData() {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &data : sizes) {
			for (auto &left : data.left) {
				left = QPixmap();
			}
			for (auto &right : data.right) {
				right = QPixmap();
			}
		}
	}, lifetime);
}

void createCircleMask(UnreadBadgeSizeData *data, int size) {
	if (!data->circle.isNull()) return;

	data->circle = style::createCircleMask(size);
}

QImage colorizeCircleHalf(UnreadBadgeSizeData *data, int size, int half, int xoffset, style::color color) {
	auto result = style::colorizeImage(data->circle, color, QRect(xoffset, 0, half, size));
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

void PaintUnreadBadge(Painter &p, const QRect &rect, const UnreadBadgeStyle &st) {
	Assert(rect.height() == st.size);

	int index = (st.muted ? 0x03 : 0x00) + (st.active ? 0x02 : (st.selected ? 0x01 : 0x00));
	int size = st.size, sizehalf = size / 2;

	unreadBadgeStyle.createIfNull();
	auto badgeData = unreadBadgeStyle->sizes;
	if (st.sizeId > 0) {
		Assert(st.sizeId < UnreadBadgeSizesCount);
		badgeData = &unreadBadgeStyle->sizes[st.sizeId];
	}
	auto bg = (st.sizeId == UnreadBadgeReactionInDialogs)
		? unreadBadgeStyle->reactionBg[index]
		: unreadBadgeStyle->bg[index];
	if (badgeData->left[index].isNull()) {
		int imgsize = size * cIntRetinaFactor(), imgsizehalf = sizehalf * cIntRetinaFactor();
		createCircleMask(badgeData, size);
		badgeData->left[index] = PixmapFromImage(
			colorizeCircleHalf(badgeData, imgsize, imgsizehalf, 0, bg));
		badgeData->right[index] = PixmapFromImage(colorizeCircleHalf(
			badgeData,
			imgsize,
			imgsizehalf,
			imgsize - imgsizehalf,
			bg));
	}

	int bar = rect.width() - 2 * sizehalf;
	p.drawPixmap(rect.x(), rect.y(), badgeData->left[index]);
	if (bar) {
		p.fillRect(rect.x() + sizehalf, rect.y(), bar, rect.height(), bg);
	}
	p.drawPixmap(rect.x() + sizehalf + bar, rect.y(), badgeData->right[index]);
}

[[nodiscard]] QString ComputeUnreadBadgeText(
	const QString &unreadCount,
	int allowDigits) {
	return (allowDigits > 0) && (unreadCount.size() > allowDigits + 1)
		? qsl("..") + unreadCount.mid(unreadCount.size() - allowDigits)
		: unreadCount;
}

} // namespace

const style::icon *ChatTypeIcon(
		not_null<PeerData*> peer,
		bool active,
		bool selected) {
	if (peer->isChat() || peer->isMegagroup()) {
		return &(active
			? st::dialogsChatIconActive
			: (selected ? st::dialogsChatIconOver : st::dialogsChatIcon));
	} else if (peer->isChannel()) {
		return &(active
			? st::dialogsChannelIconActive
			: (selected
				? st::dialogsChannelIconOver
				: st::dialogsChannelIcon));
	} else if (const auto user = peer->asUser()) {
		if (ShowUserBotIcon(user)) {
			return &(active
				? st::dialogsBotIconActive
				: (selected
					? st::dialogsBotIconOver
					: st::dialogsBotIcon));
		}
	}
	return nullptr;
}

UnreadBadgeStyle::UnreadBadgeStyle()
: size(st::dialogsUnreadHeight)
, padding(st::dialogsUnreadPadding)
, font(st::dialogsUnreadFont) {
}

QSize CountUnreadBadgeSize(
		const QString &unreadCount,
		const UnreadBadgeStyle &st,
		int allowDigits) {
	const auto text = ComputeUnreadBadgeText(unreadCount, allowDigits);
	const auto unreadRectHeight = st.size;
	const auto unreadWidth = st.font->width(text);
	return {
		std::max(unreadWidth + 2 * st.padding, unreadRectHeight),
		unreadRectHeight,
	};
}

QRect PaintUnreadBadge(
		Painter &p,
		const QString &unreadCount,
		int x,
		int y,
		const UnreadBadgeStyle &st,
		int allowDigits) {
	const auto text = ComputeUnreadBadgeText(unreadCount, allowDigits);
	const auto unreadRectHeight = st.size;
	const auto unreadWidth = st.font->width(text);
	const auto unreadRectWidth = std::max(
		unreadWidth + 2 * st.padding,
		unreadRectHeight);

	const auto unreadRectLeft = ((st.align & Qt::AlignHorizontal_Mask) & style::al_center)
		? (x - unreadRectWidth) / 2
		: ((st.align & Qt::AlignHorizontal_Mask) & style::al_right)
		? (x - unreadRectWidth)
		: x;
	const auto unreadRectTop = y;

	const auto badge = QRect(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight);
	PaintUnreadBadge(p, badge, st);

	const auto textTop = st.textTop ? st.textTop : (unreadRectHeight - st.font->height) / 2;
	p.setFont(st.font);
	p.setPen(st.active
		? st::dialogsUnreadFgActive
		: st.selected
		? st::dialogsUnreadFgOver
		: st::dialogsUnreadFg);
	p.drawText(unreadRectLeft + (unreadRectWidth - unreadWidth) / 2, unreadRectTop + textTop + st.font->ascent, text);

	return badge;
}

void RowPainter::paint(
		Painter &p,
		not_null<const Row*> row,
		VideoUserpic *videoUserpic,
		FilterId filterId,
		int fullWidth,
		bool active,
		bool selected,
		crl::time ms,
		bool paused) {
	const auto entry = row->entry();
	const auto history = row->history();
	const auto peer = history ? history->peer.get() : nullptr;
	const auto unreadCount = entry->chatListUnreadCount();
	const auto unreadMark = entry->chatListUnreadMark();
	const auto unreadMuted = entry->chatListMutedBadge();
	const auto item = entry->chatListMessage();
	const auto cloudDraft = [&]() -> const Data::Draft*{
		if (history && (!item || (!unreadCount && !unreadMark))) {
			// Draw item, if there are unread messages.
			if (const auto draft = history->cloudDraft()) {
				if (!Data::draftIsNull(draft)) {
					return draft;
				}
			}
		}
		return nullptr;
	}();
	const auto displayDate = [&] {
		if (item) {
			if (cloudDraft) {
				return (item->date() > cloudDraft->date)
					? ItemDateTime(item)
					: base::unixtime::parse(cloudDraft->date);
			}
			return ItemDateTime(item);
		}
		return cloudDraft
			? base::unixtime::parse(cloudDraft->date)
			: QDateTime();
	}();
	const auto displayMentionBadge = history
		&& history->unreadMentions().has();
	const auto displayReactionBadge = !displayMentionBadge
		&& history
		&& history->unreadReactions().has();
	const auto mentionOrReactionMuted = (entry->folder() != nullptr)
		|| (!displayMentionBadge && unreadMuted);
	const auto displayUnreadCounter = [&] {
		if (displayMentionBadge
			&& unreadCount == 1
			&& item
			&& item->isUnreadMention()) {
			return false;
		}
		return (unreadCount > 0);
	}();
	const auto displayUnreadMark = !displayUnreadCounter
		&& !displayMentionBadge
		&& history
		&& unreadMark;
	const auto displayPinnedIcon = !displayUnreadCounter
		&& !displayMentionBadge
		&& !displayReactionBadge
		&& !displayUnreadMark
		&& entry->isPinnedDialog(filterId)
		&& (filterId || !entry->fixedOnTopIndex());

	const auto from = history
		? (history->peer->migrateTo()
			? history->peer->migrateTo()
			: history->peer.get())
		: nullptr;
	const auto allowUserOnline = (fullWidth >= st::columnMinimalWidthLeft)
		|| (!displayUnreadCounter && !displayUnreadMark);
	const auto flags = (active ? Flag::Active : Flag(0))
		| (selected ? Flag::Selected : Flag(0))
		| (allowUserOnline ? Flag::AllowUserOnline : Flag(0))
		| (peer && peer->isSelf() ? Flag::SavedMessages : Flag(0))
		| (peer && peer->isRepliesChat() ? Flag::RepliesMessages : Flag(0))
		| (paused ? Flag::VideoPaused : Flag(0));
	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		const auto texttop = st::dialogsPadding.y()
			+ st::msgNameFont->height
			+ st::dialogsSkip;
		const auto availableWidth = PaintWideCounter(
			p,
			texttop,
			namewidth,
			fullWidth,
			displayUnreadCounter,
			displayUnreadMark,
			displayMentionBadge,
			displayReactionBadge,
			displayPinnedIcon,
			unreadCount,
			active,
			selected,
			unreadMuted,
			mentionOrReactionMuted);
		const auto &color = active
			? st::dialogsTextFgServiceActive
			: (selected
				? st::dialogsTextFgServiceOver
				: st::dialogsTextFgService);
		const auto rect = QRect(
			nameleft,
			texttop,
			availableWidth,
			st::dialogsTextFont->height);
		const auto actionWasPainted = ShowSendActionInDialogs(history)
			? history->sendActionPainter()->paint(
				p,
				rect.x(),
				rect.y(),
				rect.width(),
				fullWidth,
				color,
				ms)
			: false;
		if (const auto folder = row->folder()) {
			PaintListEntryText(p, rect, active, selected, row);
		} else if (history && !actionWasPainted) {
			if (!history->lastItemDialogsView.prepared(item)) {
				history->lastItemDialogsView.prepare(
					item,
					[=] { history->updateChatListEntry(); },
					{});
			}
			history->lastItemDialogsView.paint(p, rect, active, selected);
		}
	};
	const auto paintCounterCallback = [&] {
		PaintNarrowCounter(
			p,
			displayUnreadCounter,
			displayUnreadMark,
			displayMentionBadge,
			displayReactionBadge,
			unreadCount,
			selected,
			active,
			unreadMuted,
			mentionOrReactionMuted);
	};
	paintRow(
		p,
		row,
		entry,
		row->key(),
		videoUserpic,
		filterId,
		from,
		entry->chatListBadge(),
		[=] { history->updateChatListEntry(); },
		entry->chatListNameText(),
		nullptr,
		item,
		cloudDraft,
		displayDate,
		fullWidth,
		flags,
		ms,
		paintItemCallback,
		paintCounterCallback);
}

void RowPainter::paint(
		Painter &p,
		not_null<const FakeRow*> row,
		int fullWidth,
		bool active,
		bool selected,
		crl::time ms,
		bool displayUnreadInfo) {
	auto item = row->item();
	auto history = item->history();
	auto cloudDraft = nullptr;
	const auto from = [&] {
		if (row->searchInChat()) {
			return item->displayFrom();
		}
		return history->peer->migrateTo()
			? history->peer->migrateTo()
			: history->peer.get();
	}();
	const auto hiddenSenderInfo = [&]() -> const HiddenSenderInfo* {
		if (const auto searchChat = row->searchInChat()) {
			if (const auto peer = searchChat.peer()) {
				if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
					if (peer->isSelf() || forwarded->imported) {
						return forwarded->hiddenSenderInfo.get();
					}
				}
			}
		}
		return nullptr;
	}();
	const auto previewOptions = [&]() -> HistoryView::ToPreviewOptions {
		if (const auto searchChat = row->searchInChat()) {
			if (const auto peer = searchChat.peer()) {
				if (!peer->isChannel() || peer->isMegagroup()) {
					return { .hideSender = true };
				}
			}
		}
		return {};
	}();

	const auto unreadCount = displayUnreadInfo
		? history->chatListUnreadCount()
		: 0;
	const auto unreadMark = displayUnreadInfo
		&& history->chatListUnreadMark();
	const auto unreadMuted = history->chatListMutedBadge();
	const auto mentionOrReactionMuted = (history->folder() != nullptr);
	const auto displayMentionBadge = displayUnreadInfo
		&& history->unreadMentions().has();
	const auto displayReactionBadge = displayUnreadInfo
		&& !displayMentionBadge
		&& history->unreadReactions().has();
	const auto displayUnreadCounter = (unreadCount > 0);
	const auto displayUnreadMark = !displayUnreadCounter
		&& !displayMentionBadge
		&& unreadMark;
	const auto displayPinnedIcon = false;

	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		const auto texttop = st::dialogsPadding.y()
			+ st::msgNameFont->height
			+ st::dialogsSkip;
		const auto availableWidth = PaintWideCounter(
			p,
			texttop,
			namewidth,
			fullWidth,
			displayUnreadCounter,
			displayUnreadMark,
			displayMentionBadge,
			displayReactionBadge,
			displayPinnedIcon,
			unreadCount,
			active,
			selected,
			unreadMuted,
			mentionOrReactionMuted);

		const auto itemRect = QRect(
			nameleft,
			texttop,
			availableWidth,
			st::dialogsTextFont->height);
		auto &view = row->itemView();
		if (!view.prepared(item)) {
			view.prepare(item, row->repaint(), previewOptions);
		}
		row->itemView().paint(p, itemRect, active, selected);
	};
	const auto paintCounterCallback = [&] {
		PaintNarrowCounter(
			p,
			displayUnreadCounter,
			displayUnreadMark,
			displayMentionBadge,
			displayReactionBadge,
			unreadCount,
			selected,
			active,
			unreadMuted,
			mentionOrReactionMuted);
	};
	const auto showSavedMessages = history->peer->isSelf()
		&& !row->searchInChat();
	const auto showRepliesMessages = history->peer->isRepliesChat()
		&& !row->searchInChat();
	const auto flags = (active ? Flag::Active : Flag(0))
		| (selected ? Flag::Selected : Flag(0))
		| Flag::SearchResult
		| (showSavedMessages ? Flag::SavedMessages : Flag(0))
		| (showRepliesMessages ? Flag::RepliesMessages : Flag(0));
	paintRow(
		p,
		row,
		history,
		history,
		nullptr,
		FilterId(),
		from,
		row->badge(),
		row->repaint(),
		row->name(),
		hiddenSenderInfo,
		item,
		cloudDraft,
		ItemDateTime(item),
		fullWidth,
		flags,
		ms,
		paintItemCallback,
		paintCounterCallback);
}

QRect RowPainter::sendActionAnimationRect(
		int animationLeft,
		int animationWidth,
		int animationHeight,
		int fullWidth,
		bool textUpdated) {
	const auto nameleft = st::dialogsPadding.x()
		+ st::dialogsPhotoSize
		+ st::dialogsPhotoPadding;
	const auto namewidth = fullWidth - nameleft - st::dialogsPadding.x();
	const auto texttop = st::dialogsPadding.y()
		+ st::msgNameFont->height
		+ st::dialogsSkip;
	return QRect(
		nameleft + (textUpdated ? 0 : animationLeft),
		texttop,
		textUpdated ? namewidth : animationWidth,
		animationHeight);
}

void PaintCollapsedRow(
		Painter &p,
		const BasicRow &row,
		Data::Folder *folder,
		const QString &text,
		int unread,
		int fullWidth,
		bool selected) {
	p.fillRect(0, 0, fullWidth, st::dialogsImportantBarHeight, selected ? st::dialogsBgOver : st::dialogsBg);

	row.paintRipple(p, 0, 0, fullWidth);

	const auto smallWidth = st::dialogsPadding.x()
		+ st::dialogsPhotoSize
		+ st::dialogsPhotoPadding;
	const auto narrow = (fullWidth <= smallWidth);

	const auto unreadTop = (st::dialogsImportantBarHeight - st::dialogsUnreadHeight) / 2;
	if (!narrow || !folder) {
		p.setFont(st::semiboldFont);
		p.setPen(st::dialogsNameFg);

		const auto textBaseline = unreadTop
			+ (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2
			+ st::dialogsUnreadFont->ascent;
		const auto left = narrow
			? ((fullWidth - st::semiboldFont->width(text)) / 2)
			: st::dialogsPadding.x();
		p.drawText(left, textBaseline, text);
	} else {
		folder->paintUserpicLeft(
			p,
			row.userpicView(),
			(fullWidth - st::dialogsUnreadHeight) / 2,
			unreadTop,
			fullWidth,
			st::dialogsUnreadHeight);
	}
	if (!narrow && unread) {
		const auto unreadRight = fullWidth - st::dialogsPadding.x();
		UnreadBadgeStyle st;
		st.muted = true;
		PaintUnreadBadge(
			p,
			QString::number(unread),
			unreadRight,
			unreadTop,
			st);
	}
}

} // namespace Dialogs::Ui
