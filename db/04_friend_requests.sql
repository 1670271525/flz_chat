-- =====================================================================
-- 好友申请记录表
-- 一次"加好友"动作产生一条记录；通过/拒绝后状态变更
-- 业务层：同意时插入 / 更新 friendships 双方两行 status=1
-- =====================================================================
CREATE TABLE IF NOT EXISTS `friend_requests` (
    `id`             BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `from_user_id`   BIGINT UNSIGNED NOT NULL                COMMENT '申请人 user_id',
    `to_user_id`     BIGINT UNSIGNED NOT NULL                COMMENT '接收人 user_id',
    `remark`         VARCHAR(255)    DEFAULT NULL            COMMENT '打招呼语',
    `status`         TINYINT         NOT NULL DEFAULT 0
        COMMENT '0=待处理 1=已同意 2=已拒绝 3=已过期',
    `expire_at`      DATETIME        DEFAULT NULL            COMMENT '过期时间(默认 7 天,由业务层写入)',
    `handled_at`     DATETIME        DEFAULT NULL            COMMENT '处理时间',
    `created_at`     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at`     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                              ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    KEY `idx_to_status_time` (`to_user_id`, `status`, `created_at`),
    KEY `idx_from_status_time` (`from_user_id`, `status`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='好友申请流水';
