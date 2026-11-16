#pragma once

#include "death_label_damage.h"
#include "ps2_runtime.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <utility>

namespace cave::daioujou_ps2 {

// Live state from the reviewed SLPS-25233 DEATH.ELF. The overlay never
// modifies guest state and publishes one snapshot per game field.
struct DeathLabelGadgetState {
    enum class Music { None, Boss, StageClear, NameEntry, GameOver };
    enum class BossForm { Main, Jet, TwinHibachi };
    struct Damage {
        unsigned total{},hits{};
    };

    std::uint64_t score{};
    unsigned round{},stage{},doll{},ship{},stock{},bombs{},bombCapacity{},hypers{},hyperCharge{},hyperTime{};
    unsigned bulletSpeed{},bulletInterval{},hyperRank{},combo{},comboTime{},comboCapacity{};
    unsigned bossFrames{},stagePosition{};
    std::array<unsigned,4> hitDetail{};
    Damage laser,aura;
    Music music{};
    BossForm form{};
    bool playing{},hyperActive{},showHealth{},bossActive{};
};

class DeathLabelGadgetFeed {
public:
    using State = DeathLabelGadgetState;

    DeathLabelGadgetFeed() = default;
    DeathLabelGadgetFeed(const DeathLabelGadgetFeed&) = delete;
    DeathLabelGadgetFeed& operator=(const DeathLabelGadgetFeed&) = delete;
    ~DeathLabelGadgetFeed() {
        if (active_ == this) active_ = nullptr;
    }

    void Bind(PS2Runtime& runtime) {
        musicDispatch_ = runtime.lookupFunction(SoundCommand);
        if (musicDispatch_ && runtime.replaceFunction(SoundCommand,&Sound)) active_ = this;
    }

    State Observe(const std::uint8_t* memory, bool playing, bool paused) {
        if (paused) {
            auto frozen = state_;
            frozen.showHealth = false;
            return frozen;
        }
        if (!playing) {
            state_.showHealth = false;
            state_.bossActive = false;
            state_.music = music_;
            damage_ = {};
            return state_;
        }
        const unsigned round = memory[Gp - 0x5830] != 0;
        const unsigned stage = std::min<unsigned>(Read<std::uint16_t>(memory,Gp - 0x5348),4);
        if (!state_.playing || state_.round != round) {
            state_ = {};
        }
        if (!state_.playing || stage > state_.stage) ResetFight();
        state_.playing = true;
        state_.round = round;
        // The stage index wraps before the loop results have finished.
        state_.stage = std::max(stage,state_.stage);
        const auto score = std::uint64_t{Read<std::uint16_t>(memory,Gp - 0x5508)} << 32 |
            Read<std::uint32_t>(memory,Gp - 0x54fc);
        state_.score = DecimalScore(score) * 10 + Read<std::uint16_t>(memory,Gp - 0x5514) % 10;
        state_.doll = std::min<unsigned>(Read<std::uint16_t>(memory,Gp - 0x5334),2);
        state_.ship = Read<std::uint16_t>(memory,Gp - 0x532c) & 1u;
        state_.stock = std::min<unsigned>(Read<std::uint16_t>(memory,Gp - 0x53a0),20);
        state_.bombCapacity = memory[Player + 0x25];
        state_.bombs = std::min<unsigned>(memory[Player + 0x24],state_.bombCapacity);
        state_.hyperCharge = std::min<unsigned>(Read<std::uint16_t>(memory,Gp - 0x5680),2400);
        state_.hyperActive = Read<std::uint16_t>(memory,Gp - 0x5668) != 0;
        state_.hyperTime = std::max(0,int(Read<std::int16_t>(memory,Gp - 0x5670))) / 2;
        state_.hypers = std::min<unsigned>(Read<std::uint16_t>(memory,Gp - 0x56a4),5);
        state_.hyperRank = Read<std::uint16_t>(memory,Gp - 0x5678);
        state_.bulletSpeed = memory[Gp - 0x5380];
        state_.bulletInterval = memory[Gp - 0x5390];
        state_.combo = Read<std::uint16_t>(memory,Gp - 0x55c4);
        state_.comboTime = Read<std::uint16_t>(memory,Gp - 0x559c);
        state_.comboCapacity = Read<std::uint16_t>(memory,Gp - 0x5588);
        state_.music = music_;
        if (stage == state_.stage) AdvanceBoss(memory);
        else state_.showHealth = state_.bossActive = false;
        AdvanceDamage();
        return state_;
    }

    static void RecordDamage(const std::uint8_t* memory, const R5900Context* context,
                             DeathLabelAttack attack, std::uint32_t operands) {
        if (!active_ || Read<std::uint32_t>(memory,Gp - 0x5bc0) != Player) return;
        const auto offset = static_cast<std::int16_t>(operands);
        const auto address = getRegU32(context,(operands >> 21) & 31) + offset;
        if (address < EnemyPool + HealthOffset || address >= EnemyPoolEnd ||
            (address - EnemyPool - HealthOffset) % EnemyStride != 0) return;
        const auto previous = Read<std::int16_t>(memory,address);
        const auto updated = static_cast<std::int16_t>(getRegU32(context,(operands >> 16) & 31));
        if (previous <= 0 || previous <= updated) return;
        auto& feed = *active_;
        const bool hyper = Read<std::uint16_t>(memory,Gp - 0x5668) != 0;
        const auto type = (attack == DeathLabelAttack::Shot ? 0u : 1u) + (hyper ? 2 : 0);
        ++feed.hits_[type];
        if (attack == DeathLabelAttack::Shot) return;
        auto& strike = feed.damage_[attack == DeathLabelAttack::Aura];
        strike.total += previous - updated;
        ++strike.hits;
    }

private:
    static constexpr std::uint32_t Gp = 0x2965f0;
    static constexpr std::uint32_t Player = 0x2cef20;
    static constexpr std::uint32_t EnemyPool = 0x68f330, EnemyStride = 0x20, HealthOffset = 0x18;
    static constexpr std::uint32_t EnemyPoolEnd = EnemyPool + EnemyStride * 200;
    static constexpr std::uint32_t SoundCommand = 0x235b00;

    template <typename T> static T Read(const std::uint8_t* memory,std::uint32_t address) {
        T value{};
        std::memcpy(&value,memory + address,sizeof value);
        return value;
    }

    static std::uint64_t DecimalScore(std::uint64_t packed) {
        std::uint64_t value = 0;
        for (int bit = 44; bit >= 0; bit -= 4)
            value = value * 10 + ((packed >> bit) & 0xf);
        return value;
    }

    static void Sound(std::uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
        auto& feed = *active_;
        const unsigned command = Read<std::uint16_t>(memory,Gp - 0x5b70);
        const auto sequence = Read<std::uint32_t>(memory,Gp - 0x5f5c);
        const auto caller = getRegU32(context,31);
        feed.musicDispatch_(memory,context,runtime);
        if (Read<std::uint32_t>(memory,Gp - 0x5f5c) == sequence) return;
        // These call sites queue the mode's BGM. Some other users of the
        // same command queue control loading, volume or sound effects.
        if (command == 1 && caller == 0x133d5c) feed.music_ = State::Music::Boss;
        if (command == 5 && caller == 0x134fd4) feed.music_ = State::Music::Boss;
        if (command == 6 && caller == 0x234864) feed.music_ = State::Music::StageClear;
        if (command == 8 && caller == 0x199258) feed.music_ = State::Music::NameEntry;
        if (command == 0xc && caller == 0x14f8bc) feed.music_ = State::Music::GameOver;
    }

    void ResetFight() {
        hits_ = {};
        damage_ = {};
        history_ = {};
        historyIndex_ = 0;
        state_.form = State::BossForm::Main;
        state_.bossFrames = 0;
        bossStart_.reset();
    }

    void AdvanceBoss(const std::uint8_t* memory) {
        const auto stage = state_.stage;
        // Stage five spawns slot 100 for Huang Liu, then 110 for its jet
        // form, and 112 and 116 together for the two Hibachis.
        const auto phase = stage == 4 ? memory[Gp - 0x5d94] : 0;
        const auto form = phase >= 2 ? State::BossForm::TwinHibachi :
                          phase == 1 ? State::BossForm::Jet : State::BossForm::Main;
        if (state_.form != form) {
            state_.form = form;
            state_.bossFrames = 0;
            bossStart_.reset();
        }
        const auto clock = Read<std::uint32_t>(memory,Gp - 0x53a8);
        if (!bossStart_) {
            const auto collidable = [&](unsigned slot) {
                return (Read<std::uint16_t>(memory,EnemyPool + slot * EnemyStride) & 0xa000) == 0xa000;
            };
            if (stage == 4) {
                if (phase >= 2 ? collidable(112) || collidable(116) : collidable(phase == 1 ? 110 : 100))
                    bossStart_ = clock;
            } else {
                for (unsigned slot = 100; slot < 150; ++slot) {
                    if (!collidable(slot)) continue;
                    bossStart_ = clock;
                    break;
                }
            }
        }
        state_.bossActive = bossStart_.has_value() && clock >= *bossStart_ &&
                            !Read<std::uint16_t>(memory,Gp - 0x53c4);
        if (state_.bossActive) state_.bossFrames = clock - *bossStart_;
        const auto monitor = Read<std::uint32_t>(memory,Gp - 0x6530);
        state_.showHealth = state_.bossActive && (monitor == 2 || monitor == 3);
        // Use the same scroll-to-map conversion as the original PS2 HUD.
        constexpr std::array<unsigned,5> mapScale{440,601,851,471,388};
        const auto scroll = Read<std::uint32_t>(memory,Gp - 0x5a9c);
        state_.stagePosition = std::min<std::uint64_t>(368,std::uint64_t{scroll}*mapScale[stage]/1000);
    }

    void AdvanceDamage() {
        history_[historyIndex_] = std::exchange(damage_,{});
        historyIndex_ = (historyIndex_ + 1) % history_.size();
        state_.laser = {};
        state_.aura = {};
        for (const auto& field : history_) {
            state_.laser.total += field[0].total;
            state_.laser.hits += field[0].hits;
            state_.aura.total += field[1].total;
            state_.aura.hits += field[1].hits;
        }
        state_.hitDetail = hits_;
    }

    // The hosted session owns the sole feed and joins the EE thread before
    // the feed goes out of scope.
    static inline DeathLabelGadgetFeed* active_{};
    PS2Runtime::RecompiledFunction musicDispatch_{};
    State state_{};
    State::Music music_{};
    std::optional<unsigned> bossStart_;
    std::array<unsigned,4> hits_{};
    std::array<State::Damage,2> damage_{};
    std::array<std::array<State::Damage,2>,10> history_{};
    std::size_t historyIndex_{};
};

}
