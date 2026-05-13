#pragma once
#ifndef __CHAT_CHAT_SERVLET_H__
#define __CHAT_CHAT_SERVLET_H__

#include "http/ws_servlet.h"



namespace chat {

class ChatWSServlet : public flz::http::WSServlet {
public:
    typedef std::shared_ptr<ChatWSServlet> ptr;
    ChatWSServlet();
    virtual int32_t onConnect(flz::http::HttpRequest::ptr header
                              ,flz::http::WSSession::ptr session) override;
    virtual int32_t onClose(flz::http::HttpRequest::ptr header
                             ,flz::http::WSSession::ptr session) override;
    virtual int32_t handle(flz::http::HttpRequest::ptr header
                           ,flz::http::WSFrameMessage::ptr msg
                           ,flz::http::WSSession::ptr session) override;
};




}


#endif
