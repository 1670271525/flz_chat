# flz_chat 长连接服务 — Agent 开发技术规格书

> 适用对象：负责在 `flz_chat`（基于 `flz_server` 框架）上落地 MQ 接入、JWT 鉴权、客户端长连接协议、在线状态与消息路由的开发 Agent。
>
> 配套文档：
> - `docs/MQ.md` — RabbitMQ 契约总览
> - `docs/MQ_CHAT_SERVICE_API.md` — 业务侧 ⇄ 长连接侧 MQ 接口
> - `plan.txt` — 项目大纲约束
> - `db/*.sql` — 仅作为字段语义参考，本服务不直接读写 MySQL
>
> 重要约束（来自 `plan.txt`）：
> 1. **绝不修改 `flz_server/` 目录**，只能基于其网络框架进行二次开发；
> 2. 业务侧（`flz_chat_business`）已实现登录注册、好友、会话、消息持久化等；
> 3. 本服务**不直接连 MySQL**，所有持久化通过 MQ 走业务侧；
> 4. 本服务负责：WebSocket 长连接、JWT 鉴权、在线会话表、心跳、客户端 ↔ MQ 双向桥接、未读回放下发。

---

## 0. 术语与角色

| 名称 | 说明 |
|---|---|
| `chat` / 长连接服务 | 即本项目 `flz_chat`，对外暴露 WebSocket，对内 ⇄ RabbitMQ |
| `flz_chat_business` / 业务服务 | 外部 Java 服务，负责账号、登录、好友、会话、消息持久化 |
| `userId` | `BIGINT`，与 `db/01_user.sql` 一致，业务服务与 MQ 中所有标识用户的主键 |
| `clientMsgId` | 客户端生成的 UUID v4，纯文本消息的幂等键，对应 `message.client_msg_id` |
| `msgId` | MQ 信封级 UUID v4，长连接服务在发布 MQ 时由本服务生成 |
| `sessionKey` | 单条长连接连接级 ID，本服务内部用，不外发 |

---

## 1. 现状梳理

### 1.1 已有代码

- `chat/chat_servlet.{h,cc}`：基于 `flz::http::WSServlet`，目前用 `name -> WSSession` map 实现简单聊天室广播；登录是字符串 `login_request`。
- `chat/protocol.{h,cc}`：极薄的 `ChatMessage`，内部是 `map<string,string>`，序列化为 JSON。
- `chat/my_module.cc`：作为 `flz::Module` 加载，把 `ChatWSServlet` 注册到 `ws` 服务的 `/flz/chat`。
- `bin/conf/server.yml`：`http` 监听 `8090/8091`，`ws` 监听 `8072/8071`。
- `flz_server` 已提供：`IOManager`、`WSSession`、`WSFrameMessage`、`Config`、`Logger`、`Application::GetInstance()`、`CryptoUtil`、`JsonUtil`。

### 1.2 需要重写 / 新增

| 模块 | 状态 |
|---|---|
| 现有 `chat_servlet.cc` 中的 `login_request` 协议 | **删除**，替换为基于 JWT 的握手鉴权 |
| `name -> session` 表 | **重写**为 `userId -> deviceId -> session` 多端表 |
| 心跳 | **新增**：客户端 `type=ping` / 服务端 `type=pong`，并有探活定时器 |
| JWT 校验 | **新增**：在 WS 握手或首帧完成校验 |
| RabbitMQ 客户端 | **新增**：发布者 / 消费者，与 `IOManager` 协调 |
| MQ ⇄ WS 桥接 | **新增**：上行 `business.*`，下行 `chat.*` |
| 未读回放 | **新增**：用户上线 → 发 `business.user.online` → 收 `chat.msg.replay` → 推送 |
| 在线状态 | **新增**：本节点维护、上下线发 MQ |
| Redis 幂等（可选） | **新增**：`mq:processed:{msgId}` 24h |
| 配置：MQ / Redis / JWT | **新增** `bin/conf/rabbitmq.yml`、`jwt.yml`、`redis.yml` |

---

## 2. 总体架构

```
┌──────────────────┐  WSS /flz/chat?token=xxx        ┌───────────────────────────┐
│  客户端 (App/Web) │ ───────────────────────────────►│         flz_chat          │
│                  │ ◄───── ping/pong/msg/recall ────│  (本服务, 基于 flz_server) │
└──────────────────┘                                 │                           │
                                                     │ ┌───────────────────────┐ │
                                                     │ │ SessionRegistry       │ │
                                                     │ │  uid -> [device -> s] │ │
                                                     │ └───────────────────────┘ │
                                                     │ ┌───────────────────────┐ │
                                                     │ │ JWT Verifier (HS256)  │ │
                                                     │ └───────────────────────┘ │
                                                     │ ┌───────────────────────┐ │
                                                     │ │ MQ Publisher (async)  │ │
                                                     │ │ MQ Consumer  (loop)   │ │
                                                     │ └───────────────────────┘ │
                                                     └──────────┬────────────────┘
                                                                │ AMQP
                                                                ▼
                                       ┌──────────────────────────────────────────┐
                                       │           RabbitMQ Broker                │
                                       │ chat.exchange (topic)                    │
                                       │ business.exchange (topic)                │
                                       │ chat.dlx / business.dlx (fanout)         │
                                       └──────────────────────────────────────────┘
                                                                ▲
                                                                │ AMQP
                                       ┌──────────────────────────────────────────┐
                                       │      flz_chat_business (Java 业务)       │
                                       └──────────────────────────────────────────┘
```

---

## 3. 鉴权（JWT）

### 3.1 Token 来源

客户端登录由业务服务签发，本服务**只做校验，不签发**。

### 3.2 算法与 Claims

- 算法：`HS256`
- 密钥：通过 `bin/conf/jwt.yml` 注入；与业务服务保持一致；
- 必填 claims：
  - `sub`：`userId`（字符串数字）
  - `did`：`deviceId`（字符串，客户端唯一标识；如缺失，服务端按 `sessionKey` 兜底分配 `device-{n}`）
  - `iat`、`exp`（秒级 UNIX 时间）
  - `iss`：固定 `flz_chat_business`
- 可选 claims：`scope`、`platform`（`ios`/`android`/`web`）

### 3.3 Token 携带方式

WebSocket 握手阶段（HTTP Upgrade）按以下优先级取 token：

1. URL query：`/flz/chat?token=<jwt>`（推荐，兼容浏览器）；
2. Header：`Authorization: Bearer <jwt>`；
3. WS 子协议：`Sec-WebSocket-Protocol: bearer.<jwt>`（仅作兼容）。

### 3.4 校验时机

在 `ChatWSServlet::onConnect` 内立即校验：

1. 从 `HttpRequest` 解析 token；
2. 校验签名 + `exp` + `iss`；
3. 失败：写一帧 `{"type":"auth_fail","code":401,"msg":"..."}` 并 `session->close()`；
4. 成功：
   - 把 `userId/deviceId/exp` 写入 `header->setHeader("$uid", ...)` 等占位字段；
   - 加入 `SessionRegistry`；
   - 发布 `business.user.online`（仅当此 `userId` 在本节点首次上线）。

### 3.5 实现建议

- 不引入重型 jwt 库；HS256 用现成 `OpenSSL HMAC-SHA256` + 自行做 base64url 解析（`flz_server` 已链接 `openssl`）；
- 实现头：`chat/auth/jwt.h`，纯 stateless 静态方法 `bool JwtVerifier::Verify(const std::string& token, JwtClaims& out, std::string& err)`；
- 时钟漂移容忍：`±60s`。

---

## 4. 客户端 ⇄ 长连接 WebSocket 协议

所有帧 **文本帧 + JSON**，外层结构：

```json
{ "type": "<event>", "seq": 123, "data": { ... } }
```

- `type`：必填，事件名；
- `seq`：可选，客户端单调递增，服务端 `ack/resp` 时回带；
- `data`：事件载荷，结构见下。

### 4.1 上行（客户端 → 服务端）

| type | data 关键字段 | 说明 |
|---|---|---|
| `ping` | `{ "ts": 1717050000000 }` | 心跳，间隔 25s |
| `msg.send` | `{ "clientMsgId":"uuid", "conversationId":9001, "content":"hi", "sentAt":"2026-05-28T16:08:00+08:00" }` | 仅文本（type=1）；其他类型走业务 HTTP |
| `msg.ack` | `{ "messageId": 5568 }` | 客户端确认收到下发消息 |
| `msg.read` | `{ "conversationId": 9001, "lastReadMessageId": 5570 }` | 已读上报（透传到业务，可选，本期可只记录日志） |
| `bye` | `{}` | 主动登出，服务端关闭连接前发布 offline |

### 4.2 下行（服务端 → 客户端）

| type | data 关键字段 | 触发 |
|---|---|---|
| `auth_ok` | `{ "userId":1001, "deviceId":"ios-x", "serverTime": ... }` | 握手鉴权通过，连接建立首帧 |
| `auth_fail` | `{ "code":401, "msg":"token expired" }` | 鉴权失败，随后关闭 |
| `pong` | `{ "ts": 1717050000000 }` | 心跳回执 |
| `msg.send.resp` | `{ "clientMsgId":"uuid", "code":200, "messageId":5568 }` | 业务持久化结果转发（见 §6.3） |
| `msg.new` | 与 `chat.msg.send` payload 同 | 新消息下发 |
| `msg.recall` | 与 `chat.msg.recall` 同 | 撤回 |
| `msg.replay` | `{ "conversationId":9001, "messages":[...] }` | 离线回放 |
| `friend.request` / `friend.accept` | 与 MQ 同 | 好友事件 |
| `conversation.created` / `conversation.member_changed` | 与 MQ 同 | 会话事件 |
| `kicked` | `{ "reason":"login_elsewhere" }` | 被踢下线 |

### 4.3 心跳与超时

- 客户端：连接成功后每 **25s** 发送一次 `ping`；
- 服务端：每条连接维护 `lastActiveTs`；
- 服务端启动 1 个 `Timer`（`IOManager::addTimer`），周期 **10s** 巡检；
- 若 `now - lastActiveTs > 75s`：关闭连接，按下线流程处理；
- `pong` 立即回执，不排队。

### 4.4 多端登录

- 默认**允许多端共存**（不同 `deviceId`）；
- 同 `userId` + 同 `deviceId` 第二次登录：**踢掉旧连接**，向旧端先发 `kicked`，再关闭，再加入新连接；
- 是否“同账号最多 N 端”由配置 `chat.max_devices_per_user`（默认 5）控制。

---

## 5. 在线会话表 SessionRegistry

### 5.1 数据结构

```cpp
struct OnlineConn {
    uint64_t userId;
    std::string deviceId;
    flz::http::WSSession::ptr session;
    int64_t loginAt;
    std::atomic<int64_t> lastActiveTs;
};

class SessionRegistry {
public:
    using ptr = std::shared_ptr<SessionRegistry>;
    static SessionRegistry* GetInstance();

    // 返回需要踢掉的旧连接（同 uid+did 已存在时）
    OnlineConn::ptr add(OnlineConn::ptr conn);
    void remove(uint64_t userId, const std::string& deviceId);

    std::vector<OnlineConn::ptr> getByUser(uint64_t userId);
    bool isUserOnline(uint64_t userId);

    // 用于心跳巡检
    void touch(uint64_t userId, const std::string& deviceId);
    std::vector<OnlineConn::ptr> collectTimeouted(int64_t deadlineMs);

private:
    flz::RWMutex m_mutex;
    // uid -> (did -> conn)
    std::unordered_map<uint64_t,
        std::unordered_map<std::string, OnlineConn::ptr>> m_conns;
};
```

### 5.2 上线/下线策略（与 MQ 联动）

- **首端上线**（该 `userId` 之前不在线）：发布 `business.user.online`；
- **末端下线**（该 `userId` 移除最后一条连接）：发布 `business.user.offline`；
- 中间端进出不发 MQ。

> 注：本期单实例部署，集群化（多个 `chat` 节点共享在线状态）放到 §13 演进项；接口预留 `OnlineStateBackend` 抽象，默认实现为内存。

---

## 6. RabbitMQ 集成

### 6.1 选型

- C++ AMQP 客户端：**rabbitmq-c**（C 库）+ 轻量封装；不引入 SimpleAmqpClient 以减少依赖；
- 序列化：直接使用 `flz::JsonUtil`（已链接 jsoncpp）；
- UUID：自实现 v4（`/dev/urandom` + 格式化），新增 `chat/util/uuid.h`。

### 6.2 拓扑（与 `docs/MQ.md` 一致，**本服务负责声明 chat 侧拓扑**）

启动时由本服务声明（业务侧也会幂等声明，互不影响）：

```text
exchange: chat.exchange     type=topic durable=true
exchange: chat.dlx          type=fanout durable=true
queue:    chat.delivery.queue durable=true
            x-dead-letter-exchange = chat.dlx
            x-message-ttl = 86400000
binding:  chat.exchange  --routing-key=chat.#--> chat.delivery.queue
queue:    chat.dlq         durable=true
binding:  chat.dlx --> chat.dlq
```

本服务**发布**到 `business.exchange`，仅声明 exchange（队列由业务侧声明），路由键：

- `business.msg.persist`
- `business.user.online`
- `business.user.offline`
- `business.msg.ack`

### 6.3 双向数据流

#### 6.3.1 客户端文本消息上行

```
ws: msg.send (clientMsgId, conversationId, content)
   └─► MQ publish: business.msg.persist
         payload 见 docs/MQ_CHAT_SERVICE_API.md §4.1
   ◄── 业务持久化后回 chat.msg.send
   ├─► 找到 receivers 在本节点的所有连接 → 推送 msg.new
   └─► 若 receivers 含发送者自己的其他端 → 同步推送（多端同步）
   └─► 向发送者发 msg.send.resp (clientMsgId, messageId)
       └─ 通过 clientMsgId 匹配（本服务维护 pendingMap）
```

`pendingMap`：`unordered_map<clientMsgId, {senderUserId, senderDeviceId, ts}>`，TTL 30s；
若 30s 未匹配到 `chat.msg.send`，仅打 warn 日志，不重发。

#### 6.3.2 用户上线回放

```
首端上线 → publish business.user.online {userId}
业务侧异步分批 publish chat.msg.replay {targetUserId, conversationId, messages[]}
   ├─► 仅向 targetUserId 的所有连接推送 msg.replay
```

#### 6.3.3 撤回 / 好友 / 会话事件

直接消费 `chat.exchange` 上对应路由键 → 转换为 §4.2 的下行帧推送。

### 6.4 与 IOManager 的集成

`rabbitmq-c` 是阻塞 socket 模型，**禁止**在协程内直接调用。约定：

- 起 2 个**独立 OS 线程**（不在 `IOManager` 工作线程上）：
  - `mq_publisher_thread`：阻塞 publish channel；接收来自业务线程的 publish 任务（lock-free 队列，回退普通 `mutex+cv`）；
  - `mq_consumer_thread`：阻塞 `amqp_consume_message` 死循环；
- 收到消息后**不要**在该线程上做重逻辑，将消息通过 `flz::IOManager::GetThis()->schedule()` 投递回主调度器再分发；
- 关闭：进程退出时通过 `shutdown(sockfd, SHUT_RDWR)` 唤醒阻塞调用；线程 `join`；
- 重连：consumer 线程 catch 所有异常 → `sleep_for(指数退避 1~30s)` → 重连 → 重新声明 → 继续 consume。

### 6.5 消息信封

发布时统一外壳（与 `docs/MQ.md` §2 一致）：

```json
{
  "msgId": "<uuid v4>",
  "version": 1,
  "occurredAt": "<ISO8601+08:00>",
  "source": "chat",
  "type": "<routingKey>",
  "payload": { ... }
}
```

AMQP properties：
- `content_type = application/json`
- `delivery_mode = 2`（持久化）
- `priority = 0`（在线/上线类可 5）
- `message_id = msgId`

### 6.6 消费幂等

- 收到任一帧 → 取 `envelope.msgId`；
- 优先 Redis：`SET mq:processed:{msgId} 1 EX 86400 NX`，返回 0 跳过；
- 若未配置 Redis：本地 `LruCache<string,bool>`，容量 10w，TTL 24h（用 std::list+map 简易实现），**仅适合单实例**；
- 业务异常 → `basic_nack(requeue=false)`；瞬时异常 → `basic_nack(requeue=true)`，broker 端策略 + DLX 兜底。

---

## 7. 模块/目录结构

```
chat/
├── my_module.cc                   # (已存在) 入口，启动时初始化下述子模块
├── my_module.h
├── chat_servlet.cc / .h           # 重写：握手鉴权 + 路由 WS 帧到 dispatcher
├── protocol.cc / .h               # 保留并扩展：客户端 JSON 协议封装
├── auth/
│   ├── jwt.h / .cc                # JWT 校验（HS256）
│   └── jwt_claims.h
├── session/
│   ├── session_registry.h / .cc   # 在线表
│   └── online_conn.h
├── mq/
│   ├── amqp_client.h / .cc        # 对 rabbitmq-c 的薄封装
│   ├── envelope.h / .cc           # 信封序列化 / 反序列化
│   ├── publisher.h / .cc          # publisher 线程
│   ├── consumer.h / .cc           # consumer 线程
│   └── topology.h / .cc           # 声明 exchange/queue/binding
├── dispatch/
│   ├── ws_to_mq.cc / .h           # 上行：WS msg → MQ publish
│   └── mq_to_ws.cc / .h           # 下行：MQ → SessionRegistry → push
├── util/
│   ├── uuid.h / .cc
│   ├── time_util.h / .cc          # ISO8601 +08:00 格式化
│   ├── base64url.h / .cc
│   └── lru_idempotency.h / .cc
└── config/
    └── chat_config.h / .cc        # 从 yml 加载 mq/jwt/redis 配置
```

`CMakeLists.txt` 增量（保持现有结构）：

```cmake
set(LIB_SRC
    chat/my_module.cc
    chat/chat_servlet.cc
    chat/protocol.cc
    chat/auth/jwt.cc
    chat/session/session_registry.cc
    chat/mq/amqp_client.cc
    chat/mq/envelope.cc
    chat/mq/publisher.cc
    chat/mq/consumer.cc
    chat/mq/topology.cc
    chat/dispatch/ws_to_mq.cc
    chat/dispatch/mq_to_ws.cc
    chat/util/uuid.cc
    chat/util/time_util.cc
    chat/util/base64url.cc
    chat/util/lru_idempotency.cc
    chat/config/chat_config.cc
)

find_library(RABBITMQ_LIB rabbitmq REQUIRED)
target_link_libraries(flz_chat ${RABBITMQ_LIB})
```

依赖安装（Ubuntu）：

```bash
sudo apt-get install -y librabbitmq-dev
# 可选 Redis 客户端（推荐 hiredis）：
sudo apt-get install -y libhiredis-dev
```

---

## 8. 配置

### 8.1 `bin/conf/rabbitmq.yml`

```yaml
rabbitmq:
  host: 127.0.0.1
  port: 5672
  vhost: /
  username: flz_chat
  password: "********"
  heartbeat: 30
  frame_max: 131072

  publisher:
    confirm_select: true
    queue_capacity: 10000

  consumer:
    queue: chat.delivery.queue
    prefetch: 64
    auto_ack: false
    consumer_tag: chat-svc-1

  reconnect:
    initial_backoff_ms: 1000
    max_backoff_ms: 30000
```

### 8.2 `bin/conf/jwt.yml`

```yaml
jwt:
  algorithm: HS256
  secret: "${ENV:JWT_SECRET}"   # 也允许直接字面量
  issuer: flz_chat_business
  clock_skew_seconds: 60
```

### 8.3 `bin/conf/redis.yml`（可选）

```yaml
redis:
  enabled: false
  host: 127.0.0.1
  port: 6379
  password: ""
  db: 0
  pool_size: 4
  idempotency_ttl_seconds: 86400
```

### 8.4 `bin/conf/chat.yml`

```yaml
chat:
  max_devices_per_user: 5
  heartbeat_interval_seconds: 25
  heartbeat_timeout_seconds: 75
  pending_send_ttl_seconds: 30
  ws_path: /flz/chat
```

加载方式：使用 `flz_server` 已有的 `flz::Config` 注册 `ConfigVar`，在 `MyModule::onLoad()` 阶段加载并校验，失败直接返回 `false`。

---

## 9. 关键流程伪代码

### 9.1 `ChatWSServlet::onConnect`

```cpp
int32_t ChatWSServlet::onConnect(HttpRequest::ptr req, WSSession::ptr s) {
    std::string token = ExtractToken(req);   // query > header > subprotocol
    JwtClaims claims; std::string err;
    if (!JwtVerifier::Verify(token, claims, err)) {
        WriteJson(s, MakeAuthFail(401, err));
        s->close();
        return -1;
    }

    auto did = claims.deviceId.empty()
             ? AllocFallbackDeviceId(s) : claims.deviceId;

    auto conn = std::make_shared<OnlineConn>(claims.userId, did, s);
    bool firstDevice = !SessionRegistry::GetInstance()->isUserOnline(claims.userId);

    if (auto kicked = SessionRegistry::GetInstance()->add(conn)) {
        WriteJson(kicked->session, MakeKicked("login_elsewhere"));
        kicked->session->close();
    }

    req->setHeader("$uid", std::to_string(claims.userId));
    req->setHeader("$did", did);
    WriteJson(s, MakeAuthOk(claims.userId, did));

    if (firstDevice) {
        MqPublisher::Get()->publishUserOnline(claims.userId);
    }
    return 0;
}
```

### 9.2 `ChatWSServlet::onClose`

```cpp
int32_t ChatWSServlet::onClose(HttpRequest::ptr req, WSSession::ptr s) {
    uint64_t uid = ParseU64(req->getHeader("$uid"));
    auto did    = req->getHeader("$did");
    if (uid == 0) return 0;

    SessionRegistry::GetInstance()->remove(uid, did);
    if (!SessionRegistry::GetInstance()->isUserOnline(uid)) {
        MqPublisher::Get()->publishUserOffline(uid);
    }
    return 0;
}
```

### 9.3 `ChatWSServlet::handle`

```cpp
int32_t ChatWSServlet::handle(HttpRequest::ptr req,
                              WSFrameMessage::ptr msg,
                              WSSession::ptr s) {
    Json::Value j;
    if (!flz::JsonUtil::FromString(j, msg->getData())) {
        return SendErr(s, 400, "bad json");
    }
    uint64_t uid = ParseU64(req->getHeader("$uid"));
    auto did    = req->getHeader("$did");
    SessionRegistry::GetInstance()->touch(uid, did);

    const std::string type = j.get("type", "").asString();
    if (type == "ping")          return OnPing(s, j);
    if (type == "msg.send")      return OnMsgSend(uid, did, s, j);
    if (type == "msg.ack")       return OnMsgAck(uid, j);
    if (type == "msg.read")      return OnMsgRead(uid, j);
    if (type == "bye")           { s->close(); return 0; }
    return SendErr(s, 404, "unknown type");
}
```

### 9.4 上行 `msg.send` 处理

```cpp
int OnMsgSend(uint64_t uid, std::string did, WSSession::ptr s, const Json::Value& f) {
    auto data = f["data"];
    std::string clientMsgId   = data["clientMsgId"].asString();
    int64_t conversationId    = data["conversationId"].asInt64();
    std::string content       = data["content"].asString();
    std::string sentAt        = data.get("sentAt", flz::Time2Iso8601Now()).asString();

    if (clientMsgId.empty() || conversationId == 0 || content.empty()) {
        return SendErr(s, 400, "bad msg.send");
    }
    if (content.size() > 8192) return SendErr(s, 413, "content too long");

    PendingSendMap::Get()->put(clientMsgId, {uid, did, NowMs()});

    Json::Value payload;
    payload["clientMsgId"]    = clientMsgId;
    payload["conversationId"] = (Json::Int64)conversationId;
    payload["senderId"]       = (Json::UInt64)uid;
    payload["type"]           = 1;
    payload["content"]        = content;
    payload["sentAt"]         = sentAt;

    MqPublisher::Get()->publish("business.msg.persist", payload);
    return 0;
}
```

### 9.5 下行消费 `chat.msg.send`

```cpp
void OnChatMsgSend(const Envelope& env) {
    auto p = env.payload;
    int64_t  messageId      = p["messageId"].asInt64();
    int64_t  convId         = p["conversationId"].asInt64();
    uint64_t senderId       = p["senderId"].asUInt64();
    auto     receivers      = p["receivers"];

    // 1) 发送者多端同步 + msg.send.resp
    auto pending = PendingSendMap::Get()->take(p["clientMsgId"].asString());
    if (pending) {
        for (auto& c : SessionRegistry::GetInstance()->getByUser(senderId)) {
            if (c->deviceId == pending->deviceId) {
                WriteJson(c->session, MakeMsgSendResp(p["clientMsgId"].asString(),
                                                     200, messageId));
            } else {
                WriteJson(c->session, MakeMsgNew(p));   // 自己其他端同步
            }
        }
    }

    // 2) 推送给所有 receiver 在本节点的连接
    for (auto& r : receivers) {
        uint64_t uid = r.asUInt64();
        if (uid == senderId) continue;
        for (auto& c : SessionRegistry::GetInstance()->getByUser(uid)) {
            WriteJson(c->session, MakeMsgNew(p));
        }
    }
}
```

### 9.6 心跳巡检定时器

```cpp
flz::IOManager::GetThis()->addTimer(10 * 1000, [] {
    int64_t deadline = NowMs() - 75 * 1000;
    for (auto& c : SessionRegistry::GetInstance()->collectTimeouted(deadline)) {
        FLZ_LOG_INFO(g_logger) << "timeout uid=" << c->userId << " did=" << c->deviceId;
        c->session->close();
        // onClose 会处理 remove + offline 通知
    }
}, true);
```

---

## 10. 错误码与可观测性

### 10.1 客户端可见错误码

| code | 含义 |
|---|---|
| 200 | 成功 |
| 400 | 报文格式错误 |
| 401 | token 无效/过期 |
| 403 | token 校验通过但被业务侧禁用（保留） |
| 404 | 未知 type |
| 413 | 内容超长 |
| 423 | 同设备登录被踢 |
| 500 | 服务端内部错误 |
| 503 | MQ 不可用（短暂） |

### 10.2 日志

- 全部使用 `FLZ_LOG_*`；
- INFO：握手、上下线、MQ 重连、消息收发摘要（clientMsgId/messageId/uid）；
- WARN：幂等命中跳过、pending 超时、单帧解析失败、心跳超时；
- ERROR：MQ 发布/消费异常、JWT 校验失败、配置加载失败；
- 不打印 `content` 原文，必要时打 hash 前 8 位。

### 10.3 监控指标（HTTP `/metrics` 由 HttpServer 暴露，Prometheus 文本格式）

注册到现有 `http` 服务的 ServletDispatch 中（`my_module.cc` 已有钩子）：

| 指标 | 类型 | 标签 |
|---|---|---|
| `chat_online_users` | gauge | — |
| `chat_online_connections` | gauge | — |
| `chat_ws_frames_total` | counter | direction=in/out, type |
| `chat_mq_publish_total` | counter | routing_key, result=ok/fail |
| `chat_mq_consume_total` | counter | routing_key, result=ok/dup/fail |
| `chat_mq_reconnect_total` | counter | role=publisher/consumer |
| `chat_msg_send_latency_ms` | histogram | — |
| `chat_jwt_verify_fail_total` | counter | reason |

---

## 11. 安全与限流

- 单连接每秒最多 **20** 个 WS 帧，超过断连；用 `token bucket`，挂在 `OnlineConn`；
- `content` 长度上限 8KB；超出拒绝；
- token 一定要校验 `exp`；不接受 `alg=none`；
- 不接受非 `application/json` 的二进制帧（除标准 control 帧）。

---

## 12. 测试方案

### 12.1 单元测试（gtest 或现有 `flz_server/test/` 风格）

- `jwt_test`：合法 / 过期 / 错误签名 / 缺 claim；
- `session_registry_test`：多端踢人、并发增删；
- `lru_idempotency_test`；
- `envelope_test`：序列化/反序列化；
- `uuid_test`：v4 格式与去重抽样。

### 12.2 集成测试

- 在 `scripts/ws_chat_test.py` 基础上扩展：
  - 模拟两端登录、发文本、收 `msg.send.resp` + 自己其他端收 `msg.new`；
  - 第三端模拟接收方收 `msg.new`；
  - 离线后再上线收 `msg.replay`；
  - token 过期立刻断开；
  - 心跳停止 75s 被断开。
- 业务侧可用 `rabbitmqadmin publish` 手动构造 `chat.msg.send` / `chat.msg.recall` 等帧。

### 12.3 压测目标（单实例参考值）

| 项 | 目标 |
|---|---|
| 并发长连接 | ≥ 20k |
| 单连接平均空闲内存 | ≤ 40KB |
| msg.send 端到端 P95（不含业务持久化） | ≤ 50ms |
| MQ 发布 QPS | ≥ 5k |

---

## 13. 演进 / Out-of-scope

本期 **不做**，但接口预留：

- 多节点集群与跨节点路由（需要在线表外存到 Redis + 路由层）；
- 群广播大群优化（>500 人时分片下发）；
- 端到端加密；
- 富文本 / 媒体消息上行（按 `MQ.md` 走 HTTP，本期不支持 WS 上传）；
- WSS（TLS 终止建议放在 Nginx 前置）。

---

## 14. 交付物 Checklist

- [ ] §7 全部源码文件编译通过（`make` 在仓库根目录跑通）；
- [ ] `librabbitmq-dev` 安装文档已写进 `README` 顶部；
- [ ] `bin/conf/rabbitmq.yml`、`jwt.yml`、`chat.yml`（必需），`redis.yml`（可选）；
- [ ] `scripts/ws_chat_test.py` 覆盖 §12.2 列出的 5 个用例并通过；
- [ ] `/metrics` HTTP 接口可访问，关键指标齐全；
- [ ] 灰度脚本：`scripts/run_mock_business.py`（用 pika 模拟业务侧消费/发布，用于开发联调）；
- [ ] 文档：本规格书 + 在 `README.md` 中给出启动顺序与端口表。

---

## 附录 A. ISO8601 时间格式

统一 `YYYY-MM-DDTHH:MM:SS+08:00`；服务端写出用 `flz::Time2Str()` 不满足要求时新增 `chat/util/time_util.cc`。

## 附录 B. UUID v4 生成（参考实现）

```cpp
std::string Uuid::V4() {
    unsigned char b[16];
    RAND_bytes(b, 16);
    b[6] = (b[6] & 0x0F) | 0x40;
    b[8] = (b[8] & 0x3F) | 0x80;
    char out[37];
    snprintf(out, sizeof(out),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7],
        b[8],b[9], b[10],b[11],b[12],b[13],b[14],b[15]);
    return std::string(out, 36);
}
```

## 附录 C. JWT HS256 verify 思路

1. `header.payload.sig` 用 `.` 拆分；
2. base64url decode header → 校验 `alg=HS256`；
3. `HMAC_SHA256(secret, header + "." + payload)` → base64url encode → 与 `sig` 等长 const-time 比较；
4. base64url decode payload → 解析 JSON → 校验 `iss`、`exp`、`sub`。

不要使用 `==` 直接比较签名；用 `CRYPTO_memcmp`。
