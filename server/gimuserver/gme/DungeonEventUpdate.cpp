#include "App.hpp"
#include "Handlers.hpp"

/*
* DungeonEventUpdate ("BjAt1D6b", encode key k5EiNe9x, URL /actionSymbol/4iqcLm1s.php): the client
* reports a story event (area / dungeon / mission event scene, the town's first-visit dialogue with
* Karl...) as watched. Queued through ConnectRequestList by MissionEventScene / AreaEventScene /
* DungeonEventScene::updateEvent and sent with the next connection; the body is
* BaseRequest::createDungeonEventUserInfoTag (login info + the event ids) and the client needs
* nothing back. The watched events are not stored yet (UserScenarioInfo is still a mock), so the
* dialogues replay on every visit.
*/

HANDLEF(DungeonEventUpdate)
{
	LOG_DEBUG << "DungeonEventUpdate: " << json;
	co_return HandleResult::success("{}");
}
