#pragma once
#include "launcher/game_exit.h"
#include <d2d1.h>
#include <chrono>
#include <optional>
#include <utility>

namespace cave::launcher {
enum class MenuKey { None, Up, Down, Left, Right, Confirm, Back };
enum class BigPictureMode { Auto, Horizontal, Vertical };

inline BigPictureMode ReadBigPictureMode(const Ini& ini) {
    const auto* value=ini.Get("collection","big_picture_layout");
    if(value && value->text=="vertical")return BigPictureMode::Vertical;
    if(value && value->text=="horizontal")return BigPictureMode::Horizontal;
    return BigPictureMode::Auto;
}

struct BigPictureLayout {
    static constexpr float kCoverSlotAspect = 0.82f;
    float width{}, height{};
    size_t count{}, selected{};
    BigPictureMode mode{};

    bool Portrait() const { return mode==BigPictureMode::Vertical || (mode==BigPictureMode::Auto && height>width); }
    float Scale() const { return std::clamp(std::min(width, height) / 620.f, 0.73f, 2.f); }
    float Margin() const { return 30 * Scale(); }
    float Gap() const { return 22 * Scale(); }
    float CoverPadding() const { return 4 * Scale(); }
    size_t Columns() const {
        return Portrait() ? 1 : width / std::max(1.f, height) < 1.48f ? 3 : 4;
    }
    size_t PageSize() const { return Columns(); }
    size_t Page() const { return selected / PageSize(); }
    size_t PageCount() const { return (count + PageSize() - 1) / PageSize(); }
    size_t First() const { return Page() * PageSize(); }
    size_t Last() const { return std::min(count, First() + PageSize()); }
    float CardHeight() const {
        const auto available = height - 220 * Scale();
        const auto column = (width - 2 * Margin() - Gap() * (Columns() - 1)) / Columns();
        return std::max(1.f, std::min(available, column / kCoverSlotAspect));
    }
    D2D1_RECT_F Card(size_t position) const {
        const auto height = CardHeight();
        const auto width = height * kCoverSlotAspect;
        const auto step = position - First();
        const auto left = (this->width - (width + Gap()) * Columns() + Gap()) / 2 + (width + Gap()) * step;
        const auto spare = std::max(0.f, this->height - 220 * Scale() - height);
        const auto top = 88 * Scale() + spare / 2;
        return {left, top, left + width, top + height};
    }
    D2D1_RECT_F Cover(size_t position, float aspect) const {
        const auto slot = Card(position);
        const auto padding = CoverPadding();
        const auto height = std::max(0.f, std::min(slot.bottom - slot.top - 2 * padding,
                                                  (slot.right - slot.left - 2 * padding) / aspect));
        const auto width = height * aspect;
        const auto left = (slot.left + slot.right - width) / 2;
        const auto top = (slot.top + slot.bottom - height) / 2;
        return {left, top, left + width, top + height};
    }
    D2D1_RECT_F DesktopButton() const {
        return {width - Margin() - 157 * Scale(), 22 * Scale(), width - Margin(), 57 * Scale()};
    }
    D2D1_RECT_F MenuItem(size_t item) const {
        const auto scale = Scale();
        const auto left = (width - std::min(width - 2 * Margin(), 340 * scale)) / 2;
        const auto top = height / 2 + (int(item) - 1) * 49 * scale;
        return {left, top, width - left, top + 43 * scale};
    }
    size_t Move(MenuKey key) const {
        if (!count) return 0;
        if (Portrait()) {
            if (key==MenuKey::Up && selected) return selected-1;
            if (key==MenuKey::Down) return std::min(selected+1,count-1);
            return selected;
        }
        if (key == MenuKey::Left && selected) return selected - 1;
        if (key == MenuKey::Right) return std::min(selected + 1, count - 1);
        if (key == MenuKey::Up && selected >= Columns()) return selected - Columns();
        if (key == MenuKey::Down && (selected / Columns() + 1) * Columns() < count)
            return std::min(selected + Columns(), count - 1);
        return selected;
    }
};

class MenuController {
public:
    explicit MenuController(const fs::path& root) : devices_(root) {}

    void Reset() {
        for (auto& pad : pads_) {
            pad.previous = MenuKey::None;
            pad.wait_for_release = true;
        }
    }
    void ResetGameExit() {
        for(auto& pad:pads_)pad.exit_armed=false;
    }
    bool PollGameExit(const GameExitShortcuts& shortcuts) {
        UpdatePads(Clock::now());
        bool requested=false;
        for(auto& pad:pads_) {
            const bool pressed=ExitPressed(pad,shortcuts);
            if(!pressed)pad.exit_armed=true;
            else if(std::exchange(pad.exit_armed,false))requested=true;
        }
        return requested;
    }
    MenuKey Poll() {
        const auto now=Clock::now();
        UpdatePads(now);
        auto action = MenuKey::None;
        for (auto& pad : pads_) {
            const auto key = ReadKey(pad.map, devices_.Read(pad.joystick.get()));
            const auto pressed = PollKey(pad, key, now);
            if (action == MenuKey::None) action = pressed;
        }
        return action;
    }

private:
    using Clock=std::chrono::steady_clock;
    struct Pad {
        SDL_JoystickID id;
        controls::InputDevices::Joystick joystick;
        controls::PadMap map;
        std::string guid;
        bool exit_armed{};
        Clock::time_point next_repeat{};
        MenuKey previous{MenuKey::None};
        bool wait_for_release{true};
    };

    bool ExitPressed(const Pad& pad,const GameExitShortcuts& shortcuts) {
        const auto input=devices_.Read(pad.joystick.get());
        if(const auto binding=shortcuts.controller_bindings.find(pad.guid); binding!=shortcuts.controller_bindings.end()) {
            const auto& sources=binding->second;
            return controls::PadSourceActive(sources[0],input,false) &&
                   (sources[1].empty() || controls::PadSourceActive(sources[1],input,false));
        }
        const auto mapping=shortcuts.controller_mappings.find(pad.guid);
        const auto& map=mapping==shortcuts.controller_mappings.end()?pad.map:mapping->second;
        return !shortcuts.buttons.empty() && std::ranges::all_of(shortcuts.buttons,[&](std::string_view name) {
            const auto source=map.find(std::string(name));
            return source!=map.end() && controls::PadSourceActive(source->second,input,
                name=="lefttrigger" || name=="righttrigger");
        });
    }

    void UpdatePads(Clock::time_point now) {
        devices_.Update();
        if(now>=next_scan_) {
            next_scan_=now+std::chrono::milliseconds(350);
            ScanDevices();
        }
    }

    static MenuKey PollKey(Pad& pad, MenuKey key, Clock::time_point now) {
        using namespace std::chrono;
        if (key == MenuKey::None) {
            pad.wait_for_release = false;
            pad.previous = key;
            return key;
        }
        if (pad.wait_for_release) return MenuKey::None;
        if (key != pad.previous) {
            pad.previous = key;
            pad.next_repeat = now + milliseconds(330);
            return key;
        }
        if (key != MenuKey::Confirm && key != MenuKey::Back && now >= pad.next_repeat) {
            pad.next_repeat = now + milliseconds(110);
            return key;
        }
        return MenuKey::None;
    }

    void ScanDevices() {
        const auto available = devices_.List();
        std::erase_if(pads_, [&](const Pad& pad) {
            return std::ranges::find(available, pad.id, &controls::InputDevices::Device::id) == available.end();
        });
        for (const auto& device : available) {
            if (std::ranges::find(pads_, device.id, &Pad::id) != pads_.end()) continue;
            if (auto joystick = devices_.Open(device.id))
                pads_.push_back({device.id,std::move(joystick),devices_.Defaults(device.id),device.guid});
        }
    }

    static MenuKey ReadKey(const controls::PadMap& map, const controls::PadSnapshot& input) {
        const auto active = [&](const char* name, std::string_view fallback = {}) {
            const auto binding = map.find(name);
            return controls::PadSourceActive(binding == map.end() ? fallback : std::string_view(binding->second), input, false);
        };
        // Generic USB arcade encoders frequently expose a hat or two axes and
        // the first two buttons without an SDL gamepad mapping.
        const auto axis = [&](const char* name, char sign, std::string_view fallback) {
            const auto binding = map.find(name);
            if (binding == map.end()) return map.empty() && controls::PadSourceActive(fallback, input, false);
            if (binding->second.empty() || binding->second.front() != 'a') return false;
            return controls::PadSourceActive(sign + binding->second, input, false);
        };
        if (active("a", "b0")) return MenuKey::Confirm;
        if (active("b", "b1")) return MenuKey::Back;
        if (active("dpup", "h0.1") || axis("lefty", '-', "-a1")) return MenuKey::Up;
        if (active("dpdown", "h0.4") || axis("lefty", '+', "+a1")) return MenuKey::Down;
        if (active("dpleft", "h0.8") || axis("leftx", '-', "-a0")) return MenuKey::Left;
        if (active("dpright", "h0.2") || axis("leftx", '+', "+a0")) return MenuKey::Right;
        return MenuKey::None;
    }

    controls::InputDevices devices_;
    std::vector<Pad> pads_;
    Clock::time_point next_scan_{};
};
}
