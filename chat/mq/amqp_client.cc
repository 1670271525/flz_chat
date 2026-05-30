#include "chat/mq/amqp_client.h"
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#include <sstream>

namespace chat {
namespace mq {

namespace {
static amqp_bytes_t ToBytes(const std::string& s) {
    amqp_bytes_t out;
    out.len = s.size();
    out.bytes = const_cast<char*>(s.data());
    return out;
}

static std::string RpcReplyToText(const amqp_rpc_reply_t& r) {
    std::ostringstream oss;
    if(r.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
        oss << amqp_error_string2(r.library_error);
        return oss.str();
    }
    if(r.reply_type == AMQP_RESPONSE_SERVER_EXCEPTION) {
        if(r.reply.id == AMQP_CONNECTION_CLOSE_METHOD) {
            amqp_connection_close_t* m = static_cast<amqp_connection_close_t*>(r.reply.decoded);
            oss << "server connection close: code=" << m->reply_code;
            return oss.str();
        }
        if(r.reply.id == AMQP_CHANNEL_CLOSE_METHOD) {
            amqp_channel_close_t* m = static_cast<amqp_channel_close_t*>(r.reply.decoded);
            oss << "server channel close: code=" << m->reply_code;
            return oss.str();
        }
    }
    oss << "unknown rpc reply type=" << r.reply_type;
    return oss.str();
}
}

AmqpClient::AmqpClient()
    : m_conn(nullptr)
    , m_socket(nullptr)
    , m_channel_id(1)
    , m_connected(false) {
}

AmqpClient::~AmqpClient() {
    Close();
}

bool AmqpClient::Connect(const config::RabbitMqConfig& cfg, std::string& err) {
    Close();

    m_conn = amqp_new_connection();
    if(!m_conn) {
        err = "amqp_new_connection failed";
        return false;
    }
    m_socket = amqp_tcp_socket_new(m_conn);
    if(!m_socket) {
        err = "amqp_tcp_socket_new failed";
        Close();
        return false;
    }
    int ret = amqp_socket_open(m_socket, cfg.host.c_str(), cfg.port);
    if(ret != AMQP_STATUS_OK) {
        err = amqp_error_string2(ret);
        Close();
        return false;
    }

    amqp_rpc_reply_t login_reply = amqp_login(
        m_conn, cfg.vhost.c_str(), 0, cfg.frame_max, cfg.heartbeat,
        AMQP_SASL_METHOD_PLAIN, cfg.username.c_str(), cfg.password.c_str());
    if(login_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        err = RpcReplyToText(login_reply);
        Close();
        return false;
    }
    m_connected = true;
    return true;
}

bool AmqpClient::OpenChannel(uint16_t prefetch_count, std::string& err) {
    if(!m_connected) {
        err = "not connected";
        return false;
    }
    amqp_channel_open(m_conn, m_channel_id);
    if(!CheckRpcReply("amqp_channel_open", err)) {
        return false;
    }
    if(prefetch_count > 0) {
        amqp_basic_qos(m_conn, m_channel_id, 0, prefetch_count, 0);
        if(!CheckRpcReply("amqp_basic_qos", err)) {
            return false;
        }
    }
    return true;
}

bool AmqpClient::DeclareChatTopology(std::string& err) {
    amqp_exchange_declare(m_conn, m_channel_id, amqp_cstring_bytes("chat.exchange"),
                          amqp_cstring_bytes("topic"), 0, 1, 0, 0, amqp_empty_table);
    if(!CheckRpcReply("declare chat.exchange", err)) {
        return false;
    }
    amqp_exchange_declare(m_conn, m_channel_id, amqp_cstring_bytes("chat.dlx"),
                          amqp_cstring_bytes("fanout"), 0, 1, 0, 0, amqp_empty_table);
    if(!CheckRpcReply("declare chat.dlx", err)) {
        return false;
    }

    amqp_table_entry_t entries[2];
    entries[0].key = amqp_cstring_bytes("x-dead-letter-exchange");
    entries[0].value.kind = AMQP_FIELD_KIND_UTF8;
    entries[0].value.value.bytes = amqp_cstring_bytes("chat.dlx");
    entries[1].key = amqp_cstring_bytes("x-message-ttl");
    entries[1].value.kind = AMQP_FIELD_KIND_I32;
    entries[1].value.value.i32 = 86400000;
    amqp_table_t args;
    args.num_entries = 2;
    args.entries = entries;

    amqp_queue_declare(m_conn, m_channel_id, amqp_cstring_bytes("chat.delivery.queue"),
                       0, 1, 0, 0, args);
    if(!CheckRpcReply("declare chat.delivery.queue", err)) {
        return false;
    }
    amqp_queue_bind(m_conn, m_channel_id, amqp_cstring_bytes("chat.delivery.queue"),
                    amqp_cstring_bytes("chat.exchange"), amqp_cstring_bytes("chat.#"), amqp_empty_table);
    if(!CheckRpcReply("bind chat.delivery.queue", err)) {
        return false;
    }

    amqp_queue_declare(m_conn, m_channel_id, amqp_cstring_bytes("chat.dlq"), 0, 1, 0, 0, amqp_empty_table);
    if(!CheckRpcReply("declare chat.dlq", err)) {
        return false;
    }
    amqp_queue_bind(m_conn, m_channel_id, amqp_cstring_bytes("chat.dlq"),
                    amqp_cstring_bytes("chat.dlx"), amqp_cstring_bytes(""), amqp_empty_table);
    if(!CheckRpcReply("bind chat.dlq", err)) {
        return false;
    }
    return true;
}

bool AmqpClient::DeclareBusinessExchange(std::string& err) {
    amqp_exchange_declare(m_conn, m_channel_id, amqp_cstring_bytes("business.exchange"),
                          amqp_cstring_bytes("topic"), 0, 1, 0, 0, amqp_empty_table);
    return CheckRpcReply("declare business.exchange", err);
}

bool AmqpClient::StartConsume(const std::string& queue, const std::string& consumer_tag, bool auto_ack, std::string& err) {
    amqp_basic_consume(m_conn, m_channel_id, ToBytes(queue), ToBytes(consumer_tag),
                       0, auto_ack ? 1 : 0, 0, amqp_empty_table);
    return CheckRpcReply("amqp_basic_consume", err);
}

bool AmqpClient::Publish(const std::string& exchange, const std::string& routing_key, const std::string& body,
                         uint8_t priority, const std::string& message_id, std::string& err) {
    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG
                 | AMQP_BASIC_DELIVERY_MODE_FLAG
                 | AMQP_BASIC_PRIORITY_FLAG
                 | AMQP_BASIC_MESSAGE_ID_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    props.delivery_mode = 2;
    props.priority = priority;
    props.message_id = ToBytes(message_id);

    int ret = amqp_basic_publish(m_conn, m_channel_id, ToBytes(exchange), ToBytes(routing_key),
                                 0, 0, &props, ToBytes(body));
    if(ret != AMQP_STATUS_OK) {
        err = amqp_error_string2(ret);
        return false;
    }
    return true;
}

bool AmqpClient::Consume(AmqpDelivery& out, int timeout_ms, std::string& err) {
    amqp_envelope_t envelope;
    amqp_maybe_release_buffers(m_conn);
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    amqp_rpc_reply_t reply = amqp_consume_message(m_conn, &envelope, &timeout, 0);
    if(reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION && reply.library_error == AMQP_STATUS_TIMEOUT) {
        return false;
    }
    if(reply.reply_type != AMQP_RESPONSE_NORMAL) {
        err = RpcReplyToText(reply);
        return false;
    }

    out.delivery_tag = envelope.delivery_tag;
    out.routing_key = AmqpBytesToString(envelope.routing_key);
    out.body = AmqpBytesToString(envelope.message.body);
    if(envelope.message.properties._flags & AMQP_BASIC_MESSAGE_ID_FLAG) {
        out.message_id = AmqpBytesToString(envelope.message.properties.message_id);
    } else {
        out.message_id.clear();
    }
    amqp_destroy_envelope(&envelope);
    return true;
}

bool AmqpClient::Ack(uint64_t delivery_tag, std::string& err) {
    int ret = amqp_basic_ack(m_conn, m_channel_id, delivery_tag, 0);
    if(ret != AMQP_STATUS_OK) {
        err = amqp_error_string2(ret);
        return false;
    }
    return true;
}

bool AmqpClient::Nack(uint64_t delivery_tag, bool requeue, std::string& err) {
    int ret = amqp_basic_nack(m_conn, m_channel_id, delivery_tag, 0, requeue ? 1 : 0);
    if(ret != AMQP_STATUS_OK) {
        err = amqp_error_string2(ret);
        return false;
    }
    return true;
}

void AmqpClient::Close() {
    if(m_conn) {
        if(m_connected) {
            amqp_channel_close(m_conn, m_channel_id, AMQP_REPLY_SUCCESS);
            amqp_connection_close(m_conn, AMQP_REPLY_SUCCESS);
        }
        amqp_destroy_connection(m_conn);
    }
    m_conn = nullptr;
    m_socket = nullptr;
    m_connected = false;
}

bool AmqpClient::CheckRpcReply(const char* op, std::string& err) {
    amqp_rpc_reply_t r = amqp_get_rpc_reply(m_conn);
    if(r.reply_type == AMQP_RESPONSE_NORMAL) {
        return true;
    }
    std::ostringstream oss;
    oss << op << " failed: " << RpcReplyToText(r);
    err = oss.str();
    return false;
}

std::string AmqpClient::AmqpBytesToString(amqp_bytes_t in) {
    if(in.bytes == nullptr || in.len == 0) {
        return std::string();
    }
    return std::string(static_cast<const char*>(in.bytes), in.len);
}

}
}
