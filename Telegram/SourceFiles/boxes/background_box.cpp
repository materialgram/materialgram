/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/background_box.h"

#include "lang/lang_keys.h"
#include "ui/effects/round_checkbox.h"
#include "ui/image/image.h"
#include "ui/chat/attach/attach_extensions.h"
#include "ui/chat/chat_theme.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "mtproto/sender.h"
#include "core/file_utilities.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "boxes/background_preview_box.h"
#include "info/profile/info_profile_icon.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "styles/style_overview.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

namespace {

constexpr auto kBackgroundsInRow = 3;

QImage TakeMiddleSample(QImage original, QSize size) {
	size *= style::DevicePixelRatio();
	const auto from = original.size();
	if (from.isEmpty()) {
		auto result = original.scaled(size);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		return result;
	}

	const auto take = (from.width() * size.height()
		> from.height() * size.width())
		? QSize(size.width() * from.height() / size.height(), from.height())
		: QSize(from.width(), size.height() * from.width() / size.width());
	auto result = original.copy(
		(from.width() - take.width()) / 2,
		(from.height() - take.height()) / 2,
		take.width(),
		take.height()
	).scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	return result;
}

} // namespace

class BackgroundBox::Inner final : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Main::Session*> session,
		PeerData *forPeer);
	~Inner();

	[[nodiscard]] rpl::producer<Data::WallPaper> chooseEvents() const;
	[[nodiscard]] rpl::producer<Data::WallPaper> removeRequests() const;

	[[nodiscard]] auto resolveResetCustomPaper() const
		->std::optional<Data::WallPaper>;

	void removePaper(const Data::WallPaper &data);

private:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	struct Paper {
		Data::WallPaper data;
		mutable std::shared_ptr<Data::DocumentMedia> dataMedia;
		mutable QPixmap thumbnail;
	};
	struct Selected {
		int index = 0;
		inline bool operator==(const Selected &other) const {
			return index == other.index;
		}
		inline bool operator!=(const Selected &other) const {
			return !(*this == other);
		}
	};
	struct DeleteSelected {
		int index = 0;
		inline bool operator==(const DeleteSelected &other) const {
			return index == other.index;
		}
		inline bool operator!=(const DeleteSelected &other) const {
			return !(*this == other);
		}
	};
	using Selection = std::variant<v::null_t, Selected, DeleteSelected>;

	int getSelectionIndex(const Selection &selection) const;
	void repaintPaper(int index);
	void resizeToContentAndPreload();
	void updatePapers();
	void requestPapers();
	void pushCustomPapers();
	void sortPapers();
	void paintPaper(
		QPainter &p,
		const Paper &paper,
		int column,
		int row) const;
	void validatePaperThumbnail(const Paper &paper) const;

	[[nodiscard]] bool forChannel() const;

	const not_null<Main::Session*> _session;
	PeerData * const _forPeer = nullptr;

	MTP::Sender _api;

	std::vector<Paper> _papers;
	uint64 _currentId = 0;
	uint64 _insertedResetId = 0;

	Selection _over;
	Selection _overDown;

	std::unique_ptr<Ui::RoundCheckbox> _check; // this is not a widget
	rpl::event_stream<Data::WallPaper> _backgroundChosen;
	rpl::event_stream<Data::WallPaper> _backgroundRemove;

};

BackgroundBox::BackgroundBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	PeerData *forPeer)
: _controller(controller)
, _forPeer(forPeer) {
}

void BackgroundBox::prepare() {
	setTitle(tr::lng_backgrounds_header());

	addButton(tr::lng_close(), [=] { closeBox(); });

	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto container = wrap.data();

	Ui::AddSkip(container);

	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		tr::lng_settings_bg_from_file(),
		st::infoProfileButton));
	object_ptr<Info::Profile::FloatingIcon>(
		button,
		st::infoIconMediaPhoto,
		st::infoSharedMediaButtonIconPosition);

	if (forChannel() && _forPeer->wallPaper()) {
		const auto remove = container->add(object_ptr<Ui::SettingsButton>(
			container,
			tr::lng_settings_bg_remove(),
			st::infoBlockButton));
		object_ptr<Info::Profile::FloatingIcon>(
			remove,
			st::infoIconDeleteRed,
			st::infoSharedMediaButtonIconPosition);

		remove->setClickedCallback([=] {
			if (const auto resolved = _inner->resolveResetCustomPaper()) {
				chosen(*resolved);
			}
		});
	}

	button->setClickedCallback([=] {
		chooseFromFile();
	});

	Ui::AddSkip(container);
	Ui::AddDivider(container);

	_inner = container->add(
		object_ptr<Inner>(this, &_controller->session(), _forPeer));

	container->resizeToWidth(st::boxWideWidth);

	setInnerWidget(std::move(wrap), st::backgroundScroll);
	setInnerTopSkip(st::lineWidth);

	_inner->chooseEvents(
	) | rpl::start_with_next([=](const Data::WallPaper &paper) {
		chosen(paper);
	}, _inner->lifetime());

	_inner->removeRequests(
	) | rpl::start_with_next([=](const Data::WallPaper &paper) {
		removePaper(paper);
	}, _inner->lifetime());
}

void BackgroundBox::chooseFromFile() {
	const auto filterStart = _forPeer
		? u"Image files (*"_q
		: u"Theme files (*.tdesktop-theme *.tdesktop-palette *"_q;
	auto filters = QStringList(
		filterStart
		+ Ui::ImageExtensions().join(u" *"_q)
		+ u")"_q);
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = [=](const FileDialog::OpenResult &result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!_forPeer && !result.paths.isEmpty()) {
			const auto filePath = result.paths.front();
			const auto hasExtension = [&](QLatin1String extension) {
				return filePath.endsWith(extension, Qt::CaseInsensitive);
			};
			if (hasExtension(qstr(".tdesktop-theme"))
				|| hasExtension(qstr(".tdesktop-palette"))) {
				Window::Theme::Apply(filePath);
				return;
			}
		}

		auto image = Images::Read({
			.path = result.paths.isEmpty() ? QString() : result.paths.front(),
			.content = result.remoteContent,
			.forceOpaque = true,
		}).image;
		if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
			return;
		}
		auto local = Data::CustomWallPaper();
		local.setLocalImageAsThumbnail(std::make_shared<Image>(
			std::move(image)));
		_controller->show(Box<BackgroundPreviewBox>(
			_controller,
			local,
			BackgroundPreviewArgs{ _forPeer }));
	};
	FileDialog::GetOpenPath(
		this,
		tr::lng_choose_image(tr::now),
		filters.join(u";;"_q),
		crl::guard(this, callback));
}

bool BackgroundBox::hasDefaultForPeer() const {
	Expects(_forPeer != nullptr);

	const auto paper = _forPeer->wallPaper();
	if (!paper) {
		return true;
	}
	const auto reset = _inner->resolveResetCustomPaper();
	Assert(reset.has_value());
	return (paper->id() == reset->id());
}

bool BackgroundBox::chosenDefaultForPeer(
		const Data::WallPaper &paper) const {
	if (!_forPeer) {
		return false;
	}

	const auto reset = _inner->resolveResetCustomPaper();
	Assert(reset.has_value());
	return (paper.id() == reset->id());
}

void BackgroundBox::chosen(const Data::WallPaper &paper) {
	if (chosenDefaultForPeer(paper)) {
		if (!hasDefaultForPeer()) {
			const auto reset = crl::guard(this, [=](Fn<void()> close) {
				resetForPeer();
				close();
			});
			_controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_background_sure_reset_default(),
				.confirmed = reset,
				.confirmText = tr::lng_background_reset_default(),
			}));
		} else {
			closeBox();
		}
		return;
	} else if (forChannel()) {
		if (_forPeer->wallPaper() && _forPeer->wallPaper()->equals(paper)) {
			closeBox();
			return;
		}
		const auto &themes = _forPeer->owner().cloudThemes();
		for (const auto &theme : themes.chatThemes()) {
			for (const auto &[type, themed] : theme.settings) {
				if (themed.paper && themed.paper->equals(paper)) {
					_controller->show(Box<BackgroundPreviewBox>(
						_controller,
						Data::WallPaper::FromEmojiId(theme.emoticon),
						BackgroundPreviewArgs{ _forPeer }));
					return;
				}
			}
		}
	}
	_controller->show(Box<BackgroundPreviewBox>(
		_controller,
		paper,
		BackgroundPreviewArgs{ _forPeer }));
}

void BackgroundBox::resetForPeer() {
	const auto api = &_controller->session().api();
	api->request(MTPmessages_SetChatWallPaper(
		MTP_flags(0),
		_forPeer->input,
		MTPInputWallPaper(),
		MTPWallPaperSettings(),
		MTPint()
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
	}).send();

	const auto weak = base::make_weak(this);
	_forPeer->setWallPaper({});
	if (weak) {
		_controller->finishChatThemeEdit(_forPeer);
	}
}

bool BackgroundBox::forChannel() const {
	return _forPeer && _forPeer->isChannel();
}

void BackgroundBox::removePaper(const Data::WallPaper &paper) {
	const auto session = &_controller->session();
	const auto remove = [=, weak = base::make_weak(this)](Fn<void()> &&close) {
		close();
		if (weak) {
			weak->_inner->removePaper(paper);
		}
		session->data().removeWallpaper(paper);
		session->api().request(MTPaccount_SaveWallPaper(
			paper.mtpInput(session),
			MTP_bool(true),
			paper.mtpSettings()
		)).send();
	};
	_controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_background_sure_delete(),
		.confirmed = remove,
		.confirmText = tr::lng_selected_delete(),
	}));
}

BackgroundBox::Inner::Inner(
	QWidget *parent,
	not_null<Main::Session*> session,
	PeerData *forPeer)
: RpWidget(parent)
, _session(session)
, _forPeer(forPeer)
, _api(&_session->mtp())
, _check(
	std::make_unique<Ui::RoundCheckbox>(
		st::overviewCheck,
		[=] { update(); })) {
	_check->setChecked(true, anim::type::instant);
	resize(
		st::boxWideWidth,
		(2 * (st::backgroundSize.height() + st::backgroundPadding)
			+ st::backgroundPadding));

	Window::Theme::IsNightModeValue(
	) | rpl::start_with_next([=] {
		updatePapers();
	}, lifetime());
	requestPapers();

	_session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_check->invalidateCache();
	}, lifetime());

	if (forChannel()) {
		_session->data().cloudThemes().chatThemesUpdated(
		) | rpl::start_with_next([=] {
			updatePapers();
		}, lifetime());
	} else {
		using Update = Window::Theme::BackgroundUpdate;
		Window::Theme::Background()->updates(
		) | rpl::start_with_next([=](const Update &update) {
			if (update.type == Update::Type::New) {
				sortPapers();
				requestPapers();
				this->update();
			}
		}, lifetime());
	}

	setMouseTracking(true);
}

void BackgroundBox::Inner::requestPapers() {
	if (forChannel()) {
		_session->data().cloudThemes().refreshChatThemes();
		return;
	}
	_api.request(MTPaccount_GetWallPapers(
		MTP_long(_session->data().wallpapersHash())
	)).done([=](const MTPaccount_WallPapers &result) {
		if (_session->data().updateWallpapers(result)) {
			updatePapers();
		}
	}).send();
}

auto BackgroundBox::Inner::resolveResetCustomPaper() const
-> std::optional<Data::WallPaper> {
	if (!_forPeer) {
		return {};
	}
	const auto nonCustom = Window::Theme::Background()->paper();
	const auto themeEmoji = _forPeer->themeEmoji();
	if (forChannel() || themeEmoji.isEmpty()) {
		return nonCustom;
	}
	const auto &themes = _forPeer->owner().cloudThemes();
	const auto theme = themes.themeForEmoji(themeEmoji);
	if (!theme) {
		return nonCustom;
	}
	using Type = Data::CloudTheme::Type;
	const auto dark = Window::Theme::IsNightMode();
	const auto i = theme->settings.find(dark ? Type::Dark : Type::Light);
	if (i != end(theme->settings) && i->second.paper) {
		return *i->second.paper;
	}
	return nonCustom;
}

void BackgroundBox::Inner::pushCustomPapers() {
	auto customId = uint64();
	if (const auto custom = _forPeer ? _forPeer->wallPaper() : nullptr) {
		customId = custom->id();
		const auto j = ranges::find(
			_papers,
			custom->id(),
			[](const Paper &paper) { return paper.data.id(); });
		if (j != end(_papers)) {
			j->data = j->data.withParamsFrom(*custom);
		} else {
			_papers.insert(begin(_papers), Paper{ *custom });
		}
	}
	if (const auto reset = resolveResetCustomPaper()) {
		_insertedResetId = reset->id();
		const auto j = ranges::find(
			_papers,
			_insertedResetId,
			[](const Paper &paper) { return paper.data.id(); });
		if (j != end(_papers)) {
			if (_insertedResetId != customId) {
				j->data = j->data.withParamsFrom(*reset);
			}
		} else {
			_papers.insert(begin(_papers), Paper{ *reset });
		}
	}
}

void BackgroundBox::Inner::sortPapers() {
	Expects(!forChannel());

	const auto currentCustom = _forPeer ? _forPeer->wallPaper() : nullptr;
	_currentId = currentCustom
		? currentCustom->id()
		: _insertedResetId
		? _insertedResetId
		: Window::Theme::Background()->id();
	const auto dark = Window::Theme::IsNightMode();
	ranges::stable_sort(_papers, std::greater<>(), [&](const Paper &paper) {
		const auto &data = paper.data;
		return std::make_tuple(
			_insertedResetId && (_insertedResetId == data.id()),
			data.id() == _currentId,
			dark ? data.isDark() : !data.isDark(),
			Data::IsDefaultWallPaper(data),
			!data.isDefault() && !Data::IsLegacy1DefaultWallPaper(data),
			Data::IsLegacy3DefaultWallPaper(data),
			Data::IsLegacy2DefaultWallPaper(data),
			Data::IsLegacy1DefaultWallPaper(data));
	});
	if (!_papers.empty()
		&& _papers.front().data.id() == _currentId
		&& !currentCustom
		&& !_insertedResetId) {
		_papers.front().data = _papers.front().data.withParamsFrom(
			Window::Theme::Background()->paper());
	}
}

void BackgroundBox::Inner::updatePapers() {
	if (forChannel()) {
		if (_session->data().cloudThemes().chatThemes().empty()) {
			return;
		}
	} else {
		if (_session->data().wallpapers().empty()) {
			return;
		}
	}
	_over = _overDown = Selection();

	const auto was = base::take(_papers);
	if (forChannel()) {
		const auto now = _forPeer->wallPaper();
		const auto &list = _session->data().cloudThemes().chatThemes();
		if (list.empty()) {
			return;
		}
		using Type = Data::CloudThemeType;
		const auto type = Window::Theme::IsNightMode()
			? Type::Dark
			: Type::Light;
		_papers.reserve(list.size() + 1);
		const auto nowEmojiId = now ? now->emojiId() : QString();
		if (!now || !now->emojiId().isEmpty()) {
			_papers.push_back({ Window::Theme::Background()->paper() });
			_currentId = _papers.back().data.id();
		} else {
			_papers.push_back({ *now });
			_currentId = now->id();
		}
		for (const auto &theme : list) {
			const auto i = theme.settings.find(type);
			if (i != end(theme.settings) && i->second.paper) {
				_papers.push_back({ *i->second.paper });
				if (nowEmojiId == theme.emoticon) {
					_currentId = _papers.back().data.id();
				}
			}
		}
	} else {
		_papers = _session->data().wallpapers(
		) | ranges::views::filter([&](const Data::WallPaper &paper) {
			return (!paper.isPattern() || !paper.backgroundColors().empty())
				&& (!_forPeer
					|| (!Data::IsDefaultWallPaper(paper)
						&& (Data::IsCloudWallPaper(paper)
							|| Data::IsCustomWallPaper(paper))));
		}) | ranges::views::transform([](const Data::WallPaper &paper) {
			return Paper{ paper };
		}) | ranges::to_vector;
		pushCustomPapers();
		sortPapers();
	}
	resizeToContentAndPreload();
}

void BackgroundBox::Inner::resizeToContentAndPreload() {
	const auto count = _papers.size();
	const auto rows = (count / kBackgroundsInRow)
		+ (count % kBackgroundsInRow ? 1 : 0);

	resize(
		st::boxWideWidth,
		(rows * (st::backgroundSize.height() + st::backgroundPadding)
			+ st::backgroundPadding));

	const auto preload = kBackgroundsInRow * 3;
	for (const auto &paper : _papers | ranges::views::take(preload)) {
		if (!paper.data.localThumbnail() && !paper.dataMedia) {
			if (const auto document = paper.data.document()) {
				paper.dataMedia = document->createMediaView();
				paper.dataMedia->thumbnailWanted(paper.data.fileOrigin());
			}
		}
	}
	update();
}

void BackgroundBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	auto p = QPainter(this);

	if (_papers.empty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), tr::lng_contacts_loading(tr::now), style::al_center);
		return;
	}
	auto row = 0;
	auto column = 0;
	for (const auto &paper : _papers) {
		const auto increment = gsl::finally([&] {
			++column;
			if (column == kBackgroundsInRow) {
				column = 0;
				++row;
			}
		});
		if ((st::backgroundSize.height() + st::backgroundPadding) * (row + 1) <= r.top()) {
			continue;
		} else if ((st::backgroundSize.height() + st::backgroundPadding) * row >= r.top() + r.height()) {
			break;
		}
		paintPaper(p, paper, column, row);
	}
}

void BackgroundBox::Inner::validatePaperThumbnail(
		const Paper &paper) const {
	if (!paper.thumbnail.isNull()) {
		return;
	}
	const auto localThumbnail = paper.data.localThumbnail();
	if (!localThumbnail) {
		if (const auto document = paper.data.document()) {
			if (!paper.dataMedia) {
				paper.dataMedia = document->createMediaView();
				paper.dataMedia->thumbnailWanted(paper.data.fileOrigin());
			}
			if (!paper.dataMedia->thumbnail()) {
				return;
			}
		} else if (!paper.data.backgroundColors().empty()) {
			paper.thumbnail = Ui::PixmapFromImage(
				Ui::GenerateBackgroundImage(
					st::backgroundSize * style::DevicePixelRatio(),
					paper.data.backgroundColors(),
					paper.data.gradientRotation()));
			paper.thumbnail.setDevicePixelRatio(style::DevicePixelRatio());
			return;
		} else {
			return;
		}
	}
	const auto thumbnail = localThumbnail
		? localThumbnail
		: paper.dataMedia->thumbnail();
	auto original = thumbnail->original();
	if (paper.data.isPattern()) {
		original = Ui::PreparePatternImage(
			std::move(original),
			paper.data.backgroundColors(),
			paper.data.gradientRotation(),
			paper.data.patternOpacity());
	}
	paper.thumbnail = Ui::PixmapFromImage(TakeMiddleSample(
		original,
		st::backgroundSize));
	paper.thumbnail.setDevicePixelRatio(style::DevicePixelRatio());
}

bool BackgroundBox::Inner::forChannel() const {
	return _forPeer && _forPeer->isChannel();
}

void BackgroundBox::Inner::paintPaper(
		QPainter &p,
		const Paper &paper,
		int column,
		int row) const {
	const auto x = st::backgroundPadding + column * (st::backgroundSize.width() + st::backgroundPadding);
	const auto y = st::backgroundPadding + row * (st::backgroundSize.height() + st::backgroundPadding);
	validatePaperThumbnail(paper);
	if (!paper.thumbnail.isNull()) {
		p.drawPixmap(x, y, paper.thumbnail);
	}

	const auto over = !v::is_null(_overDown) ? _overDown : _over;
	if (paper.data.id() == _currentId) {
		const auto checkLeft = x + st::backgroundSize.width() - st::overviewCheckSkip - st::overviewCheck.size;
		const auto checkTop = y + st::backgroundSize.height() - st::overviewCheckSkip - st::overviewCheck.size;
		_check->paint(p, checkLeft, checkTop, width());
	} else if (!forChannel()
		&& Data::IsCloudWallPaper(paper.data)
		&& !Data::IsDefaultWallPaper(paper.data)
		&& !Data::IsLegacy2DefaultWallPaper(paper.data)
		&& !Data::IsLegacy3DefaultWallPaper(paper.data)
		&& !v::is_null(over)
		&& (&paper == &_papers[getSelectionIndex(over)])) {
		const auto deleteSelected = v::is<DeleteSelected>(over);
		const auto deletePos = QPoint(x + st::backgroundSize.width() - st::stickerPanDeleteIconBg.width(), y);
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityBgOver : st::stickerPanDeleteOpacityBg);
		st::stickerPanDeleteIconBg.paint(p, deletePos, width());
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityFgOver : st::stickerPanDeleteOpacityFg);
		st::stickerPanDeleteIconFg.paint(p, deletePos, width());
		p.setOpacity(1.);
	}
}

void BackgroundBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	const auto newOver = [&] {
		const auto x = e->pos().x();
		const auto y = e->pos().y();
		const auto width = st::backgroundSize.width();
		const auto height = st::backgroundSize.height();
		const auto skip = st::backgroundPadding;
		const auto row = int((y - skip) / (height + skip));
		const auto column = int((x - skip) / (width + skip));
		const auto result = row * kBackgroundsInRow + column;
		if (y - row * (height + skip) > skip + height) {
			return Selection();
		} else if (x - column * (width + skip) > skip + width) {
			return Selection();
		} else if (result >= _papers.size()) {
			return Selection();
		}
		auto &data = _papers[result].data;
		const auto deleteLeft = (column + 1) * (width + skip)
			- st::stickerPanDeleteIconBg.width();
		const auto deleteBottom = row * (height + skip) + skip
			+ st::stickerPanDeleteIconBg.height();
		const auto inDelete = !forChannel()
			&& (x >= deleteLeft)
			&& (y < deleteBottom)
			&& Data::IsCloudWallPaper(data)
			&& !Data::IsDefaultWallPaper(data)
			&& !Data::IsLegacy2DefaultWallPaper(data)
			&& !Data::IsLegacy3DefaultWallPaper(data)
			&& (_currentId != data.id());
		return (result >= _papers.size())
			? Selection()
			: inDelete
			? Selection(DeleteSelected{ result })
			: Selection(Selected{ result });
	}();
	if (_over != newOver) {
		repaintPaper(getSelectionIndex(_over));
		_over = newOver;
		repaintPaper(getSelectionIndex(_over));
		setCursor((!v::is_null(_over) || !v::is_null(_overDown))
			? style::cur_pointer
			: style::cur_default);
	}
}

void BackgroundBox::Inner::repaintPaper(int index) {
	if (index < 0 || index >= _papers.size()) {
		return;
	}
	const auto row = (index / kBackgroundsInRow);
	const auto column = (index % kBackgroundsInRow);
	const auto width = st::backgroundSize.width();
	const auto height = st::backgroundSize.height();
	const auto skip = st::backgroundPadding;
	update(
		(width + skip) * column + skip,
		(height + skip) * row + skip,
		width,
		height);
}

void BackgroundBox::Inner::mousePressEvent(QMouseEvent *e) {
	_overDown = _over;
}

int BackgroundBox::Inner::getSelectionIndex(
		const Selection &selection) const {
	return v::match(selection, [](const Selected &data) {
		return data.index;
	}, [](const DeleteSelected &data) {
		return data.index;
	}, [](v::null_t) {
		return -1;
	});
}

void BackgroundBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	if (base::take(_overDown) == _over && !v::is_null(_over)) {
		const auto index = getSelectionIndex(_over);
		if (index >= 0 && index < _papers.size()) {
			if (std::get_if<DeleteSelected>(&_over)) {
				_backgroundRemove.fire_copy(_papers[index].data);
			} else if (std::get_if<Selected>(&_over)) {
				auto &paper = _papers[index];
				if (!paper.dataMedia) {
					if (const auto document = paper.data.document()) {
						// Keep it alive while it is on the screen.
						paper.dataMedia = document->createMediaView();
					}
				}
				_backgroundChosen.fire_copy(paper.data);
			}
		}
	} else if (v::is_null(_over)) {
		setCursor(style::cur_default);
	}
}

void BackgroundBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	for (auto i = 0, count = int(_papers.size()); i != count; ++i) {
		const auto row = (i / kBackgroundsInRow);
		const auto height = st::backgroundSize.height();
		const auto skip = st::backgroundPadding;
		const auto top = skip + row * (height + skip);
		const auto bottom = top + height;
		if ((bottom <= visibleTop || top >= visibleBottom)
			&& !_papers[i].thumbnail.isNull()) {
			_papers[i].dataMedia = nullptr;
		}
	}
}

rpl::producer<Data::WallPaper> BackgroundBox::Inner::chooseEvents() const {
	return _backgroundChosen.events();
}

auto BackgroundBox::Inner::removeRequests() const
-> rpl::producer<Data::WallPaper> {
	return _backgroundRemove.events();
}

void BackgroundBox::Inner::removePaper(const Data::WallPaper &data) {
	const auto i = ranges::find(
		_papers,
		data.id(),
		[](const Paper &paper) { return paper.data.id(); });
	if (i != end(_papers)) {
		_papers.erase(i);
		_over = _overDown = Selection();
		resizeToContentAndPreload();
	}
}

BackgroundBox::Inner::~Inner() = default;
