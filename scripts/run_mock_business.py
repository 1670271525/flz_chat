#!/usr/bin/env python3
"""Mock flz_chat_business for local MQ联调."""

import argparse
import json
import os
import threading
import time
import uuid
from datetime import datetime, timezone, timedelta

try:
    import pika
except ImportError as exc:
    raise SystemExit("需要先安装 pika: pip install pika") from exc


TZ = timezone(timedelta(hours=8))

# 与 docs/MQ.md / 业务侧 Spring 声明保持一致
BUSINESS_QUEUE_ARGS = {
    "x-dead-letter-exchange": "business.dlx",
    "x-message-ttl": 86400000,
}


def iso_now() -> str:
    return datetime.now(TZ).replace(microsecond=0).isoformat()


def make_envelope(source: str, route: str, payload: dict) -> dict:
    return {
        "msgId": str(uuid.uuid4()),
        "version": 1,
        "occurredAt": iso_now(),
        "source": source,
        "type": route,
        "payload": payload,
    }


class MockBusiness:
    def __init__(self, host: str, port: int, user: str, password: str, vhost: str):
        self.params = pika.ConnectionParameters(
            host=host,
            port=port,
            virtual_host=vhost,
            credentials=pika.PlainCredentials(user, password),
            heartbeat=30,
        )
        self.conn = pika.BlockingConnection(self.params)
        self.ch = self.conn.channel()
        self._message_id = 5000
        self._lock = threading.Lock()
        self.declare_topology()

    def declare_topology(self) -> None:
        self.ch.exchange_declare(exchange="chat.exchange", exchange_type="topic", durable=True)
        self.ch.exchange_declare(exchange="business.exchange", exchange_type="topic", durable=True)
        self.ch.exchange_declare(exchange="business.dlx", exchange_type="fanout", durable=True)

        self.ch.queue_declare(queue="business.dlq", durable=True)
        self.ch.queue_bind("business.dlq", "business.dlx", routing_key="")

        self.ch.queue_declare(queue="business.persist.queue", durable=True, arguments=BUSINESS_QUEUE_ARGS)
        self.ch.queue_bind("business.persist.queue", "business.exchange", "business.msg.persist")

        self.ch.queue_declare(queue="business.user.event.queue", durable=True, arguments=BUSINESS_QUEUE_ARGS)
        self.ch.queue_bind("business.user.event.queue", "business.exchange", "business.user.online")
        self.ch.queue_bind("business.user.event.queue", "business.exchange", "business.user.offline")

        self.ch.queue_declare(queue="business.msg.ack.queue", durable=True, arguments=BUSINESS_QUEUE_ARGS)
        self.ch.queue_bind("business.msg.ack.queue", "business.exchange", "business.msg.ack")

    def next_message_id(self) -> int:
        with self._lock:
            self._message_id += 1
            return self._message_id

    def publish_chat(self, route: str, payload: dict) -> None:
        body = json.dumps(make_envelope("business", route, payload), ensure_ascii=False)
        self.ch.basic_publish(
            exchange="chat.exchange",
            routing_key=route,
            body=body.encode("utf-8"),
            properties=pika.BasicProperties(content_type="application/json", delivery_mode=2, message_id=str(uuid.uuid4())),
        )
        print(f"[mock-business] publish {route}: {payload}")

    def on_persist(self, payload: dict) -> None:
        message_id = self.next_message_id()
        sender = int(payload.get("senderId", 0))
        conversation = int(payload.get("conversationId", 0))
        response = {
            "clientMsgId": payload.get("clientMsgId"),
            "messageId": message_id,
            "conversationId": conversation,
            "senderId": sender,
            "receivers": payload.get("receivers", []),
            "type": 1,
            "content": payload.get("content", ""),
            "mediaMeta": payload.get("mediaMeta"),
            "createdAt": iso_now(),
        }
        self.publish_chat("chat.msg.send", response)

    def on_user_online(self, payload: dict) -> None:
        uid = int(payload.get("userId", 0))
        replay = {
            "targetUserId": uid,
            "conversationId": 9001,
            "messages": [
                {
                    "message_id": 7001,
                    "conversation_id": 9001,
                    "sender_id": 1002,
                    "content": "offline replay",
                    "type": 1,
                }
            ],
        }
        self.publish_chat("chat.msg.replay", replay)

    def run(self) -> None:
        def callback(_ch, method, _props, body):
            try:
                env = json.loads(body.decode("utf-8"))
                route = env.get("type", method.routing_key)
                payload = env.get("payload", {})
                print(f"[mock-business] recv {route}: {payload}")
                if route == "business.msg.persist":
                    self.on_persist(payload)
                elif route == "business.user.online":
                    self.on_user_online(payload)
                elif route == "business.msg.ack":
                    pass
                _ch.basic_ack(method.delivery_tag)
            except Exception as exc:
                print(f"[mock-business] error: {exc}")
                _ch.basic_nack(method.delivery_tag, requeue=False)

        self.ch.basic_consume(queue="business.persist.queue", on_message_callback=callback, auto_ack=False)
        self.ch.basic_consume(queue="business.user.event.queue", on_message_callback=callback, auto_ack=False)
        self.ch.basic_consume(queue="business.msg.ack.queue", on_message_callback=callback, auto_ack=False)
        print("[mock-business] started, waiting messages ...")
        self.ch.start_consuming()


def main() -> None:
    parser = argparse.ArgumentParser(description="Run mock flz_chat_business MQ bridge")
    parser.add_argument("--host", default=os.getenv("RABBITMQ_HOST", "127.0.0.1"))
    parser.add_argument("--port", default=int(os.getenv("RABBITMQ_PORT", "5672")), type=int)
    parser.add_argument("--user", default=os.getenv("RABBITMQ_USER", "root"))
    parser.add_argument("--password", default=os.getenv("RABBITMQ_PASSWORD", "root"))
    parser.add_argument("--vhost", default=os.getenv("RABBITMQ_VHOST", "/"))
    args = parser.parse_args()

    srv = MockBusiness(args.host, args.port, args.user, args.password, args.vhost)
    try:
        srv.run()
    except KeyboardInterrupt:
        print("\n[mock-business] stopping")
        time.sleep(0.1)


if __name__ == "__main__":
    main()
