#include "chat/metrics/metrics.h"
#include "chat/session/session_registry.h"
#include <sstream>

namespace chat {
namespace metrics {

ChatMetrics::ChatMetrics()
    : m_hist_buckets(std::vector<uint64_t>{5, 10, 20, 50, 100, 200, 500, 1000}) {
    m_hist_bucket_counts.resize(m_hist_buckets.size(), 0);
}

ChatMetrics& ChatMetrics::GetInstance() {
    static ChatMetrics inst;
    return inst;
}

void ChatMetrics::IncWsIn(const std::string& type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_ws_in[type];
}

void ChatMetrics::IncWsOut(const std::string& type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_ws_out[type];
}

void ChatMetrics::IncMqPublish(const std::string& routing_key, const std::string& result) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_mq_publish[routing_key + "|" + result];
}

void ChatMetrics::IncMqConsume(const std::string& routing_key, const std::string& result) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_mq_consume[routing_key + "|" + result];
}

void ChatMetrics::IncMqReconnect(const std::string& role) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_mq_reconnect[role];
}

void ChatMetrics::ObserveMsgSendLatencyMs(uint64_t value_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for(size_t i = 0; i < m_hist_buckets.size(); ++i) {
        if(value_ms <= m_hist_buckets[i]) {
            ++m_hist_bucket_counts[i];
            break;
        }
    }
    m_hist_sum += value_ms;
    ++m_hist_count;
}

void ChatMetrics::IncJwtVerifyFail(const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_jwt_fail[reason];
}

std::string ChatMetrics::RenderPrometheus() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream oss;

    session::SessionRegistry* reg = session::SessionRegistry::GetInstance();
    oss << "chat_online_users " << reg->OnlineUsers() << "\n";
    oss << "chat_online_connections " << reg->OnlineConnections() << "\n";

    for(std::map<std::string, uint64_t>::iterator it = m_ws_in.begin(); it != m_ws_in.end(); ++it) {
        oss << "chat_ws_frames_total{direction=\"in\",type=\"" << it->first << "\"} " << it->second << "\n";
    }
    for(std::map<std::string, uint64_t>::iterator it = m_ws_out.begin(); it != m_ws_out.end(); ++it) {
        oss << "chat_ws_frames_total{direction=\"out\",type=\"" << it->first << "\"} " << it->second << "\n";
    }

    for(std::map<std::string, uint64_t>::iterator it = m_mq_publish.begin(); it != m_mq_publish.end(); ++it) {
        size_t pos = it->first.find('|');
        std::string rk = it->first.substr(0, pos);
        std::string rs = pos == std::string::npos ? "ok" : it->first.substr(pos + 1);
        oss << "chat_mq_publish_total{routing_key=\"" << rk << "\",result=\"" << rs << "\"} " << it->second << "\n";
    }

    for(std::map<std::string, uint64_t>::iterator it = m_mq_consume.begin(); it != m_mq_consume.end(); ++it) {
        size_t pos = it->first.find('|');
        std::string rk = it->first.substr(0, pos);
        std::string rs = pos == std::string::npos ? "ok" : it->first.substr(pos + 1);
        oss << "chat_mq_consume_total{routing_key=\"" << rk << "\",result=\"" << rs << "\"} " << it->second << "\n";
    }

    for(std::map<std::string, uint64_t>::iterator it = m_mq_reconnect.begin(); it != m_mq_reconnect.end(); ++it) {
        oss << "chat_mq_reconnect_total{role=\"" << it->first << "\"} " << it->second << "\n";
    }

    uint64_t cumulative = 0;
    for(size_t i = 0; i < m_hist_buckets.size(); ++i) {
        cumulative += m_hist_bucket_counts[i];
        oss << "chat_msg_send_latency_ms_bucket{le=\"" << m_hist_buckets[i] << "\"} " << cumulative << "\n";
    }
    oss << "chat_msg_send_latency_ms_bucket{le=\"+Inf\"} " << m_hist_count << "\n";
    oss << "chat_msg_send_latency_ms_sum " << m_hist_sum << "\n";
    oss << "chat_msg_send_latency_ms_count " << m_hist_count << "\n";

    for(std::map<std::string, uint64_t>::iterator it = m_jwt_fail.begin(); it != m_jwt_fail.end(); ++it) {
        oss << "chat_jwt_verify_fail_total{reason=\"" << it->first << "\"} " << it->second << "\n";
    }

    return oss.str();
}

}
}
