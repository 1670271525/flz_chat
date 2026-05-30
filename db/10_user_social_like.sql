-- =====================================================================
-- 用户社交动态点赞表
-- 同一用户对同一动态最多点赞一次(唯一约束)
-- 取消点赞使用物理删除或软删除均可,这里使用物理删除
-- =====================================================================
CREATE TABLE IF NOT EXISTS `user_social_like` (
    `id`         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `social_id`  BIGINT UNSIGNED NOT NULL                COMMENT '被点赞的动态 id',
    `user_id`    BIGINT UNSIGNED NOT NULL                COMMENT '点赞用户 id',
    `created_at` DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                          ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uniq_social_user` (`social_id`, `user_id`),
    KEY `idx_user_time` (`user_id`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='动态点赞';
