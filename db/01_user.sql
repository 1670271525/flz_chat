-- =====================================================================
-- 用户基础表
-- 仅保存账号体系字段；个人主页展示信息见 user_information
-- =====================================================================
CREATE TABLE IF NOT EXISTS `user` (
    `user_id`         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '用户主键',
    `user_name`       VARCHAR(64)     NOT NULL                COMMENT '登录用户名(唯一)',
    `user_email`      VARCHAR(128)    DEFAULT NULL            COMMENT '邮箱(唯一,用于找回密码)',
    `user_phone`      VARCHAR(32)     DEFAULT NULL            COMMENT '手机号(可选,唯一)',
    `user_password`   VARCHAR(255)    NOT NULL                COMMENT '密码: BCrypt 加盐哈希',
    `user_status`     TINYINT         NOT NULL DEFAULT 1      COMMENT '账号状态 0=禁用 1=正常 2=注销',
    `user_deleted`    TINYINT         NOT NULL DEFAULT 0      COMMENT '逻辑删除 0=正常 1=已删除',
    `user_created_at` DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `user_updated_at` DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                                ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`user_id`),
    UNIQUE KEY `uniq_user_name`  (`user_name`),
    UNIQUE KEY `uniq_user_email` (`user_email`),
    UNIQUE KEY `uniq_user_phone` (`user_phone`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户账号表';
