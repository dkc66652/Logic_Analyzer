/**
 * @file layer0_right_pages.h
 * @brief 右侧功能页的 LVGL 对象创建接口。
 * @details 仅负责在给定页面容器内创建控件；导航、页面生命周期和采集状态
 *          由 layer0_right_panel 管理。所有接口只能在 GUI 任务上下文调用。
 */
#ifndef LAYER0_RIGHT_PAGES_H
#define LAYER0_RIGHT_PAGES_H

#include "lvgl.h"

/** @brief 创建打开文件页的背景容器。
 *  @param page_parent 页面内容区父对象；创建后由父对象管理子控件生命周期。 */
void gui_page_open_file_create(lv_obj_t *page_parent);

/** @brief 创建通道选择、采样频率和采集时间控件。
 *  @param page_parent 页面内容区父对象；页面通过 layer1_wave 公共接口读写配置。 */
void gui_page_device_config_create(lv_obj_t *page_parent);

/** @brief 创建协议解码页的当前占位内容。
 *  @param page_parent 页面内容区父对象；当前不启动解析任务。 */
void gui_page_protocol_decode_create(lv_obj_t *page_parent);

/** @brief 创建版本升级页的当前占位内容。
 *  @param page_parent 页面内容区父对象；当前不执行固件升级。 */
void gui_page_version_update_create(lv_obj_t *page_parent);

#endif /* LAYER0_RIGHT_PAGES_H */
