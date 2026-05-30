#include "chat/mq/publisher.h"
#include "chat/config/chat_config.h"
#include "chat/metrics/metrics.h"
#include "chat/mq/envelope.h"
#include "chat/mq/topology.h"
#include "chat/util/time_util.h"
#include "include/log.h"
#include <algorithm>
#include <chrono>

namespace chat {
namespace mq {

static flz::Logger::ptr g_logger = FLZ_LOG_NAME("system");

MqPublisher::MqPublisher()
    : m_running(false) {
}

MqPublisher* MqPublisher::GetInstance() {
    static MqPublisher inst;
    return &inst;
}

bool MqPublisher::Start() {
    if(m_running.load()) {
        return true;
    }
    m_running.store(true);
    m_thread = std::thread(&MqPublisher::Run, this);
    return true;
}

void MqPublisher::Stop() {
    if(!m_running.load()) {
        return;
    }
    m_running.store(false);
    m_cv.notify_all();
    if(m_thread.joinable()) {
        m_thread.join();
    }
    m_client.Close();
}

bool MqPublisher::Publish(const std::string& routing_key, const Json::Value& payload, uint8_t priority) {
    const size_t capacity = static_cast<size_t>(config::ChatConfig::GetInstance().rabbitmq().publisher_queue_capacity);
    std::unique_lock<std::mutex> lock(m_mutex);
    if(m_queue.size() >= capacity) {
        FLZ_LOG_WARN(g_logger) << "publisher queue full, drop routing_key=" << routing_key;
        metrics::ChatMetrics::GetInstance().IncMqPublish(routing_key, "fail");
        return false;
    }
    Task t;
    t.routing_key = routing_key;
    t.payload = payload;
    t.priority = priority;
    m_queue.push(t);
    lock.unlock();
    m_cv.notify_one();
    return true;
}

bool MqPublisher::PublishUserOnline(uint64_t user_id) {
    Json::Value payload;
    payload["userId"] = Json::UInt64(user_id);
    return Publish("business.user.online", payload, 5);
}

bool MqPublisher::PublishUserOffline(uint64_t user_id) {
    Json::Value payload;
    payload["userId"] = Json::UInt64(user_id);
    return Publish("business.user.offline", payload, 5);
}

bool MqPublisher::PublishMsgAck(int64_t message_id, uint64_t receiver_id) {
    Json::Value payload;
    payload["messageId"] = Json::Int64(message_id);
    payload["receiverId"] = Json::UInt64(receiver_id);
    payload["ackAt"] = util::Iso8601Now();
    return Publish("business.msg.ack", payload, 0);
}

void MqPublisher::Run() {
    int backoff = config::ChatConfig::GetInstance().rabbitmq().reconnect_initial_backoff_ms;
    while(m_running.load()) {
        Task task;
        bool has_task = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if(m_queue.empty()) {
                m_cv.wait_for(lock, std::chrono::milliseconds(500));
            }
            if(!m_queue.empty()) {
                task = m_queue.front();
                m_queue.pop();
                has_task = true;
            }
        }
        if(!has_task) {
            continue;
        }

        if(!EnsureConnected()) {
            metrics::ChatMetrics::GetInstance().IncMqPublish(task.routing_key, "fail");
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
            int max_backoff = config::ChatConfig::GetInstance().rabbitmq().reconnect_max_backoff_ms;
            backoff = std::min(max_backoff, backoff * 2);
            continue;
        }
        backoff = config::ChatConfig::GetInstance().rabbitmq().reconnect_initial_backoff_ms;

        Envelope env = BuildEnvelope("chat", task.routing_key, task.payload);
        std::string body = EnvelopeToString(env);
        std::string err;
        if(!m_client.Publish("business.exchange", task.routing_key, body, task.priority, env.msg_id, err)) {
            FLZ_LOG_ERROR(g_logger) << "publish fail routing_key=" << task.routing_key << " err=" << err;
            metrics::ChatMetrics::GetInstance().IncMqPublish(task.routing_key, "fail");
            m_client.Close();
            continue;
        }
        metrics::ChatMetrics::GetInstance().IncMqPublish(task.routing_key, "ok");
    }
}

bool MqPublisher::EnsureConnected() {
    if(m_client.IsConnected()) {
        return true;
    }
    const config::RabbitMqConfig& mq_cfg = config::ChatConfig::GetInstance().rabbitmq();
    std::string err;
    if(!m_client.Connect(mq_cfg, err)) {
        FLZ_LOG_ERROR(g_logger) << "publisher connect fail: " << err;
        metrics::ChatMetrics::GetInstance().IncMqReconnect("publisher");
        return false;
    }
    if(!m_client.OpenChannel(0, err)) {
        FLZ_LOG_ERROR(g_logger) << "publisher open channel fail: " << err;
        m_client.Close();
        metrics::ChatMetrics::GetInstance().IncMqReconnect("publisher");
        return false;
    }
    if(!Topology::DeclareForPublisher(m_client, err)) {
        FLZ_LOG_ERROR(g_logger) << "publisher declare topology fail: " << err;
        m_client.Close();
        metrics::ChatMetrics::GetInstance().IncMqReconnect("publisher");
        return false;
    }
    FLZ_LOG_INFO(g_logger) << "mq publisher connected";
    return true;
}

}
}
