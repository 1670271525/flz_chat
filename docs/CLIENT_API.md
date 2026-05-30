# flz_chat 客户端 / 测试端接入手册

> 适用对象：移动端、Web 端、自动化测试脚本，以及任何需要通过 WebSocket 接入 `flz_chat` 长连接服务的调用方。
>
> 关联文档：
> - `docs/AGENT_DEV_SPEC.md` — 服务端开发规格（不需阅读即可对接）
> - `docs/MQ.md` / `docs/MQ_CHAT_SERVICE_API.md` — 后端 MQ 契约（仅服务间使用）
>
> 文档版本：`v1`，与服务端 `version=1` 信封一致。所有时间字段使用 `Asia/Shanghai` 的 ISO 8601（如 `2026-05-28T17:30:00+08:00`）。

---

## 1. 服务概览

| 项 | 值 |
|---|---|
| 协议 | WebSocket（RFC 6455），文本帧（opcode = 0x1） |
| Payload 格式 | UTF-8 编码的 JSON |
| WS 监听 | `0.0.0.0:8072` / `0.0.0.0:8071`（见 `bin/conf/server.yml`） |
| WS Path | `/flz/chat`（见 `bin/conf/chat.yml`） |
| HTTP 监听（健康/指标） | `0.0.0.0:8090` / `127.0.0.1:8091` |
| `/metrics` | Prometheus 文本格式，挂在 HTTP 端口 |
| 鉴权 | JWT（HS256），由业务侧 `flz_chat_business` 签发，本服务校验 |
| 心跳 | 客户端每 25s 主动 `ping`；服务端 75s 无活动自动断连 |
| 限流 | 单连接每秒 ≤ 20 帧，超出立即关闭并返回 `429` |
| 文本消息大小 | `content` ≤ 8192 字节（UTF-8 原始字节计算） |
| 多端 | 同一 `userId` 默认最多 5 端；同 `deviceId` 重复登录会"挤掉"旧连接 |

部署时的实际端口以运维下发为准；测试端默认连接 `ws://127.0.0.1:8071/flz/chat`。

---

## 2. 鉴权

### 2.1 Token 来自哪里？

`flz_chat` 不签发 token，仅做校验。客户端必须先调用业务侧 `flz_chat_business` 的登录接口拿到 JWT，再用这个 JWT 接入本服务。

### 2.2 JWT 规范

- 算法：`HS256`
- 必填 claims：

| claim | 类型 | 含义 |
|---|---|---|
| `sub` | string（数字字符串） | `userId`，对应数据库 `user.user_id` |
| `did` | string | 设备唯一 ID（如 `ios-uuid` / `web-browser-fp`）；缺省时服务端会兜底分配 `device-{uid}-{seq}` |
| `iat` | int（秒） | 签发时间 |
| `exp` | int（秒） | 过期时间 |
| `iss` | string | 固定 `flz_chat_business` |

- 可选 claims：`platform`、`scope` 等，服务端透传不校验。
- 时钟漂移容忍：±60s（由 `jwt.clock_skew_seconds` 决定）。
- 服务端会拒绝 `alg=none`、签名错误、`iss` 不匹配、`exp` 失效。

### 2.3 Token 携带方式（优先级从高到低）

1. **URL Query**（推荐，浏览器最稳）：
   ```
   ws://host:8071/flz/chat?token=<JWT>
   ```
2. **HTTP Header**：
   ```
   Authorization: Bearer <JWT>
   ```
3. **WebSocket SubProtocol**（仅兼容）：
   ```
   Sec-WebSocket-Protocol: bearer.<JWT>
   ```

只要其中一种命中即可。

### 2.4 JWT 签发示例（Python，仅供测试端使用）

```python
import base64, hmac, hashlib, json, time

def b64url(b): return base64.urlsafe_b64encode(b).decode().rstrip("=")

def issue_jwt(secret, user_id, device_id, expire_seconds=600):
    header = {"alg": "HS256", "typ": "JWT"}
    payload = {
        "sub": str(user_id),
        "did": device_id,
        "iat": int(time.time()),
        "exp": int(time.time()) + expire_seconds,
        "iss": "flz_chat_business",
        "platform": "test",
    }
    h = b64url(json.dumps(header, separators=(",", ":")).encode())
    p = b64url(json.dumps(payload, separators=(",", ":")).encode())
    s = b64url(hmac.new(secret.encode(), f"{h}.{p}".encode(),
                        hashlib.sha256).digest())
    return f"{h}.{p}.{s}"
```

---

## 3. WebSocket 帧统一结构

所有数据帧使用文本帧（opcode = 0x1），二进制帧会被拒绝（返回 `error` code = 400）。

JSON 外层结构：

```json
{
  "type": "<event>",
  "seq": 123,
  "data": { ... }
}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `type` | 是 | 事件名（见 §4 / §5） |
| `seq` | 否 | 客户端单调递增 ID；服务端的同步响应帧（如 `pong`、`msg.send.resp`、`error`）会回带 |
| `data` | 否 | 事件载荷对象，未提供时按空对象处理 |

---

## 4. 上行帧（Client → Server）

### 4.1 `ping` — 心跳

```json
{ "type": "ping", "seq": 100, "data": { "ts": 1717050000000 } }
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `data.ts` | int64（ms，可选） | 客户端发送时戳，会在 `pong` 中回带 |

- 建议每 **25 秒** 发一次；
- 任意上行帧（包括 `msg.send` 等）都会刷新服务端的 `lastActiveTs`，所以"在频繁通讯期间"可以省心跳；
- 服务端 `pong` 立即回执；
- 超过 75 秒无活动 → 服务端关闭 TCP，客户端会收到 close 帧（无错误 payload）。

### 4.2 `msg.send` — 发送文本消息

```json
{
  "type": "msg.send",
  "seq": 101,
  "data": {
    "clientMsgId": "8c1a2f3d-...-uuid-v4",
    "conversationId": 9001,
    "content": "你好呀",
    "sentAt": "2026-05-28T17:30:00+08:00"
  }
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `clientMsgId` | string | 是 | 客户端生成的 UUID v4，**全局唯一**，业务侧据此幂等入库 |
| `conversationId` | int64 | 是 | 目标会话 ID（来自业务侧 API） |
| `content` | string | 是 | 文本内容，UTF-8，长度 ≤ 8192 字节 |
| `sentAt` | string | 否 | ISO8601，缺省取服务端当前时间 |

约束：
- **本接口仅支持文本消息（type = 1）**；
- 媒体类型消息（图片/语音/视频/文件）必须先调用业务侧 HTTP 接口上传，再调业务侧"发送消息"接口，**不走本 WS 通道**；
- 服务端将该消息送入 MQ → 业务侧入库 → 业务侧通过 MQ 把入库结果回投本服务 → 本服务向**发送者**回 `msg.send.resp`，并向其它端及接收者推送 `msg.new`。

### 4.3 `msg.ack` — 客户端投递确认

```json
{ "type": "msg.ack", "seq": 102, "data": { "messageId": 5568 } }
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `messageId` | int64 | 是 | 业务侧消息主键（来自下行的 `msg.new` / `msg.replay`） |

- 适用于单聊收到 `msg.new` 后回执；
- 服务端透传到业务侧 `business.msg.ack`，可能驱动 `message.status` 0→2；
- 群聊场景建议使用 `msg.read` 代替。

### 4.4 `msg.read` — 已读上报

```json
{
  "type": "msg.read",
  "seq": 103,
  "data": { "conversationId": 9001, "lastReadMessageId": 5570 }
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `conversationId` | int64 | 是 | 会话 ID |
| `lastReadMessageId` | int64 | 是 | 客户端已读的最大 `messageId` |

> 本期服务端只记录日志，不强制把这一帧透传到业务侧；客户端如需更新会话已读指针，仍需调业务侧的 `PUT /api/conversations/{id}/read` HTTP 接口。

### 4.5 `bye` — 主动登出

```json
{ "type": "bye", "data": {} }
```

- 服务端收到后**直接关闭连接**；
- `onClose` 会清理会话表；若是该用户最后一端，会向业务侧广播下线事件。

---

## 5. 下行帧（Server → Client）

### 5.1 `auth_ok` — 鉴权通过

握手成功后**首帧**：

```json
{
  "type": "auth_ok",
  "data": {
    "userId": 1001,
    "deviceId": "ios-a",
    "serverTime": "2026-05-28T17:30:00+08:00"
  }
}
```

收到这一帧才能开始发业务帧。

### 5.2 `auth_fail` — 鉴权失败

```json
{ "type": "auth_fail", "data": { "code": 401, "msg": "token expired" } }
```

服务端发完会立即 close。常见 `msg`：

| msg | 含义 |
|---|---|
| `missing token` | 三个携带位置都没有取到 |
| `token format invalid` | 不是 `xxx.yyy.zzz` 形式 |
| `token base64 decode failed` | header/payload base64url 解析失败 |
| `token header invalid` / `token payload invalid` | JSON 非法 |
| `token alg invalid` | 非 HS256 |
| `token signature invalid` | 签名校验失败（密钥不一致） |
| `token issuer mismatch` | `iss` 不是 `flz_chat_business` |
| `token expired` | 过期 |
| `token iat invalid` | iat 在未来太多 |
| `token sub invalid` | sub 缺失或非数字 |

### 5.3 `pong` — 心跳回执

```json
{ "type": "pong", "seq": 100, "data": { "ts": 1717050000000 } }
```

`seq` 与对应 `ping` 一致（若客户端给了 `seq`）。

### 5.4 `msg.send.resp` — 发送结果

```json
{
  "type": "msg.send.resp",
  "seq": 101,
  "data": { "clientMsgId": "...", "code": 200, "messageId": 5568 }
}
```

- 仅给**原发送端**（即提交 `msg.send` 的那条连接）；
- 通过 `clientMsgId` 与 `seq` 同时定位；
- `code = 200`：成功；非 200 表示业务侧拒绝（如内容过滤）。

### 5.5 `msg.new` — 收到新消息

```json
{
  "type": "msg.new",
  "data": {
    "messageId": 5568,
    "clientMsgId": "...",
    "conversationId": 9001,
    "senderId": 1001,
    "receivers": [1002, 1003],
    "type": 1,
    "content": "你好呀",
    "downloadUrl": "https://cdn/...",
    "downloadUrlExpireAt": "2026-08-26T17:30:00+08:00",
    "mediaMeta": "{\"size\":12345}",
    "createdAt": "2026-05-28T17:30:00+08:00"
  }
}
```

字段说明（来自业务侧 `chat.msg.send`，本服务**只做转发**）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `messageId` | int64 | 业务侧主键 |
| `clientMsgId` | string | 与原 `msg.send` 一致 |
| `senderId` | uint64 | 发送者 `userId` |
| `receivers` | array<uint64> | 业务侧根据 `conversation_participants` 计算的目标用户 |
| `type` | int | 1=文本 2=图片 3=语音 4=视频 5=文件 6=系统 |
| `content` | string | 文本 / 媒体 URI |
| `downloadUrl` | string（可选） | 媒体类型时，预签名直链 |
| `downloadUrlExpireAt` | string（可选） | URL 过期 |
| `mediaMeta` | string（JSON 串） | `size/duration/thumbnail` 等 |
| `createdAt` | string | 业务侧入库时间 |

接收路径：
- 接收者所有在线端均会收到；
- 发送者**自己的其他端**（多端同步）也会收到 `msg.new`，但发起端不会重复收到（发起端只收到 `msg.send.resp`）。

### 5.6 `msg.replay` — 上线离线回放

```json
{
  "type": "msg.replay",
  "data": {
    "targetUserId": 1001,
    "conversationId": 9001,
    "messages": [
      { "message_id": 7001, "conversation_id": 9001,
        "sender_id": 1002, "content": "offline replay", "type": 1 }
    ]
  }
}
```

触发条件：客户端**首端上线**（同一 `userId` 之前不在线），服务端向业务侧发 `business.user.online`，业务侧按会话分批回投 `chat.msg.replay`，本服务转发到目标用户。

客户端处理建议：把 `messages` 按 `message_id` 升序逐条与本地缓存合并，重复消息丢弃。

### 5.7 `msg.recall` — 消息撤回

```json
{
  "type": "msg.recall",
  "data": {
    "messageId": 5568,
    "conversationId": 9001,
    "operatorId": 1001,
    "receivers": [1001, 1002, 1003]
  }
}
```

收到后从本地 UI 移除/置灰对应消息。

### 5.8 `friend.request` / `friend.accept`

```json
{ "type": "friend.request",
  "data": { "requestId": 77, "fromUserId": 1001, "toUserId": 1002, "remark": "我是Alice" } }
```

```json
{ "type": "friend.accept",
  "data": { "requestId": 77, "fromUserId": 1001, "toUserId": 1002, "conversationId": 9001 } }
```

- `friend.request` 推送给 `toUserId`；
- `friend.accept` 推送给 `fromUserId` 与 `toUserId` 双方。

### 5.9 `conversation.created`

```json
{ "type": "conversation.created",
  "data": { "conversationId": 9001, "type": 2, "name": "Holiday", "memberIds": [1001,1002,1003] } }
```

推送给所有 `memberIds`。

### 5.10 `conversation.member_changed`

```json
{ "type": "conversation.member_changed",
  "data": {
    "conversationId": 9001,
    "addedIds": [1004],
    "removedIds": [1003],
    "roleChanges": [ { "userId": 1002, "role": 2 } ]
  } }
```

推送对象：`addedIds ∪ removedIds ∪ roleChanges[*].userId`。

### 5.11 `kicked` — 被踢下线

```json
{ "type": "kicked", "data": { "reason": "login_elsewhere" } }
```

`reason` 取值：

| reason | 含义 |
|---|---|
| `login_elsewhere` | 同 `userId` + 同 `deviceId` 又在别处登录 |
| `too_many_devices` | 超过 `max_devices_per_user`（默认 5），最旧连接被淘汰 |

服务端发完会立即 close。客户端遇到时应停止重连并提示用户。

### 5.12 `error` — 通用错误

```json
{ "type": "error", "seq": 101, "data": { "code": 400, "msg": "bad msg.send" } }
```

`seq` 在能识别到时回带；常见 `code` 见 §6。

---

## 6. 错误码

| code | 触发场景 | 是否会关闭连接 |
|---|---|---|
| 200 | 成功（出现在 `msg.send.resp.code`） | 否 |
| 400 | 报文非 JSON / 缺 `type` / `seq` 非整数 / 业务字段缺失 | 否 |
| 401 | JWT 校验失败 / 上下文缺失 | **是**（auth_fail 场景） |
| 404 | 未知 `type` | 否 |
| 413 | `content` 超长（> 8192 B） | 否 |
| 429 | 单连接帧速率超限（>20 帧/秒） | **是** |
| 500 | 服务端内部异常 | 否 |
| 503 | MQ 不可用，`msg.send` / `msg.ack` 暂时无法投递 | 否 |

---

## 7. 多端登录策略

- 默认允许同一 `userId` 多端共存，区分键是 `deviceId`；
- 同 `userId` + 同 `deviceId` 再次登录：
  1. 服务端先向**旧连接**推送 `{"type":"kicked","data":{"reason":"login_elsewhere"}}`，立即 close；
  2. 新连接收到 `auth_ok`；
- 超过 `max_devices_per_user`（默认 5）：最早登录的端被踢，`reason = too_many_devices`；
- 用户首端上线 / 末端下线，服务端会自动告知业务侧（`business.user.online` / `business.user.offline`），客户端无需关心。

---

## 8. 典型交互时序

### 8.1 上线 → 收到回放 → 发消息 → 收到回执 → 收到他人消息

```
Client                 flz_chat                business
  │                       │                       │
  │── WS Upgrade(token) ─►│                       │
  │◄──── 101 Switching ───│                       │
  │◄──── auth_ok ─────────│                       │
  │                       │── business.user.online (MQ) ─►│
  │                       │◄────── chat.msg.replay  (MQ) ─│
  │◄──── msg.replay ──────│                       │
  │                       │                       │
  │── msg.send ──────────►│                       │
  │                       │── business.msg.persist(MQ)──►│
  │                       │◄────── chat.msg.send   (MQ) ─│
  │◄── msg.send.resp ─────│                       │
  │◄── msg.new(其他端) ───│                       │
  │                       │                       │
  │                       │◄────── chat.msg.send   (MQ) ─│  (他人发送的消息)
  │◄── msg.new ───────────│                       │
  │── msg.ack ───────────►│                       │
  │                       │── business.msg.ack (MQ) ───►│
```

### 8.2 心跳与超时

```
Client            flz_chat
  │── ping ───────►│
  │◄─── pong ──────│
  │   ...25s...    │
  │── ping ───────►│
  │◄─── pong ──────│
  │  (60s 无任何帧) │
  │  ...75s...    │
  │◄─── close ─────│       服务端主动关闭
```

### 8.3 同设备挤号

```
ClientA(uid=1, did=ios-x)      flz_chat       ClientB(uid=1, did=ios-x)
        │ ─── auth_ok ──────────│                          │
        │                       │◄── WS Upgrade ───────────│
        │◄── kicked(login_elsewhere) ──────────────────────│
        │◄── close ─────────────│                          │
        │                       │── auth_ok ──────────────►│
```

---

## 9. 客户端最小实现示例

### 9.1 Python（无第三方依赖）

> 完整可运行版本见 `scripts/ws_chat_test.py`。

```python
import json, time, uuid, socket, struct, os, base64

def b64url(b): return base64.urlsafe_b64encode(b).decode().rstrip("=")

# 1. 与业务服务交互拿到 JWT，这里直接复用 §2.4 的 issue_jwt

token = issue_jwt("YOUR_SECRET", user_id=1001, device_id="py-demo")

# 2. 握手
sock = socket.create_connection(("127.0.0.1", 8071))
key = base64.b64encode(os.urandom(16)).decode()
req = (
    f"GET /flz/chat?token={token} HTTP/1.1\r\n"
    f"Host: 127.0.0.1:8071\r\n"
    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
    f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
)
sock.sendall(req.encode())
buf = b""
while b"\r\n\r\n" not in buf:
    buf += sock.recv(4096)
assert b"101" in buf.split(b"\r\n", 1)[0]

# 3. 帧编解码（仅文本帧）
def send(obj):
    data = json.dumps(obj, separators=(",", ":")).encode()
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    n = len(data)
    if n < 126:        h = struct.pack("!BB",  0x81, 0x80 | n)
    elif n < 65536:    h = struct.pack("!BBH", 0x81, 0x80 | 126, n)
    else:              h = struct.pack("!BBQ", 0x81, 0x80 | 127, n)
    sock.sendall(h + mask + masked)

def recv():
    h = sock.recv(2)
    n = h[1] & 0x7F
    if n == 126: n = struct.unpack("!H", sock.recv(2))[0]
    elif n == 127: n = struct.unpack("!Q", sock.recv(8))[0]
    body = b""
    while len(body) < n: body += sock.recv(n - len(body))
    return json.loads(body)

print(recv())          # auth_ok

# 4. 发送一条文本消息
send({
    "type": "msg.send",
    "seq": 1,
    "data": {
        "clientMsgId": str(uuid.uuid4()),
        "conversationId": 9001,
        "content": "hi from python",
    },
})

# 5. 心跳
import threading
def hb():
    while True:
        time.sleep(25)
        send({"type": "ping", "data": {"ts": int(time.time()*1000)}})
threading.Thread(target=hb, daemon=True).start()

while True:
    print(recv())
```

### 9.2 浏览器 / JS

```js
const token = "<由业务侧登录拿到的 JWT>";
const ws = new WebSocket(`ws://127.0.0.1:8071/flz/chat?token=${token}`);

let seq = 0;

ws.onopen = () => console.log("connected");
ws.onmessage = (e) => {
  const f = JSON.parse(e.data);
  switch (f.type) {
    case "auth_ok":      onAuthOk(f.data); break;
    case "auth_fail":    onAuthFail(f.data); ws.close(); break;
    case "pong":         /* noop */ break;
    case "msg.send.resp": onSendResp(f.seq, f.data); break;
    case "msg.new":      onMsgNew(f.data); break;
    case "msg.replay":   onReplay(f.data); break;
    case "msg.recall":   onRecall(f.data); break;
    case "kicked":       onKicked(f.data); break;
    case "error":        console.warn("err", f.data); break;
    default:             console.log("unknown", f);
  }
};

setInterval(() => {
  if (ws.readyState === 1) {
    ws.send(JSON.stringify({ type: "ping", data: { ts: Date.now() } }));
  }
}, 25000);

function sendText(conversationId, content) {
  ws.send(JSON.stringify({
    type: "msg.send",
    seq: ++seq,
    data: {
      clientMsgId: crypto.randomUUID(),
      conversationId,
      content,
      sentAt: new Date().toISOString(),
    },
  }));
}
```

### 9.3 客户端实现建议

1. **断线重连**：指数退避（1s → 2s → 4s → ... ≤ 30s）+ 抖动；重连后**先**等 `auth_ok`，再 flush 离线期间用户输入的消息。
2. **token 续签**：监控 `exp - now < 5min` 主动调业务侧拿新 token，然后重连（旧连接会被挤掉）。
3. **clientMsgId 持久化**：在本地输入框生成 UUID 时即落地草稿表，发送成功（收到 `msg.send.resp` code = 200）后删除草稿；30s 仍未收到回执 → 客户端可重试同一 `clientMsgId`（业务侧据此幂等）。
4. **多端去重**：以 `messageId` 为准；`msg.new` 与 `msg.replay` 中的消息可能交叉，按 `(conversationId, messageId)` 去重并按 `messageId` 升序入会话。
5. **消息时序**：`messageId` 是业务侧单调递增主键，可以直接作为会话内排序键；不要依赖到达顺序。
6. **不要把鉴权 token 直接打日志**。

---

## 10. 联调环境与测试脚本

### 10.1 启动顺序

```bash
# 1) RabbitMQ
docker run -d --rm -p 5672:5672 -p 15672:15672 \
  -e RABBITMQ_DEFAULT_USER=root -e RABBITMQ_DEFAULT_PASS=root rabbitmq:3-management

# 2) Mock 业务侧（无真实 flz_chat_business 时使用）
pip install pika
python3 scripts/run_mock_business.py \
  --host 127.0.0.1 --user root --password root

# 3) 启动 flz_chat
cd build && cmake .. && make -j
cd ../bin && ./flz -s   # 或按项目运行脚本
```

### 10.2 跑集成测试

```bash
export JWT_SECRET="flz200310240813200206103145"     # 与 bin/conf/jwt.yml 一致
python3 scripts/ws_chat_test.py --jwt-secret "$JWT_SECRET" \
        --host 127.0.0.1 --port 8071
```

脚本默认覆盖 5 类用例：

| 用例 | 验证点 |
|---|---|
| 多端登录与消息上行 | 同 `userId` 两端共存；`msg.send` 后回执 / 多端同步 |
| 接收方消息下发 | 异 `userId` 的 `msg.new` 推送 |
| 上线回放 | 首端上线收到 `msg.replay` |
| 过期 token 拦截 | `exp` 已过期 → `auth_fail` |
| 心跳超时断连 | 默认 80s 无活动连接被关闭（与服务端 75s 配合留余量） |

### 10.3 手工构造 MQ 帧验证下行链路

可在 RabbitMQ Management UI（`http://127.0.0.1:15672`，root/root）或 `rabbitmqadmin` 中向 `chat.exchange` 发布消息：

- Exchange：`chat.exchange`
- Routing key：`chat.msg.send` / `chat.msg.recall` / `chat.friend.request` / ...
- Headers：`content_type=application/json`, `delivery_mode=2`
- Payload（必须套统一信封）：

```json
{
  "msgId": "11111111-2222-3333-4444-555555555555",
  "version": 1,
  "occurredAt": "2026-05-30T15:00:00+08:00",
  "source": "business",
  "type": "chat.msg.send",
  "payload": {
    "messageId": 9999,
    "clientMsgId": "ad-hoc-test",
    "conversationId": 9001,
    "senderId": 1001,
    "receivers": [1002],
    "type": 1,
    "content": "manual test",
    "createdAt": "2026-05-30T15:00:00+08:00"
  }
}
```

---

## 11. `/metrics` 接口（运维 / 测试）

`GET http://127.0.0.1:8090/metrics`，Prometheus 文本格式，常见指标：

| 指标 | 含义 |
|---|---|
| `chat_online_users` | 当前在线用户数（去重） |
| `chat_online_connections` | 当前在线连接数 |
| `chat_ws_frames_total{direction,type}` | WS 帧累计计数 |
| `chat_mq_publish_total{routing_key,result}` | MQ 发布累计 |
| `chat_mq_consume_total{routing_key,result}` | MQ 消费累计 |
| `chat_mq_reconnect_total{role}` | MQ 重连次数（`publisher`/`consumer`） |
| `chat_msg_send_latency_ms_bucket{le}` 等 | `msg.send` 端到端延迟直方图 |
| `chat_jwt_verify_fail_total{reason}` | JWT 失败原因分布 |

---

## 12. 常见问题 FAQ

**Q1：握手返回 101 但马上收到 `auth_fail`？**  
A：token 不合法或 `iss/exp/sub` 不达标。比对：`iss` 是否 `flz_chat_business`、`sub` 是数字字符串、密钥与 `bin/conf/jwt.yml`/业务侧一致、本机时钟没飘太多（>60s）。

**Q2：发了 `msg.send`，但收不到 `msg.send.resp`？**  
A：常见原因：
1. RabbitMQ 没启动 / 凭据错 → 客户端会收到 `error code=503`；
2. 业务侧 `flz_chat_business` 没消费 `business.persist.queue`；
3. 业务侧入库后未发布 `chat.msg.send`；
4. 客户端 token 中的 `userId` 不在 `conversationId` 对应的 participants 内（业务侧会拒绝，可能直接死信）。

**Q3：另一端登录我自己就被踢了，怎么避免？**  
A：让两端使用**不同的 `did`**。同 `did` 视为同一设备的覆盖登录。

**Q4：客户端发了非 UTF-8 二进制帧会怎样？**  
A：服务端拒绝并回 `error code=400, msg=text frame required`，但不会关闭连接。

**Q5：能用 `wss://` 吗？**  
A：本服务本身不做 TLS 终止；生产环境建议在前置 Nginx / 负载均衡上挂证书，对客户端暴露 `wss://...:443/flz/chat`，回源到 `ws://...:8071/flz/chat`。

**Q6：客户端突然断了，重连后会丢消息吗？**  
A：不会。重连完成 `auth_ok`、首端再次上线后，业务侧会通过 `chat.msg.replay` 回放离线期间该用户在各会话中未读消息（200 条/会话 限制）。

**Q7：发同一个 `clientMsgId` 两次会被入两条吗？**  
A：不会。业务侧以 `client_msg_id` 做幂等键（`message.uniq_client_msg_id`）。建议客户端在网络抖动时安全重试同一个 `clientMsgId`。

**Q8：`msg.read` 不会真正推进会话已读？**  
A：当前是这样，已读指针请走业务侧 HTTP `PUT /api/conversations/{id}/read`；本帧仅用于服务端日志/未来扩展。

---

## 附录 A. 一览：所有事件类型

上行（客户端 → 服务端）：

| type | 必填字段 | 备注 |
|---|---|---|
| `ping` | — | 心跳 |
| `msg.send` | `clientMsgId,conversationId,content` | 仅文本 |
| `msg.ack` | `messageId` | 单聊建议使用 |
| `msg.read` | `conversationId,lastReadMessageId` | 群聊建议使用 |
| `bye` | — | 主动登出 |

下行（服务端 → 客户端）：

| type | 关键字段 | 触发 |
|---|---|---|
| `auth_ok` | `userId,deviceId,serverTime` | 握手通过 |
| `auth_fail` | `code,msg` | 鉴权失败，随后 close |
| `pong` | `ts` | 心跳回执 |
| `msg.send.resp` | `clientMsgId,code,messageId` | 发送结果 |
| `msg.new` | `messageId,senderId,conversationId,type,content,...` | 收到新消息（含多端同步） |
| `msg.replay` | `targetUserId,conversationId,messages[]` | 上线离线回放 |
| `msg.recall` | `messageId,conversationId,operatorId` | 撤回 |
| `friend.request` | `requestId,fromUserId,toUserId,remark` | 好友申请 |
| `friend.accept` | `requestId,fromUserId,toUserId,conversationId` | 申请通过 |
| `conversation.created` | `conversationId,type,name,memberIds` | 新会话 |
| `conversation.member_changed` | `conversationId,addedIds,removedIds,roleChanges` | 群成员变更 |
| `kicked` | `reason` | 被踢，随后 close |
| `error` | `code,msg` | 通用错误（不关闭连接） |

## 附录 B. 默认服务参数（可被 `bin/conf/chat.yml` 修改）

```
max_devices_per_user        = 5
heartbeat_interval_seconds  = 25     (客户端建议)
heartbeat_timeout_seconds   = 75     (服务端探活)
pending_send_ttl_seconds    = 30     (msg.send -> msg.send.resp 等待上限)
ws_path                     = /flz/chat
frame_rate_limit_per_second = 20
max_text_content_bytes      = 8192
```
