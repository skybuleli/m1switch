#pragma once

#include "common/Types.h"

// ── NV Service Forward Declarations ─────────────────────────
// NV (NVIDIA driver service) handles:
//   nvdrv:        /dev/nvdrv — device management
//   nvmap:        /dev/nvmap — GPU memory allocation
//   nvhost-gpu:   /dev/nvhost-gpu — 3D engine command submission
//   nvhost-ctrl:  /dev/nvhost-ctrl — syncpoint management

void ServiceNv_Init();
void ServiceNv_SetMemory(class Memory* mem);
void ServiceNv_SetTracker(class StateTracker* tracker);
// Future: void ServiceNv_SetGpu(class Gpu* gpu);

// ── NvMap 查询 API (C linkage) ─────────────────────────────
#ifdef __cplusplus
extern "C" {
#endif

struct NvMapEntry;
NvMapEntry* NvMap_LookupByHandle(u32 handle);
u64 NvMap_GetIOVA(u32 handle);
u32 NvMap_Alloc(u32 size, u32 align);

#ifdef __cplusplus
}
#endif