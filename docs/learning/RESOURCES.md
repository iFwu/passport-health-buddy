# 固件与记录资料

## 知识

- [仓库：用户手册](../plank-trainer.md)。本应用的按键、记录和错误语义；实际应用行为以代码与验证结果为准。
- [仓库：BLE Recovery 兼容合同](../development/engineering/ble-recovery-compatibility.zh_CN.md)。解释 UP 开机持续五秒入口、主应用大小和受保护区域。
- [Espressif：Application Startup Flow，ESP-IDF 5.5.3 / ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-guides/startup.html)。用于解释 ROM、第二阶段 bootloader、分区表与 `app_main()` 的启动链。
- [Espressif：Non-Volatile Storage Library，ESP-IDF 5.5.3 / ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/storage/nvs_flash.html)。用于解释 `nvs_commit()`、Flash 保存与写入过程中突然掉电的边界；官方要求设置值之后调用 commit 才能保证提交。
- [Espressif：USB Serial/JTAG Controller Console，ESP-IDF 5.5.3 / ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-guides/usb-serial-jtag-console.html)。用于解释 USB 串口通信和烧录，不等同于向 U 盘复制文件。

## 实践与社区

先用本地课程检验概念，再在单独确认安装后做设备验收。需要讨论通用开发问题时，可从仓库的[支持入口](../../.github/SUPPORT.zh_CN.md)寻找社区渠道；没有代用户加入社区或外发信息。

## 仍需验证

编译不能证明当前设备固件、按键手感、蜂鸣音量或 Recovery 状态。需在用户明确确认设备操作后完成真机验证。课程练习完成也不自动证明长期记忆，后续可用不同例子再核对。
