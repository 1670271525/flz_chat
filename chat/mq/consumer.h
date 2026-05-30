#pragma once
#ifndef __CHAT_MQ_CONSUMER_H__
#define __CHAT_MQ_CONSUMER_H__

#include "chat/mq/amqp_client.h"
#include "chat/mq/envelope.h"
#include "chat/util/lru_idempotency.h"
#include "include/iomanager.h"
#include <atomic>
#include <functional>
#include <thread>

namespace chat {
namespace mq {

enum class ConsumeResult {
    ACK = 0,
    NACK_REQUEUE = 1,
    NACK_DROP = 2
};

class MqConsumer {
public:
    typedef std::function<ConsumeResult(const Envelope&)> Handler;

    static MqConsumer* GetInstance();

    bool Start(flz::IOManager* scheduler, const Handler& handler);
    void Stop();
    bool IsRunning() const { return m_running; }

private:
    MqConsumer();
    void Run();
    bool EnsureConnected();
    ConsumeResult DispatchToScheduler(const Envelope& env);

private:
    std::atomic<bool> m_running;
    flz::IOManager* m_scheduler;
    Handler m_handler;
    std::thread m_thread;
    AmqpClient m_client;
    util::LruIdempotencyCache m_idempotency;
};

}
}

#endif
