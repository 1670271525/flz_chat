# flz_chat_business 对 chat 长连接服务 MQ 接口文档

> 适用对象：`chat` 长连接服务  
> 协议：RabbitMQ + Topic Exchange  
> 数据格式：`application/json`  
> 时区：`Asia/Shanghai`（时间字段使用 ISO 8601）

---

## 1. 总览

### 1.1 交换机

- `chat.exchange`：业务服务(`flz_chat_business`) -> 长连接服务(`chat`)
- `business.exchange`：长连接服务(`chat`) -> 业务服务(`flz_chat_business`)

### 1.2 长连接服务建议绑定

- 队列：`chat.delivery.queue`
- 绑定：
  - `chat.exchange` + `chat.msg.send`
  - `chat.exchange` + `chat.msg.recall`
  - `chat.exchange` + `chat.msg.replay`
  - `chat.exchange` + `chat.friend.request`
  - `chat.exchange` + `chat.friend.accept`

### 1.3 业务服务已绑定（供 chat 发布）

- `business.persist.queue` <- `business.msg.persist`
- `business.user.event.queue` <- `business.user.online` / `business.user.offline`
- `business.msg.ack.queue` <- `business.msg.ack`

---

## 2. 通用消息信封（Envelope）

所有消息统一外层结构：

```json
{
  "msgId": "uuid-v4",
  "version": 1,
  "occurredAt": "2026-05-28T17:30:00+08:00",
  "source": "business",
  "type": "chat.msg.send",
  "payload": {}
}
```

字段说明：

- `msgId`：MQ 消息唯一 ID（用于消费幂等）
- `version`：契约版本，当前固定 `1`
- `occurredAt`：消息产生时间
- `source`：消息来源（`business` 或 `chat`）
- `type`：消息类型，通常与 routingKey 一致
- `payload`：业务消息体

建议：

- chat 侧消费幂等键：`mq:processed:{msgId}`（TTL >= 24h）
- 业务错误直接丢弃或入死信，瞬时错误可重试

---

## 3. business -> chat（长连接服务消费）

## 3.1 `chat.msg.send` 新消息下发

payload：

```json
{
  "messageId": 5568,
  "conversationId": 9001,
  "senderId": 1001,
  "receivers": [1002, 1003],
  "type": 2,
  "content": "chat/1001/2026/05/28/uuid.jpg",
  "downloadUrl": "https://minio/presigned...",
  "downloadUrlExpireAt": "2026-08-26T17:30:00+08:00",
  "mediaMeta": "{\"size\":12345}",
  "createdAt": "2026-05-28T17:30:00+08:00"
}
```

说明：

- `type`：1文本、2图片、3语音、4视频、5文件
- 媒体类消息可使用 `downloadUrl` 直接下载
- chat 侧根据 `receivers` 在线路由推送

## 3.2 `chat.msg.recall` 消息撤回

payload：

```json
{
  "messageId": 5568,
  "conversationId": 9001,
  "operatorId": 1001
}
```

说明：

- chat 侧向会话在线成员广播撤回事件

## 3.3 `chat.msg.replay` 未读回放

payload：

```json
{
  "targetUserId": 1001,
  "conversationId": 9001,
  "messages": [
    {
      "message_id": 5569,
      "conversation_id": 9001,
      "sender_id": 1002,
      "content": "你好",
      "type": 1
    }
  ]
}
```

说明：

- 用于用户上线后离线消息补投递
- chat 侧只向 `targetUserId` 推送

## 3.4 `chat.friend.request` 好友申请通知

payload：

```json
{
  "requestId": 77,
  "fromUserId": 1001,
  "toUserId": 1002,
  "remark": "我是Alice"
}
```

## 3.5 `chat.friend.accept` 好友申请通过通知

payload：

```json
{
  "requestId": 77,
  "fromUserId": 1001,
  "toUserId": 1002,
  "conversationId": 9001
}
```

---

## 4. chat -> business（长连接服务发布）

## 4.1 `business.msg.persist` 消息持久化请求

用途：

- chat 收到客户端**纯文本消息**后，调用该事件请求业务服务入库

payload：

```json
{
  "clientMsgId": "5f3c-...",
  "conversationId": 9001,
  "senderId": 1001,
  "type": 1,
  "content": "你好",
  "mediaMeta": "{\"size\":123}",
  "sentAt": "2026-05-28T17:30:00+08:00"
}
```

关键约束：

- `clientMsgId` 必填，且应全局唯一（用于幂等）
- `type` 必须为 `1`（纯文本）
- business 侧会执行文本校验与过滤（内容为空、超长、敏感词等）
- 持久化成功后 business 会下发 `chat.msg.send`

> 文件类型消息（2/3/4/5）不通过该 MQ 入口，改为调用 business HTTP 文件上传接口后，再发送文件消息。

## 4.2 `business.user.online` 用户上线通知

payload：

```json
{
  "userId": 1001
}
```

说明：

- business 收到后会触发未读消息回放（`chat.msg.replay`）

## 4.3 `business.user.offline` 用户下线通知

payload：

```json
{
  "userId": 1001
}
```

说明：

- 当前主要用于统计/日志，非关键流程

## 4.4 `business.msg.ack` 客户端投递确认

payload：

```json
{
  "messageId": 5568,
  "receiverId": 1002,
  "ackAt": "2026-05-28T17:31:00+08:00"
}
```

说明：

- 业务侧会推进单聊消息状态

---

## 5. 失败处理与重试建议

- 业务服务消费者使用手动 ack：
  - 处理成功：`ack`
  - 业务不可恢复错误：`nack(requeue=false)` 进入死信
  - 短暂错误：`nack(requeue=true)` 重试
- chat 侧建议同样采用幂等 + 重试上限
- 推荐监控：
  - 队列堆积长度
  - 死信队列增量
  - 消费失败率

---

## 6. 版本策略

- 当前版本：`version=1`
- 新增字段：向后兼容（可忽略未知字段）
- 破坏性变更：新增 `*.v2` routingKey，双发过渡
