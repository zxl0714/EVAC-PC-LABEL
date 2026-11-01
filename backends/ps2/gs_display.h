#pragma once
#include "gs_device.h"
#include "gs_scanout.h"
#include <memory>
#include <string>
#include <stdexcept>

namespace cave::ps2 {
class GsWindowClosed : public std::runtime_error {
public: GsWindowClosed():std::runtime_error("PS2 graphics window closed"){}
};
enum class GsPresentTransfer {Automatic,Sampled};
// The window thread owns WSI and the GS device. The game sends ordered commands
// through the queue; it never drives this object from the EE thread.
class GsDisplay {
public:
    GsDisplay(unsigned width,unsigned height,const std::wstring& title,bool vsync,
              GsPresentTransfer transfer=GsPresentTransfer::Automatic);
    ~GsDisplay();
    GsDisplay(const GsDisplay&)=delete;
    GsDisplay& operator=(const GsDisplay&)=delete;
    GsDevice& Device();
    bool Alive();
    bool Focused() const;
    void SetKeyHandler(std::function<void(unsigned,bool)> handler);
    void ToggleFullscreen();
    // Gate commands for the next game frame, rather than waiting only after
    // those commands have already been sent to the GPU.
    void WaitPreviousFrame();
    bool Present(const GsFrame& request);
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
