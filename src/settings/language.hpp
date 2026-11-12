#pragma once

#include <string>
#include <vector>

// IBus 设置界面的全部用户可见文本。
// 新增语言时，可复制本文件为 language_<locale>.hpp，保持常量名一致，
// 并在构建时选择要包含的文件。
namespace ibus_settings::lang {

// ---- 应用信息 ----
inline constexpr const char *kAppID = "org.freedesktop.IBus.Wetypex.Setup";
inline constexpr const char *kAppName = "WeTypeX 设置";
inline constexpr const char *kBrandName = "WeTypeX";
inline constexpr const char *kDeveloperName = "WeTypeX contributors";
inline constexpr const char *kAboutComments = "\nWeTypeX IBus 输入法设置\n原版核心及相关商标归其权利人所有";
inline constexpr const char *kWebsite = "https://github.com/chencaidy/ibus-wetypex";
inline constexpr const char *kIssues = "https://github.com/chencaidy/ibus-wetypex/issues";

// ---- 页面标题 ----
inline constexpr const char *kPageInputTitle = "输入";
inline constexpr const char *kPagePhrasesTitle = "常用语";
inline constexpr const char *kPageDevicesTitle = "跨设备";
inline constexpr const char *kPageAboutTitle = "关于";

// ---- 输入页：输入方案 ----
inline constexpr const char *kGroupInputScheme = "输入方案";
inline constexpr const char *kInputPinyinTitle = "拼音输入";
inline constexpr const char *kInputDoublePinyinTitle = "双拼输入";
inline constexpr const char *kInputWubiTitle = "五笔输入";

inline const std::vector<std::string> kDoublePinyinSchemes = {"自然码",   "搜狗", "微软",    "小鹤",
                                                              "拼音加加", "紫光", "智能 ABC"};
inline const std::vector<std::string> kWubiSolutions = {"86 五笔", "98 五笔", "新世纪五笔"};

// ---- 输入页：候选窗口 ----
inline constexpr const char *kGroupCandidates = "候选窗口";
inline constexpr const char *kPageSizeTitle = "每页候选数";
inline constexpr const char *kPageSizeSubtitle = "IBus 候选窗口每页显示的候选数量";
inline constexpr const char *kVerticalCandidatesTitle = "竖排候选";
inline constexpr const char *kVerticalCandidatesSubtitle = "使用垂直排列的候选窗口";

// ---- 输入页：输入行为 ----
inline constexpr const char *kGroupBehavior = "输入行为";
inline constexpr const char *kDefaultLanguageTitle = "默认输入语言";
inline const std::vector<std::string> kDefaultLanguageLabels = {"中文", "英文"};
inline constexpr const char *kShiftSwitchTitle = "使用 Shift 切换中英文";
inline constexpr const char *kSmartInputTitle = "智能拼写";
inline constexpr const char *kSmartInputSubtitle = "启用原版智能纠错和候选";
inline constexpr const char *kEmojiRecommendTitle = "表情和颜文字推荐";
inline constexpr const char *kEmojiRecommendSubtitle = "在候选中推荐表情";
inline constexpr const char *kChinesePunctuationTitle = "中文标点";
inline constexpr const char *kChinesePunctuationSubtitle = "自动根据光标前字符切换，或固定使用全角/半角标点";
inline const std::vector<std::string> kPunctuationModes = {"自动", "全角", "半角"};
inline constexpr const char *kSlashPunctuationTitle = "将「/?」替换为 、";
inline constexpr const char *kSlashPunctuationSubtitle = "输入斜杠时输出顿号";
inline constexpr const char *kStandaloneTitle = "单机模式";
inline constexpr const char *kStandaloneSubtitle = "云输入/跨设备同步等功能将不可用";

// ---- 输入页：高级选项 ----
inline constexpr const char *kGroupAdvanced = "高级选项";
inline constexpr const char *kWubiOptionsTitle = "五笔选项";
inline constexpr const char *kWubiPinyinMixLabel = "五笔拼音混输";
inline constexpr const char *kWubiUniqueCommitLabel = "唯一候选自动上屏";
inline constexpr const char *kWubiNextCommitLabel = "输入下一码时上屏";
inline constexpr const char *kWubiWildcardCommentLabel = "通配符编码提示";

inline constexpr const char *kEmojiTypesTitle = "表情类型";
inline constexpr const char *kEmojiNormalLabel = "Emoji";
inline constexpr const char *kEmojiKaomojiLabel = "颜文字";
inline constexpr const char *kEmojiSymbolLabel = "符号表情";

inline constexpr const char *kFuzzyPinyinTitle = "模糊拼音";
inline constexpr const char *kFuzzyNlLabel = "n/l";
inline constexpr const char *kFuzzyRlLabel = "r/l";
inline constexpr const char *kFuzzyHfLabel = "h/f";
inline constexpr const char *kFuzzyGkLabel = "g/k";
inline constexpr const char *kFuzzyAnAngLabel = "an/ang";
inline constexpr const char *kFuzzyIanIangLabel = "ian/iang";
inline constexpr const char *kFuzzyUanUangLabel = "uan/uang";
inline constexpr const char *kFuzzyCChLabel = "c/ch";
inline constexpr const char *kFuzzySShLabel = "s/sh";
inline constexpr const char *kFuzzyZZhLabel = "z/zh";
inline constexpr const char *kFuzzyHuiFeiLabel = "hui/fei";
inline constexpr const char *kFuzzyEnEngLabel = "en/eng";
inline constexpr const char *kFuzzyInIngLabel = "in/ing";
inline constexpr const char *kFuzzyOnOngLabel = "on/ong";
inline constexpr const char *kFuzzyHuangWangLabel = "huang/wang";
inline constexpr const char *kFuzzyUnOngLabel = "un/ong";
inline constexpr const char *kFuzzyUnIongLabel = "un/iong";
inline constexpr const char *kFuzzyAnAiLabel = "an/ai";
inline constexpr const char *kFuzzyEngOngLabel = "eng/ong";

// ---- 常用语页 ----
inline constexpr const char *kPhrasesGroupTitle = "常用语";
inline constexpr const char *kRefreshTooltip = "刷新";
inline constexpr const char *kAddPhraseTooltip = "添加常用语";
inline constexpr const char *kEditTooltip = "编辑";
inline constexpr const char *kDeleteTooltip = "删除";

inline constexpr const char *kPhraseDialogAddTitle = "添加常用语";
inline constexpr const char *kPhraseDialogEditTitle = "编辑常用语";
inline constexpr const char *kCancelLabel = "取消";
inline constexpr const char *kSaveLabel = "保存";
inline constexpr const char *kPhraseKeyTitle = "输入码（汉字/拼音或首字母）";
inline constexpr const char *kPhraseWordsLabel = "常用语内容";
inline constexpr const char *kPhraseEmptyFields = "内容和输入码不能为空";
inline constexpr const char *kPhraseSaving = "正在保存";
inline constexpr const char *kPhraseSaveFailed = "保存失败";
inline constexpr const char *kPhraseEmptyTitle = "暂无常用语";
inline constexpr const char *kPhraseEmptySubtitle = "点击右上角添加";

// ---- 跨设备页 ----
inline constexpr const char *kDevicesGroupTitle = "跨设备";
inline constexpr const char *kStandaloneDeviceTitle = "单机模式";
inline constexpr const char *kStandaloneDeviceSubtitle = "单机模式下跨设备功能不可用";
inline constexpr const char *kDevicesNotPairedDescription = "尚未关联设备，关联后可在电脑与手机之间同步剪贴板和常用语";
inline constexpr const char *kPairDeviceTitle = "关联设备";
inline constexpr const char *kPairDeviceSubtitle = "使用另一台设备上的匹配码完成关联";
inline constexpr const char *kPairActionLabel = "获取匹配码";

inline constexpr const char *kGroupSyncFeatures = "同步功能";
inline constexpr const char *kSyncClipboardTitle = "跨设备复制粘贴";
inline constexpr const char *kSyncClipboardSubtitle = "在电脑复制的文字和图片可在手机粘贴";
inline constexpr const char *kSyncPhraseTitle = "常用语同步";
inline constexpr const char *kSyncPhraseSubtitle = "在关联设备之间同步常用语";
inline constexpr const char *kSyncDictionaryTitle = "个人词库同步";
inline constexpr const char *kSyncDictionarySubtitle = "在关联设备之间同步个人词库";
inline constexpr const char *kSyncNowTitle = "立即请求同步";
inline constexpr const char *kSyncNowSubtitle = "请求关联设备同步最新状态";
inline constexpr const char *kSyncNowActionLabel = "刷新";
inline constexpr const char *kSyncRequested = "同步完成，请稍后检查关联设备";
inline constexpr const char *kSyncRequestFailed = "同步请求失败";
inline constexpr const char *kSyncStatusTitle = "同步状态";
inline constexpr const char *kSyncStatusDescription = "空闲";
inline constexpr const char *kSyncSettingsUpdated = "同步设置已更新";
inline constexpr const char *kSyncSettingsUpdateFailed = "更新同步设置失败";

inline constexpr const char *kGroupMyDevices = "我的设备";
inline constexpr const char *kLocalDeviceSubtitle = "本机";
inline constexpr const char *kLocalDeviceBadge = "（本机）";
inline constexpr const char *kLocalDeviceFallbackName = "LINUX";
inline constexpr const char *kGroupDeviceManagement = "设备管理";
inline constexpr const char *kAssociateDeviceTitle = "关联新设备";
inline constexpr const char *kAssociateDeviceSubtitle = "关联 iOS/Android 版本以使用跨设备同步";
inline constexpr const char *kDeviceSubtitleSeparator = " · ";

inline constexpr const char *kPlatformAndroid = "Android";
inline constexpr const char *kPlatformIos = "iOS";
inline constexpr const char *kPlatformLinux = "Linux";
inline constexpr const char *kPlatformWindows = "Windows";
inline constexpr const char *kPlatformUnknown = "未知";

// ---- 关联设备窗口 ----
inline constexpr const char *kPairingWindowTitle = "关联设备";
inline constexpr const char *kPairingCodeTitle = "本机匹配码";
inline constexpr const char *kPairingCodeLoading = "正在获取匹配码";
inline constexpr const char *kPairingCodeMessage = "请在另一台设备中进入：\n"
                                                   "「手机端微信输入法设置 → 跨设备粘贴同步 → 关联设备」\n"
                                                   "输入上方匹配码关联";
inline constexpr const char *kPairingCodeValid = "匹配码 5 分钟内有效";
inline constexpr const char *kPairingCodeUnavailable = "暂时无法获取匹配码，请稍后重试";
inline constexpr const char *kPairingCodeInvalid = "服务返回了无效匹配码";
inline constexpr const char *kPairingCodeExpired = "匹配码已失效，请关闭后重试";
inline constexpr const char *kPairingConfirmOnOther = "请在另一台设备确认关联";
inline constexpr const char *kPairingConfirming = "正在关联设备";
inline constexpr const char *kPairingSuccess = "设备关联成功";
inline constexpr const char *kPairingFailed = "关联失败，请重试";

// ---- 关于页 ----
inline constexpr const char *kVersionUnavailable = "未知版本";
inline constexpr const char *kUpdateTitle = "检查更新";
inline constexpr const char *kUpdateActionLabel = "检查";
inline constexpr const char *kUpdateChecking = "正在检查更新";
inline constexpr const char *kUpdateNoUpdate = "当前已是最新版本";
inline constexpr const char *kUpdateAvailable = "发现新版本：";
inline constexpr const char *kUpdateInstall = "安装";
inline constexpr const char *kUpdateInstalling = "正在下载并安装更新";
inline constexpr const char *kUpdateInstalled = "更新安装成功，请重新启动应用";
inline constexpr const char *kUpdateInstallFailed = "安装更新失败";
inline constexpr const char *kUpdateFailed = "检查更新失败，请稍后重试";
inline constexpr const char *kDeveloperTitle = "开发者";
inline constexpr const char *kLicenseTitle = "许可证";
inline constexpr const char *kLicenseValue = "MIT/X11";
inline constexpr const char *kWebsiteTitle = "官网";
inline constexpr const char *kFeedbackTitle = "反馈";
inline constexpr const char *kFeedbackSubtitle = "报告问题或提出建议";
inline constexpr const char *kOpenActionLabel = "打开";

} // namespace ibus_settings::lang
