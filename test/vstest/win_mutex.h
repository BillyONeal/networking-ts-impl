#pragma once
#include <Windows.h>

struct win_mutex {
  void lock() noexcept { AcquireSRWLockExclusive(&srw); }
  void unlock() noexcept { ReleaseSRWLockExclusive(&srw); }
  win_mutex() = default;
  SRWLOCK srw = SRWLOCK_INIT;
};
