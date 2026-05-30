#include "my_module.h"
#include "chat/config/chat_config.h"
#include "chat/dispatch/mq_to_ws.h"
#include "chat/dispatch/ws_to_mq.h"
#include "chat/metrics/metrics.h"
#include "chat/mq/consumer.h"
#include "chat/mq/publisher.h"
#include "chat/session/session_registry.h"
#include "chat/util/time_util.h"
#include "include/application.h"
#include "include/iomanager.h"
#include "include/log.h"
#include "http/ws_server.h"
#include "chat_servlet.h"

namespace chat {

static flz::Logger::ptr g_logger = FLZ_LOG_ROOT();

MyModule::MyModule()
    : flz::Module("flz_chat", "1.0", "") {
}

bool MyModule::onLoad() {
    std::string err;
    if(!config::ChatConfig::GetInstance().Load(err)) {
        FLZ_LOG_ERROR(g_logger) << "load config fail: " << err;
        return false;
    }
    FLZ_LOG_INFO(g_logger) << "onLoad";
    return true;
}

bool MyModule::onUnload() {
    stopMq();
    FLZ_LOG_INFO(g_logger) << "onUnload";
    return true;
}

bool MyModule::registerHttpServlets() {
    std::vector<flz::TcpServer::ptr> svrs;
    if(!flz::Application::GetInstance()->getServer("http", svrs)) {
        FLZ_LOG_WARN(g_logger) << "no http server";
        return false;
    }
    for(size_t i = 0; i < svrs.size(); ++i) {
        flz::http::HttpServer::ptr http_server = std::dynamic_pointer_cast<flz::http::HttpServer>(svrs[i]);
        if(!http_server) {
            continue;
        }
        flz::http::ServletDispatch::ptr dispatch = http_server->getServletDispatch();
        dispatch->addServlet("/metrics", [](flz::http::HttpRequest::ptr req,
                                            flz::http::HttpResponse::ptr rsp,
                                            flz::http::HttpSession::ptr session) {
            (void)req;
            (void)session;
            rsp->setHeader("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
            rsp->setBody(metrics::ChatMetrics::GetInstance().RenderPrometheus());
            return 0;
        });
    }
    return true;
}

bool MyModule::registerWsServlets() {
    std::vector<flz::TcpServer::ptr> svrs;
    if(!flz::Application::GetInstance()->getServer("ws", svrs)) {
        FLZ_LOG_ERROR(g_logger) << "no ws server configured";
        return false;
    }
    const std::string ws_path = config::ChatConfig::GetInstance().chat().ws_path;
    for(size_t i = 0; i < svrs.size(); ++i) {
        flz::http::WSServer::ptr ws_server = std::dynamic_pointer_cast<flz::http::WSServer>(svrs[i]);
        if(!ws_server) {
            continue;
        }
        flz::http::ServletDispatch::ptr dispatch = ws_server->getWSServletDispatch();
        dispatch->addServlet(ws_path, std::make_shared<ChatWSServlet>());
    }
    return true;
}

bool MyModule::startMq() {
    mq::MqPublisher::GetInstance()->Start();
    return mq::MqConsumer::GetInstance()->Start(flz::IOManager::GetThis(), [](const mq::Envelope& env) {
        return dispatch::MqToWsDispatcher::GetInstance()->OnEnvelope(env);
    });
}

void MyModule::stopMq() {
    mq::MqConsumer::GetInstance()->Stop();
    mq::MqPublisher::GetInstance()->Stop();
}

bool MyModule::onServerReady() {
    FLZ_LOG_INFO(g_logger) << "onServerReady";
    if(!registerHttpServlets()) {
        return false;
    }
    if(!registerWsServlets()) {
        return false;
    }
    if(!startMq()) {
        return false;
    }

    const int64_t timeout_ms = static_cast<int64_t>(config::ChatConfig::GetInstance().chat().heartbeat_timeout_seconds) * 1000;
    flz::IOManager::GetThis()->addTimer(10 * 1000, [timeout_ms]() {
        int64_t now = util::NowMs();
        int64_t deadline = now - timeout_ms;
        std::vector<session::OnlineConn::ptr> timeouted =
            session::SessionRegistry::GetInstance()->CollectTimeouted(deadline);
        for(size_t i = 0; i < timeouted.size(); ++i) {
            session::OnlineConn::ptr conn = timeouted[i];
            if(conn && conn->io_manager) {
                conn->io_manager->schedule([conn]() {
                    if(conn->session && conn->session->getSocket()) {
                        conn->session->getSocket()->cancelAll();
                    }
                    if(conn->session) {
                        conn->session->close();
                    }
                });
            } else if(conn && conn->session) {
                conn->session->close();
            }
        }
        dispatch::WsToMqDispatcher::GetInstance()->SweepPending(now);
    }, true);
    return true;
}

bool MyModule::onServerUp() {
    FLZ_LOG_INFO(g_logger) << "onServerUp";
    return true;
}

}

extern "C" {

flz::Module* CreateModule() {
    flz::Module* module = new chat::MyModule;
    FLZ_LOG_INFO(chat::g_logger) << "CreateModule " << module;
    return module;
}

void DestoryModule(flz::Module* module) {
    FLZ_LOG_INFO(chat::g_logger) << "DestoryModule " << module;
    delete module;
}

}
