-- =====================================================================
-- 消息表
-- 消息只绑定到会话(conversation_id)，不绑定到接收人；
-- 未读 = conversations.last_message_id - participants.last_read_message_id
-- 通过 MQ 持久化，使用 client_msg_id 保证消费幂等
-- =====================================================================
CREATE TABLE IF NOT EXISTS `message` (
    `message_id`       BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '消息主键(全局严格递增)',
    `conversation_id`  BIGINT UNSIGNED NOT NULL                COMMENT '所属会话 id',
    `sender_id`        BIGINT UNSIGNED NOT NULL                COMMENT '发送方 user_id',
    `content`          TEXT            DEFAULT NULL            COMMENT '消息主体内容(文本或资源 URI)',
    `type`             TINYINT         NOT NULL DEFAULT 1
        COMMENT '消息类型: 1=文本 2=图片 3=语音 4=视频 5=文件 6=系统',
    `is_agent`         TINYINT         NOT NULL DEFAULT 0
        COMMENT '是否智能代答/智能体代发: 0=否 1=是(如 emotion_reply)',
    `media_meta`       VARCHAR(512)    DEFAULT NULL            COMMENT '媒体元信息(大小/时长/缩略图等 JSON)',
    `status`           TINYINT         NOT NULL DEFAULT 0
        COMMENT '0=未读(离线) 1=已投递 2=已读(单聊侧使用,群聊参考 participants.last_read_message_id)',
    `client_msg_id`    VARCHAR(64)     NOT NULL                COMMENT '客户端/长连接服务生成的 UUID,用于幂等',
    `deleted`          TINYINT         NOT NULL DEFAULT 0      COMMENT '0=正常 1=逻辑删除',
    `created_at`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                                ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`message_id`),
    UNIQUE KEY `uniq_client_msg_id` (`client_msg_id`),
    KEY `idx_conv_msg`     (`conversation_id`, `message_id`),
    KEY `idx_conv_agent`   (`conversation_id`, `is_agent`, `message_id`),
    KEY `idx_conv_status`  (`conversation_id`, `status`),
    KEY `idx_sender_time`  (`sender_id`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='聊天消息';
