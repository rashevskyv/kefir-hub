#pragma once

#include <switch.h>

typedef enum {
    CodeMemoryUnavailable    = 0,
    CodeMemoryForeignProcess = BIT(0),
    CodeMemorySameProcess    = BIT(0) | BIT(1),
} CodeMemoryCapability;

extern bool g_isApplication;
extern bool g_isAutomaticGameplayRecording;
extern CodeMemoryCapability g_codeMemoryCapability;
extern void*  g_heapAddr;
extern size_t g_heapSize;
extern Handle g_procHandle;

void getIsApplication(void);
void getIsAutomaticGameplayRecording(void);
u64 calculateMaxHeapSize(void);
void setupHbHeap(void);
void getOwnProcessHandle(void);
void getCodeMemoryCapability(void);
Result restoreMainThreadAffinity(void);
void findUsableHeapRange(u64 override_addr, u64 override_size, u64* out_start, u64* out_size);
