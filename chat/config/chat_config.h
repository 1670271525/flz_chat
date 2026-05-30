#pragma once
#ifndef __CHAT_CONFIG_CHAT_CONFIG_H__
#define __CHAT_CONFIG_CHAT_CONFIG_H__

#include <stdint.h>
#include <string>

namespace chat {
namespace config {

struct RabbitMqConfig {
    std::string host = "127.0.0.1";
    int port = 5672;
    std::string vhost = "/";
    std::string username = "guest";
    std::string password = "guest";
    int heartbeat = 30;
    int frame_max = 131072;

    bool publisher_confirm_select = true;
    int publisher_queue_capacity = 10000;

    std::string consumer_queue = "chat.delivery.queue";
    int consumer_prefetch = 64;
    bool consumer_auto_ack = false;
    std::string consumer_tag = "chat-svc-1";

    int reconnect_initial_backoff_ms = 1000;
    int reconnect_max_backoff_ms = 30000;
};

struct JwtConfig {
    std::string algorithm = "HS256";
    std::string secret;
    std::string issuer = "flz_chat_business";
    int clock_skew_seconds = 60;
};

struct RedisConfig {
    bool enabled = false;
    std::string host = "127.0.0.1";
    int port = 6379;
    std::string password;
    int db = 0;
    int pool_size = 4;
    int idempotency_ttl_seconds = 86400;
};

struct ChatRuntimeConfig {
    int max_devices_per_user = 5;
    int heartbeat_interval_seconds = 25;
    int heartbeat_timeout_seconds = 75;
    int pending_send_ttl_seconds = 30;
    std::string ws_path = "/flz/chat";
    int frame_rate_limit_per_second = 20;
    int max_text_content_bytes = 8192;
};

class ChatConfig {
public:
    static ChatConfig& GetInstance();

    bool Load(std::string& err);

    const RabbitMqConfig& rabbitmq() const { return m_rabbitmq; }
    const JwtConfig& jwt() const { return m_jwt; }
    const RedisConfig& redis() const { return m_redis; }
    const ChatRuntimeConfig& chat() const { return m_chat; }

private:
    ChatConfig() {}
    std::string ResolveEnv(const std::string& value) const;

private:
    RabbitMqConfig m_rabbitmq;
    JwtConfig m_jwt;
    RedisConfig m_redis;
    ChatRuntimeConfig m_chat;
};

}
}

#endif
