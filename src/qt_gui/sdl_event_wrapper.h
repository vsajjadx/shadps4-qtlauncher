// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <QObject>
#include <SDL3/SDL_events.h>

namespace SdlEventWrapper {

// Pseudo button ids sent in SDLEvent's Input for a click on the DualSense / DualShock 4 touchpad,
// by the part of the pad the finger was on. They never collide with SDL_GamepadButton values.
constexpr int TouchpadLeft = 0x7001;
constexpr int TouchpadCenter = 0x7002;
constexpr int TouchpadRight = 0x7003;

class Wrapper : public QObject {
    Q_OBJECT

public:
    explicit Wrapper(QObject* parent = nullptr);
    ~Wrapper();
    bool ProcessEvent(SDL_Event* event);
    static Wrapper* GetInstance();
    static bool wrapperActive;
    static Wrapper* WrapperInstance;

private:
    float lastTouchX = -1.0f; // x (0..1) of the finger that touched the touchpad last
    int heldTouchpadRegion = 0;

public:

signals:
    void SDLEvent(int Type, int Input, int Value);
    void audioDeviceChanged(bool isAdd);
};

} // namespace SdlEventWrapper
