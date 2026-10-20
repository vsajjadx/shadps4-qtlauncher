// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "sdl_event_wrapper.h"

using namespace SdlEventWrapper;

Wrapper* Wrapper::WrapperInstance = nullptr;
bool Wrapper::wrapperActive = false;

Wrapper::Wrapper(QObject* parent) : QObject(parent) {}

Wrapper* Wrapper::GetInstance() {
    if (WrapperInstance == nullptr) {
        WrapperInstance = new Wrapper();
    }
    return WrapperInstance;
}

static int TouchpadRegionFromX(float x) {
    if (x < 0.0f) {
        return TouchpadCenter; // no finger position known: treat the click as the middle
    }
    if (x < 1.0f / 3.0f) {
        return TouchpadLeft;
    }
    if (x > 2.0f / 3.0f) {
        return TouchpadRight;
    }
    return TouchpadCenter;
}

bool Wrapper::ProcessEvent(SDL_Event* event) {
    switch (event->type) {
    case SDL_EVENT_WINDOW_RESTORED:
        return false;
    case SDL_EVENT_WINDOW_EXPOSED:
        return false;
    case SDL_EVENT_GAMEPAD_ADDED:
        emit SDLEvent(SDL_EVENT_GAMEPAD_ADDED, 0, 0);
        return false;
    case SDL_EVENT_GAMEPAD_REMOVED:
        emit SDLEvent(SDL_EVENT_GAMEPAD_REMOVED, 0, 0);
        return false;
    case SDL_EVENT_QUIT:
        emit SDLEvent(SDL_EVENT_QUIT, 0, 0);
        return true;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
        lastTouchX = event->gtouchpad.x;
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (event->gbutton.button == SDL_GAMEPAD_BUTTON_TOUCHPAD) {
            // A touchpad click is reported as the left / middle / right part of the pad that
            // the finger was on, the way the emulator reads it in game.
            heldTouchpadRegion = TouchpadRegionFromX(lastTouchX);
            emit SDLEvent(SDL_EVENT_GAMEPAD_BUTTON_DOWN, heldTouchpadRegion, 0);
            return true;
        }
        emit SDLEvent(SDL_EVENT_GAMEPAD_BUTTON_DOWN, event->gbutton.button, 0);
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (event->gbutton.button == SDL_GAMEPAD_BUTTON_TOUCHPAD) {
            emit SDLEvent(SDL_EVENT_GAMEPAD_BUTTON_UP,
                          heldTouchpadRegion != 0 ? heldTouchpadRegion : TouchpadCenter, 0);
            heldTouchpadRegion = 0;
            return true;
        }
        emit SDLEvent(SDL_EVENT_GAMEPAD_BUTTON_UP, event->gbutton.button, 0);
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        emit SDLEvent(SDL_EVENT_GAMEPAD_AXIS_MOTION, event->gaxis.axis, event->gaxis.value);
        return true;
    case SDL_EVENT_AUDIO_DEVICE_ADDED:
        if (event->adevice.recording == 0)
            emit audioDeviceChanged(true);
        return true;
    case SDL_EVENT_AUDIO_DEVICE_REMOVED:
        if (event->adevice.recording == 0)
            emit audioDeviceChanged(false);
        return true;
    // block all other SDL events while wrapper is active
    default:
        return true;
    }
}
Wrapper::~Wrapper() {}
