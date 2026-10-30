#include "App.hpp"
#include "Handlers.hpp"

#include <set>

/*
* DeckEdit ("m2Ve9PkJ", encode key d7UuQsq8): sent when leaving Manage Squad (PartyEditTopScene2),
* from the squad list, the player info screen and the options screen (DeckEditRequest::createBody).
*
* Request: "dX7S2Lc1" every slot of every squad (deck type, squad number, user unit id, member
* type 0 leader / 1 member, slot), "Ti62XfZK" the team settings (active squad, helper squads
* "a,b,c", arena squad, and the handle name / friend message when those were edited).
*
* Reply: the saved squads and the team info. Slots whose unit does not belong to the player are
* dropped.
*/

HANDLEF(DeckEdit)
{
	DeckEditReq req = {};
	const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
	if (ec)
	{
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme DeckEdit Error during JSON read: " << fmte;
		co_return HandleResult::error("Deserialization error", fmte);
	}

	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;

	std::set<int32_t> owned;
	for (const auto& u : co_await Db::LoadUnits(p.user_id))
		owned.insert(u.user_unit_id);

	std::vector<UserPartyDeckInfo> decks;
	for (const auto& e : req.decks)
	{
		if (!owned.count(e.user_unit_id))
		{
			LOG_WARN << "DeckEdit: user " << p.user_id << " squad " << e.deck_num << " slot " << e.disp_order
				<< " refers to unit " << e.user_unit_id << " it does not own, dropped";
			continue;
		}
		UserPartyDeckInfo d = {};
		d.deck_type = e.deck_type;
		d.deck_num = e.deck_num;
		d.user_unit_id = e.user_unit_id;
		d.member_type = e.member_type;
		d.disp_order = e.disp_order;
		decks.emplace_back(d);
	}
	co_await Db::SaveDecks(p.user_id, decks);

	p.team.active_deck = req.info.active_deck;
	if (req.info.reinforcement_deck.size() == 3)
		p.team.reinforcement_deck = req.info.reinforcement_deck;
	p.team.arena_deck_num = req.info.arena_deck;
	if (req.info.friend_message.has_value())
		p.team.friend_message = *req.info.friend_message;
	if (req.info.handle_name.has_value() && !req.info.handle_name->empty())
		p.handle_name = *req.info.handle_name;
	co_await Db::SavePlayer(p);

	LOG_DEBUG << "DeckEdit: user " << p.user_id << " saved " << decks.size() << " slots, active squad " << p.team.active_deck;

	DeckEditResp resp = {};
	resp.team_info = p.team;
	resp.party_deck_info = co_await Db::LoadDecks(p.user_id);

	std::string buffer{};
	const auto& ec2 = glz::write_json(resp, buffer);
	if (ec2)
	{
		const auto& glze = glz::format_error(ec2, buffer);
		LOG_DEBUG << "Gme DeckEdit Error during JSON writing: " << glze;
		co_return HandleResult::error("Serialization error", glze);
	}

	co_return HandleResult::success(buffer);
}
