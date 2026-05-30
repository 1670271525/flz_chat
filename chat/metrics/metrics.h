#pragma once
#ifndef __CHAT_METRICS_METRICS_H__
#define __CHAT_METRICS_METRICS_H__

#include <stdint.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace chat {
namespace metrics {

class ChatMetrics {
public:
    static ChatMetrics& GetInstance();

    void IncWsIn(const std::string& type);
    void IncWsOut(const std::string& type);
    void IncMqPublish(const std::string& routing_key, const std::string& result);
    void IncMqConsume(const std::string& routing_key, const std::string& result);
    void IncMqReconnect(const std::string& role);
    void ObserveMsgSendLatencyMs(uint64_t value_ms);
    void IncJwtVerifyFail(const std::string& reason);

    std::string RenderPrometheus();

private:
    ChatMetrics();

private:
    std::mutex m_mutex;
    std::map<std::string, uint64_t> m_ws_in;
    std::map<std::string, uint64_t> m_ws_out;
    std::map<std::string, uint64_t> m_mq_publish;
    std::map<std::string, uint64_t> m_mq_consume;
    std::map<std::string, uint64_t> m_mq_reconnect;
    std::map<std::string, uint64_t> m_jwt_fail;

    std::vector<uint64_t> m_hist_buckets;
    std::vector<uint64_t> m_hist_bucket_counts;
    uint64_t m_hist_sum = 0;
    uint64_t m_hist_count = 0;
};

}
}

#endif
