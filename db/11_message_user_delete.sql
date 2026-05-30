-- =====================================================================
-- 单边删除关联表
-- 用户删除消息时仅对该用户隐藏，不影响其他成员
-- =====================================================================
CREATE TABLE IF NOT EXISTS `message_user_delete` (
    `id`         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `message_id` BIGINT UNSIGNED NOT NULL                COMMENT '消息ID',
    `user_id`    BIGINT UNSIGNED NOT NULL                COMMENT '删除该消息的用户ID',
    `created_at` DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                          ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uniq_message_user` (`message_id`, `user_id`),
    KEY `idx_user_created` (`user_id`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='消息单边删除关联表';
