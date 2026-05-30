#pragma once
#ifndef __CHAT_MQ_PUBLISHER_H__
#define __CHAT_MQ_PUBLISHER_H__

#include "chat/mq/amqp_client.h"
#include <atomic>
#include <condition_variable>
#include <json/json.h>
#include <mutex>
#include <queue>
#include <thread>

namespace chat {
namespace mq {

class MqPublisher {
public:
    static MqPublisher* GetInstance();

    bool Start();
    void Stop();
    bool IsRunning() const { return m_running; }

    bool Publish(const std::string& routing_key, const Json::Value& payload, uint8_t priority = 0);

    bool PublishUserOnline(uint64_t user_id);
    bool PublishUserOffline(uint64_t user_id);
    bool PublishMsgAck(int64_t message_id, uint64_t receiver_id);

private:
    struct Task {
        std::string routing_key;
        Json::Value payload;
        uint8_t priority = 0;
    };

    MqPublisher();
    void Run();
    bool EnsureConnected();

private:
    std::atomic<bool> m_running;
    std::thread m_thread;
    std::queue<Task> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    AmqpClient m_client;
};

}
}

#endif
