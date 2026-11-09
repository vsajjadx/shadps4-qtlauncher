#pragma once

// The lifecycle target deliberately tests IPC without linking the emulator logging backend.
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
