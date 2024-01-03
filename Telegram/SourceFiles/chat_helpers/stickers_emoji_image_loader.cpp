/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_emoji_image_loader.h"

#include "styles/style_chat.h"

namespace Stickers {

EmojiImageLoader::EmojiImageLoader(crl::weak_on_queue<EmojiImageLoader> weak)
: _weak(std::move(weak)) {
}

void EmojiImageLoader::init(
		std::shared_ptr<UniversalImages> images,
		bool largeEnabled) {
	Expects(images != nullptr);

	_images = std::move(images);
	if (largeEnabled) {
		_images->ensureLoaded();
	}
}

QImage EmojiImageLoader::prepare(EmojiPtr emoji) const {
	const auto loaded = _images->ensureLoaded();
	const auto factor = cIntRetinaFactor();
	const auto side = st::largeEmojiSize;
	auto result = QImage(
		QSize(side, side) * factor,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	if (loaded) {
		QPainter p(&result);
		_images->draw(
			p,
			emoji,
			st::largeEmojiSize * factor,
			0,
			0);
	}
	return result;
}

void EmojiImageLoader::switchTo(std::shared_ptr<UniversalImages> images) {
	_images = std::move(images);
}

auto EmojiImageLoader::releaseImages() -> std::shared_ptr<UniversalImages> {
	return std::exchange(
		_images,
		std::make_shared<UniversalImages>(_images->id()));
}

} // namespace Stickers
