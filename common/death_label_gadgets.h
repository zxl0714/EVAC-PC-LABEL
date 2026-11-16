#pragma once

#include <cstdint>

namespace cave::death_label {

// Local cache format. Pixels come from the player's own PS4 title_gadget
// atlas. The PS2 host consumes the same indexed sprites for either PS4 build.
inline constexpr std::uint32_t GadgetMagic = 0x37474c44; // DLG7
inline constexpr std::uint32_t GadgetPixelBudget = 16 * 1024 * 1024;
inline constexpr char GadgetCachePath[] = "arrange/death-label.gadgets";
enum Sprite : std::uint32_t {
    ScorePanel, ShotiaPanel, LeinyanPanel, ExyPanel, HyperPanel, RoundPanel,
    StagePanel, BossPanel, BossBackdrop,
    Stage1Boss, Stage2Boss, Stage3Boss, Stage4Boss, Stage5Boss,
    Stage1Name, Stage2Name, Stage3Name, Stage4Name, Stage5Name,
    HyperBack, HyperFill, HyperStandby, HyperActive, HyperStock0, HyperStock1, HyperStock2,
    HyperStock3, HyperStock4, HyperStock5,
    Round1, Round2, StageLabel, Stage1, Stage2, Stage3, Stage4, Stage5,
    ShipA, ShipB, BombIcon, Slash,
    Big0, Big1, Big2, Big3, Big4, Big5, Big6, Big7, Big8, Big9,
    Small0, Small1, Small2, Small3, Small4, Small5, Small6, Small7, Small8, Small9,
    DamagePanel, RankPanel, ComboPanel, ComboFrame, ComboFill,
    StockA, StockB, MusicPanel, MusicIcon, MusicNote, MusicTitle,
    MusicBoss, MusicClear, MusicEntry, MusicGameOver,
    Stage5Jet, Hibachi, HibachiName, TimeCaption, TimeColon, TimeDecimal,
    BossHealthFill, BossDefeated, ShotLevel, LaserLevel, PowerSegment,
    FighterAName, FighterBName, ShotiaName, LeinyanName, ExyName,
    Invincible, ScoreDetails, HyperTimerEmpty, HyperTimerFill, HyperPercent,
    BattlePanel,
    SpriteCount,
};

struct GadgetSpriteHeader {
    std::uint32_t width, height;
};

}
