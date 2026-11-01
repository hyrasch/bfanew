#include "App.hpp"
#include "Handlers.hpp"
#include "Tutorial.hpp"

/*
* bfanew: the tutorial requests (CLAUDE.md "Tutorial", net/tutorial.kdl, gme/Tutorial.hpp).
*
* CreateUser (uV6yH5MX): the name and element typed on TutorialScene1; the player exists since
* the Initialize of the guest login, so this names it and gives it the starter set. The scene only
* checks the response message and then sends UserInfo, which carries everything else.
* TutorialUpdate (T1nCVvx4): the chapter and end flag (sent at chapter 12, queued at 54).
* TutorialSkip (d36DaiJl): the Skip of the tutorial battle, the client jumps to chapter 10.
*/

namespace
{
	template <typename T>
	HandleResult Reply(T& resp)
	{
		std::string buffer{};
		const auto& ec = glz::write_json(resp, buffer);
		if (ec)
			return HandleResult::error("Serialization error", glz::format_error(ec, buffer));
		return HandleResult::success(buffer);
	}

	template <typename Req>
	std::optional<HandleResult> Read(Req& req, const std::string& json, const char* name)
	{
		const auto& ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(req, json);
		if (!ec) return std::nullopt;
		const auto& fmte = glz::format_error(ec, json);
		LOG_DEBUG << "Gme " << name << " Error during JSON read: " << fmte;
		return HandleResult::error("Deserialization error", fmte);
	}

	std::string Trim(std::string s)
	{
		while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
		size_t i = 0;
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
		return s.substr(i);
	}
}

HANDLEF(CreateUser)
{
	CreateUserReq req = {};
	if (auto err = Read(req, json, "CreateUser")) co_return *err;
	const auto& li = req.login_info; // [X]::size(1) groups are plain structs in C++
	LoginInfoReq login = {};
	login.gumi_live_userid = li.gumi_live_userid;
	auto player = co_await Db::LoadPlayer(login);
	if (!player)
		co_return HandleResult::error("Unknown user", li.gumi_live_userid);
	auto& p = *player;

	auto name = Trim(li.handle_name);
	if (name.empty()) name = NewPlayer::kHandleName;
	if (name.size() > 24) name.resize(24);
	p.handle_name = name;
	const int32_t element = req.select.element > 0 ? req.select.element : 1;
	const bool given = co_await Tutorial::GiveStarters(p, element);
	if (li.skip_main_tutorial == 1 && !p.tutorial_end)
		co_await Tutorial::Skip(p);
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "CreateUser: user " << p.user_id << " named '" << name << "', element " << element << (given ? ", starters given" : ", box kept")
		<< ", skip " << static_cast<int>(li.skip_main_tutorial);

	CreateUserResp resp = {};
	resp.login_info = theServer()->cache().userInfoResp().login_info;
	auto& out = resp.login_info;
	out.user_id = p.user_id;
	out.account_id = p.account_id;
	out.friend_id = p.account_id;
	out.handle_name = p.handle_name;
	out.tutorial_end_flag = p.tutorial_end;
	out.tutorial_status = Tutorial::Status(p);
	co_return Reply(resp);
}

HANDLEF(TutorialUpdate)
{
	TutorialUpdateReq req = {};
	if (auto err = Read(req, json, "TutorialUpdate")) co_return *err;
	const auto& li = req.login_info;
	LoginInfoReq login = {};
	login.user_id = li.user_id;
	login.gumi_live_userid = li.gumi_live_userid;
	auto player = co_await Db::LoadPlayer(login);
	if (!player)
		co_return HandleResult::error("Unknown user", li.user_id);
	auto& p = *player;

	if (li.tutorial_status > 0) p.tutorial_status = li.tutorial_status;
	if (li.tutorial_end_flag == 1 || li.tutorial_status >= Tutorial::kDone)
		co_await Tutorial::Finish(p);
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "TutorialUpdate: user " << p.user_id << " chapter " << li.tutorial_status << " end " << li.tutorial_end_flag;
	co_return HandleResult::success("{}");
}

HANDLEF(TutorialSkip)
{
	TutorialSkipReq req = {};
	if (auto err = Read(req, json, "TutorialSkip")) co_return *err;
	auto player = co_await Db::LoadPlayer(req.login_info);
	if (!player)
		co_return HandleResult::error("Unknown user", req.login_info.user_id);
	auto& p = *player;
	co_await Tutorial::Skip(p);
	co_await Db::SavePlayer(p);
	LOG_DEBUG << "TutorialSkip: user " << p.user_id << " -> chapter " << p.tutorial_status;
	TutorialSkipResp resp = {};
	resp.result = 1;
	co_return Reply(resp);
}

/*
* NgwordCheck ("TA4MnZX8", key r4Smw5TX, URL /actionSymbol/3qrWb9Q6.php): TutorialScene1 asks whether
* the name typed is allowed before CreateUser (body: IKqx1Cn9 {B5JQyV8j the name, the gumi params} and
* the signal key); the client has no response class for it, an error message is a refused name and
* a plain success lets the name through. Unanswered it popped "Unsupported request". No word list
* here: every name passes.
*/
HANDLEF(NgwordCheck)
{
	co_return HandleResult::success("{}");
}
