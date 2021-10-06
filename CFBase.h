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

/*	CFBase.h
	Copyright (c) 1998-2011, Apple Inc. All rights reserved.
*/

#if !defined(__COREFOUNDATION_CFBASE__)
#define __COREFOUNDATION_CFBASE__ 1

#include <TargetConditionals.h>

#if (defined(__CYGWIN32__) || defined(_WIN32)) && !defined(__WIN32__)
#define __WIN32__ 1
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
#define __i386__ 1
#endif

#if (defined(__i386__) || defined(__x86_64__)) && !defined(__LITTLE_ENDIAN__)
#define __LITTLE_ENDIAN__ 1
#endif

#if !defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__)
#error Do not know the endianess of this architecture
#endif

#if !__BIG_ENDIAN__ && !__LITTLE_ENDIAN__
#error Both __BIG_ENDIAN__ and __LITTLE_ENDIAN__ cannot be false
#endif

#if __BIG_ENDIAN__ && __LITTLE_ENDIAN__
#error Both __BIG_ENDIAN__ and __LITTLE_ENDIAN__ cannot be true
#endif

// Some compilers provide the capability to test if certain features are available. This macro provides a compatibility path for other compilers.
#ifndef __has_feature
#define __has_feature(x) 0
#endif

#if defined(__GNUC__) || TARGET_OS_WIN32
#include <stdint.h>
#include <stdbool.h>
#endif

#if __BLOCKS__
#include <Block.h>
#endif

// The arguments to these availability macros is a version number, e.g. 10_6, 3_0
#if TARGET_OS_MAC && !(TARGET_OS_EMBEDDED || TARGET_OS_IPHONE)
#include <AvailabilityMacros.h>
#include <Availability.h>

// Available on MacOS and iOS
#define CF_AVAILABLE(_mac, _ios) AVAILABLE_MAC_OS_X_VERSION_##_mac##_AND_LATER

// Available on MacOS only
#define CF_AVAILABLE_MAC(_mac) AVAILABLE_MAC_OS_X_VERSION_##_mac##_AND_LATER

// Available on iOS only
#define CF_AVAILABLE_IOS(_ios) __OSX_AVAILABLE_STARTING(__MAC_NA, __IPHONE_##_ios)

// Deprecated on either MacOS or iOS, or deprecated on both (check version numbers for details)
#define CF_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep) AVAILABLE_MAC_OS_X_VERSION_##_macIntro##_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_##_macDep

// Deprecated on MacOS, unavailable on iOS
#define CF_DEPRECATED_MAC(_macIntro, _macDep) AVAILABLE_MAC_OS_X_VERSION_##_macIntro##_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_##_macDep

// Unavailable on MacOS, deprecated on iOS
#define CF_DEPRECATED_IOS(_iosIntro, _iosDep) __OSX_AVAILABLE_BUT_DEPRECATED(__MAC_NA, __MAC_NA, __IPHONE_##_iosIntro, __IPHONE_##_iosDep)

#elif (TARGET_OS_EMBEDDED || TARGET_OS_IPHONE)
#include <AvailabilityMacros.h>
#include <Availability.h>

#define CF_AVAILABLE(_mac, _ios) __OSX_AVAILABLE_STARTING(__MAC_##_mac, __IPHONE_##_ios)
#define CF_AVAILABLE_MAC(_mac) __OSX_AVAILABLE_STARTING(__MAC_##_mac, __IPHONE_NA)
#define CF_AVAILABLE_IOS(_ios) __OSX_AVAILABLE_STARTING(__MAC_NA, __IPHONE_##_ios)
#define CF_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep) __OSX_AVAILABLE_BUT_DEPRECATED(__MAC_##_macIntro, __MAC_##_macDep, __IPHONE_##_iosIntro, __IPHONE_##_iosDep)
#define CF_DEPRECATED_MAC(_macIntro, _macDep) __OSX_AVAILABLE_BUT_DEPRECATED(__MAC_##_macIntro, __MAC_##_macDep, __IPHONE_NA, __IPHONE_NA)
#define CF_DEPRECATED_IOS(_iosIntro, _iosDep) __OSX_AVAILABLE_BUT_DEPRECATED(__MAC_NA, __MAC_NA, __IPHONE_##_iosIntro, __IPHONE_##_iosDep)

#else

#if TARGET_OS_WIN32
#include <AvailabilityMacros.h>
#include <Availability.h>
#endif

#define CF_AVAILABLE(_mac, _ios)
#define CF_AVAILABLE_MAC(_mac)
#define CF_AVAILABLE_IOS(_ios)
#define CF_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep)
#define CF_DEPRECATED_MAC(_macIntro, _macDep)
#define CF_DEPRECATED_IOS(_iosIntro, _iosDep)

#endif


// Older versions of these macro; use IOS versions instead
#define CF_AVAILABLE_IPHONE(_ios) CF_AVAILABLE_IOS(_ios)
#define CF_DEPRECATED_IPHONE(_iosIntro, _iosDep) CF_DEPRECATED_IOS(_iosIntro, _iosDep)

  #if (TARGET_OS_MAC && !(TARGET_OS_EMBEDDED || TARGET_OS_IPHONE)) || (TARGET_OS_EMBEDDED || TARGET_OS_IPHONE)
    #include <libkern/OSTypes.h>
  #endif

#if !defined(__MACTYPES__)
#if !defined(_OS_OSTYPES_H)
    typedef unsigned char           Boolean;
    typedef unsigned char           UInt8;
    typedef signed char             SInt8;
    typedef unsigned short          UInt16;
    typedef signed short            SInt16;
    typedef unsigned int            UInt32;
    typedef signed int              SInt32;
    typedef uint64_t		    UInt64;
    typedef int64_t		    SInt64;
    typedef SInt32                  OSStatus;
#endif
    typedef float                   Float32;
    typedef double                  Float64;
    typedef unsigned short          UniChar;
    typedef unsigned long           UniCharCount;
    typedef unsigned char *         StringPtr;
    typedef const unsigned char *   ConstStringPtr;
    typedef unsigned char           Str255[256];
    typedef const unsigned char *   ConstStr255Param;
    typedef SInt16                  OSErr;
    typedef SInt16                  RegionCode;
    typedef SInt16                  LangCode;
    typedef SInt16                  ScriptCode;
    typedef UInt32                  FourCharCode;
    typedef FourCharCode            OSType;
    typedef UInt8                   Byte;
    typedef SInt8                   SignedByte;
#endif
#if !defined(__MACTYPES__) || (defined(UNIVERSAL_INTERFACES_VERSION) && UNIVERSAL_INTERFACES_VERSION < 0x0340)
    typedef UInt32                  UTF32Char;
    typedef UInt16                  UTF16Char;
    typedef UInt8                   UTF8Char;
#endif

#if !defined(CF_EXTERN_C_BEGIN)
#if defined(__cplusplus)
#define CF_EXTERN_C_BEGIN extern "C" {
#define CF_EXTERN_C_END   }
#else
#define CF_EXTERN_C_BEGIN
#define CF_EXTERN_C_END
#endif
#endif

#if TARGET_OS_WIN32

#if !defined(CF_EXPORT)
#if defined(CF_BUILDING_CF) && defined(__cplusplus)
#define CF_EXPORT extern "C" __declspec(dllexport) 
#elif defined(CF_BUILDING_CF) && !defined(__cplusplus)
#define CF_EXPORT extern __declspec(dllexport) 
#elif defined(__cplusplus)
#define CF_EXPORT extern "C" __declspec(dllimport) 
#else
#define CF_EXPORT extern __declspec(dllimport) 
#endif
#endif

#else
#define CF_EXPORT extern
#endif

CF_EXTERN_C_BEGIN

#if !defined(NULL)
#if defined(__GNUG__)
    #define NULL __null
#elif defined(__cplusplus)
    #define NULL 0
#else
    #define NULL ((void *)0)
#endif
#endif

#if !defined(TRUE)
    #define TRUE	1
#endif

#if !defined(FALSE)
    #define FALSE	0
#endif

#if !defined(CF_INLINE)
    #if defined(__GNUC__) && (__GNUC__ == 4) && !defined(DEBUG)
        #define CF_INLINE static __inline__ __attribute__((always_inline))
    #elif defined(__GNUC__)
        #define CF_INLINE static __inline__
    #elif defined(__MWERKS__) || defined(__cplusplus)
	#define CF_INLINE static inline
    #elif defined(_MSC_VER)
        #define CF_INLINE static __inline
    #elif TARGET_OS_WIN32
	#define CF_INLINE static __inline__
    #endif
#endif

// Marks functions which return a CF type that needs to be released by the caller but whose names are not consistent with CoreFoundation naming rules. The recommended fix to this is to rename the functions, but this macro can be used to let the clang static analyzer know of any exceptions that cannot be fixed.
// This macro is ONLY to be used in exceptional circumstances, not to annotate functions which conform to the CoreFoundation naming rules.
#if __has_feature(attribute_cf_returns_retained)
#define CF_RETURNS_RETAINED __attribute__((cf_returns_retained))
#else
#define CF_RETURNS_RETAINED
#endif

// Marks functions which return a CF type that may need to be retained by the caller but whose names are not consistent with CoreFoundation naming rules. The recommended fix to this is to rename the functions, but this macro can be used to let the clang static analyzer know of any exceptions that cannot be fixed. 
// This macro is ONLY to be used in exceptional circumstances, not to annotate functions which conform to the CoreFoundation naming rules.
#if __has_feature(attribute_cf_returns_not_retained)
#define CF_RETURNS_NOT_RETAINED __attribute__((cf_returns_not_retained))
#else
#define CF_RETURNS_NOT_RETAINED
#endif

// Marks functions which cannot be used when compiling in automatic reference counting mode.
#if __has_feature(objc_arc)
#define CF_AUTOMATED_REFCOUNT_UNAVAILABLE __attribute__((unavailable("not available in automatic reference counting mode")))
#else
#define CF_AUTOMATED_REFCOUNT_UNAVAILABLE
#endif

CF_EXPORT double kCFCoreFoundationVersionNumber;

#if TARGET_OS_MAC
#define kCFCoreFoundationVersionNumber10_0	196.40
#define kCFCoreFoundationVersionNumber10_0_3	196.50
#define kCFCoreFoundationVersionNumber10_1	226.00
#define kCFCoreFoundationVersionNumber10_1_1	226.00
/* Note the next three do not follow the usual numbering policy from the base release */
#define kCFCoreFoundationVersionNumber10_1_2	227.20
#define kCFCoreFoundationVersionNumber10_1_3	227.20
#define kCFCoreFoundationVersionNumber10_1_4	227.30
#define kCFCoreFoundationVersionNumber10_2	263.00
#define kCFCoreFoundationVersionNumber10_2_1	263.10
#define kCFCoreFoundationVersionNumber10_2_2	263.10
#define kCFCoreFoundationVersionNumber10_2_3	263.30
#define kCFCoreFoundationVersionNumber10_2_4	263.30
#define kCFCoreFoundationVersionNumber10_2_5	263.50
#define kCFCoreFoundationVersionNumber10_2_6	263.50
#define kCFCoreFoundationVersionNumber10_2_7	263.50
#define kCFCoreFoundationVersionNumber10_2_8	263.50
#define kCFCoreFoundationVersionNumber10_3	299.00
#define kCFCoreFoundationVersionNumber10_3_1	299.00
#define kCFCoreFoundationVersionNumber10_3_2	299.00
#define kCFCoreFoundationVersionNumber10_3_3	299.30
#define kCFCoreFoundationVersionNumber10_3_4	299.31
#define kCFCoreFoundationVersionNumber10_3_5	299.31
#define kCFCoreFoundationVersionNumber10_3_6	299.32
#define kCFCoreFoundationVersionNumber10_3_7	299.33
#define kCFCoreFoundationVersionNumber10_3_8	299.33
#define kCFCoreFoundationVersionNumber10_3_9	299.35
#define kCFCoreFoundationVersionNumber10_4	368.00
#define kCFCoreFoundationVersionNumber10_4_1	368.10
#define kCFCoreFoundationVersionNumber10_4_2	368.11
#define kCFCoreFoundationVersionNumber10_4_3	368.18
#define kCFCoreFoundationVersionNumber10_4_4_Intel	368.26
#define kCFCoreFoundationVersionNumber10_4_4_PowerPC	368.25
#define kCFCoreFoundationVersionNumber10_4_5_Intel	368.26
#define kCFCoreFoundationVersionNumber10_4_5_PowerPC	368.25
#define kCFCoreFoundationVersionNumber10_4_6_Intel	368.26
#define kCFCoreFoundationVersionNumber10_4_6_PowerPC	368.25
#define kCFCoreFoundationVersionNumber10_4_7	368.27
#define kCFCoreFoundationVersionNumber10_4_8	368.27
#define kCFCoreFoundationVersionNumber10_4_9	368.28
#define kCFCoreFoundationVersionNumber10_4_10	368.28
#define kCFCoreFoundationVersionNumber10_4_11	368.31
#define kCFCoreFoundationVersionNumber10_5	476.00
#define kCFCoreFoundationVersionNumber10_5_1	476.00
#define kCFCoreFoundationVersionNumber10_5_2	476.10
#define kCFCoreFoundationVersionNumber10_5_3	476.13
#define kCFCoreFoundationVersionNumber10_5_4	476.14
#define kCFCoreFoundationVersionNumber10_5_5	476.15
#define kCFCoreFoundationVersionNumber10_5_6	476.17
#define kCFCoreFoundationVersionNumber10_5_7	476.18
#define kCFCoreFoundationVersionNumber10_5_8	476.19
#define kCFCoreFoundationVersionNumber10_6	550.00
#define kCFCoreFoundationVersionNumber10_6_1	550.00
#define kCFCoreFoundationVersionNumber10_6_2	550.13
#define kCFCoreFoundationVersionNumber10_6_3	550.19
#define kCFCoreFoundationVersionNumber10_6_4	550.29
#define kCFCoreFoundationVersionNumber10_6_5	550.42
#endif

#if TARGET_OS_IPHONE
#define kCFCoreFoundationVersionNumber_iPhoneOS_2_0	478.23
#define kCFCoreFoundationVersionNumber_iPhoneOS_2_1 478.26
#define kCFCoreFoundationVersionNumber_iPhoneOS_2_2 478.29
#define kCFCoreFoundationVersionNumber_iPhoneOS_3_0 478.47
#define kCFCoreFoundationVersionNumber_iPhoneOS_3_1 478.52
#define kCFCoreFoundationVersionNumber_iPhoneOS_3_2 478.61
#define kCFCoreFoundationVersionNumber_iOS_4_0 550.32
#define kCFCoreFoundationVersionNumber_iOS_4_1 550.38
#define kCFCoreFoundationVersionNumber_iOS_4_2 550.52
#endif

typedef unsigned long CFTypeID;
typedef unsigned long CFOptionFlags;
typedef unsigned long CFHashCode;
typedef signed long CFIndex;

/* Base "type" of all "CF objects", and polymorphic functions on them */
typedef const void * CFTypeRef;

typedef const struct __CFString * CFStringRef;
typedef struct __CFString * CFMutableStringRef;

/*
        Type to mean any instance of a property list type;
        currently, CFString, CFData, CFNumber, CFBoolean, CFDate,
        CFArray, and CFDictionary.
*/
typedef CFTypeRef CFPropertyListRef;

/* Values returned from comparison functions */
enum {
    kCFCompareLessThan = -1,
    kCFCompareEqualTo = 0,
    kCFCompareGreaterThan = 1
};
typedef CFIndex CFComparisonResult;

/* A standard comparison function */
typedef CFComparisonResult (*CFComparatorFunction)(const void *val1, const void *val2, void *context);

/* Constant used by some functions to indicate failed searches. */
/* This is of type CFIndex. */
enum {
    kCFNotFound = -1
};


/* Range type */
typedef struct {
    CFIndex location;
    CFIndex length;
} CFRange;

#if defined(CF_INLINE)
CF_INLINE CFRange CFRangeMake(CFIndex loc, CFIndex len) {
    CFRange range;
    range.location = loc;
    range.length = len;
    return range;
}
#else
#define CFRangeMake(LOC, LEN) __CFRangeMake(LOC, LEN)
#endif

/* Private; do not use */
CF_EXPORT
CFRange __CFRangeMake(CFIndex loc, CFIndex len);


/* Null representant */

typedef const struct __CFNull * CFNullRef;

CF_EXPORT
CFTypeID CFNullGetTypeID(void);

CF_EXPORT
const CFNullRef kCFNull;	// the singleton null instance


/* Allocator API

   Most of the time when specifying an allocator to Create functions, the NULL
   argument indicates "use the default"; this is the same as using kCFAllocatorDefault
   or the return value from CFAllocatorGetDefault().  This assures that you will use
   the allocator in effect at that time.

   You should rarely use kCFAllocatorSystemDefault, the default default allocator.
*/
typedef const struct __CFAllocator * CFAllocatorRef;

/* This is a synonym for NULL, if you'd rather use a named constant. */
CF_EXPORT
const CFAllocatorRef kCFAllocatorDefault;

/* Default system allocator; you rarely need to use this. */
CF_EXPORT
const CFAllocatorRef kCFAllocatorSystemDefault;

/* This allocator uses malloc(), realloc(), and free(). This should not be
   generally used; stick to kCFAllocatorDefault whenever possible. This
   allocator is useful as the "bytesDeallocator" in CFData or
   "contentsDeallocator" in CFString where the memory was obtained as a
   result of malloc() type functions.
*/
CF_EXPORT
const CFAllocatorRef kCFAllocatorMalloc;

/* This allocator explicitly uses the default malloc zone, returned by
   malloc_default_zone(). It should only be used when an object is
   safe to be allocated in non-scanned memory.
 */
CF_EXPORT
const CFAllocatorRef kCFAllocatorMallocZone;

/* Null allocator which does nothing and allocates no memory. This allocator
   is useful as the "bytesDeallocator" in CFData or "contentsDeallocator"
   in CFString where the memory should not be freed. 
*/
CF_EXPORT
const CFAllocatorRef kCFAllocatorNull;

/* Special allocator argument to CFAllocatorCreate() which means
   "use the functions given in the context to allocate the allocator
   itself as well". 
*/
CF_EXPORT
const CFAllocatorRef kCFAllocatorUseContext;

typedef const void *	(*CFAllocatorRetainCallBack)(const void *info);
typedef void		(*CFAllocatorReleaseCallBack)(const void *info);
typedef CFStringRef	(*CFAllocatorCopyDescriptionCallBack)(const void *info);
typedef void *		(*CFAllocatorAllocateCallBack)(CFIndex allocSize, CFOptionFlags hint, void *info);
typedef void *		(*CFAllocatorReallocateCallBack)(void *ptr, CFIndex newsize, CFOptionFlags hint, void *info);
typedef void		(*CFAllocatorDeallocateCallBack)(void *ptr, void *info);
typedef CFIndex		(*CFAllocatorPreferredSizeCallBack)(CFIndex size, CFOptionFlags hint, void *info);
typedef struct {
    CFIndex				version;
    void *				info;
    CFAllocatorRetainCallBack		retain;
    CFAllocatorReleaseCallBack		release;        
    CFAllocatorCopyDescriptionCallBack	copyDescription;
    CFAllocatorAllocateCallBack		allocate;
    CFAllocatorReallocateCallBack	reallocate;
    CFAllocatorDeallocateCallBack	deallocate;
    CFAllocatorPreferredSizeCallBack	preferredSize;
} CFAllocatorContext;

CF_EXPORT
CFTypeID	CFAllocatorGetTypeID(void);

/*
	CFAllocatorSetDefault() sets the allocator that is used in the current
	thread whenever NULL is specified as an allocator argument. This means
	that most, if not all allocations will go through this allocator. It
	also means that any allocator set as the default needs to be ready to
	deal with arbitrary memory allocation requests; in addition, the size
	and number of requests will change between releases.

	An allocator set as the default will never be released, even if later
	another allocator replaces it as the default. Not only is it impractical
	for it to be released (as there might be caches created under the covers
	that refer to the allocator), in general it's also safer and more
	efficient to keep it around.

	If you wish to use a custom allocator in a context, it's best to provide
	it as the argument to the various creation functions rather than setting
	it as the default. Setting the default allocator is not encouraged.

	If you do set an allocator as the default, either do it for all time in
	your app, or do it in a nested fashion (by restoring the previous allocator
	when you exit your context). The latter might be appropriate for plug-ins
	or libraries that wish to set the default allocator.
*/
CF_EXPORT
void CFAllocatorSetDefault(CFAllocatorRef allocator);

CF_EXPORT
CFAllocatorRef CFAllocatorGetDefault(void);

CF_EXPORT
CFAllocatorRef CFAllocatorCreate(CFAllocatorRef allocator, CFAllocatorContext *context);

CF_EXPORT
void *CFAllocatorAllocate(CFAllocatorRef allocator, CFIndex size, CFOptionFlags hint);

CF_EXPORT
void *CFAllocatorReallocate(CFAllocatorRef allocator, void *ptr, CFIndex newsize, CFOptionFlags hint);

CF_EXPORT
void CFAllocatorDeallocate(CFAllocatorRef allocator, void *ptr);

CF_EXPORT
CFIndex CFAllocatorGetPreferredSizeForSize(CFAllocatorRef allocator, CFIndex size, CFOptionFlags hint);

CF_EXPORT
void CFAllocatorGetContext(CFAllocatorRef allocator, CFAllocatorContext *context);


/* Polymorphic CF functions */

CF_EXPORT
CFTypeID CFGetTypeID(CFTypeRef cf);

CF_EXPORT
CFStringRef CFCopyTypeIDDescription(CFTypeID type_id);

CF_EXPORT
CFTypeRef CFRetain(CFTypeRef cf);

CF_EXPORT
void CFRelease(CFTypeRef cf);

CF_EXPORT
CFIndex CFGetRetainCount(CFTypeRef cf);

// This function is unavailable in ARC mode. Use CFBridgingRelease instead.
CF_EXPORT
CFTypeRef CFMakeCollectable(CFTypeRef cf) CF_AUTOMATED_REFCOUNT_UNAVAILABLE;

CF_EXPORT
Boolean CFEqual(CFTypeRef cf1, CFTypeRef cf2);

CF_EXPORT
CFHashCode CFHash(CFTypeRef cf);

CF_EXPORT
CFStringRef CFCopyDescription(CFTypeRef cf);

CF_EXPORT
CFAllocatorRef CFGetAllocator(CFTypeRef cf);

CF_EXTERN_C_END

#endif /* ! __COREFOUNDATION_CFBASE__ */

