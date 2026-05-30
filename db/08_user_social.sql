-- =====================================================================
-- 用户社交动态(类似朋友圈)
-- 文本 + 可选图片(图片单独存在 user_social_image)
-- visibility 控制可见范围
-- =====================================================================
CREATE TABLE IF NOT EXISTS `user_social` (
    `social_id`     BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '动态主键',
    `user_id`       BIGINT UNSIGNED NOT NULL                COMMENT '发布者 user_id',
    `content`       VARCHAR(2000)   DEFAULT NULL            COMMENT '正文文本',
    `visibility`    TINYINT         NOT NULL DEFAULT 0
        COMMENT '0=公开 1=仅好友可见 2=仅自己可见',
    `like_count`    INT UNSIGNED    NOT NULL DEFAULT 0      COMMENT '点赞冗余计数',
    `image_count`   TINYINT UNSIGNED NOT NULL DEFAULT 0     COMMENT '图片数量冗余',
    `deleted`       TINYINT         NOT NULL DEFAULT 0      COMMENT '0=正常 1=逻辑删除',
    `created_at`    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at`    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                             ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`social_id`),
    KEY `idx_user_time`        (`user_id`, `created_at`),
    KEY `idx_visibility_time`  (`visibility`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户社交动态';
