# flz_chat_business 消息队列契约

> Broker：RabbitMQ。本文档定义 **业务侧服务 (`flz_chat_business`)** 与 **长连接侧服务 (`chat`)** 之间的 AMQP 契约。
>
> 任何对契约的破坏性变更（topic、字段、语义）必须同步更新本文件、长连接服务文档、双方版本号。

---

## 0. 总览

```
┌────────────────────┐       chat.exchange (topic)
│ flz_chat_business  │ ───────────────────────────────┐
└─────────┬──────────┘                                │
          │ publish: chat.msg.send / chat.msg.recall  │
          │          chat.friend.* / chat.user.online │
          ▼                                           ▼
                                          ┌────────────────┐
                                          │ chat 长连接服务 │
                                          └─────┬──────────┘
          ▲                                     │
          │ consume: business.persist.queue     │
          │  (chat.msg.persist / user.online.notify ...)
          │                                     │
┌────────────────────┐                          │
│ flz_chat_business  │ ◀────────────────────────┘
└────────────────────┘
```

- 两个 topic exchange：
  - `chat.exchange` —— **业务侧 ➜ 长连接侧** 的下发；
  - `business.exchange` —— **长连接侧 ➜ 业务侧** 的回调（持久化/上线通知等）。
- 业务侧队列前缀 `business.*`，长连接侧 `chat.*`，避免相互消费错乱。

---

## 1. 交换机 / 队列 / 路由键

### 1.1 业务侧 → 长连接侧（`chat.exchange`）

| 路由键 | 含义 | 持久化 |
|---|---|---|
| `chat.msg.send`        | 新消息下发（文本/媒体） | 是 |
| `chat.msg.recall`      | 消息撤回通知 | 是 |
| `chat.msg.replay`      | 用户上线回放未读 | 是 |
| `chat.friend.request`  | 新好友申请 | 是 |
| `chat.friend.accept`   | 好友申请被接受 | 是 |
| `chat.conversation.created`  | 新会话创建（拉对方进群/拉单聊） | 是 |
| `chat.conversation.member_changed` | 成员增减/角色变更 | 是 |
| `chat.user.profile_changed`        | 头像/昵称/状态变更（可选 broadcast 给好友） | 否（瞬时） |

长连接侧绑定示例：

```
queue: chat.delivery.queue
binding: chat.exchange  --routing-key=chat.#--> chat.delivery.queue
```

### 1.2 长连接侧 → 业务侧（`business.exchange`）

| 路由键 | 含义 | 备注 |
|---|---|---|
| `business.msg.persist` | 长连接收到客户端纯文本消息，请求持久化 | **仅 type=1 文本** |
| `business.user.online` | 用户上线通知 | 业务侧据此触发未读回放 |
| `business.user.offline`| 用户下线通知 | 业务侧仅记录/统计 |
| `business.msg.ack`     | 客户端确认已投递 | 可用于把 `message.status` 0→1 |

业务侧绑定示例：

```
queue: business.persist.queue       binding: business.msg.persist
queue: business.user.event.queue    binding: business.user.*
queue: business.msg.ack.queue       binding: business.msg.ack
```

队列均设置：`durable=true`、`x-dead-letter-exchange=business.dlx`、`x-message-ttl=86400000`。

### 1.3 死信

```
exchange: business.dlx (fanout)  → queue: business.dlq
exchange: chat.dlx (fanout)      → queue: chat.dlq
```

死信队列由运维告警监控；业务侧消费时配置最多 3 次重试，超过转死信。

---

## 2. 通用消息信封

所有消息体统一外层 JSON：

```json
{
  "msgId": "uuid-v4",            // MQ 消息唯一 id（与 business 内 client_msg_id 不同）
  "version": 1,
  "occurredAt": "2026-05-28T16:08:00+08:00",
  "source": "business" | "chat",
  "type": "chat.msg.send",       // 与 routingKey 同
  "payload": { ... }             // 见下文各类型
}
```

AMQP 头补充：
- `content-type: application/json`
- `delivery-mode: 2`（持久化）
- `priority`: 默认 0；系统通知/上线回放可 5

消费方必须：
1. 校验 `version`；
2. 用 `msgId` 做幂等（Redis SETNX `mq:processed:{msgId}` 24h）；
3. 失败抛异常 → 触发 nack & retry → 死信。

---

## 3. payload 详细字段

### 3.1 `business.msg.persist`（长连接 ➜ 业务，持久化主入口）

```json
{
  "clientMsgId": "5f3c-...",          // 客户端 UUID，业务侧入库唯一键
  "conversationId": 9001,
  "senderId": 1001,
  "type": 1,                          // 固定 1（纯文本）
  "content": "你好",                  // 文本内容
  "mediaMeta": {
    "size": 12345, "duration": 8, "thumbnail": "..."
  },
  "sentAt": "2026-05-28T16:08:00+08:00"
}
```

业务侧处理：
1. 仅接受 `type=1`，非文本消息直接拒绝（入死信）；
2. 校验 sender 属于该会话；
3. 对文本执行校验与过滤（非空、长度限制、敏感词替换）；
4. `INSERT message ... ON DUPLICATE KEY (client_msg_id) UPDATE id=LAST_INSERT_ID(id)`；
5. `UPDATE conversations SET last_message_id = GREATEST(last_message_id, NEW), last_message_at = NEW.created_at`；
6. 入库成功后发布 `chat.msg.send` 供长连接服务转发。

> 文件类型消息（2/3/4/5）不通过该 MQ 入口，由客户端/发送方调用业务服务 HTTP 上传后再走业务侧发送接口。

### 3.2 `chat.msg.send`（业务 ➜ 长连接，下发新消息）

```json
{
  "messageId": 5568,
  "conversationId": 9001,
  "senderId": 1001,
  "receivers": [1002, 1003],          // 业务侧从 participants 计算；不在群者忽略
  "type": 2,
  "content": "chat/1001/2026/05/28/uuid.jpg",
  "downloadUrl": "http://cdn/...&signature=...",   // 媒体类型时附带，TTL 见配置
  "downloadUrlExpireAt": "2026-08-28T...",
  "mediaMeta": { ... },
  "createdAt": "2026-05-28T16:08:00+08:00"
}
```

`receivers` 由业务侧 `conversation_participants` 计算：
```sql
SELECT user_id FROM conversation_participants
WHERE conversation_id=? AND quit=0 AND user_id<>?
```
长连接服务据 `receivers` 与在线表查找连接并扇出，不在线者由业务侧另行通知（无需重复，因消息已存为 `status=0`）。

### 3.3 `business.user.online`（长连接 ➜ 业务）

```json
{ "userId": 1001 }
```

业务侧动作：
1. 查询该用户所有 `quit=0` 的 `participants`；
2. 对每个会话查询 `message_id > last_read_message_id` 的消息（限 200 条/会话）；
3. 分批发布 `chat.msg.replay`：

```json
{
  "targetUserId": 1001,
  "conversationId": 9001,
  "messages": [ /* 与 chat.msg.send 同结构的数组 */ ]
}
```

### 3.4 `business.user.offline`

```json
{ "userId": 1001 }
```

业务侧用于统计/日志，不强制处理。

### 3.5 `business.msg.ack`

```json
{ "messageId": 5568, "receiverId": 1002, "ackAt": "..." }
```

业务侧可：
- 单聊场景：`UPDATE message SET status=2 WHERE message_id=? AND status<2`；
- 群聊：忽略，统一通过 `PUT /api/conversations/{id}/read` 更新 participants 指针。

### 3.6 `chat.msg.recall`

```json
{ "messageId": 5568, "conversationId": 9001, "operatorId": 1001 }
```

长连接侧据此向接收者推送撤回事件，客户端从本地移除。

### 3.7 `chat.friend.request` / `chat.friend.accept`

```json
{
  "requestId": 77,
  "fromUserId": 1001,
  "toUserId": 1002,
  "remark": "我是Alice"
}
```

### 3.8 `chat.conversation.created`

```json
{
  "conversationId": 9001,
  "type": 2,
  "name": "Holiday",
  "memberIds": [1001, 1002, 1003]
}
```

### 3.9 `chat.conversation.member_changed`

```json
{
  "conversationId": 9001,
  "addedIds": [1004],
  "removedIds": [1003],
  "roleChanges": [ { "userId": 1002, "role": 2 } ]
}
```

---

## 4. Spring AMQP 声明示例（业务侧）

```java
@Configuration
public class RabbitMqConfig {

    public static final String CHAT_EXCHANGE     = "chat.exchange";
    public static final String BUSINESS_EXCHANGE = "business.exchange";

    public static final String BUSINESS_DLX = "business.dlx";
    public static final String BUSINESS_DLQ = "business.dlq";

    public static final String Q_PERSIST       = "business.persist.queue";
    public static final String Q_USER_EVENT    = "business.user.event.queue";
    public static final String Q_MSG_ACK       = "business.msg.ack.queue";

    @Bean TopicExchange chatExchange()     { return ExchangeBuilder.topicExchange(CHAT_EXCHANGE).durable(true).build(); }
    @Bean TopicExchange businessExchange() { return ExchangeBuilder.topicExchange(BUSINESS_EXCHANGE).durable(true).build(); }
    @Bean FanoutExchange businessDlx()     { return ExchangeBuilder.fanoutExchange(BUSINESS_DLX).durable(true).build(); }

    @Bean Queue businessDlq() { return QueueBuilder.durable(BUSINESS_DLQ).build(); }

    @Bean Queue qPersist() {
        return QueueBuilder.durable(Q_PERSIST)
                .deadLetterExchange(BUSINESS_DLX)
                .ttl(86_400_000)
                .build();
    }
    @Bean Queue qUserEvent() {
        return QueueBuilder.durable(Q_USER_EVENT)
                .deadLetterExchange(BUSINESS_DLX).ttl(86_400_000).build();
    }
    @Bean Queue qMsgAck() {
        return QueueBuilder.durable(Q_MSG_ACK)
                .deadLetterExchange(BUSINESS_DLX).ttl(86_400_000).build();
    }

    @Bean Binding bPersist()   { return BindingBuilder.bind(qPersist()).to(businessExchange()).with("business.msg.persist"); }
    @Bean Binding bUserOn()    { return BindingBuilder.bind(qUserEvent()).to(businessExchange()).with("business.user.online"); }
    @Bean Binding bUserOff()   { return BindingBuilder.bind(qUserEvent()).to(businessExchange()).with("business.user.offline"); }
    @Bean Binding bAck()       { return BindingBuilder.bind(qMsgAck()).to(businessExchange()).with("business.msg.ack"); }
    @Bean Binding bDlq()       { return BindingBuilder.bind(businessDlq()).to(businessDlx()); }
}
```

---

## 5. 消费端伪代码

```java
@RabbitListener(queues = RabbitMqConfig.Q_PERSIST, ackMode = "MANUAL")
public void onPersist(Message amqp, Channel channel) throws IOException {
    Envelope env = JSON.parseObject(amqp.getBody(), Envelope.class);
    try {
        if (!idempotency.tryMark(env.getMsgId())) {              // Redis 幂等
            channel.basicAck(amqp.getMessageProperties().getDeliveryTag(), false);
            return;
        }
        messageService.persistFromMq(env.getPayload(PersistDto.class));
        channel.basicAck(amqp.getMessageProperties().getDeliveryTag(), false);
    } catch (BizException be) {
        // 业务错误：不重试，直接死信
        channel.basicNack(amqp.getMessageProperties().getDeliveryTag(), false, false);
    } catch (Exception ex) {
        // 临时错误：重试，超过次数走 DLX
        channel.basicNack(amqp.getMessageProperties().getDeliveryTag(), false, true);
    }
}
```

---

## 6. 版本与演进

- 当前契约版本：`version = 1`；
- 字段只增不减，删除字段需提升 `version` 且双方协商；
- 任意 break-change 必须通过新增 `*.v2` 路由键 + 双发兼容期完成迁移。
