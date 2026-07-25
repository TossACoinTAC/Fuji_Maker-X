# 首次 USB 探测与原始 Flash 备份

这一步只操作泽杰 ESP32-S3-N16R8 开发板。目标是在改变板内数据前确认串口、芯片和
Flash，并做一份完整原厂 Flash 备份。本文中的命令不会写入开发板；真正刷入探测固件
必须等备份核验通过后再进行。

## 0. 接线前检查

1. 断开屏幕、麦克风、功放、扬声器、按钮和所有面包板跳线。
2. 电池继续保持完全断开，裸线分别绝缘，不能触碰开发板、面包板或彼此。
3. 只用一根确认支持数据传输的 USB-C 线，连接开发板上标有 `COM` 的接口和 Mac。
4. 开发板应平放在绝缘桌面，不能放在金属表面或松散导线上。

如出现明显发热、焦味、冒烟或 USB 反复断连，立刻拔掉 USB，不要继续按键或反复上电。

## 1. 加载 ESP-IDF 6.0.2

仓库只支持官方 ESP-IDF `v6.0.2`。主开发机的实体安装目录为
`/Volumes/Mac_DiskExtension/Developer/Toolchains/esp/esp-idf-v6.0.2`，工具目录为
`/Volumes/Mac_DiskExtension/Developer/Toolchains/espressif`；`~/esp` 与
`~/.espressif` 仅保留为兼容软链接。本文不依赖 `/private/tmp`。先挂载移动 SSD，再使用
zsh 打开终端后运行：

```zsh
cd /Users/apple/Documents/Skd_Learning/26summer/Maker-X
source embedded/activate_idf.zsh
cd embedded/xiaozhi
idf.py --version
```

最后一条应显示 `ESP-IDF v6.0.2`。如果没有安装、路径不同或版本不是 6.0.2，先停止，
不要使用其他版本继续。若显示 `v6.0.2-dirty`，运行 `git -C "$IDF_PATH" status` 查明
原因；正式构建环境不应依赖缺文件或带本地修改的 ESP-IDF 源码。激活脚本会优先选择
移动 SSD 上的 `IDF_PATH` 与 `IDF_TOOLS_PATH`，并清除旧的
`IDF_PYTHON_ENV_PATH`，避免继续引用已经清理的临时目录。

## 2. 找到 COM 串口

先在开发板未连接时运行一次：

```sh
ls /dev/cu.*
```

接上标有 `COM` 的 USB-C 口后再运行一次。新增项目通常类似：

```text
/dev/cu.usbserial-110
/dev/cu.wchusbserial110
```

把实际新增路径代入下文的 `<PORT>`。不要凭猜测选择 `/dev/cu.Bluetooth-Incoming-Port`。
如果没有新增串口，优先更换支持数据的 USB 线或 USB 口，不要接任何外设来测试。

## 3. 只读芯片和 Flash 信息

以下命令可能让开发板短暂进入下载模式，但不会擦除或写入 Flash：

```sh
python -m esptool --chip esp32s3 --port <PORT> chip-id
python -m esptool --chip esp32s3 --port <PORT> flash-id
```

记录完整输出。期望芯片为 ESP32-S3，Flash 容量为 16 MB。若自动复位失败：

1. 按住开发板 `BOOT` 键。
2. 短按一次 `RST`/`EN` 键后松开。
3. 松开 `BOOT` 键，立即重试命令。

`esptool` 在未运行新固件时不能可靠报告外接 PSRAM 容量。因此此处先确认芯片和
Flash；8 MB Octal PSRAM 必须在原始 Flash 备份完成、刷入 `fuji-devkit-s3-probe`
后，从串口启动日志确认。

## 4. 备份完整 16 MB 原始 Flash

在仓库外建立一个不会被 Git 管理的备份目录。文件名中的日期可以按当天修改：

```sh
mkdir -p "$HOME/Documents/Maker-X-device-backups"
python -m esptool --chip esp32s3 --port <PORT> read-flash \
  0x0 0x1000000 \
  "$HOME/Documents/Maker-X-device-backups/zejie-n16r8-original-2026-07-23.bin"
```

备份应恰好是 `16777216` 字节。核验大小和 SHA-256：

```sh
stat -f '%z bytes' "$HOME/Documents/Maker-X-device-backups/zejie-n16r8-original-2026-07-23.bin"
shasum -a 256 "$HOME/Documents/Maker-X-device-backups/zejie-n16r8-original-2026-07-23.bin"
```

为排除 USB 传输错误，建议立即读第二份并比较：

```sh
python -m esptool --chip esp32s3 --port <PORT> read-flash \
  0x0 0x1000000 \
  "$HOME/Documents/Maker-X-device-backups/zejie-n16r8-original-verify.bin"
cmp \
  "$HOME/Documents/Maker-X-device-backups/zejie-n16r8-original-2026-07-23.bin" \
  "$HOME/Documents/Maker-X-device-backups/zejie-n16r8-original-verify.bin"
```

`cmp` 没有任何输出并以成功结束，才表示两次读取逐字节一致。请保留第一份文件及其
SHA-256；第二份验证文件确认一致后可以稍后删除。

## 5. 暂停并同步结果

在刷写前暂停。需要同步以下信息：

- `chip-id` 完整输出；
- `flash-id` 完整输出；
- 两次备份的文件大小；
- 第一份备份的 SHA-256；
- `cmp` 是否无输出且成功；
- 开发板 `COM` 口和按键标识是否与本文一致。

确认这些结果后，下一步才是刷入纯探测版，读取板型、复位原因、实际 Flash 和实际
PSRAM 日志。此时仍然不连接屏幕或任何其他外设。
