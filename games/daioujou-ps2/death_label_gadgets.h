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

    void Bind(PS2Runtime& runtime) { feed_.Bind(runtime); }

    void Observe(const std::uint8_t* memory, bool playing, bool menu) {
        auto state = feed_.Observe(memory,playing,menu);
        std::lock_guard lock(mutex_);
        state_ = state;
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
                               std::array<float,4> scanout = {},float verticalFill = 1) {
            quads.push_back({sprite,(anchor+(x-origin)*scale)/1920,(32+(y-32)*scale)/1080,
                w*scale/1920,h*scale/1080,fill,scanout,verticalFill});
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
            draw(ScorePanel,68,32);
            number(state.score,Big0,548,86,42);
            draw(dolls[state.doll],68,288);
            centered(state.ship ? ShipB : ShipA,166,468);
            centered(state.ship ? StockB : StockA,227,350);
            number(state.stock,Small0,289,340,17);
            centered(BombIcon,227,402);
            number(state.bombs,Small0,253,398,17);
            centered(Slash,262,410);
            number(state.bombCapacity,Small0,288,398,17);
            draw(HyperPanel,324,288);
            if (state.hyperCharge != 0)
                place(HyperFill,333,372,230,22,state.hyperCharge/2400.0f);
            draw(HyperBack,333,371);
            number(state.hyperCharge*100/2400,Small0,555,340,17);
            centered(state.hyperActive ? HyperActive : HyperStandby,452,502);
            if (state.hyperActive)
                number((state.hyperTime + 59)/60,Small0,445,493,17);
            else
                centered(static_cast<Sprite>(HyperStock0 + state.hypers),495,507);
            draw(RoundPanel,68,544);
            centered(state.round ? Round2 : Round1,113,612,0.5f);
            for (unsigned i = 0; i < 5; ++i)
                centered(static_cast<Sprite>(Stage1+i),174+27.0f*i,594,i == state.stage ? 1.0f : 0.62f);
            const float stageProgress = (state.stage+state.stagePosition/368.0f)/5;
            place(HyperFill,169,621,135,5,stageProgress);
            draw(DamagePanel,324,544);
            const auto damage = [&](const DeathLabelGadgetState::Damage& strike,float y) {
                const unsigned average = strike.hits ? strike.total/strike.hits : 0;
                number(std::min(9999u,average),Small0,419,y,15);
                number(std::min(99u,strike.hits),Small0,499,y,15);
            };
            damage(state.laser,593);
            damage(state.aura,637);
            draw(RankPanel,68,672);
            number(state.bulletSpeed,Small0,221,718,16);
            number(state.bulletInterval,Small0,221,761,16);
            number(state.hyperRank,Small0,281,741,16);
            draw(ComboPanel,324,672);
            number(state.combo,Small0,437,721,16,1.0f);
            number(state.comboTime,Small0,537,722,14,0.86f);
            draw(ComboFrame,490,763);
            if (state.comboCapacity != 0) {
                const auto fill = std::min(1.0f,float(state.comboTime)/state.comboCapacity);
                place(ComboFill,492,765,44,140,1,{},fill);
            }
            for (unsigned i = 0; i < state.hitDetail.size(); ++i)
                number(std::min(9999u,state.hitDetail[i]),Small0,477,770+34.0f*i,15);
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
            // Mirror the PS2 HUD's actual boss meter. It includes refill and
            // scripted transitions, which are not represented by actor HP.
            if (state.showHealth)
                place(BossPanel,1374,688,450,13,1,{194.0f/960,33.0f/720,554.0f/960,9.0f/720});
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
