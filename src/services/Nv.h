#pragma once

#include "common/Types.h"

// ── NV Service Forward Declarations ─────────────────────────
// NV (NVIDIA driver service) handles:
//   nvdrv:     /dev/nvdrv  — control
//   nvdrv#     /dev/nvhost-ctrl, /dev/nvhost-gpu, etc.
//   nvmap:     /dev/nvmap   — memory allocation
//   nvhost-as-gpu:           — GPU address space

void ServiceNv_Init();
void ServiceNv_SetMemory(class Memory* mem);
void ServiceNv_SetGpuFifo(class GPFifo* fifo);
void ServiceNv_SetTracker(class StateTracker* tracker);
