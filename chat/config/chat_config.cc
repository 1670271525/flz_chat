#include "chat/config/chat_config.h"
#include "include/config.h"
#include <cstdlib>

namespace chat {
namespace config {

namespace {

static flz::ConfigVar<std::string>::ptr g_rabbitmq_host =
    flz::Config::Lookup("rabbitmq.host", std::string("127.0.0.1"), "rabbitmq host");
static flz::ConfigVar<int>::ptr g_rabbitmq_port =
    flz::Config::Lookup("rabbitmq.port", 5672, "rabbitmq port");
static flz::ConfigVar<std::string>::ptr g_rabbitmq_vhost =
    flz::Config::Lookup("rabbitmq.vhost", std::string("/"), "rabbitmq vhost");
static flz::ConfigVar<std::string>::ptr g_rabbitmq_username =
    flz::Config::Lookup("rabbitmq.username", std::string("guest"), "rabbitmq username");
static flz::ConfigVar<std::string>::ptr g_rabbitmq_password =
    flz::Config::Lookup("rabbitmq.password", std::string("guest"), "rabbitmq password");
static flz::ConfigVar<int>::ptr g_rabbitmq_heartbeat =
    flz::Config::Lookup("rabbitmq.heartbeat", 30, "rabbitmq heartbeat");
static flz::ConfigVar<int>::ptr g_rabbitmq_frame_max =
    flz::Config::Lookup("rabbitmq.frame_max", 131072, "rabbitmq frame max");
static flz::ConfigVar<bool>::ptr g_rabbitmq_publisher_confirm_select =
    flz::Config::Lookup("rabbitmq.publisher.confirm_select", true, "rabbitmq publisher confirm");
static flz::ConfigVar<int>::ptr g_rabbitmq_publisher_queue_capacity =
    flz::Config::Lookup("rabbitmq.publisher.queue_capacity", 10000, "rabbitmq publisher queue capacity");
static flz::ConfigVar<std::string>::ptr g_rabbitmq_consumer_queue =
    flz::Config::Lookup("rabbitmq.consumer.queue", std::string("chat.delivery.queue"), "rabbitmq consumer queue");
static flz::ConfigVar<int>::ptr g_rabbitmq_consumer_prefetch =
    flz::Config::Lookup("rabbitmq.consumer.prefetch", 64, "rabbitmq consumer prefetch");
static flz::ConfigVar<bool>::ptr g_rabbitmq_consumer_auto_ack =
    flz::Config::Lookup("rabbitmq.consumer.auto_ack", false, "rabbitmq consumer auto ack");
static flz::ConfigVar<std::string>::ptr g_rabbitmq_consumer_tag =
    flz::Config::Lookup("rabbitmq.consumer.consumer_tag", std::string("chat-svc-1"), "rabbitmq consumer tag");
static flz::ConfigVar<int>::ptr g_rabbitmq_reconnect_initial_backoff_ms =
    flz::Config::Lookup("rabbitmq.reconnect.initial_backoff_ms", 1000, "rabbitmq reconnect initial");
static flz::ConfigVar<int>::ptr g_rabbitmq_reconnect_max_backoff_ms =
    flz::Config::Lookup("rabbitmq.reconnect.max_backoff_ms", 30000, "rabbitmq reconnect max");

static flz::ConfigVar<std::string>::ptr g_jwt_algorithm =
    flz::Config::Lookup("jwt.algorithm", std::string("HS256"), "jwt algorithm");
static flz::ConfigVar<std::string>::ptr g_jwt_secret =
    flz::Config::Lookup("jwt.secret", std::string(""), "jwt secret");
static flz::ConfigVar<std::string>::ptr g_jwt_issuer =
    flz::Config::Lookup("jwt.issuer", std::string("flz_chat_business"), "jwt issuer");
static flz::ConfigVar<int>::ptr g_jwt_clock_skew_seconds =
    flz::Config::Lookup("jwt.clock_skew_seconds", 60, "jwt clock skew");

static flz::ConfigVar<bool>::ptr g_redis_enabled =
    flz::Config::Lookup("redis.enabled", false, "redis enabled");
static flz::ConfigVar<std::string>::ptr g_redis_host =
    flz::Config::Lookup("redis.host", std::string("127.0.0.1"), "redis host");
static flz::ConfigVar<int>::ptr g_redis_port =
    flz::Config::Lookup("redis.port", 6379, "redis port");
static flz::ConfigVar<std::string>::ptr g_redis_password =
    flz::Config::Lookup("redis.password", std::string(""), "redis password");
static flz::ConfigVar<int>::ptr g_redis_db =
    flz::Config::Lookup("redis.db", 0, "redis db");
static flz::ConfigVar<int>::ptr g_redis_pool_size =
    flz::Config::Lookup("redis.pool_size", 4, "redis pool size");
static flz::ConfigVar<int>::ptr g_redis_idempotency_ttl_seconds =
    flz::Config::Lookup("redis.idempotency_ttl_seconds", 86400, "redis idempotency ttl");

static flz::ConfigVar<int>::ptr g_chat_max_devices_per_user =
    flz::Config::Lookup("chat.max_devices_per_user", 5, "chat max devices");
static flz::ConfigVar<int>::ptr g_chat_heartbeat_interval_seconds =
    flz::Config::Lookup("chat.heartbeat_interval_seconds", 25, "chat heartbeat interval");
static flz::ConfigVar<int>::ptr g_chat_heartbeat_timeout_seconds =
    flz::Config::Lookup("chat.heartbeat_timeout_seconds", 75, "chat heartbeat timeout");
static flz::ConfigVar<int>::ptr g_chat_pending_send_ttl_seconds =
    flz::Config::Lookup("chat.pending_send_ttl_seconds", 30, "chat pending send ttl");
static flz::ConfigVar<std::string>::ptr g_chat_ws_path =
    flz::Config::Lookup("chat.ws_path", std::string("/flz/chat"), "chat ws path");
static flz::ConfigVar<int>::ptr g_chat_frame_rate_limit_per_second =
    flz::Config::Lookup("chat.frame_rate_limit_per_second", 20, "chat frame rate limit");
static flz::ConfigVar<int>::ptr g_chat_max_text_content_bytes =
    flz::Config::Lookup("chat.max_text_content_bytes", 8192, "chat max text content bytes");

}

ChatConfig& ChatConfig::GetInstance() {
    static ChatConfig instance;
    return instance;
}

std::string ChatConfig::ResolveEnv(const std::string& value) const {
    static const std::string kPrefix = "${ENV:";
    if(value.size() <= kPrefix.size() + 1) {
        return value;
    }
    if(value.compare(0, kPrefix.size(), kPrefix) != 0 || value[value.size() - 1] != '}') {
        return value;
    }
    const std::string env_key = value.substr(kPrefix.size(), value.size() - kPrefix.size() - 1);
    const char* env_val = ::getenv(env_key.c_str());
    return env_val ? std::string(env_val) : std::string();
}

bool ChatConfig::Load(std::string& err) {
    m_rabbitmq.host = g_rabbitmq_host->getValue();
    m_rabbitmq.port = g_rabbitmq_port->getValue();
    m_rabbitmq.vhost = g_rabbitmq_vhost->getValue();
    m_rabbitmq.username = g_rabbitmq_username->getValue();
    m_rabbitmq.password = g_rabbitmq_password->getValue();
    m_rabbitmq.heartbeat = g_rabbitmq_heartbeat->getValue();
    m_rabbitmq.frame_max = g_rabbitmq_frame_max->getValue();
    m_rabbitmq.publisher_confirm_select = g_rabbitmq_publisher_confirm_select->getValue();
    m_rabbitmq.publisher_queue_capacity = g_rabbitmq_publisher_queue_capacity->getValue();
    m_rabbitmq.consumer_queue = g_rabbitmq_consumer_queue->getValue();
    m_rabbitmq.consumer_prefetch = g_rabbitmq_consumer_prefetch->getValue();
    m_rabbitmq.consumer_auto_ack = g_rabbitmq_consumer_auto_ack->getValue();
    m_rabbitmq.consumer_tag = g_rabbitmq_consumer_tag->getValue();
    m_rabbitmq.reconnect_initial_backoff_ms = g_rabbitmq_reconnect_initial_backoff_ms->getValue();
    m_rabbitmq.reconnect_max_backoff_ms = g_rabbitmq_reconnect_max_backoff_ms->getValue();

    m_jwt.algorithm = g_jwt_algorithm->getValue();
    m_jwt.secret = ResolveEnv(g_jwt_secret->getValue());
    m_jwt.issuer = g_jwt_issuer->getValue();
    m_jwt.clock_skew_seconds = g_jwt_clock_skew_seconds->getValue();

    m_redis.enabled = g_redis_enabled->getValue();
    m_redis.host = g_redis_host->getValue();
    m_redis.port = g_redis_port->getValue();
    m_redis.password = g_redis_password->getValue();
    m_redis.db = g_redis_db->getValue();
    m_redis.pool_size = g_redis_pool_size->getValue();
    m_redis.idempotency_ttl_seconds = g_redis_idempotency_ttl_seconds->getValue();

    m_chat.max_devices_per_user = g_chat_max_devices_per_user->getValue();
    m_chat.heartbeat_interval_seconds = g_chat_heartbeat_interval_seconds->getValue();
    m_chat.heartbeat_timeout_seconds = g_chat_heartbeat_timeout_seconds->getValue();
    m_chat.pending_send_ttl_seconds = g_chat_pending_send_ttl_seconds->getValue();
    m_chat.ws_path = g_chat_ws_path->getValue();
    m_chat.frame_rate_limit_per_second = g_chat_frame_rate_limit_per_second->getValue();
    m_chat.max_text_content_bytes = g_chat_max_text_content_bytes->getValue();

    if(m_jwt.algorithm != "HS256") {
        err = "jwt.algorithm must be HS256";
        return false;
    }
    if(m_jwt.secret.empty()) {
        err = "jwt.secret is empty";
        return false;
    }
    if(m_chat.max_devices_per_user <= 0 || m_chat.heartbeat_timeout_seconds <= 0
            || m_chat.heartbeat_interval_seconds <= 0) {
        err = "chat config has non-positive value";
        return false;
    }
    if(m_chat.heartbeat_timeout_seconds <= m_chat.heartbeat_interval_seconds) {
        err = "heartbeat_timeout_seconds must be greater than heartbeat_interval_seconds";
        return false;
    }
    if(m_rabbitmq.port <= 0 || m_rabbitmq.port > 65535) {
        err = "rabbitmq.port out of range";
        return false;
    }
    return true;
}

}
}
