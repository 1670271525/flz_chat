-- =====================================================================
-- 好友关系表
-- 设计为"双向双行"：A->B 和 B->A 各一行，便于各自维护 alias / status
-- 若 status=3(拉黑) 仅影响发起方对被拉黑方的可见性
-- =====================================================================
CREATE TABLE IF NOT EXISTS `friendships` (
    `id`            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `user_id`       BIGINT UNSIGNED NOT NULL                COMMENT '关系归属用户',
    `friend_id`     BIGINT UNSIGNED NOT NULL                COMMENT '对方用户',
    `alias`         VARCHAR(64)     DEFAULT NULL            COMMENT '好友备注名',
    `status`        TINYINT         NOT NULL DEFAULT 0
        COMMENT '0=申请中 1=已成为好友 2=已拒绝 3=已拉黑',
    `created_at`    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at`    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                             ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uniq_user_friend` (`user_id`, `friend_id`),
    KEY `idx_friend_user` (`friend_id`, `user_id`),
    KEY `idx_user_status` (`user_id`, `status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='好友关系表(双向双行)';
