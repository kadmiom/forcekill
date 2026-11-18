#pragma once

namespace autostart {

// True when the per-user Run value exists and points at this executable.
bool IsEnabled();

// Adds or removes the per-user Run value for the running executable.
bool SetEnabled(bool enable);

}  // namespace autostart
