#include "my_module.h"


#include "include/config.h"
#include "include/log.h"

namespace chat {

static flz::Logger::ptr g_logger = FLZ_LOG_ROOT();

MyModule::MyModule()
    :flz::Module("flz_chat", "1.0", "") {
}

bool MyModule::onLoad() {
    FLZ_LOG_INFO(g_logger) << "onLoad";
    return true;
}

bool MyModule::onUnload() {
    FLZ_LOG_INFO(g_logger) << "onUnload";
    return true;
}

bool MyModule::onServerReady() {
    FLZ_LOG_INFO(g_logger) << "onServerReady";
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
    FLZ_LOG_INFO(chat::g_logger) << "CreateModule " << module;
    delete module;
}

}

