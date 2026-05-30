#pragma once
#ifndef __CHAT_MQ_TOPOLOGY_H__
#define __CHAT_MQ_TOPOLOGY_H__

#include "chat/mq/amqp_client.h"
#include <string>

namespace chat {
namespace mq {

class Topology {
public:
    static bool DeclareForPublisher(AmqpClient& client, std::string& err);
    static bool DeclareForConsumer(AmqpClient& client, std::string& err);
};

}
}

#endif
