#include "App.hpp"
#include "Handlers.hpp"

/*
* bfanew: GetAchievementInfo ("YPBU7MD8", encode key AKjzyZ81, URL /actionSymbol/17Q6ARps.php): the
* home screen's gift box icon opens RewardsTopScene (the 2016 rewards hub: journal, level-up rewards,
* brave points, Presents, keys, slots, mystery chest, daily spin), whose initConnect asks for the
* achievement state (body group 1Q2pGAXE {YQG2AE5h, KT71m8Ae, 3rhygS9K}) before drawing the menu.
* Achievements are off in phase 1 (bf_achievement), so the four lists the client maps
* (Bnc4LpM8 UserAchievementInfo, YTRJLG65 UserAchievementSubjectInfo, 9j3ALx8I
* UserAchievementTradeInfo, LcFCx1Uz UserAchievementSPSubjectInfo) come back empty; unanswered,
* the hub popped "Unsupported request" and the client restarted. Presents live behind this hub.
*/

HANDLEF(GetAchievementInfo)
{
	co_return HandleResult::success("{\"Bnc4LpM8\":[],\"YTRJLG65\":[],\"9j3ALx8I\":[],\"LcFCx1Uz\":[]}");
}
