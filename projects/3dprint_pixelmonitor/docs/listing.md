# 3D 印况像素屏 · 3D Print Pixel Monitor

**分类 / Category**　效率工具 / Productivity

## 发布简介

在 ESP-Mosaico 上查看拓竹打印进度、层数、剩余时间和状态动画。短按 AI 键切换视图，也可通过蓝牙同步到 32×32 iDotMatrix 像素屏。

## 发布详情

把 ESP-Mosaico 放在桌边，抬眼就能看到打印做到哪一步。彩色打印机视图用像素动画呈现打印过程，进度视图集中显示百分比、层数和剩余时间。

- **打印状态**　显示准备、打印、进退料、暂停、完成、取消和故障。部分状态会播放短提示音。
- **双屏联动**　可选配 32×32 iDotMatrix，通过蓝牙显示同一台打印机的状态。
- **联网与管理**　可直接在触屏扫描并连接 Wi-Fi，连接后自动返回主界面。在浏览器后台添加局域网打印机或拓竹云账号。
- **亮度与息屏**　亮度可调。长按 AI 键 2 秒息屏，也可设为 5–30 分钟无打印自动息屏。按 AI 键或开始新打印可唤醒。

适用于 ESP-Mosaico（ESP32-S31），需要 Vibe Mode 0.2.0 或更新版本。下载并安装发布附件中的 `3dprint_pixelmonitor.irisfw`。 [下载 1.0.2](https://github.com/tbc0309/esp-mosaico-vibe/releases/tag/1.0.2)

### 相关项目

- **xTouch-CN** 适合想在触屏上查看和操作拓竹打印机的用户。支持多种 ESP32 触屏开发板，并可联动温湿度计和 iDotMatrix 像素屏。 [xTouch-CN](https://github.com/tbc0309/xTouch-CN)
- **iDotMatrix × Bambu Lab** 适合已有 iDotMatrix 像素屏的用户。可用立创 ESP32S3R8N8 或 Seeed Studio XIAO ESP32C6 无屏开发板接收打印状态，并通过蓝牙驱动像素屏。 [iDotMatrix × Bambu Lab](https://github.com/tbc0309/idotmatrix-bambulab)

## Short description

See Bambu Lab print progress, layers, time remaining and animated states on ESP-Mosaico. Press AI to switch views, or sync the status to a 32×32 iDotMatrix display over Bluetooth.

## Full description

Keep your printer status within sight. The animated printer view shows the print in pixels, while the progress view puts percentage, layers and time remaining together.

- **Print states** show preparation, printing, filament changes, pause, completion, cancellation and faults. Short tones accompany selected states.
- **Optional second display** shows the same printer status on a 32×32 iDotMatrix panel over Bluetooth.
- **Setup and management** start with touchscreen Wi-Fi setup. Add a LAN printer or Bambu Lab cloud account in the browser dashboard.
- **Brightness and standby** are adjustable. Hold AI for two seconds to turn off the built-in screen, or set automatic standby after 5–30 minutes without printing. Press AI or start a print to wake the screen.

Requires ESP-Mosaico (ESP32-S31) and Vibe Mode 0.2.0 or later. Install the `3dprint_pixelmonitor.irisfw` release asset. [Download 1.0.2](https://github.com/tbc0309/esp-mosaico-vibe/releases/tag/1.0.2)

### Related projects

- **xTouch-CN** provides a touchscreen dashboard for monitoring and controlling Bambu Lab printers. It supports several ESP32 display boards, a temperature and humidity sensor, and an optional iDotMatrix display. [xTouch-CN](https://github.com/tbc0309/xTouch-CN)
- **iDotMatrix × Bambu Lab** turns an LCKFB ESP32S3R8N8 or Seeed Studio XIAO ESP32C6 into a headless controller for an iDotMatrix panel. It receives printer status and drives the panel over Bluetooth. [iDotMatrix × Bambu Lab](https://github.com/tbc0309/idotmatrix-bambulab)
