#pragma once
#include "App.hpp"
#include "Friends.hpp"
#include "NewPlayer.hpp"
#include <string>
#include <vector>

/*
* bfanew: gifts between friends (CLAUDE.md "Gifts"; Social > Gifts = GiftRecieveScene2).
*
* GiftItemMst (deploy/system/gift.json, Initialize Bm1WNDQ0; the phase copy extends the 2025 end
* dates) lists what can be given: type 1 zel, 2 friend points, 4 karma, 3 an item, with the
* amount in possession. Every player keeps a "want" list of three gift ids (UserTeamInfo
* want_gift, "100,200,300" by default: zel, friend points, karma); a friend sends one of a
* friend's wanted gifts once a day (FriendInfo today_yale 1 once done), the receiver sees the gifts
* in the Gift box (UserInfo / GetGiftInfo 30uygM9m) and takes them with FixGiftInfo.
*/
namespace Gifts
{
	constexpr const char* kDefaultWants = NewPlayer::kDefaultWantGifts;

	inline const GiftItemMst* Def(int32_t giftId)
	{
		for (const auto& g : theServer()->cache().userInfoResp().gift)
			if (g.id == giftId) return &g;
		return nullptr;
	}

	inline int64_t DayStart(int64_t now)
	{
		const int64_t offset = static_cast<int64_t>(theServer()->cache().vortex().utc_offset_hours) * 3600;
		const int64_t local = now + offset;
		const int64_t day = local >= 0 ? local / 86400 : (local - 86399) / 86400;
		return day * 86400 - offset;
	}

	inline int32_t Ymd(int64_t t)
	{
		const std::string d = Friends::GameClock(t); // YYYY/MM/DD HH:MM
		return std::atoi((d.substr(0, 4) + d.substr(5, 2) + d.substr(8, 2)).c_str());
	}

	inline drogon::Task<UserGiftInfo> Record(const Db::GiftRow& g)
	{
		UserGiftInfo r = {};
		r.gift_identify_id = static_cast<int32_t>(g.id);
		r.user_id_from = g.from_user;
		r.user_id_to = g.to_user;
		r.gift_id = g.gift_id;
		if (const auto* d = Def(g.gift_id))
		{
			r.gift_type = d->type;
			r.item_id = d->item_id;
			r.possession = d->possession;
		}
		r.gift_date = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>(std::chrono::seconds(g.created));
		r.gift_ymd = Ymd(g.created);
		r.receive_flg = g.received ? 1 : 0;
		r.unit_img_type = 0;
		if (const auto l = co_await Friends::LoadOther(g.from_user))
		{
			r.handle_name = l->player.handle_name;
			if (l->leader) r.unit_id = l->leader->first.unit_id;
		}
		co_return r;
	}

	/*! Pays a gift into the receiver (wallet in p.team, items to the warehouse). */
	inline drogon::Task<void> Give(Db::Player& p, const Db::GiftRow& g)
	{
		const auto* d = Def(g.gift_id);
		if (!d) co_return;
		switch (d->type)
		{
		case 1: p.team.zel += d->possession; break;
		case 2: p.team.friend_point += d->possession; break;
		case 4: p.team.karma += d->possession; break;
		case 3: co_await Db::AddItems(p.user_id, std::to_string(d->item_id) + ":" + std::to_string(std::max(1, d->possession))); break;
		default: break;
		}
	}

	inline drogon::Task<std::vector<UserGiftInfo>> Received(const std::string& userId)
	{
		std::vector<UserGiftInfo> out;
		for (const auto& g : co_await Db::LoadGiftsTo(userId))
			if (!g.received) out.push_back(co_await Record(g));
		co_return out;
	}
}
