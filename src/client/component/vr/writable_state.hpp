#pragma once

// These process-lifetime publication buffers are mutated by native hook paths.
// v142 WPO/LTCG has emitted their copies while promoting zero-initialized
// storage into .rdata. Pin only the affected state to an explicitly writable,
// non-executable PE section; object layout, locking and lifetime stay unchanged.
// https://learn.microsoft.com/en-us/cpp/cpp/allocate
#if defined(_MSC_VER)
#pragma section(".vrstate", read, write)
#define H2V_WRITABLE_STATE __declspec(allocate(".vrstate"))
#else
#define H2V_WRITABLE_STATE
#endif
