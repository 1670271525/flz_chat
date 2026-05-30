#include "chat/mq/topology.h"

namespace chat {
namespace mq {

bool Topology::DeclareForPublisher(AmqpClient& client, std::string& err) {
    if(!client.DeclareBusinessExchange(err)) {
        return false;
    }
    if(!client.DeclareChatTopology(err)) {
        return false;
    }
    return true;
}

bool Topology::DeclareForConsumer(AmqpClient& client, std::string& err) {
    return client.DeclareChatTopology(err);
}

}
}
