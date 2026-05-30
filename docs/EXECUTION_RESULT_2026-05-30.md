# flz_chat 本次执行结果文档（2026-05-30）

## 1. 执行目标

依据 `docs/AGENT_DEV_SPEC.md`，完成 `flz_chat` 长连接服务的一次完整工程化落地，重点覆盖：

- WebSocket 鉴权与协议重构
- 在线会话管理（多端）
- MQ 双向桥接（WS <-> RabbitMQ <-> business）
- 心跳与限流
- 配置化与可观测性
- 联调脚本与启动文档

---

## 2. 本次已完成内容

## 2.1 协议与连接层改造

- 重写 `chat/chat_servlet.cc`、`chat/chat_servlet.h`，移除旧版 `login_request/send_request` 聊天室逻辑。
- 新连接流程支持 JWT 鉴权，鉴权通过返回 `auth_ok`，失败返回 `auth_fail` 并关闭连接。
- 收包统一按 `{"type","seq","data"}` 解析（`chat/protocol.*` 重构）。
- 支持核心上行事件：
  - `ping`
  - `msg.send`
  - `msg.ack`
  - `msg.read`
  - `bye`
- 支持下行事件路由：
  - `pong`
  - `msg.send.resp`
  - `msg.new`
  - `msg.replay`
  - `msg.recall`
  - `friend.request`
  - `friend.accept`
  - `conversation.created`
  - `conversation.member_changed`
  - `kicked`

## 2.2 JWT 校验模块

新增：

- `chat/auth/jwt.h`
- `chat/auth/jwt.cc`
- `chat/auth/jwt_claims.h`

能力：

- HS256 签名校验（OpenSSL HMAC）
- `iss/exp/iat/sub/did` 等 claim 校验
- 时钟偏移容忍（来自配置）
- 常量时间签名比较（`CRYPTO_memcmp`）

## 2.3 在线会话表（SessionRegistry）

新增：

- `chat/session/online_conn.h`
- `chat/session/session_registry.h`
- `chat/session/session_registry.cc`

实现要点：

- 存储结构：`userId -> deviceId -> OnlineConn`
- 支持同设备新登录踢旧连接
- 支持用户端数上限（超限淘汰最旧连接）
- 维护 `last_active_ts`，供心跳巡检
- 支持在线用户数、在线连接数统计

## 2.4 心跳与限流

- `ping` 即时回 `pong`。
- 在 `my_module` 中注册 10 秒巡检定时器。
- 超过超时时间（配置 `chat.heartbeat_timeout_seconds`）自动关闭连接。
- 单连接 token bucket 限流（默认每秒 20 帧），超限断连。

## 2.5 MQ 集成与桥接

新增目录与模块：

- `chat/mq/amqp_client.*`
- `chat/mq/envelope.*`
- `chat/mq/publisher.*`
- `chat/mq/consumer.*`
- `chat/mq/topology.*`
- `chat/dispatch/ws_to_mq.*`
- `chat/dispatch/mq_to_ws.*`

实现要点：

- 使用 `rabbitmq-c`，不修改 `flz_server/`。
- Publisher/Consumer 使用独立线程运行。
- 统一 Envelope：`msgId/version/occurredAt/source/type/payload`。
- 启动时声明 `chat` 侧拓扑，并声明 `business.exchange`。
- 消费端支持本地 LRU 幂等（24h 级别）。
- `msg.send` 上行建立 pendingMap，收到 `chat.msg.send` 后回执 `msg.send.resp` 并分发消息。

## 2.6 配置模块

新增：

- `chat/config/chat_config.h`
- `chat/config/chat_config.cc`

新增配置文件：

- `bin/conf/rabbitmq.yml`
- `bin/conf/jwt.yml`
- `bin/conf/chat.yml`
- `bin/conf/redis.yml`

能力：

- 统一加载 MQ/JWT/Redis/Chat 运行参数
- JWT secret 支持 `${ENV:JWT_SECRET}`
- 关键字段合法性校验（端口、心跳配置、算法等）

## 2.7 指标与可观测性

新增：

- `chat/metrics/metrics.h`
- `chat/metrics/metrics.cc`

新增接口：

- HTTP `GET /metrics`（Prometheus 文本格式）

覆盖指标：

- 在线用户数/连接数
- WS 帧收发计数
- MQ publish/consume/reconnect 计数
- JWT 失败计数
- `msg.send` 延迟直方图

## 2.8 脚本与文档

- 重写 `scripts/ws_chat_test.py`，覆盖 5 类集成测试场景。
- 新增 `scripts/run_mock_business.py`（pika 模拟业务侧消费/回投）。
- 新增 `README.md`（依赖、启动顺序、端口与构建说明）。
- 更新 `CMakeLists.txt` 纳入新增源码并链接 `librabbitmq`。

---

## 3. 构建与静态验证结果

已执行并通过：

1. `cmake .. && make -j4`（在 `build/`）
2. `python3 -m py_compile scripts/ws_chat_test.py scripts/run_mock_business.py`
3. IDE 诊断检查（`ReadLints`）无新增错误

---

## 4. 与规格对照结果

已覆盖的核心规格点：

- JWT 鉴权（握手阶段）
- `userId->deviceId->session` 在线表
- 心跳超时断开
- 多端策略（同设备顶替、端数限制）
- MQ 上行（`business.msg.persist/user.online/user.offline/msg.ack`）
- MQ 下行（`chat.msg.send/replay/recall/friend/conversation`）
- `/metrics` 暴露
- 配置文件补齐（rabbitmq/jwt/chat/redis）
- mock business 联调脚本补齐

尚未完全闭环/需二次增强项：

- Redis 幂等客户端未接入（当前为本地 LRU 幂等，满足“可选 Redis”最小实现）。
- 文档要求的 C++ 单元测试（`jwt_test/session_registry_test/...`）尚未单独新增测试目标。
- 集成测试脚本虽已覆盖场景，但在当前执行中未进行“带 RabbitMQ 实例”的全链路实跑验收。

---

## 5. 变更文件清单（本次新增/重构）

- 构建：
  - `CMakeLists.txt`
- 核心：
  - `chat/chat_servlet.cc`
  - `chat/chat_servlet.h`
  - `chat/protocol.cc`
  - `chat/protocol.h`
  - `chat/my_module.cc`
  - `chat/my_module.h`
- 新增模块：
  - `chat/auth/*`
  - `chat/config/*`
  - `chat/session/*`
  - `chat/mq/*`
  - `chat/dispatch/*`
  - `chat/util/*`
  - `chat/metrics/*`
- 配置：
  - `bin/conf/rabbitmq.yml`
  - `bin/conf/jwt.yml`
  - `bin/conf/chat.yml`
  - `bin/conf/redis.yml`
- 脚本：
  - `scripts/ws_chat_test.py`
  - `scripts/run_mock_business.py`
- 文档：
  - `README.md`
  - `docs/EXECUTION_RESULT_2026-05-30.md`（本文档）

---

## 6. 建议的下一步

1. 启动 RabbitMQ + `scripts/run_mock_business.py`，实跑 `scripts/ws_chat_test.py`，固化联调日志。  
2. 补齐 C++ 单元测试目标（JWT、SessionRegistry、Envelope、LRU、UUID）。  
3. 若准备多实例部署，优先接入 Redis 幂等与在线状态外部化抽象。  
