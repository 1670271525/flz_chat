#include "chat/mq/consumer.h"
#include "chat/config/chat_config.h"
#include "chat/metrics/metrics.h"
#include "chat/mq/topology.h"
#include "chat/util/time_util.h"
#include "include/log.h"
#include <algorithm>
#include <chrono>
#include <future>

namespace chat {
namespace mq {

static flz::Logger::ptr g_logger = FLZ_LOG_NAME("system");

MqConsumer::MqConsumer()
    : m_running(false)
    , m_scheduler(nullptr)
    , m_idempotency(100000, 24LL * 3600 * 1000) {
}

MqConsumer* MqConsumer::GetInstance() {
    static MqConsumer inst;
    return &inst;
}

bool MqConsumer::Start(flz::IOManager* scheduler, const Handler& handler) {
    if(m_running.load()) {
        return true;
    }
    m_scheduler = scheduler;
    m_handler = handler;
    m_running.store(true);
    m_thread = std::thread(&MqConsumer::Run, this);
    return true;
}

void MqConsumer::Stop() {
    if(!m_running.load()) {
        return;
    }
    m_running.store(false);
    if(m_thread.joinable()) {
        m_thread.join();
    }
    m_client.Close();
}

bool MqConsumer::EnsureConnected() {
    if(m_client.IsConnected()) {
        return true;
    }
    const config::RabbitMqConfig& mq_cfg = config::ChatConfig::GetInstance().rabbitmq();
    std::string err;
    if(!m_client.Connect(mq_cfg, err)) {
        FLZ_LOG_ERROR(g_logger) << "consumer connect fail: " << err;
        metrics::ChatMetrics::GetInstance().IncMqReconnect("consumer");
        return false;
    }
    if(!m_client.OpenChannel(static_cast<uint16_t>(mq_cfg.consumer_prefetch), err)) {
        FLZ_LOG_ERROR(g_logger) << "consumer open channel fail: " << err;
        m_client.Close();
        metrics::ChatMetrics::GetInstance().IncMqReconnect("consumer");
        return false;
    }
    if(!Topology::DeclareForConsumer(m_client, err)) {
        FLZ_LOG_ERROR(g_logger) << "consumer declare topology fail: " << err;
        m_client.Close();
        metrics::ChatMetrics::GetInstance().IncMqReconnect("consumer");
        return false;
    }
    if(!m_client.StartConsume(mq_cfg.consumer_queue, mq_cfg.consumer_tag, mq_cfg.consumer_auto_ack, err)) {
        FLZ_LOG_ERROR(g_logger) << "consumer start consume fail: " << err;
        m_client.Close();
        metrics::ChatMetrics::GetInstance().IncMqReconnect("consumer");
        return false;
    }
    FLZ_LOG_INFO(g_logger) << "mq consumer connected";
    return true;
}

ConsumeResult MqConsumer::DispatchToScheduler(const Envelope& env) {
    if(!m_handler) {
        return ConsumeResult::ACK;
    }
    if(!m_scheduler) {
        try {
            return m_handler(env);
        } catch (...) {
            return ConsumeResult::NACK_REQUEUE;
        }
    }
    std::shared_ptr<std::promise<ConsumeResult> > promise(new std::promise<ConsumeResult>());
    std::future<ConsumeResult> f = promise->get_future();
    m_scheduler->schedule([this, env, promise]() {
        try {
            promise->set_value(m_handler(env));
        } catch (...) {
            promise->set_value(ConsumeResult::NACK_REQUEUE);
        }
    });
    if(f.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
        return f.get();
    }
    return ConsumeResult::NACK_REQUEUE;
}

void MqConsumer::Run() {
    const config::RabbitMqConfig& mq_cfg = config::ChatConfig::GetInstance().rabbitmq();
    int backoff = mq_cfg.reconnect_initial_backoff_ms;
    while(m_running.load()) {
        if(!EnsureConnected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
            backoff = std::min(mq_cfg.reconnect_max_backoff_ms, backoff * 2);
            continue;
        }
        backoff = mq_cfg.reconnect_initial_backoff_ms;

        AmqpDelivery delivery;
        std::string consume_err;
        bool got = m_client.Consume(delivery, 1000, consume_err);
        if(!m_running.load()) {
            break;
        }
        if(!got) {
            if(!consume_err.empty()) {
                FLZ_LOG_ERROR(g_logger) << "consume fail: " << consume_err;
                m_client.Close();
            }
            continue;
        }

        Envelope env;
        std::string parse_err;
        if(!EnvelopeFromString(delivery.body, env, parse_err)) {
            FLZ_LOG_WARN(g_logger) << "bad envelope, drop: " << parse_err;
            metrics::ChatMetrics::GetInstance().IncMqConsume(delivery.routing_key, "fail");
            std::string ack_err;
            m_client.Ack(delivery.delivery_tag, ack_err);
            continue;
        }

        if(!m_idempotency.TryMark(env.msg_id, util::NowMs())) {
            metrics::ChatMetrics::GetInstance().IncMqConsume(env.type, "dup");
            std::string ack_err;
            m_client.Ack(delivery.delivery_tag, ack_err);
            continue;
        }

        ConsumeResult result = DispatchToScheduler(env);
        std::string ack_err;
        if(result == ConsumeResult::ACK) {
            if(!m_client.Ack(delivery.delivery_tag, ack_err)) {
                FLZ_LOG_ERROR(g_logger) << "ack fail: " << ack_err;
                m_client.Close();
            }
            metrics::ChatMetrics::GetInstance().IncMqConsume(env.type, "ok");
        } else if(result == ConsumeResult::NACK_REQUEUE) {
            if(!m_client.Nack(delivery.delivery_tag, true, ack_err)) {
                FLZ_LOG_ERROR(g_logger) << "nack requeue fail: " << ack_err;
                m_client.Close();
            }
            metrics::ChatMetrics::GetInstance().IncMqConsume(env.type, "fail");
        } else {
            if(!m_client.Nack(delivery.delivery_tag, false, ack_err)) {
                FLZ_LOG_ERROR(g_logger) << "nack drop fail: " << ack_err;
                m_client.Close();
            }
            metrics::ChatMetrics::GetInstance().IncMqConsume(env.type, "fail");
        }
    }
}

}
}
