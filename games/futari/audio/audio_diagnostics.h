#pragma once
#include <memory>
namespace rex::ui { class Window; class WindowInputListener; }
std::unique_ptr<rex::ui::WindowInputListener> FutariCreateAudioMarker(rex::ui::Window*);
