#pragma once

#include "death_label_damage.h"
#include "ps2_runtime.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cave::daioujou_ps2 {

// Live state from the reviewed SLPS-25233 DEATH.ELF. The overlay never
// modifies guest state and publishes one snapshot per game field.
struct DeathLabelGadgetState {
    enum class Music { None, Boss, StageClear, NameEntry, GameOver };
    enum class BossForm { Main, Jet, TwinHibachi };
    struct Damage {
        unsigned total{},hits{},multiHit{};
    };

    std::uint64_t score{},stageScore{};
    unsigned round{},stage{},doll{},ship{},stock{},bombs{},bombCapacity{};
    std::array<unsigned,2> clearedStages{};
    unsigned shotLevel{},laserLevel{},invincibleFrames{},misses{},bombsFired{};
    unsigned hypers{},hyperCharge{},hyperGain{},hyperTime{},hyperDuration{};
    unsigned bulletSpeed{},bulletInterval{},hyperRank{},combo{},comboTime{},comboCapacity{};
    unsigned bossFrames{},bossHp{},bossMaxHp{};
    std::array<unsigned,4> hitDetail{};
    Damage laser,aura;
    Music music{};
    BossForm form{};
    bool playing{},hyperActive{},bossActive{},bossDefeated{};
};

// Values accumulated by the PC dashboard between guest frames. Keep these
// apart from the live function bindings, which belong to the current runtime.
struct DeathLabelGadgetHistory {
    using State = DeathLabelGadgetState;
    State state_{};
    State::Music music_{};
    std::optional<unsigned> bossStart_;
    std::uint64_t stageStartScore_{};
    std::array<unsigned,2> misses_{};
    std::array<unsigned,4> hits_{};
    std::array<State::Damage,2> damage_{};
    std::array<std::array<State::Damage,2>,10> history_{};
    unsigned historyIndex_{};

    template<class Archive> void ArchiveFields(Archive& ar) {
        auto& s=state_;
        const auto damage=[&](State::Damage& d) { ar.Fields(d.total,d.hits,d.multiHit); };
        ar.Fields(s.score,s.stageScore,s.round,s.stage,s.doll,s.ship,s.stock,s.bombs,s.bombCapacity);
        for (auto& value:s.clearedStages) ar.Field(value);
        ar.Fields(s.shotLevel,s.laserLevel,s.invincibleFrames,s.misses,s.bombsFired,
                  s.hypers,s.hyperCharge,s.hyperGain,s.hyperTime,s.hyperDuration,
                  s.bulletSpeed,s.bulletInterval,s.hyperRank,s.combo,s.comboTime,s.comboCapacity,
                  s.bossFrames,s.bossHp,s.bossMaxHp);
        for (auto& value:s.hitDetail) ar.Field(value);
        damage(s.laser);
        damage(s.aura);
        unsigned music=unsigned(s.music), form=unsigned(s.form), currentMusic=unsigned(music_);
        ar.Fields(music,form,currentMusic,s.playing,s.hyperActive,s.bossActive,s.bossDefeated);
        if (s.round>1 || s.stage>4 || s.doll>2 || s.shotLevel>5 || s.laserLevel>5 ||
            music>unsigned(State::Music::GameOver) || currentMusic>unsigned(State::Music::GameOver) ||
            form>unsigned(State::BossForm::TwinHibachi))
            throw std::invalid_argument("Invalid Death Label dashboard state");
        s.music=State::Music(music);
        s.form=State::BossForm(form);
        music_=State::Music(currentMusic);
        bool started=bossStart_.has_value();
        unsigned first=bossStart_.value_or(0);
        ar.Fields(started,first,stageStartScore_);
        bossStart_=started?std::optional(first):std::nullopt;
        for (auto& value:misses_) ar.Field(value);
        for (auto& value:hits_) ar.Field(value);
        for (auto& strike:damage_) damage(strike);
        for (auto& field:history_) for (auto& strike:field) damage(strike);
        ar.Field(historyIndex_);
        if (historyIndex_>=history_.size()) throw std::invalid_argument("Invalid Death Label damage history");
    }
};

class DeathLabelGadgetFeed : private DeathLabelGadgetHistory {
public:
    using State = DeathLabelGadgetState;

    explicit DeathLabelGadgetFeed(std::function<void(const State&)> restored = {})
        : restored_(std::move(restored)) {}
    DeathLabelGadgetFeed(const DeathLabelGadgetFeed&) = delete;
    DeathLabelGadgetFeed& operator=(const DeathLabelGadgetFeed&) = delete;
    DeathLabelGadgetHistory History() const { return *this; }
    void RestoreHistory(const DeathLabelGadgetHistory& value) noexcept {
        static_cast<DeathLabelGadgetHistory&>(*this)=value;
        if (restored_) restored_(state_);
    }
    void ResetRun() {
        state_ = {};
        music_ = {};
        misses_ = {};
        stageStartScore_ = 0;
        ResetFight();
    }
    class Binding {
    public:
        Binding(DeathLabelGadgetFeed& feed, PS2Runtime& runtime)
            : runtime_(runtime), sound_(runtime.lookupFunction(SoundCommand)),
              miss_(runtime.lookupFunction(PlayerMiss)) {
            if (active_ || !sound_ || sound_ == &Sound || !miss_ || miss_ == &Miss ||
                !runtime_.replaceFunction(SoundCommand, &Sound))
                throw std::runtime_error("Cannot bind Death Label Gadget audio");
            if (!runtime_.replaceFunction(PlayerMiss, &Miss)) {
                runtime_.replaceFunction(SoundCommand, sound_);
                throw std::runtime_error("Cannot bind Death Label Gadget player deaths");
            }
            feed.musicDispatch_ = sound_;
            feed.missDispatch_ = miss_;
            active_ = &feed;
        }
        ~Binding() {
            // The generated program's function table is shared by subsequent
            // runtime instances. Restore it before either owner is destroyed.
            runtime_.replaceFunction(PlayerMiss, miss_);
            runtime_.replaceFunction(SoundCommand, sound_);
            active_ = nullptr;
        }
        Binding(const Binding&) = delete;
        Binding& operator=(const Binding&) = delete;

    private:
        PS2Runtime& runtime_;
        PS2Runtime::RecompiledFunction sound_, miss_;
    };

    State Observe(const std::uint8_t* memory, bool playing, bool paused) {
        if (paused) return state_;
        if (!playing) {
            state_.bossActive = false;
            state_.music = music_;
            damage_ = {};
            return state_;
        }
        const unsigned round = memory[Gp - 0x5830] != 0;
        const unsigned stage = std::min<unsigned>(Read<std::uint16_t>(memory,Gp - 0x5348),4);
        const bool gameplayStopped = Read<std::uint16_t>(memory,Gp - 0x53c4) != 0;
        // The guest writes the next stage before paying out the clear bonuses.
        // Advance the whole overlay when the guest starts gameplay again.
        const bool newRound = !state_.playing || (!gameplayStopped && state_.round != round);
        if (newRound) {
            const auto clears = state_.clearedStages;
            state_ = {};
            state_.clearedStages = clears;
        }
        const bool newStage = newRound || (!gameplayStopped && stage > state_.stage);
        const auto score = std::uint64_t{Read<std::uint16_t>(memory,Gp - 0x5508)} << 32 |
            Read<std::uint32_t>(memory,Gp - 0x54fc);
        state_.score = DecimalScore(score) * 10 + Read<std::uint16_t>(memory,Gp - 0x5514) % 10;
        if (newStage) {
            state_.round = round;
            state_.stage = stage;
            stageStartScore_ = state_.score;
            ResetFight();
        }
        state_.playing = true;
        state_.stageScore = state_.score - std::min(state_.score,stageStartScore_);
        // The fighter select table stores A/B as 0/2 and the dolls as 2/4/6.
        const auto doll = Read<std::uint16_t>(memory,Gp - 0x5334);
        state_.doll = doll == 4 ? 1 : doll == 6 ? 2 : 0;
        state_.ship = Read<std::uint16_t>(memory,Gp - 0x532c) == 2;
        state_.misses = misses_[state_.round];
        state_.stock = std::clamp<int>(Read<std::int16_t>(memory,Gp - 0x53a0),0,20);
        state_.bombCapacity = memory[Player + 0x24];
        state_.bombs = std::min<unsigned>(memory[Player + 0x25],state_.bombCapacity);
        // The original increments this counter only when it spends a bomb
        // in combat. The shared effect timers also run during a Hyper.
        state_.bombsFired = Read<std::uint16_t>(memory,Gp - 0x6004);
        // The firing tables use even indices 0..8 for their five power levels.
        state_.shotLevel = std::min(5u,memory[Player + 0x20] / 2u + 1);
        state_.laserLevel = std::min(5u,memory[Player + 0x22] / 2u + 1);
        state_.invincibleFrames = memory[Player + 0x3f];
        const auto previousCharge = state_.hyperCharge;
        state_.hyperCharge = std::min<unsigned>(Read<std::uint16_t>(memory,Gp - 0x5680),2400);
        if (newStage) state_.hyperGain = 0;
        else if (state_.hyperCharge > previousCharge) state_.hyperGain = state_.hyperCharge - previousCharge;
        state_.hyperActive = Read<std::uint16_t>(memory,Gp - 0x5668) != 0;
        state_.hyperTime = std::max(0,int(Read<std::int16_t>(memory,Gp - 0x5670))) / 2;
        state_.hyperDuration = state_.hyperActive ? std::max(state_.hyperDuration,state_.hyperTime) : 0;
        state_.hypers = std::min<unsigned>(Read<std::uint16_t>(memory,Gp - 0x56a4),5);
        state_.hyperRank = Read<std::uint16_t>(memory,Gp - 0x5678);
        state_.bulletSpeed = memory[Gp - 0x5380];
        state_.bulletInterval = memory[Gp - 0x5390];
        state_.combo = Read<std::uint16_t>(memory,Gp - 0x55c4);
        state_.comboTime = Read<std::uint16_t>(memory,Gp - 0x559c);
        state_.comboCapacity = Read<std::uint16_t>(memory,Gp - 0x5588);
        state_.music = music_;
        if (round == state_.round && stage == state_.stage) AdvanceBoss(memory,gameplayStopped);
        else state_.bossActive = false;
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
    static constexpr std::uint32_t PlayerMiss = 0x134150, PlayerEventReturn = 0x134108;
    static constexpr std::uint32_t PlayerOneEvent = 0x691f00;

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

    static void Miss(std::uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
        auto& feed = *active_;
        // Event 1 is the game's actual life loss, including the last ship.
        // Stage bonuses temporarily spend from the same stock through a different path.
        if (getRegU32(context,31) == PlayerEventReturn && getRegU32(context,28) == Gp &&
            memory[Gp - 0x5cb0] && Read<std::uint32_t>(memory,Gp - 0x5bc8) == PlayerOneEvent)
            ++feed.misses_[memory[Gp - 0x5830] != 0];
        feed.missDispatch_(memory,context,runtime);
    }

    void ResetFight() {
        hits_ = {};
        damage_ = {};
        history_ = {};
        historyIndex_ = 0;
        state_.form = State::BossForm::Main;
        state_.bossFrames = 0;
        state_.bossHp = state_.bossMaxHp = 0;
        state_.bossDefeated = false;
        bossStart_.reset();
    }

    void AdvanceBoss(const std::uint8_t* memory, bool gameplayStopped) {
        const auto stage = state_.stage;
        // Stage five spawns slot 100 for Huang Liu, then 110 for its jet
        // form, and 112 and 116 together for the two Hibachis.
        const auto phase = stage == 4 ? memory[Gp - 0x5d94] : 0;
        const auto form = phase >= 2 ? State::BossForm::TwinHibachi :
                          phase == 1 ? State::BossForm::Jet : State::BossForm::Main;
        if (state_.form != form) {
            state_.form = form;
            state_.bossFrames = 0;
            state_.bossHp = state_.bossMaxHp = 0;
            state_.bossDefeated = false;
            bossStart_.reset();
        }
        const auto clock = Read<std::uint32_t>(memory,Gp - 0x53a8);
        // The original boss scripts consume damage from the hitboxes and
        // refill them. Read the signed total on the owning boss controller.
        struct Encounter { unsigned script,root,hitbox,capacity; };
        constexpr std::array<Encounter,5> encounters{{
            {0x08,101,101,0x16c00}, {0x0b,100,100,0x2bdc0}, {0x09,100,100,0x40c00},
            {0x0c,100,101,0x46000}, {0x12,100,100,0x3f000}
        }};
        const auto& encounter = encounters[stage];
        const auto collidable = [&](unsigned slot) {
            return (Read<std::uint16_t>(memory,EnemyPool + slot * EnemyStride) & 0xa000) == 0xa000;
        };
        const auto hitbox = phase >= 2 ? 112u : phase == 1 ? 110u : encounter.hitbox;
        const auto capacity = phase >= 2 ? 0x4d000u : phase == 1 ? 0x2a000u : encounter.capacity;
        const bool inCombat = collidable(hitbox) || (phase >= 2 && collidable(116));
        if (!state_.bossDefeated && (bossStart_ || inCombat)) {
            const auto root = EnemyPool + encounter.root * EnemyStride;
            constexpr unsigned ControllerPool = 0x690f50, ControllerStride = 0x50, ControllerCount = 48;
            for (unsigned i = 0; i < ControllerCount; ++i) {
                const auto controller = ControllerPool + i * ControllerStride;
                if (!(Read<std::uint16_t>(memory,controller) & 0x8000) ||
                    Read<std::uint16_t>(memory,controller + 4) != encounter.script ||
                    ReadControllerWord(memory,controller + 6) != root) continue;
                const auto hp = static_cast<std::int32_t>(ReadControllerWord(memory,controller + 0x16));
                if (!bossStart_) bossStart_ = clock;
                state_.bossMaxHp = capacity;
                state_.bossHp = std::clamp(hp,0,static_cast<std::int32_t>(capacity));
                state_.bossDefeated = hp <= 0;
                break;
            }
        }
        if (state_.bossDefeated && (stage < 4 || form == State::BossForm::TwinHibachi))
            state_.clearedStages[state_.round] |= 1u << stage;
        // The guest stops gameplay on Game Over as well as after a clear.
        state_.bossActive = bossStart_.has_value() && clock >= *bossStart_ && !state_.bossDefeated && !gameplayStopped;
        if (state_.bossActive) state_.bossFrames = clock - *bossStart_;
    }

    static std::uint32_t ReadControllerWord(const std::uint8_t* memory,std::uint32_t address) {
        return std::uint32_t{Read<std::uint16_t>(memory,address)} << 16 |
            Read<std::uint16_t>(memory,address + 2);
    }

    void AdvanceDamage() {
        history_[historyIndex_] = std::exchange(damage_,{});
        historyIndex_ = (historyIndex_ + 1) % history_.size();
        state_.laser = {};
        state_.aura = {};
        for (const auto& field : history_) {
            state_.laser.total += field[0].total;
            state_.laser.hits += field[0].hits;
            state_.laser.multiHit = std::max(state_.laser.multiHit,field[0].hits);
            state_.aura.total += field[1].total;
            state_.aura.hits += field[1].hits;
            state_.aura.multiHit = std::max(state_.aura.multiHit,field[1].hits);
        }
        state_.hitDetail = hits_;
    }

    // The hosted session owns the sole feed and joins the EE thread before
    // the feed goes out of scope.
    static inline DeathLabelGadgetFeed* active_{};
    std::function<void(const State&)> restored_;
    PS2Runtime::RecompiledFunction musicDispatch_{};
    PS2Runtime::RecompiledFunction missDispatch_{};
};

}
