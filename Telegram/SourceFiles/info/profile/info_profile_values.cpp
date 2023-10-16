/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_values.h"

#include "info/profile/info_profile_badge.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "countries/countries_instance.h"
#include "main/main_session.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "lang/lang_keys.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_peer_values.h"
#include "data/data_shared_media.h"
#include "data/data_message_reactions.h"
#include "data/data_folder.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/data_premium_limits.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "base/unixtime.h"

namespace Info {
namespace Profile {
namespace {

using UpdateFlag = Data::PeerUpdate::Flag;

auto PlainAboutValue(not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::About
	) | rpl::map([=] {
		return peer->about();
	});
}

auto PlainUsernameValue(not_null<PeerData*> peer) {
	return rpl::merge(
		peer->session().changes().peerFlagsValue(peer, UpdateFlag::Username),
		peer->session().changes().peerFlagsValue(peer, UpdateFlag::Usernames)
	) | rpl::map([=] {
		return peer->userName();
	});
}

auto PlainPrimaryUsernameValue(not_null<PeerData*> peer) {
	return UsernamesValue(
		peer
	) | rpl::map([=](std::vector<TextWithEntities> usernames) {
		if (!usernames.empty()) {
			return rpl::single(usernames.front().text) | rpl::type_erased();
		} else {
			return PlainUsernameValue(peer) | rpl::type_erased();
		}
	}) | rpl::flatten_latest();
}

void StripExternalLinks(TextWithEntities &text) {
	const auto local = [](const QString &url) {
		return !UrlRequiresConfirmation(QUrl::fromUserInput(url));
	};
	const auto notLocal = [&](const EntityInText &entity) {
		if (entity.type() == EntityType::CustomUrl) {
			return !local(entity.data());
		} else if (entity.type() == EntityType::Url) {
			return !local(text.text.mid(entity.offset(), entity.length()));
		} else {
			return false;
		}
	};
	text.entities.erase(
		ranges::remove_if(text.entities, notLocal),
		text.entities.end());
}

} // namespace

QString findRegistrationTime(long long userId) {
	struct UserData {
		long long id;
		long long registrationTime;
	};
	std::vector<UserData> userData = {
		{1000000, 1380326400}, // other source
		{2768409, 1383264000},
		{7679610, 1388448000},
		// {7679610, 1389744000}, other source
		// {10000000, 1413331200}, other source
		{11538514, 1391212000},
		{15835244, 1392940000},
		{23646077, 1393459000},
		{38015510, 1393632000},
		{44634663, 1399334000},
		{46145305, 1400198000},
		{54845238, 1411257000},
		{63263518, 1414454000},
		{101260938, 1425600000},
		{101323197, 1426204000},
		{111220210, 1429574000},
		{103258382, 1432771000},
		{103151531, 1433376000},
		{116812045, 1437696000},
		{122600695, 1437782000},
		{109393468, 1439078000},
		{112594714, 1439683000},
		{124872445, 1439856000},
		{130029930, 1441324000},
		{125828524, 1444003000},
		{133909606, 1444176000},
		{157242073, 1446768000},
		{143445125, 1448928000},
		{148670295, 1452211000},
		// {150000000, 1434326400}, other source
		{152079341, 1453420000},
		{171295414, 1457481000},
		{181783990, 1460246000},
		// {200000000, 1451606400}, other source
		{222021233, 1465344000},
		{225034354, 1466208000},
		// {234480941, 1464825600}, other source
		{278941742, 1473465000},
		{285253072, 1476835000},
		{294851037, 1479600000},
		{297621225, 1481846000},
		{328594461, 1482969000},
		{337808429, 1487707000},
		{341546272, 1487782000},
		{352940995, 1487894000},
		{369669043, 1490918000}, // other source
		// {369669043, 1492214400}, other source
		// {391882013, 1509926400}, other source
		{400169472, 1501459000}, // other source
		{616816630, 1529625600}, // other source
		{727572658, 1543708800}, // other source
		// {755000000, 1548028800}, other source
		{782000000, 1546300800}, // other source
		{805158066, 1563208000},
		// {1919230638, 1598028800}, other source
		{1974255900, 1634000000},
		// {2018845111, 1608028800}, other source
		{3318845111, 1618028800}, // other source
		{4317845111, 1620028800}, // other source
		{5336336790, 1646368100}, // other source
		{5396587273, 1648014800}, // other source
		{6020888206, 1675534800},
		{6020888206, 1676198350},
		{6554264430, 1695654800}
	};
	std::sort(userData.begin(), userData.end(), [](const UserData& a, const UserData& b) {
		return a.id < b.id;
		});
	for (size_t i = 1; i < userData.size(); ++i) {
		if (userId >= userData[i - 1].id && userId <= userData[i].id) {
			double t = static_cast<double>(userId - userData[i - 1].id) / (userData[i].id - userData[i - 1].id);

			return QString("~ ") + base::unixtime::parse(userData[i - 1].registrationTime + t * 
				(userData[i].registrationTime - userData[i - 1].registrationTime))
				.toString(QLocale::system().dateFormat(QLocale::ShortFormat));
		}
	}
	if (userId <= 1000000) {
		return QString("< 28.09.2013");
	}
	else {
		return QString("> 25.09.2023");
	}
}

rpl::producer<TextWithEntities> RegistrationValue(not_null<PeerData*> peer) {
	auto userId = peer->id.to<UserId>().bare;
	return rpl::single(findRegistrationTime(userId)) | Ui::Text::ToWithEntities();
}

rpl::producer<QString> NameValue(not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		UpdateFlag::Name
	) | rpl::map([=] { return peer->name(); });
}

rpl::producer<QString> TitleValue(not_null<Data::ForumTopic*> topic) {
	return topic->session().changes().topicFlagsValue(
		topic,
		Data::TopicUpdate::Flag::Title
	) | rpl::map([=] { return topic->title(); });
}

rpl::producer<DocumentId> IconIdValue(not_null<Data::ForumTopic*> topic) {
	return topic->session().changes().topicFlagsValue(
		topic,
		Data::TopicUpdate::Flag::IconId
	) | rpl::map([=] { return topic->iconId(); });
}

rpl::producer<int32> ColorIdValue(not_null<Data::ForumTopic*> topic) {
	return topic->session().changes().topicFlagsValue(
		topic,
		Data::TopicUpdate::Flag::ColorId
	) | rpl::map([=] { return topic->colorId(); });
}

rpl::producer<TextWithEntities> PhoneValue(not_null<UserData*> user) {
	return rpl::merge(
		Countries::Instance().updated(),
		user->session().changes().peerFlagsValue(
			user,
			UpdateFlag::PhoneNumber) | rpl::to_empty
	) | rpl::map([=] {
		return Ui::FormatPhone(user->phone());
	}) | Ui::Text::ToWithEntities();
}

rpl::producer<TextWithEntities> PhoneOrHiddenValue(not_null<UserData*> user) {
	return rpl::combine(
		PhoneValue(user),
		PlainUsernameValue(user),
		PlainAboutValue(user),
		tr::lng_info_mobile_hidden()
	) | rpl::map([](
			const TextWithEntities &phone,
			const QString &username,
			const QString &about,
			const QString &hidden) {
		return (phone.text.isEmpty() && username.isEmpty() && about.isEmpty())
			? Ui::Text::WithEntities(hidden)
			: phone;
	});
}

rpl::producer<TextWithEntities> UsernameValue(
		not_null<UserData*> user,
		bool primary) {
	return (primary
		? PlainPrimaryUsernameValue(user)
		: (PlainUsernameValue(user) | rpl::type_erased())
	) | rpl::map([](QString &&username) {
		return username.isEmpty()
			? QString()
			: ('@' + username);
	}) | Ui::Text::ToWithEntities();
}

rpl::producer<std::vector<TextWithEntities>> UsernamesValue(
		not_null<PeerData*> peer) {
	const auto map = [=](const std::vector<QString> &usernames) {
		return ranges::views::all(
			usernames
		) | ranges::views::transform([&](const QString &u) {
			return Ui::Text::Link(
				u,
				peer->session().createInternalLinkFull(u));
		}) | ranges::to_vector;
	};
	auto value = rpl::merge(
		peer->session().changes().peerFlagsValue(peer, UpdateFlag::Username),
		peer->session().changes().peerFlagsValue(peer, UpdateFlag::Usernames)
	);
	if (const auto user = peer->asUser()) {
		return std::move(value) | rpl::map([=] {
			return map(user->usernames());
		});
	} else if (const auto channel = peer->asChannel()) {
		return std::move(value) | rpl::map([=] {
			return map(channel->usernames());
		});
	} else {
		return rpl::single(std::vector<TextWithEntities>());
	}
}

TextWithEntities AboutWithEntities(
		not_null<PeerData*> peer,
		const QString &value) {
	auto flags = TextParseLinks | TextParseMentions;
	const auto user = peer->asUser();
	const auto isBot = user && user->isBot();
	const auto isPremium = user && user->isPremium();
	if (!user) {
		flags |= TextParseHashtags;
	} else if (isBot) {
		flags |= TextParseHashtags | TextParseBotCommands;
	}
	const auto stripExternal = peer->isChat()
		|| peer->isMegagroup()
		|| (user && !isBot && !isPremium);
	const auto limit = Data::PremiumLimits(&peer->session())
		.aboutLengthDefault();
	const auto used = (!user || isPremium || value.size() <= limit)
		? value
		: value.mid(0, limit) + "...";
	auto result = TextWithEntities{ value };
	TextUtilities::ParseEntities(result, flags);
	if (stripExternal) {
		StripExternalLinks(result);
	}
	return result;
}

rpl::producer<TextWithEntities> AboutValue(not_null<PeerData*> peer) {
	return PlainAboutValue(
		peer
	) | rpl::map([peer](const QString &value) {
		return AboutWithEntities(peer, value);
	});
}

rpl::producer<QString> LinkValue(not_null<PeerData*> peer, bool primary) {
	return (primary
		? PlainPrimaryUsernameValue(peer)
		: PlainUsernameValue(peer) | rpl::type_erased()
	) | rpl::map([=](QString &&username) {
		return username.isEmpty()
			? QString()
			: peer->session().createInternalLinkFull(username);
	});
}

rpl::producer<const ChannelLocation*> LocationValue(
		not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::ChannelLocation
	) | rpl::map([=] {
		return channel->getLocation();
	});
}

rpl::producer<bool> NotificationsEnabledValue(
		not_null<Data::Thread*> thread) {
	const auto topic = thread->asTopic();
	if (!topic) {
		return NotificationsEnabledValue(thread->peer());
	}
	return rpl::merge(
		topic->session().changes().topicFlagsValue(
			topic,
			Data::TopicUpdate::Flag::Notifications
		) | rpl::to_empty,
		topic->session().changes().peerUpdates(
			topic->channel(),
			UpdateFlag::Notifications
		) | rpl::to_empty,
		topic->owner().notifySettings().defaultUpdates(topic->channel())
	) | rpl::map([=] {
		return !topic->owner().notifySettings().isMuted(topic);
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> NotificationsEnabledValue(not_null<PeerData*> peer) {
	return rpl::merge(
		peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Notifications
		) | rpl::to_empty,
		peer->owner().notifySettings().defaultUpdates(peer)
	) | rpl::map([=] {
		return !peer->owner().notifySettings().isMuted(peer);
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> IsContactValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::IsContact
	) | rpl::map([=] {
		return user->isContact();
	});
}

[[nodiscard]] rpl::producer<QString> InviteToChatButton(
		not_null<UserData*> user) {
	if (!user->isBot() || user->isRepliesChat() || user->isSupport()) {
		return rpl::single(QString());
	}
	using Flag = Data::PeerUpdate::Flag;
	return user->session().changes().peerFlagsValue(
		user,
		Flag::BotCanBeInvited | Flag::Rights
	) | rpl::map([=] {
		const auto info = user->botInfo.get();
		return info->cantJoinGroups
			? (info->channelAdminRights
				? tr::lng_profile_invite_to_channel(tr::now)
				: QString())
			: (info->channelAdminRights
				? tr::lng_profile_add_bot_as_admin(tr::now)
				: tr::lng_profile_invite_to_group(tr::now));
	});
}

[[nodiscard]] rpl::producer<QString> InviteToChatAbout(
		not_null<UserData*> user) {
	if (!user->isBot() || user->isRepliesChat() || user->isSupport()) {
		return rpl::single(QString());
	}
	using Flag = Data::PeerUpdate::Flag;
	return user->session().changes().peerFlagsValue(
		user,
		Flag::BotCanBeInvited | Flag::Rights
	) | rpl::map([=] {
		const auto info = user->botInfo.get();
		return (info->cantJoinGroups || !info->groupAdminRights)
			? (info->channelAdminRights
				? tr::lng_profile_invite_to_channel_about(tr::now)
				: QString())
			: (info->channelAdminRights
				? tr::lng_profile_add_bot_as_admin_about(tr::now)
				: tr::lng_profile_invite_to_group_about(tr::now));
	});
}

rpl::producer<bool> CanShareContactValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::CanShareContact
	) | rpl::map([=] {
		return user->canShareThisContact();
	});
}

rpl::producer<bool> CanAddContactValue(not_null<UserData*> user) {
	using namespace rpl::mappers;
	if (user->isBot() || user->isSelf()) {
		return rpl::single(false);
	}
	return IsContactValue(
		user
	) | rpl::map(!_1);
}

rpl::producer<bool> AmInChannelValue(not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::ChannelAmIn
	) | rpl::map([=] {
		return channel->amIn();
	});
}

rpl::producer<int> MembersCountValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Members
		) | rpl::map([=] {
			return chat->amIn()
				? std::max(chat->count, int(chat->participants.size()))
				: 0;
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Members
		) | rpl::map([=] {
			return channel->membersCount();
		});
	}
	Unexpected("User in MembersCountViewer().");
}

rpl::producer<int> PendingRequestsCountValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::PendingRequests
		) | rpl::map([=] {
			return chat->pendingRequestsCount();
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::PendingRequests
		) | rpl::map([=] {
			return channel->pendingRequestsCount();
		});
	}
	Unexpected("User in MembersCountViewer().");
}

rpl::producer<int> AdminsCountValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Admins | UpdateFlag::Rights
		) | rpl::map([=] {
			return chat->participants.empty()
				? 0
				: int(chat->admins.size() + (chat->creator ? 1 : 0));
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Admins | UpdateFlag::Rights
		) | rpl::map([=] {
			return channel->canViewAdmins()
				? channel->adminsCount()
				: 0;
		});
	}
	Unexpected("User in AdminsCountValue().");
}


rpl::producer<int> RestrictionsCountValue(not_null<PeerData*> peer) {
	const auto countOfRestrictions = [](
			Data::RestrictionsSetOptions options,
			ChatRestrictions restrictions) {
		auto count = 0;
		const auto list = Data::ListOfRestrictions(options);
		for (const auto &f : list) {
			if (restrictions & f) count++;
		}
		return int(list.size()) - count;
	};

	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
		) | rpl::map([=] {
			return countOfRestrictions({}, chat->defaultRestrictions());
		});
	} else if (const auto channel = peer->asChannel()) {
		return rpl::combine(
			Data::PeerFlagValue(channel, ChannelData::Flag::Forum),
			channel->session().changes().peerFlagsValue(
				channel,
				UpdateFlag::Rights)
		) | rpl::map([=] {
			return countOfRestrictions(
				{ .isForum = channel->isForum() },
				channel->defaultRestrictions());
		});
	}
	Unexpected("User in RestrictionsCountValue().");
}

rpl::producer<not_null<PeerData*>> MigratedOrMeValue(
		not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Migration
		) | rpl::map([=] {
			return chat->migrateToOrMe();
		});
	} else {
		return rpl::single(peer);
	}
}

rpl::producer<int> RestrictedCountValue(not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::BannedUsers | UpdateFlag::Rights
	) | rpl::map([=] {
		return channel->canViewBanned()
			? channel->restrictedCount()
			: 0;
	});
}

rpl::producer<int> KickedCountValue(not_null<ChannelData*> channel) {
	return channel->session().changes().peerFlagsValue(
		channel,
		UpdateFlag::BannedUsers | UpdateFlag::Rights
	) | rpl::map([=] {
		return channel->canViewBanned()
			? channel->kickedCount()
			: 0;
	});
}

rpl::producer<int> SharedMediaCountValue(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		PeerData *migrated,
		Storage::SharedMediaType type) {
	auto aroundId = 0;
	auto limit = 0;
	auto updated = SharedMediaMergedViewer(
		&peer->session(),
		SharedMediaMergedKey(
			SparseIdsMergedSlice::Key(
				peer->id,
				topicRootId,
				migrated ? migrated->id : 0,
				aroundId),
			type),
		limit,
		limit
	) | rpl::map([](const SparseIdsMergedSlice &slice) {
		return slice.fullCount();
	}) | rpl::filter_optional();
	return rpl::single(0) | rpl::then(std::move(updated));
}

rpl::producer<int> CommonGroupsCountValue(not_null<UserData*> user) {
	return user->session().changes().peerFlagsValue(
		user,
		UpdateFlag::CommonChats
	) | rpl::map([=] {
		return user->commonChatsCount();
	});
}

rpl::producer<bool> CanAddMemberValue(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
		) | rpl::map([=] {
			return chat->canAddMembers();
		});
	} else if (const auto channel = peer->asChannel()) {
		return peer->session().changes().peerFlagsValue(
			peer,
			UpdateFlag::Rights
		) | rpl::map([=] {
			return channel->canAddMembers();
		});
	}
	return rpl::single(false);
}

rpl::producer<int> FullReactionsCountValue(
		not_null<Main::Session*> session) {
	const auto reactions = &session->data().reactions();
	return rpl::single(rpl::empty) | rpl::then(
		reactions->defaultUpdates()
	) | rpl::map([=] {
		return int(reactions->list(Data::Reactions::Type::Active).size());
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> CanViewParticipantsValue(
		not_null<ChannelData*> megagroup) {
	if (megagroup->amCreator()) {
		return rpl::single(true);
	}
	return rpl::combine(
		megagroup->session().changes().peerFlagsValue(
			megagroup,
			UpdateFlag::Rights),
		megagroup->flagsValue(),
		[=] { return megagroup->canViewMembers(); }
	) | rpl::distinct_until_changed();
}

template <typename Flag, typename Peer>
rpl::producer<BadgeType> BadgeValueFromFlags(Peer peer) {
	return rpl::combine(
		Data::PeerFlagsValue(
			peer,
			Flag::Verified | Flag::Scam | Flag::Fake),
		Data::PeerPremiumValue(peer)
	) | rpl::map([=](base::flags<Flag> value, bool premium) {
		return (value & Flag::Scam)
			? BadgeType::Scam
			: (value & Flag::Fake)
			? BadgeType::Fake
			: (value & Flag::Verified)
			? BadgeType::Verified
			: premium
			? BadgeType::Premium
			: BadgeType::None;
	});
}

rpl::producer<BadgeType> BadgeValue(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return BadgeValueFromFlags<UserDataFlag>(user);
	} else if (const auto channel = peer->asChannel()) {
		return BadgeValueFromFlags<ChannelDataFlag>(channel);
	}
	return rpl::single(BadgeType::None);
}

rpl::producer<DocumentId> EmojiStatusIdValue(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->session().changes().peerFlagsValue(
			peer,
			Data::PeerUpdate::Flag::EmojiStatus
		) | rpl::map([=] { return user->emojiStatusId(); });
	}
	return rpl::single(DocumentId(0));
}


} // namespace Profile
} // namespace Info
