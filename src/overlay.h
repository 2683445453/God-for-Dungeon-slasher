#pragma once
#include "protocol.h"
namespace ds {
// Requires MinHook to be initialized. Creates and queues, but does not enable hooks.
bool InstallOverlay(Shared* state);
void RemoveOverlayHooks(); // Only before hooks are enabled / in an isolated test.
void DrawPanel(Shared* state); // Also exercised by the software-renderer smoke test.
}
