#pragma once
#include <cstddef>
#include <atomic>

class CentralHeap;

extern std::atomic<CentralHeap*> g_central;
extern thread_local CentralHeap* t_central;

void SetupCentral(void* shm_base, std::size_t bytes);
CentralHeap* getCentral();
