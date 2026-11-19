#pragma once

#include <ibus.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ibus_engine::constants {

inline std::filesystem::path configPath() {
  return std::filesystem::path(g_get_user_config_dir()) / "ibus/wetypex/wetypex.json";
}

inline constexpr std::array<const char *, 19> fuzzyKeys = {
    "fuzzy_nl",       "fuzzy_rl",       "fuzzy_hf",     "fuzzy_gk",     "fuzzy_an_ang",
    "fuzzy_ian_iang", "fuzzy_uan_uang", "fuzzy_c_ch",   "fuzzy_s_sh",   "fuzzy_z_zh",
    "fuzzy_hui_fei",  "fuzzy_en_eng",   "fuzzy_in_ing", "fuzzy_on_ong", "fuzzy_huang_wang",
    "fuzzy_un_ong",   "fuzzy_un_iong",  "fuzzy_an_ai",  "fuzzy_eng_ong"};

enum ShortcutKey : std::size_t {
  kShortcutShiftSwitch,
  kShortcutCtrlSwitch,
  kShortcutAiAssistant,
  kShortcutVMode,
  kShortcutVoiceInput,
  kShortcutVoiceHoldToTalk,
  kShortcutFullHalfSwitch,
  kShortcutPunctuationSwitch,
  kShortcutTraditionalSwitch,
  kShortcutPageMinusEqual,
  kShortcutPageBrackets,
  kShortcutPageCommaPeriod,
  kShortcutPageShiftTab,
  kShortcutCandidateNumpad,
  kShortcutCount,
};

inline constexpr std::array<const char *, kShortcutCount> shortcutKeys = {
    "shortcut_shift_switch",       "shortcut_ctrl_switch",
    "shortcut_ai_assistant",       "shortcut_v_mode",
    "shortcut_voice_input",        "shortcut_voice_hold_to_talk",
    "shortcut_full_half_switch",   "shortcut_punctuation_switch",
    "shortcut_traditional_switch", "shortcut_page_minus_equal",
    "shortcut_page_brackets",      "shortcut_page_comma_period",
    "shortcut_page_shift_tab",     "shortcut_candidate_numpad",
};

struct PunctuationEntry {
  char ascii;
  const char *normal;
  const char *english;
  std::uint8_t pairType;
};

inline constexpr std::array<PunctuationEntry, 32> kPunctuation = {{
    {'!', "！", "!", 0}, {'"', "“”", "\"\"", 2}, {'#', "#", "#", 0},     {'$', "¥", "$", 0},     {'%', "%", "%", 0},
    {'&', "&", "&", 0},  {'\'', "‘’", "''", 2},  {'(', "（）", "()", 1}, {')', "）", ")", 0},    {'*', "*", "*", 0},
    {'+', "+", "+", 0},  {',', "，", ",", 0},    {'-', "-", "-", 0},     {'.', "。", ".", 0},    {'/', "/", "/", 0},
    {':', "：", ":", 0}, {';', "；", ";", 0},    {'<', "《》", "<>", 1}, {'=', "=", "=", 0},     {'>', "》", ">", 0},
    {'?', "？", "?", 0}, {'@', "@", "@", 0},     {'[', "【】", "[]", 1}, {'\\', "、", "\\", 0},  {']', "】", "]", 0},
    {'^', "……", "^", 0}, {'_', "——", "_", 0},    {'`', "·", "`", 0},     {'{', "「」", "{}", 1}, {'|', "|", "|", 0},
    {'}', "」", "}", 0}, {'~', "～", "~", 0},
}};

inline constexpr std::array<int, 7> doubleSchemes = {5, 1, 2, 3, 4, 6, 7};

inline constexpr guint kCommandModifierMask = IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_MOD3_MASK | IBUS_MOD4_MASK |
                                              IBUS_MOD5_MASK | IBUS_SUPER_MASK | IBUS_HYPER_MASK | IBUS_META_MASK;

} // namespace ibus_engine::constants
