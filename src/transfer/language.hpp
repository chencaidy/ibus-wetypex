#pragma once

namespace wetypex_transfer::lang {

inline constexpr const char *kAppID = "org.freedesktop.IBus.Wetypex.Transfer";
inline constexpr const char *kAppName = "WeTypeX 隔空传送";
inline constexpr const char *kAppTitle = "隔空传送";
inline constexpr const char *kBackTooltip = "返回";
inline constexpr const char *kLoadingDevices = "正在读取设备…";
inline constexpr const char *kDevicesUnavailable = "暂时无法读取设备列表";
inline constexpr const char *kSelectDeviceDescription = "选择设备以发起文件传输";
inline constexpr const char *kNoPairedDevices = "尚未关联其他设备";
inline constexpr const char *kPairOtherDevices = "关联我的其他设备";
inline constexpr const char *kMyQrCode = "我的二维码";
inline constexpr const char *kAccountOperationFailed = "账户服务操作失败";
inline constexpr const char *kAccountOperationFailedPrefix = "账户服务操作失败：";
inline constexpr const char *kReadTransferStatusFailedPrefix = "读取安全传输状态失败：";
inline constexpr const char *kSendAction = "发送";
inline constexpr const char *kCancelAction = "取消";
inline constexpr const char *kMyDevices = "我的设备";
inline constexpr const char *kSendToOthers = "传给其他人";
inline constexpr const char *kViewMyQrCode = "查看我的二维码";
inline constexpr const char *kInitializingTransfer = "正在初始化安全传输…";
inline constexpr const char *kSendFile = "传文件";
inline constexpr const char *kSendDirectory = "传文件夹";
inline constexpr const char *kInviteDevice = "邀请设备";
inline constexpr const char *kInvitingPairedDevice = "正在邀请关联设备…";
inline constexpr const char *kShowTransferQrFailedPrefix = "无法显示传输二维码：";
inline constexpr const char *kInvalidImageFormat = "图片格式无效";
inline constexpr const char *kCreateTransferDirectoryFailed = "无法创建传输目录";
inline constexpr const char *kStartTransferCoreFailedPrefix = "安全传输核心启动失败：";
inline constexpr const char *kUnknownError = "未知错误";
inline constexpr const char *kConnectionClosed = "连接已断开";
inline constexpr const char *kSavePhoneDispatchFailed = "无法保存手机传输调度信息";
inline constexpr const char *kSavePhoneCredentialsFailed = "无法保存手机安全凭据";
inline constexpr const char *kConnectingToPhone = "正在通过安全直连或中继连接手机…";
inline constexpr const char *kDeviceFoundConnecting = "已找到设备，正在建立安全连接…";
inline constexpr const char *kConnectedCanTransfer = "已连接，可以收发文件";
inline constexpr const char *kDeviceDisconnected = "设备连接已断开";
inline constexpr const char *kConnectionRetrying = "未能连接设备，正在等待手机重试…";
inline constexpr const char *kSendingFileProgress = "正在发送文件…";
inline constexpr const char *kReceivingFileProgress = "正在接收文件…";
inline constexpr const char *kFileSent = "文件发送完成";
inline constexpr const char *kFileSaved = "文件已保存到下载目录";
inline constexpr const char *kFileSendFailed = "文件发送失败";
inline constexpr const char *kFileSaveFailed = "文件保存失败";
inline constexpr const char *kUnableToEstablishSecureConnection = "无法建立安全传输连接";
inline constexpr const char *kNoLanAddress = "未找到可用于设备直连的局域网地址";
inline constexpr const char *kCreateTransferSessionDirectoryFailed = "无法创建传输会话目录";
inline constexpr const char *kSaveTransferSessionFailed = "无法保存传输会话";
inline constexpr const char *kCreateTransferQrFailed = "无法创建传输二维码，请检查配对和网络状态";
inline constexpr const char *kQrExpiresIn = "二维码将在 ";
inline constexpr const char *kInviteExpiresIn = "邀请将在 ";
inline constexpr const char *kMinutesUntilExpiry = " 分钟后失效";
inline constexpr const char *kSavePeerCredentialsFailed = "无法保存对端安全凭据";
inline constexpr const char *kPublishPhoneEndpointFailed = "无法发布手机传输端点";
inline constexpr const char *kSaveTransferEndpointFailed = "无法保存传输端点";
inline constexpr const char *kPublishSecureEndpointFailed = "无法向手机发布安全传输端点";
inline constexpr const char *kStartAccountServiceFailedPrefix = "无法启动账户服务：";
inline constexpr const char *kSendCommandToTransferCoreFailedPrefix = "无法向传输核心发送命令：";

} // namespace wetypex_transfer::lang
