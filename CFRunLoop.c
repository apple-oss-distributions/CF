/*
 * Copyright (c) 2012 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

/*	CFRunLoop.c
	Copyright (c) 1998-2011, Apple Inc. All rights reserved.
	Responsibility: Tony Parker
*/

#include <CoreFoundation/CFRunLoop.h>
#include <CoreFoundation/CFSet.h>
#include <CoreFoundation/CFBag.h>
#include "CFInternal.h"
#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <pthread.h>
#include <dispatch/dispatch.h>
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
#include <CoreFoundation/CFUserNotification.h>
#include <mach/mach.h>
#include <mach/clock_types.h>
#include <mach/clock.h>
#include <unistd.h>
#include <dlfcn.h>
extern mach_port_t _dispatch_get_main_queue_port_4CF(void);
extern void _dispatch_main_queue_callback_4CF(mach_msg_header_t *msg);
#else
#include <process.h>
__private_extern__ LONGLONG __CFTSRToFiletime(int64_t tsr);
DISPATCH_EXPORT HANDLE _dispatch_get_main_queue_handle_4CF(void);
DISPATCH_EXPORT void _dispatch_main_queue_callback_4CF(void);
#endif
#include <Block.h>

static int _LogCFRunLoop = 0;

// for conservative arithmetic safety, such that (TIMER_DATE_LIMIT + TIMER_INTERVAL_LIMIT + kCFAbsoluteTimeIntervalSince1970) * 10^9 < 2^63
#define TIMER_DATE_LIMIT	4039289856.0
#define TIMER_INTERVAL_LIMIT	504911232.0

#define HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY 0

#if DEPLOYMENT_TARGET_WINDOWS

static pthread_t kNilPthreadT = { nil, nil };
#define pthreadPointer(a) a.p

#else

static pthread_t kNilPthreadT = (pthread_t)0;
#define pthreadPointer(a) a
#define lockCount(a) a
#endif


CF_EXPORT bool CFDictionaryGetKeyIfPresent(CFDictionaryRef dict, const void *key, const void **actualkey);

// In order to reuse most of the code across Mach and Windows v1 RunLoopSources, we define a
// simple abstraction layer spanning Mach ports and Windows HANDLES
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED

__private_extern__ uint32_t __CFGetProcessPortCount(void) {
    ipc_info_space_t info;
    ipc_info_name_array_t table = 0;
    mach_msg_type_number_t tableCount = 0;
    ipc_info_tree_name_array_t tree = 0;
    mach_msg_type_number_t treeCount = 0;
    
    kern_return_t ret = mach_port_space_info(mach_task_self(), &info, &table, &tableCount, &tree, &treeCount);
    if (ret != KERN_SUCCESS) {
        return (uint32_t)0;
    }
    if (table != NULL) {
        ret = vm_deallocate(mach_task_self(), (vm_address_t)table, tableCount * sizeof(*table));
    }
    if (tree != NULL) {
        ret = vm_deallocate(mach_task_self(), (vm_address_t)tree, treeCount * sizeof(*tree));
    }
    return (uint32_t)tableCount;
}

__private_extern__ CFArrayRef __CFStopAllThreads(void) {
    CFMutableArrayRef suspended_list = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, NULL);
    mach_port_t my_task = mach_task_self();
    mach_port_t my_thread = mach_thread_self();
    thread_act_array_t thr_list = 0;
    mach_msg_type_number_t thr_cnt = 0;

    // really, should loop doing the stopping until no more threads get added to the list N times in a row
    kern_return_t ret = task_threads(my_task, &thr_list, &thr_cnt);
    if (ret == KERN_SUCCESS) {
        for (CFIndex idx = 0; idx < thr_cnt; idx++) {
            thread_act_t thread = thr_list[idx];
            if (thread == my_thread) continue;
            if (CFArrayContainsValue(suspended_list, CFRangeMake(0, CFArrayGetCount(suspended_list)), (const void *)(uintptr_t)thread)) continue;
            ret = thread_suspend(thread);
            if (ret == KERN_SUCCESS) {
                CFArrayAppendValue(suspended_list, (const void *)(uintptr_t)thread);
            } else {
                mach_port_deallocate(my_task, thread);
            }
        }
        vm_deallocate(my_task, (vm_address_t)thr_list, sizeof(thread_t) * thr_cnt);
    }
    mach_port_deallocate(my_task, my_thread);
    return suspended_list;
}

__private_extern__ void __CFRestartAllThreads(CFArrayRef threads) {
    for (CFIndex idx = 0; idx < CFArrayGetCount(threads); idx++) {
        thread_act_t thread = (thread_act_t)(uintptr_t)CFArrayGetValueAtIndex(threads, idx);
        kern_return_t ret = thread_resume(thread);
        if (ret != KERN_SUCCESS) HALT;
        mach_port_deallocate(mach_task_self(), thread);
    }
}

static uint32_t __CF_last_warned_port_count = 0;

static void foo() __attribute__((unused));
static void foo() {
    uint32_t pcnt = __CFGetProcessPortCount();
    if (__CF_last_warned_port_count + 1000 < pcnt) {
        CFArrayRef threads = __CFStopAllThreads();


// do stuff here
CFOptionFlags responseFlags = 0;
SInt32 result = CFUserNotificationDisplayAlert(0.0, kCFUserNotificationCautionAlertLevel, NULL, NULL, NULL, CFSTR("High Mach Port Usage"), CFSTR("This application is using a lot of Mach ports."), CFSTR("Default"), CFSTR("Altern"), CFSTR("Other b"), &responseFlags);
if (0 != result) {
    CFLog(3, CFSTR("ERROR"));
} else {
    switch (responseFlags) {
    case kCFUserNotificationDefaultResponse: CFLog(3, CFSTR("DefaultR")); break;
    case kCFUserNotificationAlternateResponse: CFLog(3, CFSTR("AltR")); break;
    case kCFUserNotificationOtherResponse: CFLog(3, CFSTR("OtherR")); break;
    case kCFUserNotificationCancelResponse: CFLog(3, CFSTR("CancelR")); break;
    }
}


        __CFRestartAllThreads(threads);
        CFRelease(threads);
        __CF_last_warned_port_count = pcnt;
    }
}


typedef mach_port_t __CFPort;
#define CFPORT_NULL MACH_PORT_NULL
typedef mach_port_t __CFPortSet;

static void __THE_SYSTEM_HAS_NO_PORTS_AVAILABLE__(kern_return_t ret) __attribute__((noinline));
static void __THE_SYSTEM_HAS_NO_PORTS_AVAILABLE__(kern_return_t ret) { HALT; };

static __CFPort __CFPortAllocate(void) {
    __CFPort result = CFPORT_NULL;
    kern_return_t ret;
    ret = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &result);
    if (KERN_SUCCESS == ret) {
        ret = mach_port_insert_right(mach_task_self(), result, result, MACH_MSG_TYPE_MAKE_SEND);
    } else {
        __THE_SYSTEM_HAS_NO_PORTS_AVAILABLE__(ret);
    }
    if (KERN_SUCCESS == ret) {
        mach_port_limits_t limits;
        limits.mpl_qlimit = 1;
        ret = mach_port_set_attributes(mach_task_self(), result, MACH_PORT_LIMITS_INFO, (mach_port_info_t)&limits, MACH_PORT_LIMITS_INFO_COUNT);
    }
    if (KERN_SUCCESS != ret) mach_port_destroy(mach_task_self(), result);
    return (KERN_SUCCESS == ret) ? result : CFPORT_NULL;
}

CF_INLINE void __CFPortFree(__CFPort port) {
    mach_port_destroy(mach_task_self(), port);
}

static void __THE_SYSTEM_HAS_NO_PORT_SETS_AVAILABLE__(kern_return_t ret) __attribute__((noinline));
static void __THE_SYSTEM_HAS_NO_PORT_SETS_AVAILABLE__(kern_return_t ret) { HALT; };

CF_INLINE __CFPortSet __CFPortSetAllocate(void) {
    __CFPortSet result;
    kern_return_t ret = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &result);
    if (KERN_SUCCESS != ret) { __THE_SYSTEM_HAS_NO_PORT_SETS_AVAILABLE__(ret); }
    return (KERN_SUCCESS == ret) ? result : CFPORT_NULL;
}

CF_INLINE Boolean __CFPortSetInsert(__CFPort port, __CFPortSet portSet) {
    if (MACH_PORT_NULL == port) {
        return false;
    }
    kern_return_t ret = mach_port_insert_member(mach_task_self(), port, portSet);
    return (KERN_SUCCESS == ret);
}

CF_INLINE Boolean __CFPortSetRemove(__CFPort port, __CFPortSet portSet) {
    if (MACH_PORT_NULL == port) {
        return false;
    }
    kern_return_t ret = mach_port_extract_member(mach_task_self(), port, portSet);
    return (KERN_SUCCESS == ret);
}

CF_INLINE void __CFPortSetFree(__CFPortSet portSet) {
    kern_return_t ret;
    mach_port_name_array_t array;
    mach_msg_type_number_t idx, number;

    ret = mach_port_get_set_status(mach_task_self(), portSet, &array, &number);
    if (KERN_SUCCESS == ret) {
        for (idx = 0; idx < number; idx++) {
            mach_port_extract_member(mach_task_self(), array[idx], portSet);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)array, number * sizeof(mach_port_name_t));
    }
    mach_port_destroy(mach_task_self(), portSet);
}

#elif DEPLOYMENT_TARGET_WINDOWS

typedef HANDLE __CFPort;
#define CFPORT_NULL NULL

// A simple dynamic array of HANDLEs, which grows to a high-water mark
typedef struct ___CFPortSet {
    uint16_t	used;
    uint16_t	size;
    HANDLE	*handles;
    CFSpinLock_t lock;		// insert and remove must be thread safe, like the Mach calls
} *__CFPortSet;

CF_INLINE __CFPort __CFPortAllocate(void) {
    return CreateEventA(NULL, true, false, NULL);
}

CF_INLINE void __CFPortFree(__CFPort port) {
    CloseHandle(port);
}

static __CFPortSet __CFPortSetAllocate(void) {
    __CFPortSet result = (__CFPortSet)CFAllocatorAllocate(kCFAllocatorSystemDefault, sizeof(struct ___CFPortSet), 0);
    result->used = 0;
    result->size = 4;
    result->handles = (HANDLE *)CFAllocatorAllocate(kCFAllocatorSystemDefault, result->size * sizeof(HANDLE), 0);
    CF_SPINLOCK_INIT_FOR_STRUCTS(result->lock);
    return result;
}

static void __CFPortSetFree(__CFPortSet portSet) {
    CFAllocatorDeallocate(kCFAllocatorSystemDefault, portSet->handles);
    CFAllocatorDeallocate(kCFAllocatorSystemDefault, portSet);
}

// Returns portBuf if ports fit in that space, else returns another ptr that must be freed
static __CFPort *__CFPortSetGetPorts(__CFPortSet portSet, __CFPort *portBuf, uint32_t bufSize, uint32_t *portsUsed) {
    __CFSpinLock(&(portSet->lock));
    __CFPort *result = portBuf;
    if (bufSize < portSet->used)
	result = (__CFPort *)CFAllocatorAllocate(kCFAllocatorSystemDefault, portSet->used * sizeof(HANDLE), 0);
    if (portSet->used > 1) {
	// rotate the ports to vaguely simulate round-robin behaviour
	uint16_t lastPort = portSet->used - 1;
	HANDLE swapHandle = portSet->handles[0];
	memmove(portSet->handles, &portSet->handles[1], lastPort * sizeof(HANDLE));
	portSet->handles[lastPort] = swapHandle;
    }
    memmove(result, portSet->handles, portSet->used * sizeof(HANDLE));
    *portsUsed = portSet->used;
    __CFSpinUnlock(&(portSet->lock));
    return result;
}

static Boolean __CFPortSetInsert(__CFPort port, __CFPortSet portSet) {
    if (NULL == port) {
        return false;
    }
    __CFSpinLock(&(portSet->lock));
    if (portSet->used >= portSet->size) {
        portSet->size += 4;
        portSet->handles = (HANDLE *)CFAllocatorReallocate(kCFAllocatorSystemDefault, portSet->handles, portSet->size * sizeof(HANDLE), 0);
    }
    if (portSet->used >= MAXIMUM_WAIT_OBJECTS) {
        CFLog(kCFLogLevelWarning, CFSTR("*** More than MAXIMUM_WAIT_OBJECTS (%d) ports add to a port set.  The last ones will be ignored."), MAXIMUM_WAIT_OBJECTS);
    }
    portSet->handles[portSet->used++] = port;
    __CFSpinUnlock(&(portSet->lock));
    return true;
}

static Boolean __CFPortSetRemove(__CFPort port, __CFPortSet portSet) {
    int i, j;
    if (NULL == port) {
        return false;
    }
    __CFSpinLock(&(portSet->lock));
    for (i = 0; i < portSet->used; i++) {
        if (portSet->handles[i] == port) {
            for (j = i+1; j < portSet->used; j++) {
                portSet->handles[j-1] = portSet->handles[j];
            }
            portSet->used--;
            __CFSpinUnlock(&(portSet->lock));
            return true;
        }
    }
    __CFSpinUnlock(&(portSet->lock));
    return false;
}

#endif

#if !defined(__MACTYPES__) && !defined(_OS_OSTYPES_H)
#if defined(__BIG_ENDIAN__)
typedef	struct UnsignedWide {
    UInt32		hi;
    UInt32		lo;
} UnsignedWide;
#elif defined(__LITTLE_ENDIAN__)
typedef	struct UnsignedWide {
    UInt32		lo;
    UInt32		hi;
} UnsignedWide;
#endif
typedef UnsignedWide		AbsoluteTime;
#endif

#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
extern mach_port_name_t mk_timer_create(void);
extern kern_return_t mk_timer_destroy(mach_port_name_t name);
extern kern_return_t mk_timer_arm(mach_port_name_t name, AbsoluteTime expire_time);
extern kern_return_t mk_timer_cancel(mach_port_name_t name, AbsoluteTime *result_time);

CF_INLINE AbsoluteTime __CFUInt64ToAbsoluteTime(int64_t x) {
    AbsoluteTime a;
    a.hi = x >> 32;
    a.lo = x & (int64_t)0xFFFFFFFF;
    return a;
}

static uint32_t __CFSendTrivialMachMessage(mach_port_t port, uint32_t msg_id, CFOptionFlags options, uint32_t timeout) {
    kern_return_t result;
    mach_msg_header_t header;
    header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    header.msgh_size = sizeof(mach_msg_header_t);
    header.msgh_remote_port = port;
    header.msgh_local_port = MACH_PORT_NULL;
    header.msgh_id = msg_id;
    result = mach_msg(&header, MACH_SEND_MSG|options, header.msgh_size, 0, MACH_PORT_NULL, timeout, MACH_PORT_NULL);
    if (result == MACH_SEND_TIMED_OUT) mach_msg_destroy(&header);
    return result;
}
#endif

/* unlock a run loop and modes before doing callouts/sleeping */
/* never try to take the run loop lock with a mode locked */
/* be very careful of common subexpression elimination and compacting code, particular across locks and unlocks! */
/* run loop mode structures should never be deallocated, even if they become empty */

static CFTypeID __kCFRunLoopModeTypeID = _kCFRuntimeNotATypeID;
static CFTypeID __kCFRunLoopTypeID = _kCFRuntimeNotATypeID;
static CFTypeID __kCFRunLoopSourceTypeID = _kCFRuntimeNotATypeID;
static CFTypeID __kCFRunLoopObserverTypeID = _kCFRuntimeNotATypeID;
static CFTypeID __kCFRunLoopTimerTypeID = _kCFRuntimeNotATypeID;

typedef struct __CFRunLoopMode *CFRunLoopModeRef;

struct __CFRunLoopMode {
    CFRuntimeBase _base;
    pthread_mutex_t _lock;	/* must have the run loop locked before locking this */
    CFStringRef _name;
    Boolean _stopped;
    char _padding[3];
    CFMutableSetRef _sources0;
    CFMutableSetRef _sources1;
    CFMutableArrayRef _observers;
    CFMutableArrayRef _timers;
    CFMutableDictionaryRef _portToV1SourceMap;
    __CFPortSet _portSet;
    CFIndex _observerMask;
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
    mach_port_t _timerPort;
#endif
#if DEPLOYMENT_TARGET_WINDOWS
    HANDLE _timerPort;
    DWORD _msgQMask;
    void (*_msgPump)(void);
#endif
};

CF_INLINE void __CFRunLoopModeLock(CFRunLoopModeRef rlm) {
    pthread_mutex_lock(&(rlm->_lock));
//    CFLog(6, CFSTR("__CFRunLoopModeLock locked %p"), rlm);
}

CF_INLINE void __CFRunLoopModeUnlock(CFRunLoopModeRef rlm) {
//    CFLog(6, CFSTR("__CFRunLoopModeLock unlocking %p"), rlm);
    pthread_mutex_unlock(&(rlm->_lock));
}

static Boolean __CFRunLoopModeEqual(CFTypeRef cf1, CFTypeRef cf2) {
    CFRunLoopModeRef rlm1 = (CFRunLoopModeRef)cf1;
    CFRunLoopModeRef rlm2 = (CFRunLoopModeRef)cf2;
    return CFEqual(rlm1->_name, rlm2->_name);
}

static CFHashCode __CFRunLoopModeHash(CFTypeRef cf) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)cf;
    return CFHash(rlm->_name);
}

static CFStringRef __CFRunLoopModeCopyDescription(CFTypeRef cf) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)cf;
    CFMutableStringRef result;
    result = CFStringCreateMutable(kCFAllocatorSystemDefault, 0);
    CFStringAppendFormat(result, NULL, CFSTR("<CFRunLoopMode %p [%p]>{name = %@, "), rlm, CFGetAllocator(rlm), rlm->_name);
    CFStringAppendFormat(result, NULL, CFSTR("port set = %p, "), rlm->_portSet);
    CFStringAppendFormat(result, NULL, CFSTR("timer port = %p, "), rlm->_timerPort);
#if DEPLOYMENT_TARGET_WINDOWS
    CFStringAppendFormat(result, NULL, CFSTR("MSGQ mask = %p, "), rlm->_msgQMask);
#endif
    CFStringAppendFormat(result, NULL, CFSTR("\n\tsources0 = %@,\n\tsources1 = %@,\n\tobservers = %@,\n\ttimers = %@\n},\n"), rlm->_sources0, rlm->_sources1, rlm->_observers, rlm->_timers);
    return result;
}

static void __CFRunLoopModeDeallocate(CFTypeRef cf) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)cf;
    if (NULL != rlm->_sources0) CFRelease(rlm->_sources0);
    if (NULL != rlm->_sources1) CFRelease(rlm->_sources1);
    if (NULL != rlm->_observers) CFRelease(rlm->_observers);
    if (NULL != rlm->_timers) CFRelease(rlm->_timers);
    if (NULL != rlm->_portToV1SourceMap) CFRelease(rlm->_portToV1SourceMap);
    CFRelease(rlm->_name);
    __CFPortSetFree(rlm->_portSet);
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
    if (MACH_PORT_NULL != rlm->_timerPort) mk_timer_destroy(rlm->_timerPort);
#elif DEPLOYMENT_TARGET_WINDOWS
    if (NULL != rlm->_timerPort) CloseHandle(rlm->_timerPort);
#endif
    pthread_mutex_destroy(&rlm->_lock);
    memset((char *)cf + sizeof(CFRuntimeBase), 0x7C, sizeof(struct __CFRunLoopMode) - sizeof(CFRuntimeBase));
}

struct _block_item {
    struct _block_item *_next;
    CFTypeRef _mode;	// CFString or CFSet
    void (^_block)(void);
};

struct __CFRunLoop {
    CFRuntimeBase _base;
    pthread_mutex_t _lock;			/* locked for accessing mode list */
    __CFPort _wakeUpPort;			// used for CFRunLoopWakeUp 
    Boolean _ignoreWakeUps;
    volatile uint32_t *_stopped;
    pthread_t _pthread;
    uint32_t _winthread;
    CFMutableSetRef _commonModes;
    CFMutableSetRef _commonModeItems;
    CFRunLoopModeRef _currentMode;
    CFMutableSetRef _modes;
    struct _block_item *_blocks_head;
    struct _block_item *_blocks_tail;
    CFTypeRef _counterpart;
};

#if DEPLOYMENT_TARGET_WINDOWS
static Boolean __CFRunLoopDoSource1(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFRunLoopSourceRef rls);
#endif

/* Bit 0 of the base reserved bits is used for stopped state */
/* Bit 1 of the base reserved bits is used for sleeping state */
/* Bit 2 of the base reserved bits is used for deallocating state */

CF_INLINE Boolean __CFRunLoopIsStopped(CFRunLoopRef rl) {
    return (rl->_stopped && rl->_stopped[2]) ? true : false;
}

CF_INLINE void __CFRunLoopSetStopped(CFRunLoopRef rl) {
    if (!rl->_stopped) {
        rl->_stopped = (uint32_t volatile *)CFAllocatorAllocate(kCFAllocatorSystemDefault, 4 * sizeof(uint32_t), 0);
        rl->_stopped[0] = 0x4346524C;
        rl->_stopped[1] = 0x4346524C; // 'CFRL'
        rl->_stopped[2] = 0x00000000; // here the value is stored
        rl->_stopped[3] = 0x4346524C;
    }
    if (rl->_stopped) rl->_stopped[2] = 0x53544F50;	// 'STOP'
}

CF_INLINE void __CFRunLoopUnsetStopped(CFRunLoopRef rl) {
    if (rl->_stopped) rl->_stopped[2] = 0x0;
}

CF_INLINE Boolean __CFRunLoopIsSleeping(CFRunLoopRef rl) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 1, 1);
}

CF_INLINE void __CFRunLoopSetSleeping(CFRunLoopRef rl) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 1, 1, 1);
}

CF_INLINE void __CFRunLoopUnsetSleeping(CFRunLoopRef rl) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 1, 1, 0);
}

CF_INLINE Boolean __CFRunLoopIsDeallocating(CFRunLoopRef rl) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 2, 2);
}

CF_INLINE void __CFRunLoopSetDeallocating(CFRunLoopRef rl) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rl)->_cfinfo[CF_INFO_BITS], 2, 2, 1);
}

CF_INLINE void __CFRunLoopLock(CFRunLoopRef rl) {
    pthread_mutex_lock(&(((CFRunLoopRef)rl)->_lock));
//    CFLog(6, CFSTR("__CFRunLoopLock locked %p"), rl);
}

CF_INLINE void __CFRunLoopUnlock(CFRunLoopRef rl) {
//    CFLog(6, CFSTR("__CFRunLoopLock unlocking %p"), rl);
    pthread_mutex_unlock(&(((CFRunLoopRef)rl)->_lock));
}

static CFStringRef __CFRunLoopCopyDescription(CFTypeRef cf) {
    CFRunLoopRef rl = (CFRunLoopRef)cf;
    CFMutableStringRef result;
    result = CFStringCreateMutable(kCFAllocatorSystemDefault, 0);
#if DEPLOYMENT_TARGET_WINDOWS
    CFStringAppendFormat(result, NULL, CFSTR("<CFRunLoop %p [%p]>{wakeup port = 0x%x, stopped = %s,\ncurrent mode = %@,\n"), cf, CFGetAllocator(cf), rl->_wakeUpPort, (rl->_stopped && *(rl->_stopped)) ? "true" : "false", rl->_currentMode ? rl->_currentMode->_name : CFSTR("(none)"));
#else
    CFStringAppendFormat(result, NULL, CFSTR("<CFRunLoop %p [%p]>{wakeup port = 0x%x, stopped = %s,\ncurrent mode = %@,\n"), cf, CFGetAllocator(cf), rl->_wakeUpPort, (rl->_stopped && (rl->_stopped[2] == 0x53544F50)) ? "true" : "false", rl->_currentMode ? rl->_currentMode->_name : CFSTR("(none)"));
#endif
    CFStringAppendFormat(result, NULL, CFSTR("common modes = %@,\ncommon mode items = %@,\nmodes = %@}\n"), rl->_commonModes, rl->_commonModeItems, rl->_modes);
    return result;
}

__private_extern__ void __CFRunLoopDump() { // __private_extern__ to keep the compiler from discarding it
    CFShow(CFCopyDescription(CFRunLoopGetCurrent()));
}

CF_INLINE void __CFRunLoopLockInit(pthread_mutex_t *lock) {
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
    int32_t mret = pthread_mutex_init(lock, &mattr);
    pthread_mutexattr_destroy(&mattr);
    if (0 != mret) {
    }
}

/* call with rl locked */
static CFRunLoopModeRef __CFRunLoopFindMode(CFRunLoopRef rl, CFStringRef modeName, Boolean create) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    struct __CFRunLoopMode srlm;
    memset(&srlm, 0, sizeof(srlm));
    srlm._base._cfisa = __CFISAForTypeID(__kCFRunLoopModeTypeID);
    _CFRuntimeSetInstanceTypeID(&srlm, __kCFRunLoopModeTypeID);
    srlm._name = modeName;
    rlm = (CFRunLoopModeRef)CFSetGetValue(rl->_modes, &srlm);
    if (NULL != rlm) {
	__CFRunLoopModeLock(rlm);
	return rlm;
    }
    if (!create) {
	return NULL;
    }
    rlm = (CFRunLoopModeRef)_CFRuntimeCreateInstance(kCFAllocatorSystemDefault, __kCFRunLoopModeTypeID, sizeof(struct __CFRunLoopMode) - sizeof(CFRuntimeBase), NULL);
    if (NULL == rlm) {
	return NULL;
    }
    __CFRunLoopLockInit(&rlm->_lock);
    rlm->_name = CFStringCreateCopy(kCFAllocatorSystemDefault, modeName);
    rlm->_stopped = false;
    rlm->_portToV1SourceMap = NULL;
    rlm->_sources0 = NULL;
    rlm->_sources1 = NULL;
    rlm->_observers = NULL;
    rlm->_timers = NULL;
    rlm->_observerMask = 0;
    rlm->_portSet = __CFPortSetAllocate();
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
    rlm->_timerPort = mk_timer_create();
#elif DEPLOYMENT_TARGET_WINDOWS
    // We use a manual reset timer because it is possible that we will WaitForMultipleObjectsEx on the timer port but not service it on that run loop iteration. The event is reset when we handle the timers.
    rlm->_timerPort = CreateWaitableTimer(NULL, TRUE, NULL);
#endif
    if (!__CFPortSetInsert(rlm->_timerPort, rlm->_portSet)) HALT;
    if (!__CFPortSetInsert(rl->_wakeUpPort, rlm->_portSet)) HALT;
#if DEPLOYMENT_TARGET_WINDOWS    
    rlm->_msgQMask = 0;
    rlm->_msgPump = NULL;
#endif
    CFSetAddValue(rl->_modes, rlm);
    CFRelease(rlm);
    __CFRunLoopModeLock(rlm);	/* return mode locked */
    return rlm;
}


// expects rl and rlm locked
static Boolean __CFRunLoopModeIsEmpty(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFRunLoopModeRef previousMode) {
    CHECK_FOR_FORK();
    if (NULL == rlm) return true;
#if DEPLOYMENT_TARGET_WINDOWS
    if (0 != rlm->_msgQMask) return false;
#endif
    Boolean libdispatchQSafe = pthread_main_np() && ((HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY && NULL == previousMode) || (!HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY && 0 == _CFGetTSD(__CFTSDKeyIsInGCDMainQ)));
    if (libdispatchQSafe && (CFRunLoopGetMain() == rl) && CFSetContainsValue(rl->_commonModes, rlm->_name)) return false; // represents the libdispatch main queue
    if (NULL != rlm->_sources0 && 0 < CFSetGetCount(rlm->_sources0)) return false;
    if (NULL != rlm->_sources1 && 0 < CFSetGetCount(rlm->_sources1)) return false;
    if (NULL != rlm->_timers && 0 < CFArrayGetCount(rlm->_timers)) return false;
    struct _block_item *item = rl->_blocks_head;
    while (item) {
        struct _block_item *curr = item;
        item = item->_next;
        Boolean doit = false;
        if (CFStringGetTypeID() == CFGetTypeID(curr->_mode)) {
            doit = CFEqual(curr->_mode, rlm->_name) || (CFEqual(curr->_mode, kCFRunLoopCommonModes) && CFSetContainsValue(rl->_commonModes, rlm->_name));
        } else {
            doit = CFSetContainsValue((CFSetRef)curr->_mode, rlm->_name) || (CFSetContainsValue((CFSetRef)curr->_mode, kCFRunLoopCommonModes) && CFSetContainsValue(rl->_commonModes, rlm->_name));
        }
        if (doit) return false;
    }
    return true;
}

#if DEPLOYMENT_TARGET_WINDOWS

uint32_t _CFRunLoopGetWindowsMessageQueueMask(CFRunLoopRef rl, CFStringRef modeName) {
    if (modeName == kCFRunLoopCommonModes) {
	CFLog(kCFLogLevelError, CFSTR("_CFRunLoopGetWindowsMessageQueueMask: kCFRunLoopCommonModes unsupported"));
	HALT;
    }
    DWORD result = 0;
    __CFRunLoopLock(rl);
    CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
    if (rlm) {
	result = rlm->_msgQMask;
	__CFRunLoopModeUnlock(rlm);
    }
    __CFRunLoopUnlock(rl);
    return (uint32_t)result;
}

void _CFRunLoopSetWindowsMessageQueueMask(CFRunLoopRef rl, uint32_t mask, CFStringRef modeName) {
    if (modeName == kCFRunLoopCommonModes) {
	CFLog(kCFLogLevelError, CFSTR("_CFRunLoopSetWindowsMessageQueueMask: kCFRunLoopCommonModes unsupported"));
	HALT;
    }
    __CFRunLoopLock(rl);
    CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, true);
    rlm->_msgQMask = (DWORD)mask;
    __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
}

uint32_t _CFRunLoopGetWindowsThreadID(CFRunLoopRef rl) {
    return rl->_winthread;
}

CFWindowsMessageQueueHandler _CFRunLoopGetWindowsMessageQueueHandler(CFRunLoopRef rl, CFStringRef modeName) {
    if (modeName == kCFRunLoopCommonModes) {
	CFLog(kCFLogLevelError, CFSTR("_CFRunLoopGetWindowsMessageQueueMask: kCFRunLoopCommonModes unsupported"));
	HALT;
    }
    if (rl != CFRunLoopGetCurrent()) {
	CFLog(kCFLogLevelError, CFSTR("_CFRunLoopGetWindowsMessageQueueHandler: run loop parameter must be the current run loop"));
	HALT;
    }
    void (*result)(void) = NULL;
    __CFRunLoopLock(rl);
    CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
    if (rlm) {
	result = rlm->_msgPump;
	__CFRunLoopModeUnlock(rlm);
    }
    __CFRunLoopUnlock(rl);
    return result;
}

void _CFRunLoopSetWindowsMessageQueueHandler(CFRunLoopRef rl, CFStringRef modeName, CFWindowsMessageQueueHandler func) {
    if (modeName == kCFRunLoopCommonModes) {
	CFLog(kCFLogLevelError, CFSTR("_CFRunLoopGetWindowsMessageQueueMask: kCFRunLoopCommonModes unsupported"));
	HALT;
    }
    if (rl != CFRunLoopGetCurrent()) {
	CFLog(kCFLogLevelError, CFSTR("_CFRunLoopGetWindowsMessageQueueHandler: run loop parameter must be the current run loop"));
	HALT;
    }
    __CFRunLoopLock(rl);
    CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, true);
    rlm->_msgPump = func;
    __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
}

#endif

/* Bit 3 in the base reserved bits is used for invalid state in run loop objects */

CF_INLINE Boolean __CFIsValid(const void *cf) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)cf)->_cfinfo[CF_INFO_BITS], 3, 3);
}

CF_INLINE void __CFSetValid(void *cf) {
    __CFBitfieldSetValue(((CFRuntimeBase *)cf)->_cfinfo[CF_INFO_BITS], 3, 3, 1);
}

CF_INLINE void __CFUnsetValid(void *cf) {
    __CFBitfieldSetValue(((CFRuntimeBase *)cf)->_cfinfo[CF_INFO_BITS], 3, 3, 0);
}

struct __CFRunLoopSource {
    CFRuntimeBase _base;
    uint32_t _bits;
    pthread_mutex_t _lock;
    CFIndex _order;			/* immutable */
    CFMutableBagRef _runLoops;
    union {
	CFRunLoopSourceContext version0;	/* immutable, except invalidation */
        CFRunLoopSourceContext1 version1;	/* immutable, except invalidation */
    } _context;
};

/* Bit 1 of the base reserved bits is used for signalled state */

CF_INLINE Boolean __CFRunLoopSourceIsSignaled(CFRunLoopSourceRef rls) {
    return (Boolean)__CFBitfieldGetValue(rls->_bits, 1, 1);
}

CF_INLINE void __CFRunLoopSourceSetSignaled(CFRunLoopSourceRef rls) {
    __CFBitfieldSetValue(rls->_bits, 1, 1, 1);
}

CF_INLINE void __CFRunLoopSourceUnsetSignaled(CFRunLoopSourceRef rls) {
    __CFBitfieldSetValue(rls->_bits, 1, 1, 0);
}

CF_INLINE void __CFRunLoopSourceLock(CFRunLoopSourceRef rls) {
    pthread_mutex_lock(&(rls->_lock));
//    CFLog(6, CFSTR("__CFRunLoopSourceLock locked %p"), rls);
}

CF_INLINE void __CFRunLoopSourceUnlock(CFRunLoopSourceRef rls) {
//    CFLog(6, CFSTR("__CFRunLoopSourceLock unlocking %p"), rls);
    pthread_mutex_unlock(&(rls->_lock));
}


struct __CFRunLoopObserver {
    CFRuntimeBase _base;
    pthread_mutex_t _lock;
    CFRunLoopRef _runLoop;
    CFIndex _rlCount;
    CFOptionFlags _activities;		/* immutable */
    CFIndex _order;			/* immutable */
    CFRunLoopObserverCallBack _callout;	/* immutable */
    CFRunLoopObserverContext _context;	/* immutable, except invalidation */
};

/* Bit 0 of the base reserved bits is used for firing state */
/* Bit 1 of the base reserved bits is used for repeats state */

CF_INLINE Boolean __CFRunLoopObserverIsFiring(CFRunLoopObserverRef rlo) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 0, 0);
}

CF_INLINE void __CFRunLoopObserverSetFiring(CFRunLoopObserverRef rlo) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 0, 0, 1);
}

CF_INLINE void __CFRunLoopObserverUnsetFiring(CFRunLoopObserverRef rlo) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 0, 0, 0);
}

CF_INLINE Boolean __CFRunLoopObserverRepeats(CFRunLoopObserverRef rlo) {
    return (Boolean)__CFBitfieldGetValue(((const CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 1, 1);
}

CF_INLINE void __CFRunLoopObserverSetRepeats(CFRunLoopObserverRef rlo) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 1, 1, 1);
}

CF_INLINE void __CFRunLoopObserverUnsetRepeats(CFRunLoopObserverRef rlo) {
    __CFBitfieldSetValue(((CFRuntimeBase *)rlo)->_cfinfo[CF_INFO_BITS], 1, 1, 0);
}

CF_INLINE void __CFRunLoopObserverLock(CFRunLoopObserverRef rlo) {
    pthread_mutex_lock(&(rlo->_lock));
//    CFLog(6, CFSTR("__CFRunLoopObserverLock locked %p"), rlo);
}

CF_INLINE void __CFRunLoopObserverUnlock(CFRunLoopObserverRef rlo) {
//    CFLog(6, CFSTR("__CFRunLoopObserverLock unlocking %p"), rlo);
    pthread_mutex_unlock(&(rlo->_lock));
}

static void __CFRunLoopObserverSchedule(CFRunLoopObserverRef rlo, CFRunLoopRef rl, CFRunLoopModeRef rlm) {
    __CFRunLoopObserverLock(rlo);
    if (0 == rlo->_rlCount) {
	rlo->_runLoop = rl;
    }
    rlo->_rlCount++;
    __CFRunLoopObserverUnlock(rlo);
}

static void __CFRunLoopObserverCancel(CFRunLoopObserverRef rlo, CFRunLoopRef rl, CFRunLoopModeRef rlm) {
    __CFRunLoopObserverLock(rlo);
    rlo->_rlCount--;
    if (0 == rlo->_rlCount) {
	rlo->_runLoop = NULL;
    }
    __CFRunLoopObserverUnlock(rlo);
}

struct __CFRunLoopTimer {
    CFRuntimeBase _base;
    uint16_t _bits;
    pthread_mutex_t _lock;
    CFRunLoopRef _runLoop;
    CFMutableSetRef _rlModes;
    CFAbsoluteTime _nextFireDate;
    CFTimeInterval _interval;		/* immutable */
    int64_t _fireTSR;			/* TSR units */
    CFIndex _order;			/* immutable */
    CFRunLoopTimerCallBack _callout;	/* immutable */
    CFRunLoopTimerContext _context;	/* immutable, except invalidation */
};

/* Bit 0 of the base reserved bits is used for firing state */
/* Bit 1 of the base reserved bits is used for fired-during-callout state */

CF_INLINE Boolean __CFRunLoopTimerIsFiring(CFRunLoopTimerRef rlt) {
    return (Boolean)__CFBitfieldGetValue(rlt->_bits, 0, 0);
}

CF_INLINE void __CFRunLoopTimerSetFiring(CFRunLoopTimerRef rlt) {
    __CFBitfieldSetValue(rlt->_bits, 0, 0, 1);
}

CF_INLINE void __CFRunLoopTimerUnsetFiring(CFRunLoopTimerRef rlt) {
    __CFBitfieldSetValue(rlt->_bits, 0, 0, 0);
}

CF_INLINE Boolean __CFRunLoopTimerIsDeallocating(CFRunLoopTimerRef rlt) {
    return (Boolean)__CFBitfieldGetValue(rlt->_bits, 2, 2);
}

CF_INLINE void __CFRunLoopTimerSetDeallocating(CFRunLoopTimerRef rlt) {
    __CFBitfieldSetValue(rlt->_bits, 2, 2, 1);
}

CF_INLINE void __CFRunLoopTimerLock(CFRunLoopTimerRef rlt) {
    pthread_mutex_lock(&(rlt->_lock));
//    CFLog(6, CFSTR("__CFRunLoopTimerLock locked %p"), rlt);
}

CF_INLINE void __CFRunLoopTimerUnlock(CFRunLoopTimerRef rlt) {
//    CFLog(6, CFSTR("__CFRunLoopTimerLock unlocking %p"), rlt);
    pthread_mutex_unlock(&(rlt->_lock));
}

static CFSpinLock_t __CFRLTFireTSRLock = CFSpinLockInit;

CF_INLINE void __CFRunLoopTimerFireTSRLock(void) {
    __CFSpinLock(&__CFRLTFireTSRLock);
}

CF_INLINE void __CFRunLoopTimerFireTSRUnlock(void) {
    __CFSpinUnlock(&__CFRLTFireTSRLock);
}

#if DEPLOYMENT_TARGET_WINDOWS

struct _collectTimersContext {
    CFMutableArrayRef results;
    int64_t cutoffTSR;
};

static void __CFRunLoopCollectTimers(const void *value, void *ctx) {
    CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)value;
    struct _collectTimersContext *context = (struct _collectTimersContext *)ctx;
    if (rlt->_fireTSR <= context->cutoffTSR) {
        if (NULL == context->results)
            context->results = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeArrayCallBacks);
        CFArrayAppendValue(context->results, rlt);
    }
}

// RunLoop and RunLoopMode must be locked
static void __CFRunLoopTimersToFireRecursive(CFRunLoopRef rl, CFRunLoopModeRef rlm, struct _collectTimersContext *ctxt) {
    __CFRunLoopTimerFireTSRLock();
    if (NULL != rlm->_timers && 0 < CFArrayGetCount(rlm->_timers)) {
        CFArrayApplyFunction(rlm->_timers, CFRangeMake(0, CFArrayGetCount(rlm->_timers)),  __CFRunLoopCollectTimers, ctxt);
    }
    __CFRunLoopTimerFireTSRUnlock();
}

// RunLoop and RunLoopMode must be locked
static CFArrayRef __CFRunLoopTimersToFire(CFRunLoopRef rl, CFRunLoopModeRef rlm) {
    struct _collectTimersContext ctxt = {NULL, mach_absolute_time()};
    __CFRunLoopTimersToFireRecursive(rl, rlm, &ctxt);
    return ctxt.results;
}

#endif

/* CFRunLoop */

CONST_STRING_DECL(kCFRunLoopDefaultMode, "kCFRunLoopDefaultMode")
CONST_STRING_DECL(kCFRunLoopCommonModes, "kCFRunLoopCommonModes")

// call with rl and rlm locked
static CFRunLoopSourceRef __CFRunLoopModeFindSourceForMachPort(CFRunLoopRef rl, CFRunLoopModeRef rlm, __CFPort port) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    CFRunLoopSourceRef found = rlm->_portToV1SourceMap ? (CFRunLoopSourceRef)CFDictionaryGetValue(rlm->_portToV1SourceMap, (const void *)(uintptr_t)port) : NULL;
    return found;
}

// Remove backreferences the mode's sources have to the rl (context);
// the primary purpose of rls->_runLoops is so that Invalidation can remove
// the source from the run loops it is in, but during deallocation of a
// run loop, we already know that the sources are going to be punted
// from it, so invalidation of sources does not need to remove from a
// deallocating run loop.
static void __CFRunLoopCleanseSources(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)context;
    CFIndex idx, cnt;
    const void **list, *buffer[256];
    if (NULL == rlm->_sources0 && NULL == rlm->_sources1) return;

    cnt = (rlm->_sources0 ? CFSetGetCount(rlm->_sources0) : 0) + (rlm->_sources1 ? CFSetGetCount(rlm->_sources1) : 0);
    list = (const void **)((cnt <= 256) ? buffer : CFAllocatorAllocate(kCFAllocatorSystemDefault, cnt * sizeof(void *), 0));
    if (rlm->_sources0) CFSetGetValues(rlm->_sources0, list);
    if (rlm->_sources1) CFSetGetValues(rlm->_sources1, list + (rlm->_sources0 ? CFSetGetCount(rlm->_sources0) : 0));
    for (idx = 0; idx < cnt; idx++) {
	CFRunLoopSourceRef rls = (CFRunLoopSourceRef)list[idx];
	__CFRunLoopSourceLock(rls);
	if (NULL != rls->_runLoops) {
	    CFBagRemoveValue(rls->_runLoops, rl);
	}
	__CFRunLoopSourceUnlock(rls);
    }
    if (list != buffer) CFAllocatorDeallocate(kCFAllocatorSystemDefault, list);
}

static void __CFRunLoopDeallocateSources(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)context;
    CFIndex idx, cnt;
    const void **list, *buffer[256];
    if (NULL == rlm->_sources0 && NULL == rlm->_sources1) return;

    cnt = (rlm->_sources0 ? CFSetGetCount(rlm->_sources0) : 0) + (rlm->_sources1 ? CFSetGetCount(rlm->_sources1) : 0);
    list = (const void **)((cnt <= 256) ? buffer : CFAllocatorAllocate(kCFAllocatorSystemDefault, cnt * sizeof(void *), 0));
    if (rlm->_sources0) CFSetGetValues(rlm->_sources0, list);
    if (rlm->_sources1) CFSetGetValues(rlm->_sources1, list + (rlm->_sources0 ? CFSetGetCount(rlm->_sources0) : 0));
    for (idx = 0; idx < cnt; idx++) {
	CFRetain(list[idx]);
    }
    if (rlm->_sources0) CFSetRemoveAllValues(rlm->_sources0);
    if (rlm->_sources1) CFSetRemoveAllValues(rlm->_sources1);
    for (idx = 0; idx < cnt; idx++) {
        CFRunLoopSourceRef rls = (CFRunLoopSourceRef)list[idx];
        __CFRunLoopSourceLock(rls);
        if (NULL != rls->_runLoops) {
            CFBagRemoveValue(rls->_runLoops, rl);
        }
        __CFRunLoopSourceUnlock(rls);
        if (0 == rls->_context.version0.version) {
            if (NULL != rls->_context.version0.cancel) {
                rls->_context.version0.cancel(rls->_context.version0.info, rl, rlm->_name);	/* CALLOUT */
            }
        } else if (1 == rls->_context.version0.version) {
            __CFPort port = rls->_context.version1.getPort(rls->_context.version1.info);	/* CALLOUT */
            if (CFPORT_NULL != port) {
                __CFPortSetRemove(port, rlm->_portSet);
            }
        }
	CFRelease(rls);
    }
    if (list != buffer) CFAllocatorDeallocate(kCFAllocatorSystemDefault, list);
}

static void __CFRunLoopDeallocateObservers(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)context;
    CFIndex idx, cnt;
    const void **list, *buffer[256];
    if (NULL == rlm->_observers) return;
    cnt = CFArrayGetCount(rlm->_observers);
    list = (const void **)((cnt <= 256) ? buffer : CFAllocatorAllocate(kCFAllocatorSystemDefault, cnt * sizeof(void *), 0));
    CFArrayGetValues(rlm->_observers, CFRangeMake(0, cnt), list);
    for (idx = 0; idx < cnt; idx++) {
	CFRetain(list[idx]);
    }
    CFArrayRemoveAllValues(rlm->_observers);
    for (idx = 0; idx < cnt; idx++) {
	__CFRunLoopObserverCancel((CFRunLoopObserverRef)list[idx], rl, rlm);
	CFRelease(list[idx]);
    }
    if (list != buffer) CFAllocatorDeallocate(kCFAllocatorSystemDefault, list);
}

static void __CFRunLoopDeallocateTimers(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    CFIndex idx, cnt;
    const void **list, *buffer[256];
    if (NULL == rlm->_timers) return;
    cnt = CFArrayGetCount(rlm->_timers);
    list = (const void **)((cnt <= 256) ? buffer : CFAllocatorAllocate(kCFAllocatorSystemDefault, cnt * sizeof(void *), 0));
    CFArrayGetValues(rlm->_timers, CFRangeMake(0, CFArrayGetCount(rlm->_timers)), list);
    for (idx = 0; idx < cnt; idx++) {
	CFRetain(list[idx]);
    }
    CFArrayRemoveAllValues(rlm->_timers);
    for (idx = 0; idx < cnt; idx++) {
        CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)list[idx];
        __CFRunLoopTimerLock(rlt);
        // if the run loop is deallocating, and since a timer can only be in one
        // run loop, we're going to be removing the timer from all modes, so be
        // a little heavy-handed and direct
        CFSetRemoveAllValues(rlt->_rlModes);
        rlt->_runLoop = NULL;
        __CFRunLoopTimerUnlock(rlt);
	CFRelease(list[idx]);
    }
    if (list != buffer) CFAllocatorDeallocate(kCFAllocatorSystemDefault, list);
}

CF_EXPORT pthread_t _CFMainPThread;
CF_EXPORT CFRunLoopRef _CFRunLoopGet0b(pthread_t t);

static void __CFRunLoopDeallocate(CFTypeRef cf) {
    CFRunLoopRef rl = (CFRunLoopRef)cf;

    if (_CFRunLoopGet0b(_CFMainPThread) == cf) HALT;

    /* We try to keep the run loop in a valid state as long as possible,
       since sources may have non-retained references to the run loop.
       Another reason is that we don't want to lock the run loop for
       callback reasons, if we can get away without that.  We start by
       eliminating the sources, since they are the most likely to call
       back into the run loop during their "cancellation". Common mode
       items will be removed from the mode indirectly by the following
       three lines. */
    __CFRunLoopSetDeallocating(rl);
    if (NULL != rl->_modes) {
	CFSetApplyFunction(rl->_modes, (__CFRunLoopCleanseSources), rl); // remove references to rl
	CFSetApplyFunction(rl->_modes, (__CFRunLoopDeallocateSources), rl);
	CFSetApplyFunction(rl->_modes, (__CFRunLoopDeallocateObservers), rl);
	CFSetApplyFunction(rl->_modes, (__CFRunLoopDeallocateTimers), rl);
    }
    __CFRunLoopLock(rl);
    struct _block_item *item = rl->_blocks_head;
    while (item) {
	struct _block_item *curr = item;
	item = item->_next;
	CFRelease(curr->_mode);
	Block_release(curr->_block);
	free(curr);
    }
    if (NULL != rl->_commonModeItems) {
	CFRelease(rl->_commonModeItems);
    }
    if (NULL != rl->_commonModes) {
	CFRelease(rl->_commonModes);
    }
    if (NULL != rl->_modes) {
	CFRelease(rl->_modes);
    }
    __CFPortFree(rl->_wakeUpPort);
    rl->_wakeUpPort = CFPORT_NULL;
    __CFRunLoopUnlock(rl);
    pthread_mutex_destroy(&rl->_lock);
    memset((char *)cf + sizeof(CFRuntimeBase), 0x8C, sizeof(struct __CFRunLoop) - sizeof(CFRuntimeBase));
}

static const CFRuntimeClass __CFRunLoopModeClass = {
    0,
    "CFRunLoopMode",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopModeDeallocate,
    __CFRunLoopModeEqual,
    __CFRunLoopModeHash,
    NULL,      // 
    __CFRunLoopModeCopyDescription
};

static const CFRuntimeClass __CFRunLoopClass = {
    0,
    "CFRunLoop",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopDeallocate,
    NULL,
    NULL,
    NULL,      // 
    __CFRunLoopCopyDescription
};

__private_extern__ void __CFFinalizeRunLoop(uintptr_t data);

static int64_t tenus = 0LL;

__private_extern__ void __CFRunLoopInitialize(void) {
    tenus = __CFTimeIntervalToTSR(0.000010000);
    __kCFRunLoopTypeID = _CFRuntimeRegisterClass(&__CFRunLoopClass);
    __kCFRunLoopModeTypeID = _CFRuntimeRegisterClass(&__CFRunLoopModeClass);
}
 
CFTypeID CFRunLoopGetTypeID(void) {
    return __kCFRunLoopTypeID;
}

static CFRunLoopRef __CFRunLoopCreate(pthread_t t) {
    CFRunLoopRef loop = NULL;
    CFRunLoopModeRef rlm;
    uint32_t size = sizeof(struct __CFRunLoop) - sizeof(CFRuntimeBase);
    loop = (CFRunLoopRef)_CFRuntimeCreateInstance(kCFAllocatorSystemDefault, __kCFRunLoopTypeID, size, NULL);
    if (NULL == loop) {
	return NULL;
    }
    loop->_stopped = NULL;
    __CFRunLoopLockInit(&loop->_lock);
    loop->_wakeUpPort = __CFPortAllocate();
    if (CFPORT_NULL == loop->_wakeUpPort) HALT;
    loop->_ignoreWakeUps = true;
    loop->_commonModes = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
    CFSetAddValue(loop->_commonModes, kCFRunLoopDefaultMode);
    loop->_commonModeItems = NULL;
    loop->_currentMode = NULL;
    loop->_modes = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
    loop->_blocks_head = NULL;
    loop->_blocks_tail = NULL;
    loop->_counterpart = NULL;
    loop->_pthread = t;
#if DEPLOYMENT_TARGET_WINDOWS
    loop->_winthread = GetCurrentThreadId();
#else
    loop->_winthread = 0;
#endif
    rlm = __CFRunLoopFindMode(loop, kCFRunLoopDefaultMode, true);
    if (NULL != rlm) __CFRunLoopModeUnlock(rlm);
    return loop;
}

static CFMutableDictionaryRef __CFRunLoops = NULL;
static CFSpinLock_t loopsLock = CFSpinLockInit;

// should only be called by Foundation
// t==0 is a synonym for "main thread" that always works
CF_EXPORT CFRunLoopRef _CFRunLoopGet0(pthread_t t) {
    if (pthread_equal(t, kNilPthreadT)) {
	t = _CFMainPThread;
    }
    __CFSpinLock(&loopsLock);
    if (!__CFRunLoops) {
        __CFSpinUnlock(&loopsLock);
	CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorSystemDefault, 0, NULL, &kCFTypeDictionaryValueCallBacks);
	CFRunLoopRef mainLoop = __CFRunLoopCreate(_CFMainPThread);
	CFDictionarySetValue(dict, pthreadPointer(_CFMainPThread), mainLoop);
	if (!OSAtomicCompareAndSwapPtrBarrier(NULL, dict, (void * volatile *)&__CFRunLoops)) {
	    CFRelease(dict);
	}
	CFRelease(mainLoop);
        __CFSpinLock(&loopsLock);
    }
    CFRunLoopRef loop = (CFRunLoopRef)CFDictionaryGetValue(__CFRunLoops, pthreadPointer(t));
    if (!loop) {
        __CFSpinUnlock(&loopsLock);
	CFRunLoopRef newLoop = __CFRunLoopCreate(t);
        __CFSpinLock(&loopsLock);
	loop = (CFRunLoopRef)CFDictionaryGetValue(__CFRunLoops, pthreadPointer(t));
	if (!loop) {
	    CFDictionarySetValue(__CFRunLoops, pthreadPointer(t), newLoop);
	    loop = newLoop;
	}
	CFRelease(newLoop);
    }
    if (pthread_equal(t, pthread_self())) {
        _CFSetTSD(__CFTSDKeyRunLoop, (void *)loop, NULL);
        if (0 == _CFGetTSD(__CFTSDKeyRunLoopCntr)) {
            _CFSetTSD(__CFTSDKeyRunLoopCntr, (void *)(PTHREAD_DESTRUCTOR_ITERATIONS-1), (void (*)(void *))__CFFinalizeRunLoop);
        }
    }
    __CFSpinUnlock(&loopsLock);
    return loop;
}

// should only be called by Foundation
CFRunLoopRef _CFRunLoopGet0b(pthread_t t) {
    if (pthread_equal(t, kNilPthreadT)) {
	t = _CFMainPThread;
    }
    __CFSpinLock(&loopsLock);
    CFRunLoopRef loop = NULL;
    if (__CFRunLoops) {
        loop = (CFRunLoopRef)CFDictionaryGetValue(__CFRunLoops, pthreadPointer(t));
    }
    __CFSpinUnlock(&loopsLock);
    return loop;
}

static void __CFRunLoopRemoveAllSources(CFRunLoopRef rl, CFStringRef modeName);

// Called for each thread as it exits
__private_extern__ void __CFFinalizeRunLoop(uintptr_t data) {
    CFRunLoopRef rl = NULL;
    if (data <= 1) {
	__CFSpinLock(&loopsLock);
	if (__CFRunLoops) {
	    rl = (CFRunLoopRef)CFDictionaryGetValue(__CFRunLoops, pthreadPointer(pthread_self()));
	    if (rl) CFRetain(rl);
	    CFDictionaryRemoveValue(__CFRunLoops, pthreadPointer(pthread_self()));
	}
	__CFSpinUnlock(&loopsLock);
    } else {
        _CFSetTSD(__CFTSDKeyRunLoopCntr, (void *)(data - 1), (void (*)(void *))__CFFinalizeRunLoop);
    }
    if (rl && CFRunLoopGetMain() != rl) { // protect against cooperative threads
        if (NULL != rl->_counterpart) {
            CFRelease(rl->_counterpart);
	    rl->_counterpart = NULL;
        }
	// purge all sources before deallocation
        CFArrayRef array = CFRunLoopCopyAllModes(rl);
        for (CFIndex idx = CFArrayGetCount(array); idx--;) {
            CFStringRef modeName = (CFStringRef)CFArrayGetValueAtIndex(array, idx);
            __CFRunLoopRemoveAllSources(rl, modeName);
        }
        __CFRunLoopRemoveAllSources(rl, kCFRunLoopCommonModes);
        CFRelease(array);
    }
    if (rl) CFRelease(rl);
}

pthread_t _CFRunLoopGet1(CFRunLoopRef rl) {
    return rl->_pthread;
}

// should only be called by Foundation
CF_EXPORT CFTypeRef _CFRunLoopGet2(CFRunLoopRef rl) {
    CFTypeRef ret = NULL;
    __CFSpinLock(&loopsLock);
    ret = rl->_counterpart;
    __CFSpinUnlock(&loopsLock);
    return ret;
}

// should only be called by Foundation
CF_EXPORT CFTypeRef _CFRunLoopGet2b(CFRunLoopRef rl) {
    return rl->_counterpart;
}

#if DEPLOYMENT_TARGET_MACOSX
void _CFRunLoopSetCurrent(CFRunLoopRef rl) {
    if (pthread_main_np()) return;
    CFRunLoopRef currentLoop = CFRunLoopGetCurrent();
    if (rl != currentLoop) {
        CFRetain(currentLoop); // avoid a deallocation of the currentLoop inside the lock
        __CFSpinLock(&loopsLock);
	if (rl) {
	    CFDictionarySetValue(__CFRunLoops, pthreadPointer(pthread_self()), rl);
	} else {
	    CFDictionaryRemoveValue(__CFRunLoops, pthreadPointer(pthread_self()));
	}
        __CFSpinUnlock(&loopsLock);
	CFRelease(currentLoop);
        _CFSetTSD(__CFTSDKeyRunLoop, NULL, NULL);
        _CFSetTSD(__CFTSDKeyRunLoopCntr, 0, (void (*)(void *))__CFFinalizeRunLoop);
    }
}
#endif

CFRunLoopRef CFRunLoopGetMain(void) {
    CHECK_FOR_FORK();
    static CFRunLoopRef __main = NULL; // no retain needed
    if (!__main) __main = _CFRunLoopGet0(_CFMainPThread); // no CAS needed
    return __main;
}

CFRunLoopRef CFRunLoopGetCurrent(void) {
    CHECK_FOR_FORK();
    CFRunLoopRef rl = (CFRunLoopRef)_CFGetTSD(__CFTSDKeyRunLoop);
    if (rl) return rl;
    return _CFRunLoopGet0(pthread_self());
}

CFStringRef CFRunLoopCopyCurrentMode(CFRunLoopRef rl) {
    CHECK_FOR_FORK();
    CFStringRef result = NULL;
    __CFRunLoopLock(rl);
    if (NULL != rl->_currentMode) {
	result = (CFStringRef)CFRetain(rl->_currentMode->_name);
    }
    __CFRunLoopUnlock(rl);
    return result;
}

static void __CFRunLoopGetModeName(const void *value, void *context) {
    CFRunLoopModeRef rlm = (CFRunLoopModeRef)value;
    CFMutableArrayRef array = (CFMutableArrayRef)context;
    CFArrayAppendValue(array, rlm->_name);
}

CFArrayRef CFRunLoopCopyAllModes(CFRunLoopRef rl) {
    CHECK_FOR_FORK();
    CFMutableArrayRef array;
    __CFRunLoopLock(rl);
    array = CFArrayCreateMutable(kCFAllocatorSystemDefault, CFSetGetCount(rl->_modes), &kCFTypeArrayCallBacks);
    CFSetApplyFunction(rl->_modes, (__CFRunLoopGetModeName), array);
    __CFRunLoopUnlock(rl);
    return array;
}

static void __CFRunLoopAddItemsToCommonMode(const void *value, void *ctx) {
    CFTypeRef item = (CFTypeRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)(((CFTypeRef *)ctx)[0]);
    CFStringRef modeName = (CFStringRef)(((CFTypeRef *)ctx)[1]);
    if (CFGetTypeID(item) == __kCFRunLoopSourceTypeID) {
	CFRunLoopAddSource(rl, (CFRunLoopSourceRef)item, modeName);
    } else if (CFGetTypeID(item) == __kCFRunLoopObserverTypeID) {
	CFRunLoopAddObserver(rl, (CFRunLoopObserverRef)item, modeName);
    } else if (CFGetTypeID(item) == __kCFRunLoopTimerTypeID) {
	CFRunLoopAddTimer(rl, (CFRunLoopTimerRef)item, modeName);
    }
}

static void __CFRunLoopAddItemToCommonModes(const void *value, void *ctx) {
    CFStringRef modeName = (CFStringRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)(((CFTypeRef *)ctx)[0]);
    CFTypeRef item = (CFTypeRef)(((CFTypeRef *)ctx)[1]);
    if (CFGetTypeID(item) == __kCFRunLoopSourceTypeID) {
	CFRunLoopAddSource(rl, (CFRunLoopSourceRef)item, modeName);
    } else if (CFGetTypeID(item) == __kCFRunLoopObserverTypeID) {
	CFRunLoopAddObserver(rl, (CFRunLoopObserverRef)item, modeName);
    } else if (CFGetTypeID(item) == __kCFRunLoopTimerTypeID) {
	CFRunLoopAddTimer(rl, (CFRunLoopTimerRef)item, modeName);
    }
}

static void __CFRunLoopRemoveItemFromCommonModes(const void *value, void *ctx) {
    CFStringRef modeName = (CFStringRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)(((CFTypeRef *)ctx)[0]);
    CFTypeRef item = (CFTypeRef)(((CFTypeRef *)ctx)[1]);
    if (CFGetTypeID(item) == __kCFRunLoopSourceTypeID) {
	CFRunLoopRemoveSource(rl, (CFRunLoopSourceRef)item, modeName);
    } else if (CFGetTypeID(item) == __kCFRunLoopObserverTypeID) {
	CFRunLoopRemoveObserver(rl, (CFRunLoopObserverRef)item, modeName);
    } else if (CFGetTypeID(item) == __kCFRunLoopTimerTypeID) {
	CFRunLoopRemoveTimer(rl, (CFRunLoopTimerRef)item, modeName);
    }
}

CF_EXPORT Boolean _CFRunLoop01(CFRunLoopRef rl, CFStringRef modeName) {
    __CFRunLoopLock(rl);
    Boolean present = CFSetContainsValue(rl->_commonModes, modeName);
    __CFRunLoopUnlock(rl);
    return present; 
}

void CFRunLoopAddCommonMode(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    if (__CFRunLoopIsDeallocating(rl)) return;
    __CFRunLoopLock(rl);
    if (!CFSetContainsValue(rl->_commonModes, modeName)) {
	CFSetRef set = rl->_commonModeItems ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModeItems) : NULL;
	CFSetAddValue(rl->_commonModes, modeName);
	if (NULL != set) {
	    CFTypeRef context[2] = {rl, modeName};
	    /* add all common-modes items to new mode */
	    CFSetApplyFunction(set, (__CFRunLoopAddItemsToCommonMode), (void *)context);
	    CFRelease(set);
	}
    } else {
    }
    __CFRunLoopUnlock(rl);
}


static void __CFRUNLOOP_IS_CALLING_OUT_TO_AN_OBSERVER_CALLBACK_FUNCTION__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_AN_OBSERVER_CALLBACK_FUNCTION__(CFRunLoopObserverCallBack func, CFRunLoopObserverRef observer, CFRunLoopActivity activity, void *info) {
    if (func) {
        func(observer, activity, info);
    }
    getpid(); // thwart tail-call optimization
}

static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_TIMER_CALLBACK_FUNCTION__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_TIMER_CALLBACK_FUNCTION__(CFRunLoopTimerCallBack func, CFRunLoopTimerRef timer, void *info) {
    if (func) {
        func(timer, info);
    }
    getpid(); // thwart tail-call optimization
}

static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_BLOCK__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_BLOCK__(void (^block)(void)) {
    if (block) {
        block();
    }
    getpid(); // thwart tail-call optimization
}

static Boolean __CFRunLoopDoBlocks(CFRunLoopRef rl, CFRunLoopModeRef rlm) { // Call with rl and rlm locked
    if (!rl->_blocks_head) return false;
    if (!rlm || !rlm->_name) return false;
    Boolean did = false;
    struct _block_item *head = rl->_blocks_head;
    struct _block_item *tail = rl->_blocks_tail;
    rl->_blocks_head = NULL;
    rl->_blocks_tail = NULL;
    CFSetRef commonModes = rl->_commonModes;
    CFStringRef curMode = rlm->_name;
    __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    struct _block_item *prev = NULL;
    struct _block_item *item = head;
    while (item) {
        struct _block_item *curr = item;
        item = item->_next;
	Boolean doit = false;
	if (CFStringGetTypeID() == CFGetTypeID(curr->_mode)) {
	    doit = CFEqual(curr->_mode, curMode) || (CFEqual(curr->_mode, kCFRunLoopCommonModes) && CFSetContainsValue(commonModes, curMode));
        } else {
	    doit = CFSetContainsValue((CFSetRef)curr->_mode, curMode) || (CFSetContainsValue((CFSetRef)curr->_mode, kCFRunLoopCommonModes) && CFSetContainsValue(commonModes, curMode));
	}
	if (!doit) prev = curr;
	if (doit) {
	    if (prev) prev->_next = item;
	    if (curr == head) head = item;
	    if (curr == tail) tail = prev;
	    void (^block)(void) = curr->_block;
            CFRelease(curr->_mode);
            free(curr);
	    if (doit) {
                __CFRUNLOOP_IS_CALLING_OUT_TO_A_BLOCK__(block);
	        did = true;
	    }
            Block_release(block); // do this before relocking to prevent deadlocks where some yahoo wants to run the run loop reentrantly from their dealloc
	}
    }
    __CFRunLoopLock(rl);
    __CFRunLoopModeLock(rlm);
    if (head) {
	tail->_next = rl->_blocks_head;
	rl->_blocks_head = head;
        if (!rl->_blocks_tail) rl->_blocks_tail = tail;
    }
    return did;
}

/* rl is locked, rlm is locked on entrance and exit */
static void __CFRunLoopDoObservers() __attribute__((noinline));
static void __CFRunLoopDoObservers(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFRunLoopActivity activity) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();

    CFIndex cnt = rlm->_observers ? CFArrayGetCount(rlm->_observers) : 0;
    if (cnt < 1) return;

    /* Fire the observers */
    STACK_BUFFER_DECL(CFRunLoopObserverRef, buffer, (cnt <= 1024) ? cnt : 1);
    CFRunLoopObserverRef *collectedObservers = (cnt <= 1024) ? buffer : (CFRunLoopObserverRef *)malloc(cnt * sizeof(CFRunLoopObserverRef));
    CFIndex obs_cnt = 0;
    for (CFIndex idx = 0; idx < cnt; idx++) {
        CFRunLoopObserverRef rlo = (CFRunLoopObserverRef)CFArrayGetValueAtIndex(rlm->_observers, idx);
        if (0 != (rlo->_activities & activity) && __CFIsValid(rlo) && !__CFRunLoopObserverIsFiring(rlo)) {
            collectedObservers[obs_cnt++] = (CFRunLoopObserverRef)CFRetain(rlo);
        }
    }
    __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    for (CFIndex idx = 0; idx < obs_cnt; idx++) {
        CFRunLoopObserverRef rlo = collectedObservers[idx];
        __CFRunLoopObserverLock(rlo);
        if (__CFIsValid(rlo)) {
            Boolean doInvalidate = !__CFRunLoopObserverRepeats(rlo);
            __CFRunLoopObserverSetFiring(rlo);
            __CFRunLoopObserverUnlock(rlo);
            __CFRUNLOOP_IS_CALLING_OUT_TO_AN_OBSERVER_CALLBACK_FUNCTION__(rlo->_callout, rlo, activity, rlo->_context.info);
            if (doInvalidate) {
                CFRunLoopObserverInvalidate(rlo);
            }
            __CFRunLoopObserverUnsetFiring(rlo);
        } else {
            __CFRunLoopObserverUnlock(rlo);
        }
        CFRelease(rlo);
    }
    __CFRunLoopLock(rl);
    __CFRunLoopModeLock(rlm);

    if (collectedObservers != buffer) free(collectedObservers);
}

static CFComparisonResult __CFRunLoopSourceComparator(const void *val1, const void *val2, void *context) {
    CFRunLoopSourceRef o1 = (CFRunLoopSourceRef)val1;
    CFRunLoopSourceRef o2 = (CFRunLoopSourceRef)val2;
    if (o1->_order < o2->_order) return kCFCompareLessThan;
    if (o2->_order < o1->_order) return kCFCompareGreaterThan;
    return kCFCompareEqualTo;
}

static void __CFRunLoopCollectSources0(const void *value, void *context) {
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)value;
    CFTypeRef *sources = (CFTypeRef *)context;
    if (0 == rls->_context.version0.version && __CFIsValid(rls) && __CFRunLoopSourceIsSignaled(rls)) {
	if (NULL == *sources) {
	    *sources = CFRetain(rls);
	} else if (CFGetTypeID(*sources) == __kCFRunLoopSourceTypeID) {
	    CFTypeRef oldrls = *sources;
	    *sources = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeArrayCallBacks);
	    CFArrayAppendValue((CFMutableArrayRef)*sources, oldrls);
	    CFArrayAppendValue((CFMutableArrayRef)*sources, rls);
	    CFRelease(oldrls);
	} else {
	    CFArrayAppendValue((CFMutableArrayRef)*sources, rls);
	}
    }
}

static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE0_PERFORM_FUNCTION__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE0_PERFORM_FUNCTION__(void (*perform)(void *), void *info) {
    if (perform) {
        perform(info);
    }
    getpid(); // thwart tail-call optimization
}

static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE1_PERFORM_FUNCTION__() __attribute__((noinline));
static void __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE1_PERFORM_FUNCTION__(
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
        void *(*perform)(void *msg, CFIndex size, CFAllocatorRef allocator, void *info),
        mach_msg_header_t *msg, CFIndex size, mach_msg_header_t **reply,
#else
        void (*perform)(void *),
#endif
        void *info) {
    if (perform) {
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
        *reply = perform(msg, size, kCFAllocatorSystemDefault, info);
#else
        perform(info);
#endif
    }
    getpid(); // thwart tail-call optimization
}

/* rl is locked, rlm is locked on entrance and exit */
static Boolean __CFRunLoopDoSources0(CFRunLoopRef rl, CFRunLoopModeRef rlm, Boolean stopAfterHandle) __attribute__((noinline));
static Boolean __CFRunLoopDoSources0(CFRunLoopRef rl, CFRunLoopModeRef rlm, Boolean stopAfterHandle) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    CFTypeRef sources = NULL;
    Boolean sourceHandled = false;

    /* Fire the version 0 sources */
    if (NULL != rlm->_sources0 && 0 < CFSetGetCount(rlm->_sources0)) {
	CFSetApplyFunction(rlm->_sources0, (__CFRunLoopCollectSources0), &sources);
    }
    if (NULL != sources) {
	__CFRunLoopModeUnlock(rlm);
	__CFRunLoopUnlock(rl);
	// sources is either a single (retained) CFRunLoopSourceRef or an array of (retained) CFRunLoopSourceRef
	if (CFGetTypeID(sources) == __kCFRunLoopSourceTypeID) {
	    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)sources;
	    __CFRunLoopSourceLock(rls);
            if (__CFRunLoopSourceIsSignaled(rls)) {
	        __CFRunLoopSourceUnsetSignaled(rls);
	        if (__CFIsValid(rls)) {
	            __CFRunLoopSourceUnlock(rls);
                    __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE0_PERFORM_FUNCTION__(rls->_context.version0.perform, rls->_context.version0.info);
	            CHECK_FOR_FORK();
	            sourceHandled = true;
	        } else {
	            __CFRunLoopSourceUnlock(rls);
	        }
            } else {
                __CFRunLoopSourceUnlock(rls);
            }
	} else {
	    CFIndex cnt = CFArrayGetCount((CFArrayRef)sources);
	    CFArraySortValues((CFMutableArrayRef)sources, CFRangeMake(0, cnt), (__CFRunLoopSourceComparator), NULL);
	    for (CFIndex idx = 0; idx < cnt; idx++) {
		CFRunLoopSourceRef rls = (CFRunLoopSourceRef)CFArrayGetValueAtIndex((CFArrayRef)sources, idx);
		__CFRunLoopSourceLock(rls);
                if (__CFRunLoopSourceIsSignaled(rls)) {
		    __CFRunLoopSourceUnsetSignaled(rls);
		    if (__CFIsValid(rls)) {
		        __CFRunLoopSourceUnlock(rls);
                        __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE0_PERFORM_FUNCTION__(rls->_context.version0.perform, rls->_context.version0.info);
		        CHECK_FOR_FORK();
		        sourceHandled = true;
		    } else {
		        __CFRunLoopSourceUnlock(rls);
		    }
                } else {
                    __CFRunLoopSourceUnlock(rls);
                }
		if (stopAfterHandle && sourceHandled) {
		    break;
		}
	    }
	}
	CFRelease(sources);
	__CFRunLoopLock(rl);
	__CFRunLoopModeLock(rlm);
    }
    return sourceHandled;
}

// msg, size and reply are unused on Windows
static Boolean __CFRunLoopDoSource1() __attribute__((noinline));
static Boolean __CFRunLoopDoSource1(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFRunLoopSourceRef rls
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
                                    , mach_msg_header_t *msg, CFIndex size, mach_msg_header_t **reply
#endif
                                    ) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    Boolean sourceHandled = false;

    /* Fire a version 1 source */
    CFRetain(rls);
    __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    __CFRunLoopSourceLock(rls);
    if (__CFIsValid(rls)) {
	__CFRunLoopSourceUnsetSignaled(rls);
	__CFRunLoopSourceUnlock(rls);
        __CFRUNLOOP_IS_CALLING_OUT_TO_A_SOURCE1_PERFORM_FUNCTION__(rls->_context.version1.perform,
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
            msg, size, reply,
#endif
            rls->_context.version1.info);
	CHECK_FOR_FORK();
	sourceHandled = true;
    } else {
        if (_LogCFRunLoop) { CFLog(kCFLogLevelDebug, CFSTR("%p (%s) __CFRunLoopDoSource1 rls %p is invalid"), CFRunLoopGetCurrent(), *_CFGetProgname(), rls); }
	__CFRunLoopSourceUnlock(rls);
    }
    CFRelease(rls);
    __CFRunLoopLock(rl);
    __CFRunLoopModeLock(rlm);
    return sourceHandled;
}

static CFIndex __CFRunLoopInsertionIndexInTimerArray(CFArrayRef array, CFRunLoopTimerRef rlt) __attribute__((noinline));
static CFIndex __CFRunLoopInsertionIndexInTimerArray(CFArrayRef array, CFRunLoopTimerRef rlt) {
    CFIndex cnt = CFArrayGetCount(array);
    if (cnt <= 0) {
        return 0;
    }
    if (256 < cnt) {
        CFRunLoopTimerRef item = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(array, cnt - 1);
        if (item->_fireTSR <= rlt->_fireTSR) {
            return cnt;
        }
        item = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(array, 0);
        if (rlt->_fireTSR < item->_fireTSR) {
            return 0;
        }
    }

    CFIndex add = (1 << flsl(cnt)) * 2;
    CFIndex idx = 0;
    Boolean lastTestLEQ;
    do {
        add = add / 2;
	lastTestLEQ = false;
        CFIndex testIdx = idx + add;
        if (testIdx < cnt) {
            CFRunLoopTimerRef item = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(array, testIdx);
            if (item->_fireTSR <= rlt->_fireTSR) {
                idx = testIdx;
		lastTestLEQ = true;
            }
        }
    } while (0 < add);

    return lastTestLEQ ? idx + 1 : idx;
}

// call with rlm and its run loop locked, and the TSRLock locked; rlt not locked; returns with same state
static void __CFRepositionTimerInMode(CFRunLoopModeRef rlm, CFRunLoopTimerRef rlt, Boolean isInArray) __attribute__((noinline));
static void __CFRepositionTimerInMode(CFRunLoopModeRef rlm, CFRunLoopTimerRef rlt, Boolean isInArray) {
    if (!rlt || !rlm->_timers) return;
    Boolean found = false;
    if (isInArray) {
	CFIndex idx = CFArrayGetFirstIndexOfValue(rlm->_timers, CFRangeMake(0, CFArrayGetCount(rlm->_timers)), rlt);
	if (kCFNotFound != idx) {
	    CFRetain(rlt);
	    CFArrayRemoveValueAtIndex(rlm->_timers, idx);
	    found = true;
	}
    }
    if (!found && isInArray) return;
    CFIndex newIdx = __CFRunLoopInsertionIndexInTimerArray(rlm->_timers, rlt);
    CFArrayInsertValueAtIndex(rlm->_timers, newIdx, rlt);
    CFRunLoopTimerRef nextTimer = NULL;
    for (CFIndex idx = 0, cnt = CFArrayGetCount(rlm->_timers); idx < cnt; idx++) {
        CFRunLoopTimerRef t = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(rlm->_timers, idx);
        if (!__CFRunLoopTimerIsFiring(t)) {
            nextTimer = t;
            break;
        }
    }
    if (nextTimer) {
        int64_t fireTSR = nextTimer->_fireTSR;
        fireTSR = (fireTSR / tenus + 1) * tenus;
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
        mk_timer_arm(rlm->_timerPort, __CFUInt64ToAbsoluteTime(fireTSR));
#elif DEPLOYMENT_TARGET_WINDOWS
        LARGE_INTEGER dueTime;
        dueTime.QuadPart = __CFTSRToFiletime(fireTSR);
        SetWaitableTimer(rlm->_timerPort, &dueTime, 0, NULL, NULL, FALSE);
#endif
    }
    if (isInArray) CFRelease(rlt);
}

// mode and rl are locked on entry and exit
static Boolean __CFRunLoopDoTimer(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFRunLoopTimerRef rlt) {	/* DOES CALLOUT */
    Boolean timerHandled = false;
    int64_t oldFireTSR = 0;

    /* Fire a timer */
    CFRetain(rlt);
    __CFRunLoopTimerLock(rlt);

    if (__CFIsValid(rlt) && rlt->_fireTSR <= (int64_t)mach_absolute_time() && !__CFRunLoopTimerIsFiring(rlt) && rlt->_runLoop == rl) {
        void *context_info = NULL;
        void (*context_release)(const void *) = NULL;
        if (rlt->_context.retain) {
            context_info = (void *)rlt->_context.retain(rlt->_context.info);
            context_release = rlt->_context.release;
        } else {
            context_info = rlt->_context.info;
        }
        Boolean doInvalidate = (0.0 == rlt->_interval);
	__CFRunLoopTimerSetFiring(rlt);
        __CFRunLoopTimerUnlock(rlt);
	__CFRunLoopTimerFireTSRLock();
	oldFireTSR = rlt->_fireTSR;
	__CFRunLoopTimerFireTSRUnlock();

        CFRunLoopTimerRef nextTimer = NULL;
        for (CFIndex idx = 0, cnt = CFArrayGetCount(rlm->_timers); idx < cnt; idx++) {
            CFRunLoopTimerRef t = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(rlm->_timers, idx);
            if (!__CFRunLoopTimerIsFiring(t)) {
                nextTimer = t;
                break;
            }
        }
        if (nextTimer) {
            int64_t fireTSR = nextTimer->_fireTSR;
            fireTSR = (fireTSR / tenus + 1) * tenus;
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
            mk_timer_arm(rlm->_timerPort, __CFUInt64ToAbsoluteTime(fireTSR));
#elif DEPLOYMENT_TARGET_WINDOWS
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = __CFTSRToFiletime(fireTSR);
            SetWaitableTimer(rlm->_timerPort, &dueTime, 0, NULL, NULL, FALSE);
#endif
        }

	__CFRunLoopModeUnlock(rlm);
	__CFRunLoopUnlock(rl);
	__CFRUNLOOP_IS_CALLING_OUT_TO_A_TIMER_CALLBACK_FUNCTION__(rlt->_callout, rlt, context_info);
	CHECK_FOR_FORK();
        if (doInvalidate) {
            CFRunLoopTimerInvalidate(rlt);      /* DOES CALLOUT */
        }
        if (context_release) {
            context_release(context_info);
        }
	__CFRunLoopLock(rl);
	__CFRunLoopModeLock(rlm);
        __CFRunLoopTimerLock(rlt);
	timerHandled = true;
	__CFRunLoopTimerUnsetFiring(rlt);
    }
    if (__CFIsValid(rlt) && timerHandled) {
        /* This is just a little bit tricky: we want to support calling
         * CFRunLoopTimerSetNextFireDate() from within the callout and
         * honor that new time here if it is a later date, otherwise
         * it is completely ignored. */
        if (oldFireTSR < rlt->_fireTSR) {
            /* Next fire TSR was set, and set to a date after the previous
            * fire date, so we honor it. */
            __CFRunLoopTimerUnlock(rlt);
            // The timer was adjusted and repositioned, during the
            // callout, but if it was still the min timer, it was
            // skipped because it was firing.  Need to redo the
            // min timer calculation in case rlt should now be that
            // timer instead of whatever was chosen.
            CFRunLoopTimerRef nextTimer = NULL;
            for (CFIndex idx = 0, cnt = CFArrayGetCount(rlm->_timers); idx < cnt; idx++) {
                CFRunLoopTimerRef t = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(rlm->_timers, idx);
                if (!__CFRunLoopTimerIsFiring(t)) {
                    nextTimer = t;
                    break;
                }
            }
            if (nextTimer) {
                int64_t fireTSR = nextTimer->_fireTSR;
                fireTSR = (fireTSR / tenus + 1) * tenus;
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
                mk_timer_arm(rlm->_timerPort, __CFUInt64ToAbsoluteTime(fireTSR));
#else
                LARGE_INTEGER dueTime;
                dueTime.QuadPart = __CFTSRToFiletime(fireTSR);
                SetWaitableTimer(rlm->_timerPort, &dueTime, 0, NULL, NULL, FALSE);
#endif
            }
        } else {
	    int64_t nextFireTSR = 0LL;
            int64_t intervalTSR = 0LL;
            if (rlt->_interval <= 0.0) {
            } else if (TIMER_INTERVAL_LIMIT < rlt->_interval) {
        	intervalTSR = __CFTimeIntervalToTSR(TIMER_INTERVAL_LIMIT);
            } else {
        	intervalTSR = __CFTimeIntervalToTSR(rlt->_interval);
            }
            if (LLONG_MAX - intervalTSR <= oldFireTSR) {
                nextFireTSR = LLONG_MAX;
            } else {
                int64_t currentTSR = (int64_t)mach_absolute_time();
                nextFireTSR = oldFireTSR;
                while (nextFireTSR <= currentTSR) {
                    nextFireTSR += intervalTSR;
                }
            }
            CFRunLoopRef rlt_rl = rlt->_runLoop;
            if (rlt_rl) {
                CFRetain(rlt_rl);
		CFIndex cnt = CFSetGetCount(rlt->_rlModes);
		STACK_BUFFER_DECL(CFTypeRef, modes, cnt);
		CFSetGetValues(rlt->_rlModes, (const void **)modes);
		// To avoid A->B, B->A lock ordering issues when coming up
		// towards the run loop from a source, the timer has to be
		// unlocked, which means we have to protect from object
		// invalidation, although that's somewhat expensive.
		for (CFIndex idx = 0; idx < cnt; idx++) {
		    CFRetain(modes[idx]);
		}
		__CFRunLoopTimerUnlock(rlt);
		for (CFIndex idx = 0; idx < cnt; idx++) {
		    CFStringRef name = (CFStringRef)modes[idx];
		    modes[idx] = (CFTypeRef)__CFRunLoopFindMode(rlt_rl, name, false);
		    CFRelease(name);
		}
		__CFRunLoopTimerFireTSRLock();
		rlt->_fireTSR = nextFireTSR;
                rlt->_nextFireDate = CFAbsoluteTimeGetCurrent() + __CFTSRToTimeInterval(nextFireTSR - (int64_t)mach_absolute_time());
		for (CFIndex idx = 0; idx < cnt; idx++) {
		    CFRunLoopModeRef rlm = (CFRunLoopModeRef)modes[idx];
		    if (rlm) {
			__CFRepositionTimerInMode(rlm, rlt, true);
		    }
		}
		__CFRunLoopTimerFireTSRUnlock();
		for (CFIndex idx = 0; idx < cnt; idx++) {
		    __CFRunLoopModeUnlock((CFRunLoopModeRef)modes[idx]);
		}
		CFRelease(rlt_rl);
	    } else {
		__CFRunLoopTimerUnlock(rlt);
		__CFRunLoopTimerFireTSRLock();
		rlt->_fireTSR = nextFireTSR;
                rlt->_nextFireDate = CFAbsoluteTimeGetCurrent() + __CFTSRToTimeInterval(nextFireTSR - (int64_t)mach_absolute_time());
		__CFRunLoopTimerFireTSRUnlock();
            }
        }
    } else {
        __CFRunLoopTimerUnlock(rlt);
    }
    CFRelease(rlt);
    return timerHandled;
}

// rl and rlm are locked on entry and exit
static Boolean __CFRunLoopDoTimers(CFRunLoopRef rl, CFRunLoopModeRef rlm, int64_t limitTSR) {	/* DOES CALLOUT */
    Boolean timerHandled = false;
    CFMutableArrayRef timers = NULL;
    for (CFIndex idx = 0, cnt = rlm->_timers ? CFArrayGetCount(rlm->_timers) : 0; idx < cnt; idx++) {
        CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(rlm->_timers, idx);
        if (__CFIsValid(rlt) && rlt->_fireTSR <= limitTSR && !__CFRunLoopTimerIsFiring(rlt)) {
            if (!timers) timers = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeArrayCallBacks);
            CFArrayAppendValue(timers, rlt);
        }
    }
    for (CFIndex idx = 0, cnt = timers ? CFArrayGetCount(timers) : 0; idx < cnt; idx++) {
        CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(timers, idx);
        Boolean did = __CFRunLoopDoTimer(rl, rlm, rlt);
        timerHandled = timerHandled || did;
    }
    if (timers) CFRelease(timers);
    return timerHandled;
}


CF_EXPORT Boolean _CFRunLoopFinished(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    Boolean result = false;
    __CFRunLoopLock(rl);
    rlm = __CFRunLoopFindMode(rl, modeName, false);
    if (NULL == rlm || __CFRunLoopModeIsEmpty(rl, rlm, NULL)) {
	result = true;
    }
    if (rlm) __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    return result;
}

static int32_t __CFRunLoopRun(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFTimeInterval seconds, Boolean stopAfterHandle, CFRunLoopModeRef previousMode) __attribute__((noinline));

#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED

#define TIMEOUT_INFINITY (~(mach_msg_timeout_t)0)

static Boolean __CFRunLoopServiceMachPort(mach_port_name_t port, mach_msg_header_t **buffer, size_t buffer_size, mach_msg_timeout_t timeout) {
    Boolean originalBuffer = true;
    for (;;) {		/* In that sleep of death what nightmares may come ... */
        mach_msg_header_t *msg = (mach_msg_header_t *)*buffer;
        msg->msgh_bits = 0;
        msg->msgh_local_port = port;
        msg->msgh_remote_port = MACH_PORT_NULL;
        msg->msgh_size = buffer_size;
        msg->msgh_id = 0;
        kern_return_t ret = mach_msg(msg, MACH_RCV_MSG|MACH_RCV_LARGE|((TIMEOUT_INFINITY != timeout) ? MACH_RCV_TIMEOUT : 0)|MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0)|MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AV), 0, msg->msgh_size, port, timeout, MACH_PORT_NULL);
        if (MACH_MSG_SUCCESS == ret) return true;
        if (MACH_RCV_TIMED_OUT == ret) {
            if (!originalBuffer) free(msg);
            *buffer = NULL;
            return false;
        }
        if (MACH_RCV_TOO_LARGE != ret) break;
        buffer_size = round_msg(msg->msgh_size + MAX_TRAILER_SIZE);
        if (originalBuffer) *buffer = NULL;
        originalBuffer = false;
        *buffer = realloc(*buffer, buffer_size);
    }
    HALT;
    return false;
}

#elif DEPLOYMENT_TARGET_WINDOWS

#define TIMEOUT_INFINITY INFINITE

// pass in either a portSet or onePort
static Boolean __CFRunLoopWaitForMultipleObjects(__CFPortSet portSet, HANDLE *onePort, DWORD timeout, DWORD mask, HANDLE *livePort, Boolean *msgReceived) {
    DWORD waitResult = WAIT_TIMEOUT;
    HANDLE handleBuf[MAXIMUM_WAIT_OBJECTS];
    HANDLE *handles = NULL;
    uint32_t handleCount = 0;
    Boolean freeHandles = false;
    Boolean result = false;
    
    if (portSet) {
	// copy out the handles to be safe from other threads at work
	handles = __CFPortSetGetPorts(portSet, handleBuf, MAXIMUM_WAIT_OBJECTS, &handleCount);
	freeHandles = (handles != handleBuf);
    } else {
	handles = onePort;
	handleCount = 1;
	freeHandles = FALSE;
    }
    
    // The run loop mode and loop are already in proper unlocked state from caller
    waitResult = MsgWaitForMultipleObjectsEx(__CFMin(handleCount, MAXIMUM_WAIT_OBJECTS), handles, timeout, mask, MWMO_INPUTAVAILABLE);
    
    CFAssert2(waitResult != WAIT_FAILED, __kCFLogAssertion, "%s(): error %d from MsgWaitForMultipleObjects", __PRETTY_FUNCTION__, GetLastError());
    
    if (waitResult == WAIT_TIMEOUT) {
	// do nothing, just return to caller
	result = false;
    } else if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0+handleCount) {
	// a handle was signaled
	if (livePort) *livePort = handles[waitResult-WAIT_OBJECT_0];
	result = true;
    } else if (waitResult == WAIT_OBJECT_0+handleCount) {
	// windows message received
        if (msgReceived) *msgReceived = true;
	result = true;
    } else if (waitResult >= WAIT_ABANDONED_0 && waitResult < WAIT_ABANDONED_0+handleCount) {
	// an "abandoned mutex object"
	if (livePort) *livePort = handles[waitResult-WAIT_ABANDONED_0];
	result = true;
    } else {
	CFAssert2(waitResult == WAIT_FAILED, __kCFLogAssertion, "%s(): unexpected result from MsgWaitForMultipleObjects: %d", __PRETTY_FUNCTION__, waitResult);
	result = false;
    }
    
    if (freeHandles) {
	CFAllocatorDeallocate(kCFAllocatorSystemDefault, handles);
    }
    
    return result;
}

#endif

struct __timeout_context {
    dispatch_source_t ds;
    CFRunLoopRef rl;
    int64_t termTSR;
};

static void __CFRunLoopTimeoutCancel(void *arg) {
    struct __timeout_context *context = (struct __timeout_context *)arg;
    CFRelease(context->rl);
    dispatch_release(context->ds);
    free(context);
}

static void __CFRunLoopTimeout(void *arg) {
    struct __timeout_context *context = (struct __timeout_context *)arg;
    context->termTSR = 0LL;
    CFRunLoopWakeUp(context->rl);
    // The interval is DISPATCH_TIME_FOREVER, so this won't fire again
}

#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
#elif DEPLOYMENT_TARGET_WINDOWS
#define MACH_PORT_NULL 0
#define mach_port_name_t HANDLE
#define _dispatch_get_main_queue_port_4CF _dispatch_get_main_queue_handle_4CF
#else
#error Unknown deployment target - CFRunLoop not functional
#endif

/* rl, rlm are locked on entrance and exit */
static int32_t __CFRunLoopRun(CFRunLoopRef rl, CFRunLoopModeRef rlm, CFTimeInterval seconds, Boolean stopAfterHandle, CFRunLoopModeRef previousMode) {
    int64_t startTSR = (int64_t)mach_absolute_time();

    if (__CFRunLoopIsStopped(rl)) {
        __CFRunLoopUnsetStopped(rl);
	return kCFRunLoopRunStopped;
    } else if (rlm->_stopped) {
	rlm->_stopped = false;
	return kCFRunLoopRunStopped;
    }

    mach_port_name_t dispatchPort = MACH_PORT_NULL;
    Boolean libdispatchQSafe = pthread_main_np() && ((HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY && NULL == previousMode) || (!HANDLE_DISPATCH_ON_BASE_INVOCATION_ONLY && 0 == _CFGetTSD(__CFTSDKeyIsInGCDMainQ)));
    if (libdispatchQSafe && (CFRunLoopGetMain() == rl) && CFSetContainsValue(rl->_commonModes, rlm->_name)) dispatchPort = _dispatch_get_main_queue_port_4CF();

    dispatch_source_t timeout_timer = NULL;
    struct __timeout_context *timeout_context = (struct __timeout_context *)malloc(sizeof(*timeout_context));
    if (seconds <= 0.0) { // instant timeout
        seconds = 0.0;
        timeout_context->termTSR = 0LL;
    } else if (seconds <= TIMER_INTERVAL_LIMIT) {
	dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, DISPATCH_QUEUE_OVERCOMMIT);
	timeout_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
        dispatch_retain(timeout_timer);
	timeout_context->ds = timeout_timer;
	timeout_context->rl = (CFRunLoopRef)CFRetain(rl);
	timeout_context->termTSR = startTSR + __CFTimeIntervalToTSR(seconds);
	dispatch_set_context(timeout_timer, timeout_context); // source gets ownership of context
	dispatch_source_set_event_handler_f(timeout_timer, __CFRunLoopTimeout);
        dispatch_source_set_cancel_handler_f(timeout_timer, __CFRunLoopTimeoutCancel);
        uint64_t nanos = (uint64_t)(seconds * 1000 * 1000 + 1) * 1000;
	dispatch_source_set_timer(timeout_timer, dispatch_time(DISPATCH_TIME_NOW, nanos), DISPATCH_TIME_FOREVER, 0);
	dispatch_resume(timeout_timer);
    } else { // infinite timeout
        seconds = 9999999999.0;
        timeout_context->termTSR = INT64_MAX;
    }

    Boolean didDispatchPortLastTime = true;
    int32_t retVal = 0;
    do {
        uint8_t msg_buffer[3 * 1024];
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
        mach_msg_header_t *msg = NULL;
#elif DEPLOYMENT_TARGET_WINDOWS
        HANDLE livePort = NULL;
        Boolean windowsMessageReceived = false;
#endif
	__CFPortSet waitSet = rlm->_portSet;

	rl->_ignoreWakeUps = false;

        if (rlm->_observerMask & kCFRunLoopBeforeTimers) __CFRunLoopDoObservers(rl, rlm, kCFRunLoopBeforeTimers);
        if (rlm->_observerMask & kCFRunLoopBeforeSources) __CFRunLoopDoObservers(rl, rlm, kCFRunLoopBeforeSources);

	__CFRunLoopDoBlocks(rl, rlm);

        Boolean sourceHandledThisLoop = __CFRunLoopDoSources0(rl, rlm, stopAfterHandle);
        if (sourceHandledThisLoop) {
            __CFRunLoopDoBlocks(rl, rlm);
	}

        Boolean poll = sourceHandledThisLoop || (0LL == timeout_context->termTSR);

        if (MACH_PORT_NULL != dispatchPort && !didDispatchPortLastTime) {
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
            msg = (mach_msg_header_t *)msg_buffer;
            if (__CFRunLoopServiceMachPort(dispatchPort, &msg, sizeof(msg_buffer), 0)) {
                goto handle_msg;
            }
#elif DEPLOYMENT_TARGET_WINDOWS
            if (__CFRunLoopWaitForMultipleObjects(NULL, &dispatchPort, 0, 0, &livePort, NULL)) {
                goto handle_msg;
            }
#endif
        }

        didDispatchPortLastTime = false;

	if (!poll && (rlm->_observerMask & kCFRunLoopBeforeWaiting)) __CFRunLoopDoObservers(rl, rlm, kCFRunLoopBeforeWaiting);
	__CFRunLoopSetSleeping(rl);
	// do not do any user callouts after this point (after notifying of sleeping)

        // Must push the local-to-this-activation ports in on every loop
        // iteration, as this mode could be run re-entrantly and we don't
        // want these ports to get serviced.

        __CFPortSetInsert(dispatchPort, waitSet);

	__CFRunLoopModeUnlock(rlm);
	__CFRunLoopUnlock(rl);

#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
        if (kCFUseCollectableAllocator) {
            objc_clear_stack(0);
            memset(msg_buffer, 0, sizeof(msg_buffer));
        }
        msg = (mach_msg_header_t *)msg_buffer;
        __CFRunLoopServiceMachPort(waitSet, &msg, sizeof(msg_buffer), poll ? 0 : TIMEOUT_INFINITY);
#elif DEPLOYMENT_TARGET_WINDOWS
        // Here, use the app-supplied message queue mask. They will set this if they are interested in having this run loop receive windows messages.
        // Note: don't pass 0 for polling, or this thread will never yield the CPU.
        __CFRunLoopWaitForMultipleObjects(waitSet, NULL, poll ? 0 : TIMEOUT_INFINITY, rlm->_msgQMask, &livePort, &windowsMessageReceived);
#endif
        
        __CFRunLoopLock(rl);
        __CFRunLoopModeLock(rlm);

        // Must remove the local-to-this-activation ports in on every loop
        // iteration, as this mode could be run re-entrantly and we don't
        // want these ports to get serviced. Also, we don't want them left
        // in there if this function returns.

        __CFPortSetRemove(dispatchPort, waitSet);

	rl->_ignoreWakeUps = true;

        // user callouts now OK again
	__CFRunLoopUnsetSleeping(rl);
	if (!poll && (rlm->_observerMask & kCFRunLoopAfterWaiting)) __CFRunLoopDoObservers(rl, rlm, kCFRunLoopAfterWaiting);

        handle_msg:;
	rl->_ignoreWakeUps = true;

#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
        mach_port_t livePort = msg ? msg->msgh_local_port : MACH_PORT_NULL;
#endif
#if DEPLOYMENT_TARGET_WINDOWS
        if (windowsMessageReceived) {
            // These Win32 APIs cause a callout, so make sure we're unlocked first and relocked after
            __CFRunLoopModeUnlock(rlm);
	    __CFRunLoopUnlock(rl);

            if (rlm->_msgPump) {
                rlm->_msgPump();
            } else {
                MSG msg;
                if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_NOYIELD)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
            
            __CFRunLoopLock(rl);
	    __CFRunLoopModeLock(rlm);
 	    sourceHandledThisLoop = true;
        } else
#endif
        if (MACH_PORT_NULL == livePort) {
            // handle nothing
        } else if (livePort == rl->_wakeUpPort) {
            // do nothing on Mac OS
#if DEPLOYMENT_TARGET_WINDOWS
            // Always reset the wake up port, or risk spinning forever
            ResetEvent(rl->_wakeUpPort);
#endif
        } else if (livePort == rlm->_timerPort) {
#if DEPLOYMENT_TARGET_WINDOWS
            // We use a manual reset timer to ensure that we don't miss timers firing because the run loop did the wakeUpPort this time
            // The only way to reset a timer is to reset the timer using SetWaitableTimer. We don't want it to fire again though, so we set the timeout to a large negative value. The timer may be reset again inside the timer handling code.
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = LONG_MIN;
            SetWaitableTimer(rlm->_timerPort, &dueTime, 0, NULL, NULL, FALSE);
#endif
	    __CFRunLoopDoTimers(rl, rlm, mach_absolute_time());
        } else if (livePort == dispatchPort) {
	    __CFRunLoopModeUnlock(rlm);
	    __CFRunLoopUnlock(rl);
            _CFSetTSD(__CFTSDKeyIsInGCDMainQ, (void *)6, NULL);
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
 	    _dispatch_main_queue_callback_4CF(msg);
#elif DEPLOYMENT_TARGET_WINDOWS
            _dispatch_main_queue_callback_4CF();
#endif
            _CFSetTSD(__CFTSDKeyIsInGCDMainQ, (void *)0, NULL);
	    __CFRunLoopLock(rl);
	    __CFRunLoopModeLock(rlm);
 	    sourceHandledThisLoop = true;
            didDispatchPortLastTime = true;
        } else {
            // Despite the name, this works for windows handles as well
            CFRunLoopSourceRef rls = __CFRunLoopModeFindSourceForMachPort(rl, rlm, livePort);
            if (rls) {
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
		mach_msg_header_t *reply = NULL;
		sourceHandledThisLoop = __CFRunLoopDoSource1(rl, rlm, rls, msg, msg->msgh_size, &reply) || sourceHandledThisLoop;
		if (NULL != reply) {
		    (void)mach_msg(reply, MACH_SEND_MSG, reply->msgh_size, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
		    CFAllocatorDeallocate(kCFAllocatorSystemDefault, reply);
		}
#elif DEPLOYMENT_TARGET_WINDOWS
                sourceHandledThisLoop = __CFRunLoopDoSource1(rl, rlm, rls) || sourceHandledThisLoop;
#endif
	    }
        } 
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
        if (msg && msg != (mach_msg_header_t *)msg_buffer) free(msg);
#endif
        
	__CFRunLoopDoBlocks(rl, rlm);

	if (sourceHandledThisLoop && stopAfterHandle) {
	    retVal = kCFRunLoopRunHandledSource;
        } else if (timeout_context->termTSR < (int64_t)mach_absolute_time()) {
            retVal = kCFRunLoopRunTimedOut;
	} else if (__CFRunLoopIsStopped(rl)) {
            __CFRunLoopUnsetStopped(rl);
	    retVal = kCFRunLoopRunStopped;
	} else if (rlm->_stopped) {
	    rlm->_stopped = false;
	    retVal = kCFRunLoopRunStopped;
	} else if (__CFRunLoopModeIsEmpty(rl, rlm, previousMode)) {
	    retVal = kCFRunLoopRunFinished;
	}
    } while (0 == retVal);

    if (timeout_timer) {
        dispatch_source_cancel(timeout_timer);
        dispatch_release(timeout_timer);
    } else {
        free(timeout_context);
    }

    return retVal;
}

SInt32 CFRunLoopRunSpecific(CFRunLoopRef rl, CFStringRef modeName, CFTimeInterval seconds, Boolean returnAfterSourceHandled) {     /* DOES CALLOUT */
    CHECK_FOR_FORK();
    if (__CFRunLoopIsDeallocating(rl)) return kCFRunLoopRunFinished;
    __CFRunLoopLock(rl);
    CFRunLoopModeRef currentMode = __CFRunLoopFindMode(rl, modeName, false);
    if (NULL == currentMode || __CFRunLoopModeIsEmpty(rl, currentMode, rl->_currentMode)) {
	Boolean did = false;
	if (currentMode) __CFRunLoopModeUnlock(currentMode);
	__CFRunLoopUnlock(rl);
	return did ? kCFRunLoopRunHandledSource : kCFRunLoopRunFinished;
    }
    uint32_t *previousStopped = (uint32_t *)rl->_stopped;
    rl->_stopped = NULL;
    CFRunLoopModeRef previousMode = rl->_currentMode;
    rl->_currentMode = currentMode;
    int32_t result;
	if (currentMode->_observerMask & kCFRunLoopEntry ) __CFRunLoopDoObservers(rl, currentMode, kCFRunLoopEntry);
	result = __CFRunLoopRun(rl, currentMode, seconds, returnAfterSourceHandled, previousMode);
	if (currentMode->_observerMask & kCFRunLoopExit ) __CFRunLoopDoObservers(rl, currentMode, kCFRunLoopExit);
        __CFRunLoopModeUnlock(currentMode);
	if (rl->_stopped) CFAllocatorDeallocate(kCFAllocatorSystemDefault, (uint32_t *)rl->_stopped);
	rl->_stopped = previousStopped;
	rl->_currentMode = previousMode;
    __CFRunLoopUnlock(rl);
    return result;
}

void CFRunLoopRun(void) {	/* DOES CALLOUT */
    int32_t result;
    do {
        result = CFRunLoopRunSpecific(CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, 1.0e10, false);
        CHECK_FOR_FORK();
    } while (kCFRunLoopRunStopped != result && kCFRunLoopRunFinished != result);
}

SInt32 CFRunLoopRunInMode(CFStringRef modeName, CFTimeInterval seconds, Boolean returnAfterSourceHandled) {     /* DOES CALLOUT */
    CHECK_FOR_FORK();
    return CFRunLoopRunSpecific(CFRunLoopGetCurrent(), modeName, seconds, returnAfterSourceHandled);
}

CFAbsoluteTime CFRunLoopGetNextTimerFireDate(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    __CFRunLoopLock(rl);
    CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
    CFAbsoluteTime at = 0.0;
    CFRunLoopTimerRef timer = (rlm && rlm->_timers && 0 < CFArrayGetCount(rlm->_timers)) ? (CFRunLoopTimerRef)CFArrayGetValueAtIndex(rlm->_timers, 0) : NULL;
    if (timer) at = CFRunLoopTimerGetNextFireDate(timer);
    if (rlm) __CFRunLoopModeUnlock(rlm);
    __CFRunLoopUnlock(rl);
    return at;
}

Boolean CFRunLoopIsWaiting(CFRunLoopRef rl) {
    CHECK_FOR_FORK();
    return __CFRunLoopIsSleeping(rl);
}

void CFRunLoopWakeUp(CFRunLoopRef rl) {
    CHECK_FOR_FORK();
    // This lock is crucial to ignorable wakeups, do not remove it.
    __CFRunLoopLock(rl);
    if (rl->_ignoreWakeUps) {
        __CFRunLoopUnlock(rl);
        return;
    }
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
    kern_return_t ret;
    /* We unconditionally try to send the message, since we don't want
     * to lose a wakeup, but the send may fail if there is already a
     * wakeup pending, since the queue length is 1. */
    ret = __CFSendTrivialMachMessage(rl->_wakeUpPort, 0, MACH_SEND_TIMEOUT, 0);
    if (ret != MACH_MSG_SUCCESS && ret != MACH_SEND_TIMED_OUT) {
	HALT;
    }
#else
    SetEvent(rl->_wakeUpPort);
#endif
    __CFRunLoopUnlock(rl);
}

void CFRunLoopStop(CFRunLoopRef rl) {
    Boolean doWake = false;
    CHECK_FOR_FORK();
    __CFRunLoopLock(rl);
    if (rl->_currentMode) {
        __CFRunLoopSetStopped(rl);
        doWake = true;
    }
    __CFRunLoopUnlock(rl);
    if (doWake) {
        CFRunLoopWakeUp(rl);
    }
}

CF_EXPORT void _CFRunLoopStopMode(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    __CFRunLoopLock(rl);
    rlm = __CFRunLoopFindMode(rl, modeName, true);
    if (NULL != rlm) {
	rlm->_stopped = true;
	__CFRunLoopModeUnlock(rlm);
    }
    __CFRunLoopUnlock(rl);
    CFRunLoopWakeUp(rl);
}

CF_EXPORT Boolean _CFRunLoopModeContainsMode(CFRunLoopRef rl, CFStringRef modeName, CFStringRef candidateContainedName) {
    CHECK_FOR_FORK();
    return false;
}

void CFRunLoopPerformBlock(CFRunLoopRef rl, CFTypeRef mode, void (^block)(void)) {
    CHECK_FOR_FORK();
    if (CFStringGetTypeID() == CFGetTypeID(mode)) {
	mode = CFStringCreateCopy(kCFAllocatorSystemDefault, (CFStringRef)mode);
        __CFRunLoopLock(rl);
	// ensure mode exists
        CFRunLoopModeRef currentMode = __CFRunLoopFindMode(rl, (CFStringRef)mode, true);
        if (currentMode) __CFRunLoopModeUnlock(currentMode);
        __CFRunLoopUnlock(rl);
    } else if (CFArrayGetTypeID() == CFGetTypeID(mode)) {
        CFIndex cnt = CFArrayGetCount((CFArrayRef)mode);
	const void **values = (const void **)malloc(sizeof(const void *) * cnt);
        CFArrayGetValues((CFArrayRef)mode, CFRangeMake(0, cnt), values);
	mode = CFSetCreate(kCFAllocatorSystemDefault, values, cnt, &kCFTypeSetCallBacks);
        __CFRunLoopLock(rl);
	// ensure modes exist
	for (CFIndex idx = 0; idx < cnt; idx++) {
            CFRunLoopModeRef currentMode = __CFRunLoopFindMode(rl, (CFStringRef)values[idx], true);
            if (currentMode) __CFRunLoopModeUnlock(currentMode);
	}
        __CFRunLoopUnlock(rl);
	free(values);
    } else if (CFSetGetTypeID() == CFGetTypeID(mode)) {
        CFIndex cnt = CFSetGetCount((CFSetRef)mode);
	const void **values = (const void **)malloc(sizeof(const void *) * cnt);
        CFSetGetValues((CFSetRef)mode, values);
	mode = CFSetCreate(kCFAllocatorSystemDefault, values, cnt, &kCFTypeSetCallBacks);
        __CFRunLoopLock(rl);
	// ensure modes exist
	for (CFIndex idx = 0; idx < cnt; idx++) {
            CFRunLoopModeRef currentMode = __CFRunLoopFindMode(rl, (CFStringRef)values[idx], true);
            if (currentMode) __CFRunLoopModeUnlock(currentMode);
	}
        __CFRunLoopUnlock(rl);
	free(values);
    } else {
	mode = NULL;
    }
#if DEPLOYMENT_TARGET_WINDOWS
    // <rdar://problem/7659483> clang ObjC rewriter: __typeof(block) not rewritten correctly
    block = (void (^)(void))_Block_copy(block);
#else
    block = Block_copy(block);
#endif
    if (!mode || !block) {
	if (mode) CFRelease(mode);
	if (block) Block_release(block);
	return;
    }
    __CFRunLoopLock(rl);
    struct _block_item *new_item = (struct _block_item *)malloc(sizeof(struct _block_item));
    new_item->_next = NULL;
    new_item->_mode = mode;
    new_item->_block = block;
    if (!rl->_blocks_tail) {
	rl->_blocks_head = new_item;
    } else {
	rl->_blocks_tail->_next = new_item;
    }
    rl->_blocks_tail = new_item;
    __CFRunLoopUnlock(rl);
}

Boolean CFRunLoopContainsSource(CFRunLoopRef rl, CFRunLoopSourceRef rls, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    Boolean hasValue = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	if (NULL != rl->_commonModeItems) {
	    hasValue = CFSetContainsValue(rl->_commonModeItems, rls);
	}
    } else {
	rlm = __CFRunLoopFindMode(rl, modeName, false);
	if (NULL != rlm) {
	    hasValue = (rlm->_sources0 ? CFSetContainsValue(rlm->_sources0, rls) : false) || (rlm->_sources1 ? CFSetContainsValue(rlm->_sources1, rls) : false);
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
    return hasValue;
}

void CFRunLoopAddSource(CFRunLoopRef rl, CFRunLoopSourceRef rls, CFStringRef modeName) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    if (__CFRunLoopIsDeallocating(rl)) return;
    if (!__CFIsValid(rls)) return;
    Boolean doVer0Callout = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
	if (NULL == rl->_commonModeItems) {
	    rl->_commonModeItems = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
	}
	CFSetAddValue(rl->_commonModeItems, rls);
	if (NULL != set) {
	    CFTypeRef context[2] = {rl, rls};
	    /* add new item to all common-modes */
	    CFSetApplyFunction(set, (__CFRunLoopAddItemToCommonModes), (void *)context);
	    CFRelease(set);
	}
    } else {
	CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, true);
	if (NULL != rlm && NULL == rlm->_sources0) {
	    rlm->_sources0 = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
	    rlm->_sources1 = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
	    rlm->_portToV1SourceMap = CFDictionaryCreateMutable(kCFAllocatorSystemDefault, 0, NULL, NULL);
	}
	if (NULL != rlm && !CFSetContainsValue(rlm->_sources0, rls) && !CFSetContainsValue(rlm->_sources1, rls)) {
	    if (0 == rls->_context.version0.version) {
	        CFSetAddValue(rlm->_sources0, rls);
	    } else if (1 == rls->_context.version0.version) {
	        CFSetAddValue(rlm->_sources1, rls);
		__CFPort src_port = rls->_context.version1.getPort(rls->_context.version1.info);
		if (CFPORT_NULL != src_port) {
		    CFDictionarySetValue(rlm->_portToV1SourceMap, (const void *)(uintptr_t)src_port, rls);
		    __CFPortSetInsert(src_port, rlm->_portSet);
	        }
	    }
	    __CFRunLoopSourceLock(rls);
	    if (NULL == rls->_runLoops) {
	        rls->_runLoops = CFBagCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeBagCallBacks); // sources retain run loops!
	    }
	    CFBagAddValue(rls->_runLoops, rl);
	    __CFRunLoopSourceUnlock(rls);
	    if (0 == rls->_context.version0.version) {
	        if (NULL != rls->_context.version0.schedule) {
	            doVer0Callout = true;
	        }
	    }
	}
        if (NULL != rlm) {
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
    if (doVer0Callout) {
        // although it looses some protection for the source, we have no choice but
        // to do this after unlocking the run loop and mode locks, to avoid deadlocks
        // where the source wants to take a lock which is already held in another
        // thread which is itself waiting for a run loop/mode lock
	rls->_context.version0.schedule(rls->_context.version0.info, rl, modeName);	/* CALLOUT */
    }
}

void CFRunLoopRemoveSource(CFRunLoopRef rl, CFRunLoopSourceRef rls, CFStringRef modeName) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    Boolean doVer0Callout = false, doRLSRelease = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	if (NULL != rl->_commonModeItems && CFSetContainsValue(rl->_commonModeItems, rls)) {
	    CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
	    CFSetRemoveValue(rl->_commonModeItems, rls);
	    if (NULL != set) {
		CFTypeRef context[2] = {rl, rls};
		/* remove new item from all common-modes */
		CFSetApplyFunction(set, (__CFRunLoopRemoveItemFromCommonModes), (void *)context);
		CFRelease(set);
	    }
	} else {
	}
    } else {
	CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
	if (NULL != rlm && ((NULL != rlm->_sources0 && CFSetContainsValue(rlm->_sources0, rls)) || (NULL != rlm->_sources1 && CFSetContainsValue(rlm->_sources1, rls)))) {
	    CFRetain(rls);
	    if (1 == rls->_context.version0.version) {
		__CFPort src_port = rls->_context.version1.getPort(rls->_context.version1.info);
                if (CFPORT_NULL != src_port) {
		    CFDictionaryRemoveValue(rlm->_portToV1SourceMap, (const void *)(uintptr_t)src_port);
                    __CFPortSetRemove(src_port, rlm->_portSet);
                }
	    }
	    CFSetRemoveValue(rlm->_sources0, rls);
	    CFSetRemoveValue(rlm->_sources1, rls);
            __CFRunLoopSourceLock(rls);
            if (NULL != rls->_runLoops) {
                CFBagRemoveValue(rls->_runLoops, rl);
            }
            __CFRunLoopSourceUnlock(rls);
	    if (0 == rls->_context.version0.version) {
	        if (NULL != rls->_context.version0.schedule) {
	            doVer0Callout = true;
	        }
	    }
	    doRLSRelease = true;
	}
        if (NULL != rlm) {
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
    if (doVer0Callout) {
        // although it looses some protection for the source, we have no choice but
        // to do this after unlocking the run loop and mode locks, to avoid deadlocks
        // where the source wants to take a lock which is already held in another
        // thread which is itself waiting for a run loop/mode lock
        rls->_context.version0.cancel(rls->_context.version0.info, rl, modeName);	/* CALLOUT */
    }
    if (doRLSRelease) CFRelease(rls);
}

static void __CFRunLoopRemoveSourcesFromCommonMode(const void *value, void *ctx) {
    CFStringRef modeName = (CFStringRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)ctx;
    __CFRunLoopRemoveAllSources(rl, modeName);
}

static void __CFRunLoopRemoveSourceFromMode(const void *value, void *ctx) {
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)value;
    CFRunLoopRef rl = (CFRunLoopRef)(((CFTypeRef *)ctx)[0]);
    CFStringRef modeName = (CFStringRef)(((CFTypeRef *)ctx)[1]);
    CFRunLoopRemoveSource(rl, rls, modeName);
}

static void __CFRunLoopRemoveAllSources(CFRunLoopRef rl, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	if (NULL != rl->_commonModeItems) {
	    CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
	    if (NULL != set) {
                CFSetApplyFunction(set, (__CFRunLoopRemoveSourcesFromCommonMode), (void *)rl);
		CFRelease(set);
	    }
	} else {
	}
    } else {
	rlm = __CFRunLoopFindMode(rl, modeName, false);
	if (NULL != rlm && NULL != rlm->_sources0) {
	    CFSetRef set = CFSetCreateCopy(kCFAllocatorSystemDefault, rlm->_sources0);
            CFTypeRef context[2] = {rl, modeName};
            CFSetApplyFunction(set, (__CFRunLoopRemoveSourceFromMode), (void *)context);
	    CFRelease(set);
	}
	if (NULL != rlm && NULL != rlm->_sources1) {
	    CFSetRef set = CFSetCreateCopy(kCFAllocatorSystemDefault, rlm->_sources1);
            CFTypeRef context[2] = {rl, modeName};
            CFSetApplyFunction(set, (__CFRunLoopRemoveSourceFromMode), (void *)context);
	    CFRelease(set);
	}
        if (NULL != rlm) {
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
}

Boolean CFRunLoopContainsObserver(CFRunLoopRef rl, CFRunLoopObserverRef rlo, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    Boolean hasValue = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	if (NULL != rl->_commonModeItems) {
	    hasValue = CFSetContainsValue(rl->_commonModeItems, rlo);
	}
    } else {
	rlm = __CFRunLoopFindMode(rl, modeName, false);
	if (NULL != rlm && NULL != rlm->_observers) {
	    hasValue = CFArrayContainsValue(rlm->_observers, CFRangeMake(0, CFArrayGetCount(rlm->_observers)), rlo);
	}
        if (NULL != rlm) {
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
    return hasValue;
}

void CFRunLoopAddObserver(CFRunLoopRef rl, CFRunLoopObserverRef rlo, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    if (__CFRunLoopIsDeallocating(rl)) return;
    if (!__CFIsValid(rlo) || (NULL != rlo->_runLoop && rlo->_runLoop != rl)) return;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
	if (NULL == rl->_commonModeItems) {
	    rl->_commonModeItems = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
	}
	CFSetAddValue(rl->_commonModeItems, rlo);
	if (NULL != set) {
	    CFTypeRef context[2] = {rl, rlo};
	    /* add new item to all common-modes */
	    CFSetApplyFunction(set, (__CFRunLoopAddItemToCommonModes), (void *)context);
	    CFRelease(set);
	}
    } else {
	rlm = __CFRunLoopFindMode(rl, modeName, true);
	if (NULL != rlm && NULL == rlm->_observers) {
	    rlm->_observers = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeArrayCallBacks);
	}
	if (NULL != rlm && !CFArrayContainsValue(rlm->_observers, CFRangeMake(0, CFArrayGetCount(rlm->_observers)), rlo)) {
            Boolean inserted = false;
            for (CFIndex idx = CFArrayGetCount(rlm->_observers); idx--; ) {
                CFRunLoopObserverRef obs = (CFRunLoopObserverRef)CFArrayGetValueAtIndex(rlm->_observers, idx);
                if (obs->_order <= rlo->_order) {
                    CFArrayInsertValueAtIndex(rlm->_observers, idx + 1, rlo);
                    inserted = true;
                    break;
                }
            }
            if (!inserted) {
	        CFArrayInsertValueAtIndex(rlm->_observers, 0, rlo);
            }
	    rlm->_observerMask |= rlo->_activities;
	    __CFRunLoopObserverSchedule(rlo, rl, rlm);
	}
        if (NULL != rlm) {
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
}

void CFRunLoopRemoveObserver(CFRunLoopRef rl, CFRunLoopObserverRef rlo, CFStringRef modeName) {
    CHECK_FOR_FORK();
    CFRunLoopModeRef rlm;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	if (NULL != rl->_commonModeItems && CFSetContainsValue(rl->_commonModeItems, rlo)) {
	    CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
	    CFSetRemoveValue(rl->_commonModeItems, rlo);
	    if (NULL != set) {
		CFTypeRef context[2] = {rl, rlo};
		/* remove new item from all common-modes */
		CFSetApplyFunction(set, (__CFRunLoopRemoveItemFromCommonModes), (void *)context);
		CFRelease(set);
	    }
	} else {
	}
    } else {
	rlm = __CFRunLoopFindMode(rl, modeName, false);
	if (NULL != rlm && NULL != rlm->_observers) {
	    CFRetain(rlo);
            CFIndex idx = CFArrayGetFirstIndexOfValue(rlm->_observers, CFRangeMake(0, CFArrayGetCount(rlm->_observers)), rlo);
            if (kCFNotFound != idx) {
                CFArrayRemoveValueAtIndex(rlm->_observers, idx);
	        __CFRunLoopObserverCancel(rlo, rl, rlm);
            }
	    CFRelease(rlo);
	}
        if (NULL != rlm) {
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
}

Boolean CFRunLoopContainsTimer(CFRunLoopRef rl, CFRunLoopTimerRef rlt, CFStringRef modeName) {
    CHECK_FOR_FORK();
    if (NULL == rlt->_runLoop || rl != rlt->_runLoop) return false;
    Boolean hasValue = false;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	if (NULL != rl->_commonModeItems) {
	    hasValue = CFSetContainsValue(rl->_commonModeItems, rlt);
	}
    } else {
	CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
	if (NULL != rlm && NULL != rlm->_timers) {
            CFIndex idx = CFArrayGetFirstIndexOfValue(rlm->_timers, CFRangeMake(0, CFArrayGetCount(rlm->_timers)), rlt);
            hasValue = (kCFNotFound != idx);
	} 
        if (NULL != rlm) {
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
    return hasValue;
}

void CFRunLoopAddTimer(CFRunLoopRef rl, CFRunLoopTimerRef rlt, CFStringRef modeName) {
    CHECK_FOR_FORK();
    if (__CFRunLoopIsDeallocating(rl)) return;
    if (!__CFIsValid(rlt) || (NULL != rlt->_runLoop && rlt->_runLoop != rl)) return;
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
	if (NULL == rl->_commonModeItems) {
	    rl->_commonModeItems = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
	}
	CFSetAddValue(rl->_commonModeItems, rlt);
	if (NULL != set) {
	    CFTypeRef context[2] = {rl, rlt};
	    /* add new item to all common-modes */
	    CFSetApplyFunction(set, (__CFRunLoopAddItemToCommonModes), (void *)context);
	    CFRelease(set);
	}
    } else {
	CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, true);
	if (NULL != rlm && NULL == rlm->_timers) {
	    CFArrayCallBacks cb = kCFTypeArrayCallBacks;
	    cb.equal = NULL;
	    rlm->_timers = CFArrayCreateMutable(kCFAllocatorSystemDefault, 0, &cb);
	}
	if (NULL != rlm && !CFSetContainsValue(rlt->_rlModes, rlm->_name)) {
            __CFRunLoopTimerLock(rlt);
            if (NULL == rlt->_runLoop) {
		rlt->_runLoop = rl;
  	    } else if (rl != rlt->_runLoop) {
                __CFRunLoopTimerUnlock(rlt);
	        __CFRunLoopModeUnlock(rlm);
                __CFRunLoopUnlock(rl);
		return;
	    }
  	    CFSetAddValue(rlt->_rlModes, rlm->_name);
            __CFRunLoopTimerUnlock(rlt);
            __CFRunLoopTimerFireTSRLock();
	    __CFRepositionTimerInMode(rlm, rlt, false);
            __CFRunLoopTimerFireTSRUnlock();
            if (!_CFExecutableLinkedOnOrAfter(CFSystemVersionLion)) {
                // Normally we don't do this on behalf of clients, but for
                // backwards compatibility due to the change in timer handling...
                if (rl != CFRunLoopGetCurrent()) CFRunLoopWakeUp(rl);
            }
	}
        if (NULL != rlm) {
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
}

void CFRunLoopRemoveTimer(CFRunLoopRef rl, CFRunLoopTimerRef rlt, CFStringRef modeName) {
    CHECK_FOR_FORK();
    __CFRunLoopLock(rl);
    if (modeName == kCFRunLoopCommonModes) {
	if (NULL != rl->_commonModeItems && CFSetContainsValue(rl->_commonModeItems, rlt)) {
	    CFSetRef set = rl->_commonModes ? CFSetCreateCopy(kCFAllocatorSystemDefault, rl->_commonModes) : NULL;
	    CFSetRemoveValue(rl->_commonModeItems, rlt);
	    if (NULL != set) {
		CFTypeRef context[2] = {rl, rlt};
		/* remove new item from all common-modes */
		CFSetApplyFunction(set, (__CFRunLoopRemoveItemFromCommonModes), (void *)context);
		CFRelease(set);
	    }
	} else {
	}
    } else {
	CFRunLoopModeRef rlm = __CFRunLoopFindMode(rl, modeName, false);
        CFIndex idx = kCFNotFound;
	if (NULL != rlm && NULL != rlm->_timers) {
            idx = CFArrayGetFirstIndexOfValue(rlm->_timers, CFRangeMake(0, CFArrayGetCount(rlm->_timers)), rlt);
        }
        if (kCFNotFound != idx) {
            __CFRunLoopTimerLock(rlt);
            CFSetRemoveValue(rlt->_rlModes, rlm->_name);
            if (0 == CFSetGetCount(rlt->_rlModes)) {
                rlt->_runLoop = NULL;
            }
            __CFRunLoopTimerUnlock(rlt);
	    CFArrayRemoveValueAtIndex(rlm->_timers, idx);
            if (0 == CFArrayGetCount(rlm->_timers)) {
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
                AbsoluteTime dummy;
                mk_timer_cancel(rlm->_timerPort, &dummy);
#elif DEPLOYMENT_TARGET_WINDOWS
                CancelWaitableTimer(rlm->_timerPort);
#endif
            } else if (0 == idx) {
                CFRunLoopTimerRef nextTimer = NULL;
                for (CFIndex idx = 0, cnt = CFArrayGetCount(rlm->_timers); idx < cnt; idx++) {
                    CFRunLoopTimerRef t = (CFRunLoopTimerRef)CFArrayGetValueAtIndex(rlm->_timers, idx);
                    if (!__CFRunLoopTimerIsFiring(t)) {
                        nextTimer = t;
                        break;
                    }
                }
                if (nextTimer) {
		    int64_t fireTSR = nextTimer->_fireTSR;
		    fireTSR = (fireTSR / tenus + 1) * tenus;
#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
		    mk_timer_arm(rlm->_timerPort, __CFUInt64ToAbsoluteTime(fireTSR));
#elif DEPLOYMENT_TARGET_WINDOWS
                    LARGE_INTEGER dueTime;
                    dueTime.QuadPart = __CFTSRToFiletime(fireTSR);
                    SetWaitableTimer(rlm->_timerPort, &dueTime, 0, NULL, NULL, FALSE);
#endif
                }
            }
	}
        if (NULL != rlm) {
	    __CFRunLoopModeUnlock(rlm);
	}
    }
    __CFRunLoopUnlock(rl);
}

/* CFRunLoopSource */

static Boolean __CFRunLoopSourceEqual(CFTypeRef cf1, CFTypeRef cf2) {	/* DOES CALLOUT */
    CFRunLoopSourceRef rls1 = (CFRunLoopSourceRef)cf1;
    CFRunLoopSourceRef rls2 = (CFRunLoopSourceRef)cf2;
    if (rls1 == rls2) return true;
    if (__CFIsValid(rls1) != __CFIsValid(rls2)) return false;
    if (rls1->_order != rls2->_order) return false;
    if (rls1->_context.version0.version != rls2->_context.version0.version) return false;
    if (rls1->_context.version0.hash != rls2->_context.version0.hash) return false;
    if (rls1->_context.version0.equal != rls2->_context.version0.equal) return false;
    if (0 == rls1->_context.version0.version && rls1->_context.version0.perform != rls2->_context.version0.perform) return false;
    if (1 == rls1->_context.version0.version && rls1->_context.version1.perform != rls2->_context.version1.perform) return false;
    if (rls1->_context.version0.equal)
	return rls1->_context.version0.equal(rls1->_context.version0.info, rls2->_context.version0.info);
    return (rls1->_context.version0.info == rls2->_context.version0.info);
}

static CFHashCode __CFRunLoopSourceHash(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)cf;
    if (rls->_context.version0.hash)
	return rls->_context.version0.hash(rls->_context.version0.info);
    return (CFHashCode)rls->_context.version0.info;
}

static CFStringRef __CFRunLoopSourceCopyDescription(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)cf;
    CFStringRef result;
    CFStringRef contextDesc = NULL;
    if (NULL != rls->_context.version0.copyDescription) {
	contextDesc = rls->_context.version0.copyDescription(rls->_context.version0.info);
    }
    if (NULL == contextDesc) {
	void *addr = rls->_context.version0.version == 0 ? (void *)rls->_context.version0.perform : (rls->_context.version0.version == 1 ? (void *)rls->_context.version1.perform : NULL);
#if DEPLOYMENT_TARGET_WINDOWS
	contextDesc = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopSource context>{version = %ld, info = %p, callout = %p}"), rls->_context.version0.version, rls->_context.version0.info, addr);
#elif DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
	Dl_info info;
	const char *name = (dladdr(addr, &info) && info.dli_saddr == addr && info.dli_sname) ? info.dli_sname : "???";
	contextDesc = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopSource context>{version = %ld, info = %p, callout = %s (%p)}"), rls->_context.version0.version, rls->_context.version0.info, name, addr);
#else
#error Unknown or unspecified DEPLOYMENT_TARGET
#endif
    }
#if DEPLOYMENT_TARGET_WINDOWS
    result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopSource %p [%p]>{signalled = %s, valid = %s, order = %d, context = %@}"), cf, CFGetAllocator(rls), __CFRunLoopSourceIsSignaled(rls) ? "Yes" : "No", __CFIsValid(rls) ? "Yes" : "No", rls->_order, contextDesc);
#else
    result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopSource %p [%p]>{signalled = %s, valid = %s, order = %d, context = %@}"), cf, CFGetAllocator(rls), __CFRunLoopSourceIsSignaled(rls) ? "Yes" : "No", __CFIsValid(rls) ? "Yes" : "No", rls->_order, contextDesc);
#endif
    CFRelease(contextDesc);
    return result;
}

static void __CFRunLoopSourceDeallocate(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)cf;
    CFRunLoopSourceInvalidate(rls);
    if (rls->_context.version0.release) {
	rls->_context.version0.release(rls->_context.version0.info);
    }
    pthread_mutex_destroy(&rls->_lock);
    memset((char *)cf + sizeof(CFRuntimeBase), 0, sizeof(struct __CFRunLoopSource) - sizeof(CFRuntimeBase));
}

static const CFRuntimeClass __CFRunLoopSourceClass = {
    _kCFRuntimeScannedObject,
    "CFRunLoopSource",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopSourceDeallocate,
    __CFRunLoopSourceEqual,
    __CFRunLoopSourceHash,
    NULL,      // 
    __CFRunLoopSourceCopyDescription
};

__private_extern__ void __CFRunLoopSourceInitialize(void) {
    __kCFRunLoopSourceTypeID = _CFRuntimeRegisterClass(&__CFRunLoopSourceClass);
}

CFTypeID CFRunLoopSourceGetTypeID(void) {
    return __kCFRunLoopSourceTypeID;
}

CFRunLoopSourceRef CFRunLoopSourceCreate(CFAllocatorRef allocator, CFIndex order, CFRunLoopSourceContext *context) {
    CHECK_FOR_FORK();
    CFRunLoopSourceRef memory;
    uint32_t size;
    if (NULL == context) HALT;
    size = sizeof(struct __CFRunLoopSource) - sizeof(CFRuntimeBase);
    memory = (CFRunLoopSourceRef)_CFRuntimeCreateInstance(allocator, __kCFRunLoopSourceTypeID, size, NULL);
    if (NULL == memory) {
	return NULL;
    }
    __CFSetValid(memory);
    __CFRunLoopSourceUnsetSignaled(memory);
    __CFRunLoopLockInit(&memory->_lock);
    memory->_bits = 0;
    memory->_order = order;
    memory->_runLoops = NULL;
    size = 0;
    switch (context->version) {
    case 0:
	size = sizeof(CFRunLoopSourceContext);
	break;
    case 1:
	size = sizeof(CFRunLoopSourceContext1);
	break;
    }
    objc_memmove_collectable(&memory->_context, context, size);
    if (context->retain) {
	memory->_context.version0.info = (void *)context->retain(context->info);
    }
    return memory;
}

CFIndex CFRunLoopSourceGetOrder(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rls, __kCFRunLoopSourceTypeID);
    return rls->_order;
}

static void __CFRunLoopSourceWakeUpLoop(const void *value, void *context) {
    CFRunLoopWakeUp((CFRunLoopRef)value);
}

static void __CFRunLoopSourceRemoveFromRunLoop(const void *value, void *context) {
    CFRunLoopRef rl = (CFRunLoopRef)value;
    CFTypeRef *params = (CFTypeRef *)context;
    CFRunLoopSourceRef rls = (CFRunLoopSourceRef)params[0];
    CFIndex idx;
    if (rl == params[1]) return;

    // CFRunLoopRemoveSource will lock the run loop while it
    // needs that, but we also lock it out here to keep
    // changes from occurring for this whole sequence.
    __CFRunLoopLock(rl);
    CFArrayRef array = CFRunLoopCopyAllModes(rl);
    for (idx = CFArrayGetCount(array); idx--;) {
	CFStringRef modeName = (CFStringRef)CFArrayGetValueAtIndex(array, idx);
	CFRunLoopRemoveSource(rl, rls, modeName);
    }
    CFRunLoopRemoveSource(rl, rls, kCFRunLoopCommonModes);
    __CFRunLoopUnlock(rl);
    CFRelease(array);
    params[1] = rl;
}

void CFRunLoopSourceInvalidate(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rls, __kCFRunLoopSourceTypeID);
    __CFRunLoopSourceLock(rls);
    CFRetain(rls);
    if (__CFIsValid(rls)) {
        CFBagRef rloops = rls->_runLoops;
        __CFUnsetValid(rls);
        __CFRunLoopSourceUnsetSignaled(rls);
        if (NULL != rloops) {
            // To avoid A->B, B->A lock ordering issues when coming up
            // towards the run loop from a source, the source has to be
            // unlocked, which means we have to protect from object
            // invalidation.
            rls->_runLoops = NULL; // transfer ownership to local stack
            __CFRunLoopSourceUnlock(rls);
            CFTypeRef params[2] = {rls, NULL};
            CFBagApplyFunction(rloops, (__CFRunLoopSourceRemoveFromRunLoop), params);
            CFRelease(rloops);
            __CFRunLoopSourceLock(rls);
        }
        /* for hashing- and equality-use purposes, can't actually release the context here */
    }
    __CFRunLoopSourceUnlock(rls);
    CFRelease(rls);
}

Boolean CFRunLoopSourceIsValid(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rls, __kCFRunLoopSourceTypeID);
    return __CFIsValid(rls);
}

void CFRunLoopSourceGetContext(CFRunLoopSourceRef rls, CFRunLoopSourceContext *context) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rls, __kCFRunLoopSourceTypeID);
    CFAssert1(0 == context->version || 1 == context->version, __kCFLogAssertion, "%s(): context version not initialized to 0 or 1", __PRETTY_FUNCTION__);
    CFIndex size = 0;
    switch (context->version) {
    case 0:
	size = sizeof(CFRunLoopSourceContext);
	break;
    case 1:
	size = sizeof(CFRunLoopSourceContext1);
	break;
    }
    memmove(context, &rls->_context, size);
}

void CFRunLoopSourceSignal(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFRunLoopSourceLock(rls);
    if (__CFIsValid(rls)) {
	__CFRunLoopSourceSetSignaled(rls);
    }
    __CFRunLoopSourceUnlock(rls);
}

Boolean CFRunLoopSourceIsSignalled(CFRunLoopSourceRef rls) {
    CHECK_FOR_FORK();
    __CFRunLoopSourceLock(rls);
    Boolean ret = __CFRunLoopSourceIsSignaled(rls) ? true : false;
    __CFRunLoopSourceUnlock(rls);
    return ret;
}

__private_extern__ void _CFRunLoopSourceWakeUpRunLoops(CFRunLoopSourceRef rls) {
    CFBagRef loops = NULL;
    __CFRunLoopSourceLock(rls);
    if (__CFIsValid(rls) && NULL != rls->_runLoops) {
        loops = CFBagCreateCopy(kCFAllocatorSystemDefault, rls->_runLoops);
    }
    __CFRunLoopSourceUnlock(rls);
    if (loops) {
	CFBagApplyFunction(loops, __CFRunLoopSourceWakeUpLoop, NULL);
        CFRelease(loops);
    }
}

/* CFRunLoopObserver */

static CFStringRef __CFRunLoopObserverCopyDescription(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopObserverRef rlo = (CFRunLoopObserverRef)cf;
    CFStringRef result;
    CFStringRef contextDesc = NULL;
    if (NULL != rlo->_context.copyDescription) {
	contextDesc = rlo->_context.copyDescription(rlo->_context.info);
    }
    if (!contextDesc) {
	contextDesc = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopObserver context %p>"), rlo->_context.info);
    }
#if DEPLOYMENT_TARGET_WINDOWS
    result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopObserver %p [%p]>{valid = %s, activities = 0x%x, repeats = %s, order = %d, callout = %p, context = %@}"), cf, CFGetAllocator(rlo), __CFIsValid(rlo) ? "Yes" : "No", rlo->_activities, __CFRunLoopObserverRepeats(rlo) ? "Yes" : "No", rlo->_order, rlo->_callout, contextDesc);    
#elif DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
    void *addr = rlo->_callout;
    Dl_info info;
    const char *name = (dladdr(addr, &info) && info.dli_saddr == addr && info.dli_sname) ? info.dli_sname : "???";
    result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopObserver %p [%p]>{valid = %s, activities = 0x%x, repeats = %s, order = %d, callout = %s (%p), context = %@}"), cf, CFGetAllocator(rlo), __CFIsValid(rlo) ? "Yes" : "No", rlo->_activities, __CFRunLoopObserverRepeats(rlo) ? "Yes" : "No", rlo->_order, name, addr, contextDesc);
#else
#error Unknown or unspecified DEPLOYMENT_TARGET
#endif
    CFRelease(contextDesc);
    return result;
}

static void __CFRunLoopObserverDeallocate(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopObserverRef rlo = (CFRunLoopObserverRef)cf;
    CFRunLoopObserverInvalidate(rlo);
    pthread_mutex_destroy(&rlo->_lock);
}

static const CFRuntimeClass __CFRunLoopObserverClass = {
    0,
    "CFRunLoopObserver",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopObserverDeallocate,
    NULL,
    NULL,
    NULL,      // 
    __CFRunLoopObserverCopyDescription
};

__private_extern__ void __CFRunLoopObserverInitialize(void) {
    __kCFRunLoopObserverTypeID = _CFRuntimeRegisterClass(&__CFRunLoopObserverClass);
}

CFTypeID CFRunLoopObserverGetTypeID(void) {
    return __kCFRunLoopObserverTypeID;
}

CFRunLoopObserverRef CFRunLoopObserverCreate(CFAllocatorRef allocator, CFOptionFlags activities, Boolean repeats, CFIndex order, CFRunLoopObserverCallBack callout, CFRunLoopObserverContext *context) {
    CHECK_FOR_FORK();
    CFRunLoopObserverRef memory;
    UInt32 size;
    size = sizeof(struct __CFRunLoopObserver) - sizeof(CFRuntimeBase);
    memory = (CFRunLoopObserverRef)_CFRuntimeCreateInstance(allocator, __kCFRunLoopObserverTypeID, size, NULL);
    if (NULL == memory) {
	return NULL;
    }
    __CFSetValid(memory);
    __CFRunLoopObserverUnsetFiring(memory);
    if (repeats) {
	__CFRunLoopObserverSetRepeats(memory);
    } else {
	__CFRunLoopObserverUnsetRepeats(memory);
    }
    __CFRunLoopLockInit(&memory->_lock);
    memory->_runLoop = NULL;
    memory->_rlCount = 0;
    memory->_activities = activities;
    memory->_order = order;
    memory->_callout = callout;
    if (context) {
	if (context->retain) {
	    memory->_context.info = (void *)context->retain(context->info);
	} else {
	    memory->_context.info = context->info;
	}
	memory->_context.retain = context->retain;
	memory->_context.release = context->release;
	memory->_context.copyDescription = context->copyDescription;
    } else {
	memory->_context.info = 0;
	memory->_context.retain = 0;
	memory->_context.release = 0;
	memory->_context.copyDescription = 0;
    }
    return memory;
}

static void _runLoopObserverWithBlockContext(CFRunLoopObserverRef observer, CFRunLoopActivity activity, void *opaqueBlock) {
    typedef void (^observer_block_t) (CFRunLoopObserverRef observer, CFRunLoopActivity activity);
    observer_block_t block = (observer_block_t)opaqueBlock;
    block(observer, activity);
}

CFRunLoopObserverRef CFRunLoopObserverCreateWithHandler(CFAllocatorRef allocator, CFOptionFlags activities, Boolean repeats, CFIndex order,
                                                      void (^block) (CFRunLoopObserverRef observer, CFRunLoopActivity activity)) {
    CFRunLoopObserverContext blockContext;
    blockContext.version = 0;
    blockContext.info = (void *)block;
    blockContext.retain = (const void *(*)(const void *info))_Block_copy;
    blockContext.release = (void (*)(const void *info))_Block_release;
    blockContext.copyDescription = NULL;
    return CFRunLoopObserverCreate(allocator, activities, repeats, order, _runLoopObserverWithBlockContext, &blockContext);
}

CFOptionFlags CFRunLoopObserverGetActivities(CFRunLoopObserverRef rlo) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, __kCFRunLoopObserverTypeID);
    return rlo->_activities;
}

CFIndex CFRunLoopObserverGetOrder(CFRunLoopObserverRef rlo) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, __kCFRunLoopObserverTypeID);
    return rlo->_order;
}

Boolean CFRunLoopObserverDoesRepeat(CFRunLoopObserverRef rlo) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, __kCFRunLoopObserverTypeID);
    return __CFRunLoopObserverRepeats(rlo);
}

void CFRunLoopObserverInvalidate(CFRunLoopObserverRef rlo) {    /* DOES CALLOUT */
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, __kCFRunLoopObserverTypeID);
    __CFRunLoopObserverLock(rlo);
    CFRetain(rlo);
    if (__CFIsValid(rlo)) {
        CFRunLoopRef rl = rlo->_runLoop;
        void *info = rlo->_context.info;
        rlo->_context.info = NULL;
        __CFUnsetValid(rlo);
        if (NULL != rl) {
            // To avoid A->B, B->A lock ordering issues when coming up
            // towards the run loop from an observer, it has to be
            // unlocked, which means we have to protect from object
            // invalidation.
            CFRetain(rl);
            __CFRunLoopObserverUnlock(rlo);
            // CFRunLoopRemoveObserver will lock the run loop while it
            // needs that, but we also lock it out here to keep
            // changes from occurring for this whole sequence.
            __CFRunLoopLock(rl);
            CFArrayRef array = CFRunLoopCopyAllModes(rl);
            for (CFIndex idx = CFArrayGetCount(array); idx--;) {
                CFStringRef modeName = (CFStringRef)CFArrayGetValueAtIndex(array, idx);
                CFRunLoopRemoveObserver(rl, rlo, modeName);
            }
            CFRunLoopRemoveObserver(rl, rlo, kCFRunLoopCommonModes);
            __CFRunLoopUnlock(rl);
            CFRelease(array);
            CFRelease(rl);
            __CFRunLoopObserverLock(rlo);
        }
        if (NULL != rlo->_context.release) {
            rlo->_context.release(info);        /* CALLOUT */
        }
    }
    __CFRunLoopObserverUnlock(rlo);
    CFRelease(rlo);
}

Boolean CFRunLoopObserverIsValid(CFRunLoopObserverRef rlo) {
    CHECK_FOR_FORK();
    return __CFIsValid(rlo);
}

void CFRunLoopObserverGetContext(CFRunLoopObserverRef rlo, CFRunLoopObserverContext *context) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlo, __kCFRunLoopObserverTypeID);
    CFAssert1(0 == context->version, __kCFLogAssertion, "%s(): context version not initialized to 0", __PRETTY_FUNCTION__);
    *context = rlo->_context;
}

/* CFRunLoopTimer */

static CFStringRef __CFRunLoopTimerCopyDescription(CFTypeRef cf) {	/* DOES CALLOUT */
    CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)cf;
    CFStringRef contextDesc = NULL;
    if (NULL != rlt->_context.copyDescription) {
	contextDesc = rlt->_context.copyDescription(rlt->_context.info);
    }
    if (NULL == contextDesc) {
	contextDesc = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopTimer context %p>"), rlt->_context.info);
    }
    void *addr = (void *)rlt->_callout;
    const char *name = "???";
#if DEPLOYMENT_TARGET_WINDOWS
#elif DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED
    Dl_info info;
    name = (dladdr(addr, &info) && info.dli_saddr == addr && info.dli_sname) ? info.dli_sname : "???";
#else
#error Unknown or unspecified DEPLOYMENT_TARGET
#endif
    CFStringRef result = CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFRunLoopTimer %p [%p]>{valid = %s, interval = %0.09g, next fire date = %0.09g, callout = %s (%p), context = %@}"), cf, CFGetAllocator(rlt), __CFIsValid(rlt) ? "Yes" : "No", rlt->_interval, rlt->_nextFireDate, name, addr, contextDesc);
    CFRelease(contextDesc);
    return result;
}

static void __CFRunLoopTimerDeallocate(CFTypeRef cf) {	/* DOES CALLOUT */
//CFLog(6, CFSTR("__CFRunLoopTimerDeallocate(%p)"), cf);
    CFRunLoopTimerRef rlt = (CFRunLoopTimerRef)cf;
    __CFRunLoopTimerSetDeallocating(rlt);
    CFRunLoopTimerInvalidate(rlt);	/* DOES CALLOUT */
    CFRelease(rlt->_rlModes);
    rlt->_rlModes = NULL;
    pthread_mutex_destroy(&rlt->_lock);
}

static const CFRuntimeClass __CFRunLoopTimerClass = {
    0,
    "CFRunLoopTimer",
    NULL,      // init
    NULL,      // copy
    __CFRunLoopTimerDeallocate,
    NULL,	// equal
    NULL,
    NULL,      // 
    __CFRunLoopTimerCopyDescription
};

__private_extern__ void __CFRunLoopTimerInitialize(void) {
    __kCFRunLoopTimerTypeID = _CFRuntimeRegisterClass(&__CFRunLoopTimerClass);
}

CFTypeID CFRunLoopTimerGetTypeID(void) {
    return __kCFRunLoopTimerTypeID;
}

CFRunLoopTimerRef CFRunLoopTimerCreate(CFAllocatorRef allocator, CFAbsoluteTime fireDate, CFTimeInterval interval, CFOptionFlags flags, CFIndex order, CFRunLoopTimerCallBack callout, CFRunLoopTimerContext *context) {
    CHECK_FOR_FORK();    
    CFRunLoopTimerRef memory;
    UInt32 size;
    size = sizeof(struct __CFRunLoopTimer) - sizeof(CFRuntimeBase);
    memory = (CFRunLoopTimerRef)_CFRuntimeCreateInstance(allocator, __kCFRunLoopTimerTypeID, size, NULL);
    if (NULL == memory) {
	return NULL;
    }
    __CFSetValid(memory);
    __CFRunLoopTimerUnsetFiring(memory);
    __CFRunLoopLockInit(&memory->_lock);
    memory->_runLoop = NULL;
    memory->_rlModes = CFSetCreateMutable(kCFAllocatorSystemDefault, 0, &kCFTypeSetCallBacks);
    memory->_order = order;
    if (interval < 0.0) interval = 0.0;
    memory->_interval = interval;
    if (TIMER_DATE_LIMIT < fireDate) fireDate = TIMER_DATE_LIMIT;
    memory->_nextFireDate = fireDate;
    memory->_fireTSR = 0LL;
    int64_t now2 = (int64_t)mach_absolute_time();
    CFAbsoluteTime now1 = CFAbsoluteTimeGetCurrent();
    if (fireDate < now1) {
	memory->_fireTSR = now2;
    } else if (TIMER_INTERVAL_LIMIT < fireDate - now1) {
	memory->_fireTSR = now2 + __CFTimeIntervalToTSR(TIMER_INTERVAL_LIMIT);
    } else {
	memory->_fireTSR = now2 + __CFTimeIntervalToTSR(fireDate - now1);
    }
    memory->_callout = callout;
    if (NULL != context) {
	if (context->retain) {
	    memory->_context.info = (void *)context->retain(context->info);
	} else {
	    memory->_context.info = context->info;
	}
	memory->_context.retain = context->retain;
	memory->_context.release = context->release;
	memory->_context.copyDescription = context->copyDescription;
    } else {
	memory->_context.info = 0;
	memory->_context.retain = 0;
	memory->_context.release = 0;
	memory->_context.copyDescription = 0;
    }
    return memory;
}

static void _runLoopTimerWithBlockContext(CFRunLoopTimerRef timer, void *opaqueBlock) {
    typedef void (^timer_block_t) (CFRunLoopTimerRef timer);
    timer_block_t block = (timer_block_t)opaqueBlock;
    block(timer);
}

CFRunLoopTimerRef CFRunLoopTimerCreateWithHandler(CFAllocatorRef allocator, CFAbsoluteTime fireDate, CFTimeInterval interval, CFOptionFlags flags, CFIndex order,
						void (^block) (CFRunLoopTimerRef timer)) {
    
    CFRunLoopTimerContext blockContext;
    blockContext.version = 0;
    blockContext.info = (void *)block;
    blockContext.retain = (const void *(*)(const void *info))_Block_copy;
    blockContext.release = (void (*)(const void *info))_Block_release;
    blockContext.copyDescription = NULL;
    return CFRunLoopTimerCreate(allocator, fireDate, interval, flags, order, _runLoopTimerWithBlockContext, &blockContext);
}

CFAbsoluteTime CFRunLoopTimerGetNextFireDate(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCH0(__kCFRunLoopTimerTypeID, CFAbsoluteTime, rlt, "_cffireTime");
    __CFGenericValidateType(rlt, __kCFRunLoopTimerTypeID);
    CFAbsoluteTime at = 0.0;
    __CFRunLoopTimerLock(rlt);
    __CFRunLoopTimerFireTSRLock();
    if (__CFIsValid(rlt)) {
        at = rlt->_nextFireDate;
    }
    __CFRunLoopTimerFireTSRUnlock();
    __CFRunLoopTimerUnlock(rlt);
    return at;
}

void CFRunLoopTimerSetNextFireDate(CFRunLoopTimerRef rlt, CFAbsoluteTime fireDate) {
    CHECK_FOR_FORK();
    if (!__CFIsValid(rlt)) return;
    if (TIMER_DATE_LIMIT < fireDate) fireDate = TIMER_DATE_LIMIT;
    int64_t nextFireTSR = 0LL;
    int64_t now2 = (int64_t)mach_absolute_time();
    CFAbsoluteTime now1 = CFAbsoluteTimeGetCurrent();
    if (fireDate < now1) {
	nextFireTSR = now2;
    } else if (TIMER_INTERVAL_LIMIT < fireDate - now1) {
	nextFireTSR = now2 + __CFTimeIntervalToTSR(TIMER_INTERVAL_LIMIT);
    } else {
	nextFireTSR = now2 + __CFTimeIntervalToTSR(fireDate - now1);
    }
    __CFRunLoopTimerLock(rlt);
    if (NULL != rlt->_runLoop) {
        CFIndex cnt = CFSetGetCount(rlt->_rlModes);
        STACK_BUFFER_DECL(CFTypeRef, modes, cnt);
        CFSetGetValues(rlt->_rlModes, (const void **)modes);
        // To avoid A->B, B->A lock ordering issues when coming up
        // towards the run loop from a source, the timer has to be
        // unlocked, which means we have to protect from object
        // invalidation, although that's somewhat expensive.
        for (CFIndex idx = 0; idx < cnt; idx++) {
            CFRetain(modes[idx]);
        }
        CFRunLoopRef rl = (CFRunLoopRef)CFRetain(rlt->_runLoop);
        __CFRunLoopTimerUnlock(rlt);
        __CFRunLoopLock(rl);
        for (CFIndex idx = 0; idx < cnt; idx++) {
	    CFStringRef name = (CFStringRef)modes[idx];
            modes[idx] = __CFRunLoopFindMode(rl, name, false);
	    CFRelease(name);
        }
        __CFRunLoopTimerFireTSRLock();
	rlt->_fireTSR = nextFireTSR;
        rlt->_nextFireDate = fireDate;
        for (CFIndex idx = 0; idx < cnt; idx++) {
	    CFRunLoopModeRef rlm = (CFRunLoopModeRef)modes[idx];
            if (rlm) {
                __CFRepositionTimerInMode(rlm, rlt, true);
            }
        }
        __CFRunLoopTimerFireTSRUnlock();
        for (CFIndex idx = 0; idx < cnt; idx++) {
            __CFRunLoopModeUnlock((CFRunLoopModeRef)modes[idx]);
        }
        __CFRunLoopUnlock(rl);
        // This is setting the date of a timer, not a direct
        // interaction with a run loop, so we'll do a wakeup
        // (which may be costly) for the caller, just in case.
        // (And useful for binary compatibility with older
        // code used to the older timer implementation.)
        if (rl != CFRunLoopGetCurrent()) CFRunLoopWakeUp(rl);
        CFRelease(rl);
     } else {
        __CFRunLoopTimerFireTSRLock();
	rlt->_fireTSR = nextFireTSR;
        rlt->_nextFireDate = fireDate;
        __CFRunLoopTimerFireTSRUnlock();
         __CFRunLoopTimerUnlock(rlt);
     }
}

CFTimeInterval CFRunLoopTimerGetInterval(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCH0(__kCFRunLoopTimerTypeID, CFTimeInterval, rlt, "timeInterval");
    __CFGenericValidateType(rlt, __kCFRunLoopTimerTypeID);
    return rlt->_interval;
}

Boolean CFRunLoopTimerDoesRepeat(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlt, __kCFRunLoopTimerTypeID);
    return (0.0 < rlt->_interval);
}

CFIndex CFRunLoopTimerGetOrder(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlt, __kCFRunLoopTimerTypeID);
    return rlt->_order;
}

void CFRunLoopTimerInvalidate(CFRunLoopTimerRef rlt) {	/* DOES CALLOUT */
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCH0(__kCFRunLoopTimerTypeID, void, rlt, "invalidate");
    __CFGenericValidateType(rlt, __kCFRunLoopTimerTypeID);
    __CFRunLoopTimerLock(rlt);
    if (!__CFRunLoopTimerIsDeallocating(rlt)) {
        CFRetain(rlt);
    }
    if (__CFIsValid(rlt)) {
	CFRunLoopRef rl = rlt->_runLoop;
	void *info = rlt->_context.info;
	rlt->_context.info = NULL;
	__CFUnsetValid(rlt);
	if (NULL != rl) {
	    CFIndex cnt = CFSetGetCount(rlt->_rlModes);
	    STACK_BUFFER_DECL(CFStringRef, modes, cnt);
	    CFSetGetValues(rlt->_rlModes, (const void **)modes);
            // To avoid A->B, B->A lock ordering issues when coming up
            // towards the run loop from a source, the timer has to be
            // unlocked, which means we have to protect from object
            // invalidation, although that's somewhat expensive.
            for (CFIndex idx = 0; idx < cnt; idx++) {
                CFRetain(modes[idx]);
            }
            CFRetain(rl);
            __CFRunLoopTimerUnlock(rlt);
            // CFRunLoopRemoveTimer will lock the run loop while it
            // needs that, but we also lock it out here to keep
            // changes from occurring for this whole sequence.
            __CFRunLoopLock(rl);
	    for (CFIndex idx = 0; idx < cnt; idx++) {
		CFRunLoopRemoveTimer(rl, rlt, modes[idx]);
	    }
	    CFRunLoopRemoveTimer(rl, rlt, kCFRunLoopCommonModes);
            __CFRunLoopUnlock(rl);
            for (CFIndex idx = 0; idx < cnt; idx++) {
                CFRelease(modes[idx]);
            }
            CFRelease(rl);
            __CFRunLoopTimerLock(rlt);
	}
	if (NULL != rlt->_context.release) {
	    rlt->_context.release(info);	/* CALLOUT */
	}
    }
    __CFRunLoopTimerUnlock(rlt);
    if (!__CFRunLoopTimerIsDeallocating(rlt)) {
        CFRelease(rlt);
    }
}

Boolean CFRunLoopTimerIsValid(CFRunLoopTimerRef rlt) {
    CHECK_FOR_FORK();
    CF_OBJC_FUNCDISPATCH0(__kCFRunLoopTimerTypeID, Boolean, rlt, "isValid");
    __CFGenericValidateType(rlt, __kCFRunLoopTimerTypeID);
    return __CFIsValid(rlt);
}

void CFRunLoopTimerGetContext(CFRunLoopTimerRef rlt, CFRunLoopTimerContext *context) {
    CHECK_FOR_FORK();
    __CFGenericValidateType(rlt, __kCFRunLoopTimerTypeID);
    CFAssert1(0 == context->version, __kCFLogAssertion, "%s(): context version not initialized to 0", __PRETTY_FUNCTION__);
    *context = rlt->_context;
}

