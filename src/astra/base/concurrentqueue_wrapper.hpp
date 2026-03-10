// ==============================================================================
// ConcurrentQueue Wrapper - Fix BLOCK_SIZE conflict with linux/fs.h
// ==============================================================================
// When io_uring is enabled, liburing.h includes linux/fs.h which defines
// BLOCK_SIZE as a macro (value: 1024). This conflicts with concurrentqueue.h
// which has a static const size_t BLOCK_SIZE = 32. We undefine BLOCK_SIZE
// here to avoid the conflict.
// ==============================================================================

#pragma once

#ifdef BLOCK_SIZE
#undef BLOCK_SIZE
#endif

#include <blockingconcurrentqueue.h>
#include <concurrentqueue.h>
