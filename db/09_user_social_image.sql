-- =====================================================================
-- 用户社交动态图片表
-- 一条动态可包含 0~N 张图片；按 sort_order 顺序展示
-- =====================================================================
CREATE TABLE IF NOT EXISTS `user_social_image` (
    `id`           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    `social_id`    BIGINT UNSIGNED NOT NULL                COMMENT '所属动态 social_id',
    `image_url`    VARCHAR(512)    NOT NULL                COMMENT 'MinIO 静态资源 URI',
    `sort_order`   TINYINT UNSIGNED NOT NULL DEFAULT 0     COMMENT '展示顺序(0..N)',
    `width`        INT UNSIGNED    DEFAULT NULL            COMMENT '宽度(px)',
    `height`       INT UNSIGNED    DEFAULT NULL            COMMENT '高度(px)',
    `created_at`   DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at`   DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                            ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    KEY `idx_social_sort` (`social_id`, `sort_order`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='动态图片(多图)';
