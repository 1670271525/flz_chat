-- =====================================================================
-- 用户私人信息表
-- 与 user 表 1:1 关系；user_id 同时是主键也是外键
-- 个人主页和聊天室头像、对外心情状态、个性签名
-- =====================================================================
CREATE TABLE IF NOT EXISTS `user_information` (
    `user_id`        BIGINT UNSIGNED NOT NULL                COMMENT '用户主键(与 user.user_id 绑定)',
    `avatar_url`     VARCHAR(512)    DEFAULT NULL            COMMENT '头像 MinIO URI(相对路径)',
    `nickname`       VARCHAR(64)     DEFAULT NULL            COMMENT '昵称(展示用)',
    `mood`           VARCHAR(16)     NOT NULL DEFAULT 'NORMAL'
        COMMENT '对外状态枚举: HAPPY/SAD/HURT/ANGRY/IRRITATED/NORMAL ...',
    `signature`      VARCHAR(255)    DEFAULT NULL            COMMENT '个性留言',
    `gender`         TINYINT         NOT NULL DEFAULT 0      COMMENT '性别 0=未知 1=男 2=女',
    `birthday`       DATE            DEFAULT NULL            COMMENT '生日',
    `region`         VARCHAR(64)     DEFAULT NULL            COMMENT '地区',
    `created_at`     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at`     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                                              ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`user_id`),
    CONSTRAINT `fk_user_info_user`
        FOREIGN KEY (`user_id`) REFERENCES `user` (`user_id`)
        ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='用户个人主页信息';
