#pragma once
#ifndef __CHAT_DISPATCH_MQ_TO_WS_H__
#define __CHAT_DISPATCH_MQ_TO_WS_H__

#include "chat/mq/consumer.h"
#include "chat/mq/envelope.h"

namespace chat {
namespace dispatch {

class MqToWsDispatcher {
public:
    static MqToWsDispatcher* GetInstance();
    mq::ConsumeResult OnEnvelope(const mq::Envelope& env);

private:
    MqToWsDispatcher() {}

    mq::ConsumeResult OnChatMsgSend(const mq::Envelope& env);
    mq::ConsumeResult OnChatMsgReplay(const mq::Envelope& env);
    mq::ConsumeResult OnChatMsgRecall(const mq::Envelope& env);
    mq::ConsumeResult OnFriendRequest(const mq::Envelope& env);
    mq::ConsumeResult OnFriendAccept(const mq::Envelope& env);
    mq::ConsumeResult OnConversationCreated(const mq::Envelope& env);
    mq::ConsumeResult OnConversationMemberChanged(const mq::Envelope& env);

    void PushToUser(uint64_t uid, const std::string& type, const Json::Value& data);
};

}
}

#endif
