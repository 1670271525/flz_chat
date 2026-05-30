-- =====================================================================
-- 会话参与者表
-- 关联 conversations 与 user；记录角色、未读指针、群昵称、免打扰等
-- 单聊场景同样会有 2 行(双方各 1 行)，role 可统一记为 3(普通成员)
-- =====================================================================
CREATE TABLE IF NOT EXISTS `conversation_participants` (
    `id`                    BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `conversation_id`       BIGINT UNSIGNED NOT NULL                COMMENT '会话 id',
    `user_id`               BIGINT UNSIGNED NOT NULL                COMMENT '参与者 user_id',
    `role`                  TINYINT         NOT NULL DEFAULT 3
        COMMENT '1=群主 2=管理员 3=普通成员',
    `display_name`          VARCHAR(64)     DEFAULT NULL            COMMENT '群昵称(可空)',
    `last_read_message_id`  BIGINT UNSIGNED NOT NULL DEFAULT 0      COMMENT '该用户在本会话最后已读消息 id',
    `mute`                  TINYINT         NOT NULL DEFAULT 0      COMMENT '0=正常 1=免打扰',
    `pinned`                TINYINT         NOT NULL DEFAULT 0      COMMENT '0=正常 1=置顶',
    `joined_at`             DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '加入时间',
    `quit`                  TINYINT         NOT NULL DEFAULT 0      COMMENT '0=在群 1=已退出/被踢',
    `created_at`            DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at`            DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                                     ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uniq_conv_user` (`conversation_id`, `user_id`),
    KEY `idx_user_quit_updated` (`user_id`, `quit`, `updated_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='会话参与者';
