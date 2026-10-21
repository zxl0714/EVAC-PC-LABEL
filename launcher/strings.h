#pragma once
#include "common/ini.h"
#include <array>
#include <string_view>
#include <stdexcept>

namespace cave::launcher {
enum class Language { Chinese, Japanese, English };
inline Language ParseLanguage(std::string_view value) {
  if(value=="zh")return Language::Chinese;
  if(value=="ja")return Language::Japanese;
  if(value=="en")return Language::English;
  throw std::runtime_error("config.ini [collection].language: expected zh, ja or en");
}
inline Language ReadLanguage(const Ini& ini) {
  const auto value=ini.Get("collection","language");
  return value?ParseLanguage(value->text):Language::Chinese;
}
inline const char* LanguageCode(Language language) {
  return language==Language::Chinese?"zh":language==Language::Japanese?"ja":"en";
}
inline const wchar_t* LanguageLabel(Language language) {
  return language==Language::Chinese?L"中文 ▾":language==Language::Japanese?L"日本語 ▾":L"English ▾";
}
struct Translation {std::wstring_view key; const wchar_t* japanese; const wchar_t* english;};
inline constexpr std::array kTranslations{
  Translation{L"无法显示详细信息，请检查日志。",L"詳細を表示できません。ログを確認してください。",L"Cannot show details. Please check the logs."},
  Translation{L"操作失败。详细信息：\n\n",L"操作に失敗しました。詳細：\n\n",L"The operation failed. Details:\n\n"},
  Translation{L"已就绪",L"準備完了",L"Ready"},
  Translation{L"数据已找到 · 启动时校验",L"データ検出済み · 起動時に検証",L"Data found · verified on launch"},
  Translation{L"未找到游戏数据",L"ゲームデータがありません",L"Game data not found"},
  Translation{L"游戏数据不完整",L"ゲームデータが不足しています",L"Game data is incomplete"},
  Translation{L"解压目录层级不正确",L"データフォルダーの階層を確認",L"Nested game-data folder"},
  Translation{L"数据版本不支持",L"未対応のデータバージョン",L"Unsupported data version"},
  Translation{L"缺少运行组件",L"実行ファイルが不足しています",L"Runtime files are missing"},
  Translation{L"配置有误 · 请检查 config.ini",L"設定エラー · config.ini を確認",L"Configuration error · check config.ini"},
  Translation{L"数据路径不可用",L"データの場所を利用できません",L"Data path is unavailable"},
  Translation{L"正在接入",L"対応作業中",L"Integration in progress"},
  Translation{L"敬请期待",L"近日対応予定",L"Coming soon"},
  Translation{L"正在检测…",L"確認中…",L"Checking…"},
  Translation{L"熟悉的弹幕，新的入口。",L"いつもの弾幕を、新しい入口から。",L"Familiar bullet patterns. A new way in."},
  Translation{L"配置文件",L"設定ファイル",L"Config file"},
  Translation{L"日志",L"ログ",L"Logs"},
  Translation{L"启动时全屏",L"全画面で起動",L"Start fullscreen"},
  Translation{L"□ 启动时全屏",L"□ 全画面で起動",L"□ Start fullscreen"},
  Translation{L"保存",L"保存",L"Save"},
  Translation{L"排行榜昵称",L"ランキング名",L"Nickname"},
  Translation{L"未保存 · 启动前会自动保存",L"未保存 · 起動前に自動保存",L"Unsaved · saved before launch"},
  Translation{L"共用设置 · 下次启动生效",L"共通設定 · 次回起動時に適用",L"Shared · applies on next launch"},
  Translation{L"已选择",L"選択中",L"Selected"},
  Translation{L"后续作品",L"今後の対応作品",L"NEXT IN THE COLLECTION"},
  Translation{L"请先退出正在运行的游戏",L"実行中のゲームを終了してください",L"Close the running game first"},
  Translation{L"正在读取／校验所选游戏…",L"選択したゲームを検証中…",L"Reading / verifying selected game…"},
  Translation{L"游戏运行中",L"ゲーム実行中",L"Game running"},
  Translation{L"启动游戏",L"ゲームを起動",L"Launch game"},
  Translation{L"游戏数据…",L"ゲームデータ…",L"Game data…"},
  Translation{L"刷新",L"再読込",L"Refresh"},
  Translation{L"详情",L"詳細",L"Details"},
  Translation{L"视觉预览 · 安装状态为示例",L"画面プレビュー · 状態はサンプル",L"Visual preview · sample installation states"},
  Translation{L"快捷设置读取失败，请打开配置文件修正后刷新",L"設定を読み込めません。設定ファイルを修正し再読込してください",L"Cannot read settings. Fix the config file and refresh."},
  Translation{L"此游戏有独立的全屏／昵称设置，请在配置文件中调整",L"このゲームには個別設定があります。設定ファイルで変更してください",L"This game has fullscreen / nickname overrides in the config file."},
  Translation{L"快捷设置已保存，下次启动生效",L"設定を保存しました。次回起動時に適用されます",L"Settings saved. Applies on next launch."},
  Translation{L"刷新将放弃尚未保存的快捷设置，重新读取 config.ini。继续吗？",L"未保存の設定を破棄し、config.ini を再読込します。続行しますか？",L"Discard unsaved settings and reload config.ini?"},
  Translation{L"刷新配置",L"設定を再読込",L"Refresh configuration"},
  Translation{L"正在运行：",L"実行中：",L"Running: "},
  Translation{L"数据已复制并逐文件校验，配置已更新",L"コピー・検証が完了し、設定を更新しました",L"Data copied and verified; configuration updated"},
  Translation{L"数据复制未完成，请查看详细信息",L"データをコピーできませんでした。詳細を確認してください",L"Data copy did not complete. See details."},
  Translation{L"无法启动，请检查数据和运行组件",L"起動できません。データと実行ファイルを確認してください",L"Cannot launch. Check game data and runtime files."},
  Translation{L"游戏异常退出（退出码 ",L"ゲームが異常終了しました（終了コード ",L"Game exited unexpectedly (exit code "},
  Translation{L"）。\n\n日志目录：logs/",L"）。\n\nログ：logs/",L").\n\nLogs: logs/"},
  Translation{L"/\n启动器可以继续使用。",L"/\nランチャーは引き続き利用できます。",L"/\nThe launcher is still available."},
  Translation{L"游戏已关闭（退出清理限制已记入日志）",L"ゲームを終了しました（終了処理の制限をログに記録）",L"Game closed (cleanup limitation recorded in logs)"},
  Translation{L"游戏异常退出，请查看该游戏的日志",L"ゲームが異常終了しました。ログを確認してください",L"Game exited unexpectedly. Check its logs."},
  Translation{L"游戏已退出",L"ゲームを終了しました",L"Game closed"},
  Translation{L"选择要复制到合集的游戏原始数据目录",L"コピーするゲームデータのフォルダーを選択",L"Select the game data folder to copy"},
  Translation{L"选择游戏原始数据目录（不会移动或修改文件）",L"ゲームデータのフォルダーを選択（元のファイルは変更しません）",L"Select game data (original files will not be changed)"},
  Translation{L"将完整复制并校验游戏数据。\n\n来源：",L"ゲームデータをコピーして検証します。\n\nコピー元：",L"Copy and verify the complete game data.\n\nSource: "},
  Translation{L"\n目标：",L"\nコピー先：",L"\nDestination: "},
  Translation{L"\n\n不会移动或修改源文件。目标必须为空，不会合并或覆盖已有文件。\n继续吗？",L"\n\n元のファイルは変更しません。コピー先は空である必要があります。既存ファイルは上書きしません。\n続行しますか？",L"\n\nOriginal files will not be changed. The destination must be empty; existing files will not be merged or overwritten.\nContinue?"},
  Translation{L"复制到合集",L"コレクションにコピー",L"Copy to collection"},
  Translation{L"正在校验源数据…",L"元のデータを検証中…",L"Verifying source data…"},
  Translation{L"选择数据目录…",L"データフォルダーを選択…",L"Choose data folder…"},
  Translation{L"复制到合集…",L"コレクションにコピー…",L"Copy into collection…"},
  Translation{L"正在复制并校验数据… ",L"データをコピー・検証中… ",L"Copying and verifying data… "},
  Translation{L"正在取消复制，请稍候…",L"コピーを中止しています…",L"Cancelling copy, please wait…"},
  Translation{L"正在检测游戏数据…",L"ゲームデータを確認中…",L"Checking game data…"},
  Translation{L" 部作品 · 按需安装游戏数据",L" タイトル · 必要なゲームだけインストール",L" titles · install only the data you need"}
};
inline const wchar_t* Translate(Language language, std::wstring_view key) {
  for(const auto& row:kTranslations)if(row.key==key)
    return language==Language::Chinese?row.key.data():language==Language::Japanese?row.japanese:row.english;
  throw std::runtime_error("Missing launcher translation");
}
} // namespace cave::launcher
