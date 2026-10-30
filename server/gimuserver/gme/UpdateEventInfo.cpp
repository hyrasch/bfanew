#include "App.hpp"
#include "Handlers.hpp"

/*
* UpdateEventInfo ("rCB7ZI8x", encode key L1o4eGbi, URL /actionSymbol/UEt5bqp4.php): HomeScene2 /
* AnotherHomeScene::updateEvent send it (async) once more than 300 s passed since the last one
* (UpdateEventInfo::isNeedUpdate), i.e. whenever the player sits on the home screen for five
* minutes. The body is the login info + signal key (BaseRequest::createUserInfoTag /
* createSignalKeyTag). Unanswered, the client showed "Unsupported request: rCB7ZI8x" and restarted
* on OK. There is no dedicated response class; an empty object keeps the client happy.
*/

HANDLEF(UpdateEventInfo)
{
	LOG_DEBUG << "UpdateEventInfo (home screen 5-minute poll)";
	co_return HandleResult::success("{}");
}
