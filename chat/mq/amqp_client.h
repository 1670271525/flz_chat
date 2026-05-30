#pragma once
#ifndef __CHAT_MQ_AMQP_CLIENT_H__
#define __CHAT_MQ_AMQP_CLIENT_H__

#include "chat/config/chat_config.h"
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#include <stdint.h>
#include <string>

namespace chat {
namespace mq {

struct AmqpDelivery {
    uint64_t delivery_tag = 0;
    std::string routing_key;
    std::string body;
    std::string message_id;
};

class AmqpClient {
public:
    AmqpClient();
    ~AmqpClient();

    bool Connect(const config::RabbitMqConfig& cfg, std::string& err);
    bool OpenChannel(uint16_t prefetch_count, std::string& err);
    bool DeclareChatTopology(std::string& err);
    bool DeclareBusinessExchange(std::string& err);
    bool StartConsume(const std::string& queue, const std::string& consumer_tag, bool auto_ack, std::string& err);

    bool Publish(const std::string& exchange, const std::string& routing_key, const std::string& body,
                 uint8_t priority, const std::string& message_id, std::string& err);
    bool Consume(AmqpDelivery& out, int timeout_ms, std::string& err);
    bool Ack(uint64_t delivery_tag, std::string& err);
    bool Nack(uint64_t delivery_tag, bool requeue, std::string& err);

    void Close();
    bool IsConnected() const { return m_connected; }

private:
    bool CheckRpcReply(const char* op, std::string& err);
    static std::string AmqpBytesToString(amqp_bytes_t in);

private:
    amqp_connection_state_t m_conn;
    amqp_socket_t* m_socket;
    int m_channel_id;
    bool m_connected;
};

}
}

#endif
