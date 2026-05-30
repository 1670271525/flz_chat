-- =====================================================================
-- 会话表(聊天窗口)
-- 单聊 / 群聊统一抽象；last_message_id 用于快速排序与未读计算
-- =====================================================================
CREATE TABLE IF NOT EXISTS `conversations` (
    `conversation_id`  BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '会话主键',
    `type`             TINYINT         NOT NULL                COMMENT '1=单聊 2=群聊',
    `name`             VARCHAR(128)    DEFAULT NULL            COMMENT '群聊名称(单聊为空)',
    `avatar_url`       VARCHAR(512)    DEFAULT NULL            COMMENT '群头像 MinIO URI(单聊为空)',
    `owner_id`         BIGINT UNSIGNED DEFAULT NULL            COMMENT '群主 user_id(单聊为空)',
    `max_members`      INT UNSIGNED    NOT NULL DEFAULT 500    COMMENT '群成员上限',
    `last_message_id`  BIGINT UNSIGNED DEFAULT NULL            COMMENT '最后一条消息 ID(用于排序与未读计算)',
    `last_message_at`  DATETIME        DEFAULT NULL            COMMENT '最后一条消息时间(冗余,便于排序)',
    `dissolved`        TINYINT         NOT NULL DEFAULT 0      COMMENT '0=正常 1=已解散/已删除',
    `created_at`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                                ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`conversation_id`),
    KEY `idx_type_updated` (`type`, `last_message_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='会话(单聊/群聊)';
