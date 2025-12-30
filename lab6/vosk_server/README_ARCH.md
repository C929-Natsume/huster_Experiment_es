# Vosk服务器架构说明

## 问题说明

当前 `libvosk.so` 和 `server` 是 **ARM aarch64** 架构，只能在 ARM 设备上运行。

如果您的开发机器是 **x86_64**，会出现 "Exec format error" 错误。

## 解决方案

### 方案1：在ARM设备上运行（推荐）

这是嵌入式Linux项目，vosk服务器应该在目标ARM设备上运行：

1. 将整个 `vosk_server` 目录复制到ARM设备
2. 在ARM设备上运行：
   ```bash
   cd vosk_server
   ./run_server.sh
   ```

### 方案2：下载x86_64版本的库（用于开发测试）

如果您想在x86_64开发机上测试，需要：

1. 从 [Vosk官网](https://alphacephei.com/vosk/) 下载 x86_64 版本的库
2. 或者从 [GitHub releases](https://github.com/alphacep/vosk-api/releases) 下载
3. 替换 `libvosk.so` 文件
4. 重新编译 server：
   ```bash
   cd src
   make clean
   make
   ```

### 方案3：使用游戏的无语音模式

游戏可以在没有语音识别服务器的情况下运行：
- 语音按钮会显示"识别失败"，但不影响游戏
- 所有操作都可以通过触摸完成
- 游戏功能完全正常

## 运行服务器

在ARM设备上：
```bash
cd vosk_server
./run_server.sh
```

服务器会在 `127.0.0.1:8011` 上监听。

## 检查服务器状态

```bash
# 检查端口是否监听
netstat -an | grep 8011
# 或
ss -tlnp | grep 8011
```

