#pragma once

#include "backends/ps2/native_host.h"
#include "common/death_label_gadgets.h"
#include "common/death_label_host.h"
#include "death_label_gadget_state.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace cave::daioujou_ps2 {
class DeathLabelGadgets {
public:
    using Overlay = ps2::GsOverlay;
    using Sprite = death_label::Sprite;

    explicit DeathLabelGadgets(const DeathLabelScreen& screen) : screen_(screen) {}

    std::shared_ptr<Overlay> Load(const std::filesystem::path& path) {
        using namespace death_label;
        std::ifstream stream(path,std::ios::binary);
        const auto read = [&](auto& value) {
            if (!stream.read(reinterpret_cast<char*>(&value),sizeof value))
                throw std::runtime_error("Incomplete Death Label Gadget cache");
        };
        std::uint32_t magic{},count{};
        read(magic);
        read(count);
        if (magic != GadgetMagic || count != SpriteCount)
            throw std::runtime_error("Incompatible Death Label Gadget cache");
        auto overlay = std::make_shared<Overlay>();
        overlay->images.reserve(SpriteCount);
        std::uint64_t total = sizeof magic + sizeof count;
        for (auto& size : sizes_) {
            read(size);
            const std::uint64_t bytes = std::uint64_t{size.width} * size.height * 4;
            total += sizeof size + bytes;
            if (!size.width || !size.height || size.width > 4096 || size.height > 4096 ||
                total > GadgetPixelBudget)
                throw std::runtime_error("Invalid Death Label Gadget image");
            auto& image = overlay->images.emplace_back();
            image.width = size.width;
            image.height = size.height;
            image.pixels.resize(static_cast<std::size_t>(bytes));
            if (!stream.read(reinterpret_cast<char*>(image.pixels.data()),image.pixels.size()))
                throw std::runtime_error("Incomplete Death Label Gadget image");
        }
        if (stream.peek() != std::ifstream::traits_type::eof())
            throw std::runtime_error("Unexpected data in Death Label Gadget cache");
        overlay->quads = [this](auto& quads) { Draw(quads); };
        return overlay;
    }

    DeathLabelGadgetFeed& Feed() { return feed_; }

    void Observe(const std::uint8_t* memory, bool playing, bool menu) {
        auto state = feed_.Observe(memory,playing,menu);
        std::lock_guard lock(mutex_);
        state_ = state;
    }

    void ResetRun() {
        feed_.ResetRun();
        std::lock_guard lock(mutex_);
        state_ = {};
    }

private:
    void Draw(std::vector<Overlay::Quad>& quads) {
        using namespace death_label;
        DeathLabelGadgetState state;
        {
            std::lock_guard lock(mutex_);
            state = state_;
        }
        if (!state.playing) return;
        float scale=1, anchor=68, origin=68;
        const auto place = [&](Sprite sprite,float x,float y,float w,float h,float fill = 1,
                               float verticalFill = 1) {
            quads.push_back({sprite,(anchor+(x-origin)*scale)/1920,(32+(y-32)*scale)/1080,
                w*scale/1920,h*scale/1080,fill,verticalFill});
        };
        const auto draw = [&](Sprite sprite,float x,float y,float scale = 1) {
            const auto [w,h] = sizes_[sprite];
            place(sprite,x,y,w*scale,h*scale);
        };
        const auto centered = [&](Sprite sprite,float x,float y,float scale = 1) {
            const auto [w,h] = sizes_[sprite];
            draw(sprite,x-w*scale/2,y-h*scale/2,scale);
        };
        const auto number = [&](std::uint64_t value,Sprite zero,float right,float y,float advance,
                                float size = 1,unsigned digits = 1) {
            std::array<char,20> chars{};
            const auto end = std::to_chars(chars.data(),chars.data()+chars.size(),value).ptr;
            for (auto p = end; p != chars.data() || digits > 0;) {
                const auto digit = static_cast<Sprite>(zero + (p != chars.data() ? *--p - '0' : 0));
                right -= advance;
                centered(digit,right+advance/2,y+sizes_[zero].height*size/2,size);
                if (digits != 0) --digits;
            }
        };
        const float left = 960.0f + screen_.offsetX - screen_.width/2.0f;
        const float right = left + screen_.width;
        scale = std::min(1.0f,(left - 84)/504);
        if (scale >= 0.4f) {
            constexpr std::array dolls{ShotiaPanel,LeinyanPanel,ExyPanel};
            constexpr std::array dollNames{ShotiaName,LeinyanName,ExyName};
            draw(ScorePanel,68,32);
            number(state.score,Big0,548,80,42);
            draw(ScoreDetails,88,185);
            number(state.stageScore,Small0,546,182,18);
            draw(dolls[state.doll],68,288);
            centered(state.ship ? FighterBName : FighterAName,82,379);
            centered(dollNames[state.doll],102,379);
            centered(state.ship ? ShipB : ShipA,166,468);
            centered(state.ship ? StockB : StockA,227,350);
            number(state.stock,Small0,289,340,17);
            centered(BombIcon,227,402);
            number(state.bombs,Small0,253,398,17);
            centered(Slash,262,410);
            number(state.bombCapacity,Small0,288,398,17);
            const auto weaponPower = [&](Sprite label,float y,unsigned level) {
                draw(label,200,y);
                for (unsigned i = 0; i < level; ++i) draw(PowerSegment,244+11.0f*i,y+23);
            };
            weaponPower(ShotLevel,425,state.shotLevel);
            weaponPower(LaserLevel,478,state.laserLevel);
            if (state.invincibleFrames) centered(Invincible,137,519);
            draw(HyperPanel,324,288);
            draw(HyperBack,333,371);
            if (state.hyperCharge != 0)
                place(HyperFill,337,373,224,20,state.hyperCharge/2400.0f);
            number(state.hyperCharge*100/2400,Small0,538,340,17);
            centered(HyperPercent,550,352);
            if (state.hyperGain) {
                const auto permille = state.hyperGain*1000/2400;
                number(permille/10,Small0,519,429,15);
                centered(TimeDecimal,522,444,0.8f);
                number(permille%10,Small0,540,429,15);
                centered(HyperPercent,550,440);
            }
            centered(state.hyperActive ? HyperActive : HyperStandby,452,502);
            if (state.hyperActive) {
                place(HyperTimerEmpty,467,478,84,42);
                if (state.hyperDuration)
                    place(HyperTimerFill,469,479,80,40,float(state.hyperTime)/state.hyperDuration);
                number((state.hyperTime + 59)/60,Small0,522,499,17);
            } else {
                centered(static_cast<Sprite>(HyperStock0 + state.hypers),495,507);
            }
            draw(RoundPanel,68,544);
            centered(state.round ? Round2 : Round1,114,615,0.5f);
            centered(StageLabel,179,582,0.45f);
            for (unsigned i = 0; i < 5; ++i) {
                const bool reached = i == state.stage || (state.clearedStages[state.round] & (1u << i));
                centered(static_cast<Sprite>(Stage1+i),184+30.0f*i,608,reached ? 0.7f : 0.5f);
            }
            number(state.misses,Small0,228,631,17);
            number(state.bombsFired,Small0,306,631,17);
            draw(DamagePanel,324,544);
            const auto damage = [&](const DeathLabelGadgetState::Damage& strike,float y) {
                const unsigned average = strike.hits ? strike.total/strike.hits : 0;
                number(std::min(9999u,average),Small0,419,y,15);
                number(std::min(99u,strike.multiHit),Small0,499,y,15);
            };
            damage(state.laser,593);
            damage(state.aura,637);
            draw(BattlePanel,68,672);
            centered(Round1,196,729,0.68f);
            centered(Round2,265,729,0.68f);
            for (unsigned i = 0; i < 5; ++i) {
                const auto y = 780.0f + 54.0f*i;
                centered(StageLabel,106,y,0.65f);
                centered(static_cast<Sprite>(Stage1+i),145,y,0.70f);
                for (unsigned round = 0; round < state.clearedStages.size(); ++round) {
                    const auto cleared = state.clearedStages[round] & (1u << i);
                    place(cleared ? PowerSegment : HyperBack,177+69.0f*round,y-4,38,8);
                }
            }
            draw(RankPanel,324,672);
            number(state.bulletSpeed,Small0,477,718,16);
            number(state.bulletInterval,Small0,477,761,16);
            number(state.hyperRank,Small0,537,741,16);
            draw(ComboPanel,324,800);
            number(state.combo,Small0,437,849,16,1.0f);
            number(state.comboTime,Small0,537,850,14,0.86f);
            draw(ComboFrame,490,891);
            if (state.comboCapacity != 0) {
                const auto fill = std::min(1.0f,float(state.comboTime)/state.comboCapacity);
                place(ComboFill,492,893,44,140,1,fill);
            }
            for (unsigned i = 0; i < state.hitDetail.size(); ++i)
                number(std::min(9999u,state.hitDetail[i]),Small0,477,898+34.0f*i,15);
        }
        scale = std::min(1.0f,(1836 - right)/504);
        anchor = 1852 - 504*scale;
        origin = 1348;
        if (scale >= 0.4f) {
            constexpr std::array portraits{Stage1Boss,Stage2Boss,Stage3Boss,Stage4Boss,Stage5Boss};
            constexpr std::array names{Stage1Name,Stage2Name,Stage3Name,Stage4Name,Stage5Name};
            using BossForm = DeathLabelGadgetState::BossForm;
            draw(StagePanel,1348,32);
            centered(state.round ? Round2 : Round1,1600,100);
            centered(StageLabel,1551,153);
            centered(static_cast<Sprite>(Stage1 + state.stage),1653,153);
            centered(BossBackdrop,1600,378);
            if (state.form == BossForm::TwinHibachi) {
                centered(Hibachi,1504,375,0.54f);
                centered(Hibachi,1696,375,0.54f);
                centered(HibachiName,1600,612);
            } else {
                centered(state.form == BossForm::Jet ? Stage5Jet : portraits[state.stage],1600,378,0.86f);
                centered(names[state.stage],1600,612);
            }
            draw(BossPanel,1365,657);
            if (state.bossMaxHp) {
                if (state.bossHp)
                    place(BossHealthFill,1367,679,466,28,float(state.bossHp)/state.bossMaxHp);
                number(state.bossHp,Small0,1703,648,15);
                centered(Slash,1717,657,0.8f);
                number(state.bossMaxHp,Small0,1824,648,15);
            }
            if (state.bossDefeated) centered(BossDefeated,1600,786,0.85f);
            if (state.bossActive || state.bossFrames != 0) {
                const auto centiseconds=std::uint64_t{state.bossFrames}*1001/600;
                draw(TimeCaption,1372,725);
                number(centiseconds/6000,Small0,1750,723,14,1,2);
                centered(TimeColon,1754,731);
                number(centiseconds/100%60,Small0,1787,723,14,1,2);
                centered(TimeDecimal,1791,737);
                number(centiseconds%100,Small0,1824,723,14,1,2);
            }
            draw(MusicPanel,1348,928);
            constexpr std::array trackLabels{MusicBoss,MusicClear,MusicEntry,MusicGameOver};
            const auto trackIndex = static_cast<unsigned>(state.music);
            if (trackIndex != 0) {
                centered(MusicTitle,1446,987,0.82f);
                centered(trackLabels[trackIndex-1],1601,991);
                centered(MusicIcon,1784,999);
                centered(MusicNote,1784,999,0.8f);
            }
        }
    }

    DeathLabelScreen screen_;
    std::array<death_label::GadgetSpriteHeader,death_label::SpriteCount> sizes_{};
    DeathLabelGadgetFeed feed_;
    std::mutex mutex_;
    DeathLabelGadgetState state_;
};
}
