/*
 * Copyright (c) 2010 Apple Inc. All rights reserved.
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

/*	CFDate.h
	Copyright (c) 1998-2009, Apple Inc. All rights reserved.
*/

#if !defined(__COREFOUNDATION_CFDATE__)
#define __COREFOUNDATION_CFDATE__ 1

#include <CoreFoundation/CFBase.h>

CF_EXTERN_C_BEGIN

typedef double CFTimeInterval;
typedef CFTimeInterval CFAbsoluteTime;
/* absolute time is the time interval since the reference date */
/* the reference date (epoch) is 00:00:00 1 January 2001. */

CF_EXPORT
CFAbsoluteTime CFAbsoluteTimeGetCurrent(void);

CF_EXPORT
const CFTimeInterval kCFAbsoluteTimeIntervalSince1970;
CF_EXPORT
const CFTimeInterval kCFAbsoluteTimeIntervalSince1904;

typedef const struct __CFDate * CFDateRef;

CF_EXPORT
CFTypeID CFDateGetTypeID(void);

CF_EXPORT
CFDateRef CFDateCreate(CFAllocatorRef allocator, CFAbsoluteTime at);

CF_EXPORT
CFAbsoluteTime CFDateGetAbsoluteTime(CFDateRef theDate);

CF_EXPORT
CFTimeInterval CFDateGetTimeIntervalSinceDate(CFDateRef theDate, CFDateRef otherDate);

CF_EXPORT
CFComparisonResult CFDateCompare(CFDateRef theDate, CFDateRef otherDate, void *context);

typedef const struct __CFTimeZone * CFTimeZoneRef;

typedef struct {
    SInt32 year;
    SInt8 month;
    SInt8 day;
    SInt8 hour;
    SInt8 minute;
    double second;
} CFGregorianDate;

typedef struct {
    SInt32 years;
    SInt32 months;
    SInt32 days;
    SInt32 hours;
    SInt32 minutes;
    double seconds;
} CFGregorianUnits;

enum {
    kCFGregorianUnitsYears = (1UL << 0),
    kCFGregorianUnitsMonths = (1UL << 1),
    kCFGregorianUnitsDays = (1UL << 2),
    kCFGregorianUnitsHours = (1UL << 3),
    kCFGregorianUnitsMinutes = (1UL << 4),
    kCFGregorianUnitsSeconds = (1UL << 5),
    kCFGregorianAllUnits = 0x00FFFFFF
};
typedef CFOptionFlags CFGregorianUnitFlags;

CF_EXPORT
Boolean CFGregorianDateIsValid(CFGregorianDate gdate, CFOptionFlags unitFlags);

CF_EXPORT
CFAbsoluteTime CFGregorianDateGetAbsoluteTime(CFGregorianDate gdate, CFTimeZoneRef tz);

CF_EXPORT
CFGregorianDate CFAbsoluteTimeGetGregorianDate(CFAbsoluteTime at, CFTimeZoneRef tz);

CF_EXPORT
CFAbsoluteTime CFAbsoluteTimeAddGregorianUnits(CFAbsoluteTime at, CFTimeZoneRef tz, CFGregorianUnits units);

CF_EXPORT
CFGregorianUnits CFAbsoluteTimeGetDifferenceAsGregorianUnits(CFAbsoluteTime at1, CFAbsoluteTime at2, CFTimeZoneRef tz, CFOptionFlags unitFlags);

CF_EXPORT
SInt32 CFAbsoluteTimeGetDayOfWeek(CFAbsoluteTime at, CFTimeZoneRef tz);

CF_EXPORT
SInt32 CFAbsoluteTimeGetDayOfYear(CFAbsoluteTime at, CFTimeZoneRef tz);

CF_EXPORT
SInt32 CFAbsoluteTimeGetWeekOfYear(CFAbsoluteTime at, CFTimeZoneRef tz);

CF_EXTERN_C_END

#endif /* ! __COREFOUNDATION_CFDATE__ */

