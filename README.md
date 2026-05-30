# flz_chat

`flz_chat` 是基于 `flz_server` 的社交 IM 长连接服务，负责：

- WebSocket 长连接接入（`/flz/chat`）
- JWT 鉴权（HS256）
- 在线会话表（`userId -> deviceId -> session`）
- 心跳探活与断线下线
- 客户端消息上行 -> MQ -> 业务服务
- 业务服务事件下行 -> MQ -> 客户端推送

## 依赖安装

```bash
sudo apt-get install -y librabbitmq-dev
# 联调脚本可选
pip install pika
```

## 配置文件

放在 `bin/conf/`：

- `server.yml`：HTTP/WS 服务监听
- `worker.yml`：线程池
- `log.yml`：日志
- `rabbitmq.yml`：MQ 地址与消费者配置
- `jwt.yml`：JWT 校验参数（支持 `${ENV:JWT_SECRET}`）
- `chat.yml`：心跳、限流、路由参数
- `redis.yml`：幂等外部存储（本期默认关闭，走本地 LRU）

## 启动顺序（联调）

1. 启动 RabbitMQ
2. 启动 mock 业务侧（或真实 `flz_chat_business`）  
   `python3 scripts/run_mock_business.py`
3. 启动 `flz_chat`
4. 运行 WS 集成测试  
   `python3 scripts/ws_chat_test.py --jwt-secret "$JWT_SECRET"`

## 端口与接口

- HTTP：`8090` / `8091`
- WebSocket：`8072` / `8071`
- WS Path：`/flz/chat`
- Metrics：`/metrics`

## 构建

```bash
mkdir -p build
cd build
cmake ..
make -j
```
