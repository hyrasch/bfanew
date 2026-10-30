#include "App.hpp"
#include "AccountController.hpp"

using namespace drogon;
using namespace drogon::orm;

Task<> AccountController::HandleGuest(HttpRequestPtr rq, std::function<void(const HttpResponsePtr&)> callback)
{
    DumpLog log;
    theServer()->tryOpenHttpDumpLog("guestLogin", log);
    log << rq << "\n";

    // Extract parameters
    const auto& params = rq->getParameters();
    GuestLogin login;
    login.status = StatusEnum::Error;
    login.status_number = 1; // TODO: what was this again?

    try
    {
        // bfanew: one account per device. The client identifies the device with "vid" (a UUID it
        // keeps), "ak" is the user id of its last login (empty on a fresh install).
        const auto vid = rq->getParameter("vid");
        const auto dn = rq->getParameter("dn");
        const auto [gumiId, token] = co_await Db::AccountForDevice(vid, dn);
        login.user_id = gumiId;
        login.token = token;
        login.status = StatusEnum::Success;
        login.status_number = 0;
    }
    catch (const drogon::orm::DrogonDbException& ex)
    {
        LOG_ERROR << "AccountController: Query execution failed: " << ex.base().what();
    }

    auto resp = HttpResponse::newHttpResponse();
    resp->setCloseConnection(true); // we live the session torugh the gme controllers from the token...

    std::string output{};
    auto ec = glz::write_json(login, output);
    if (ec)
    {
        LOG_ERROR << "AccountController: Cannot serialize GuestLogin " << ec;
        resp->setStatusCode(k500InternalServerError);
        log << "Serialization error!";
    }
    else
    {
        resp->setStatusCode(k200OK);
        resp->setContentTypeCode(ContentType::CT_APPLICATION_JSON);
        resp->setBody(output);
        log << output;
    }

    callback(resp);
}

