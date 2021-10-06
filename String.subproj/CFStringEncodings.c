/*
 * Copyright (c) 2005 Apple Computer, Inc. All rights reserved.
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
/*	CFStringEncodings.c
	Copyright 1999-2002, Apple, Inc. All rights reserved.
	Responsibility: Aki Inoue
*/

#include "CFInternal.h"
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFByteOrder.h>
#include "CFUtilitiesPriv.h"
#include <string.h>
#include "CFStringEncodingConverterExt.h"
#include "CFUniChar.h"
#include "CFUnicodeDecomposition.h"

static UInt32 __CFWantsToUseASCIICompatibleConversion = (UInt32)-1;
CF_INLINE UInt32 __CFGetASCIICompatibleFlag(void) {
    if (__CFWantsToUseASCIICompatibleConversion == (UInt32)-1) {
        __CFWantsToUseASCIICompatibleConversion = false;
    }
    return (__CFWantsToUseASCIICompatibleConversion ? kCFStringEncodingASCIICompatibleConversion : 0);
}

void _CFStringEncodingSetForceASCIICompatibility(Boolean flag) {
    __CFWantsToUseASCIICompatibleConversion = (flag ? (UInt32)true : (UInt32)false);
}

Boolean (*__CFCharToUniCharFunc)(UInt32 flags, uint8_t ch, UniChar *unicodeChar) = NULL;

// To avoid early initialization issues, we just initialize this here
// This should not be const as it is changed
UniChar __CFCharToUniCharTable[256] = {
  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,
 16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,
 32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
 48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
 64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
 80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,
 96,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255
};    

void __CFSetCharToUniCharFunc(Boolean (*func)(UInt32 flags, UInt8 ch, UniChar *unicodeChar)) {
    if (__CFCharToUniCharFunc != func) {
        int ch;
        __CFCharToUniCharFunc = func;
        if (func) {
            for (ch = 128; ch < 256; ch++) {
                UniChar uch;
                __CFCharToUniCharTable[ch] = (__CFCharToUniCharFunc(0, ch, &uch) ? uch : 0xFFFD);
            }
        } else {	// If we have no __CFCharToUniCharFunc, assume 128..255 return the value as-is
            for (ch = 128; ch < 256; ch++) __CFCharToUniCharTable[ch] = ch;
        }
    }
}

__private_extern__ void __CFStrConvertBytesToUnicode(const uint8_t *bytes, UniChar *buffer, CFIndex numChars) {
    CFIndex idx;
    for (idx = 0; idx < numChars; idx++) buffer[idx] = __CFCharToUniCharTable[bytes[idx]];
}


/* The minimum length the output buffers should be in the above functions
*/
#define kCFCharConversionBufferLength 512


#define MAX_LOCAL_CHARS		(sizeof(buffer->localBuffer) / sizeof(uint8_t))
#define MAX_LOCAL_UNICHARS	(sizeof(buffer->localBuffer) / sizeof(UniChar))

/* Convert a byte stream to ASCII (7-bit!) or Unicode, with a CFVarWidthCharBuffer struct on the stack. false return indicates an error occured during the conversion. The caller needs to free the returned buffer in either ascii or unicode (indicated by isASCII), if shouldFreeChars is true. 
9/18/98 __CFStringDecodeByteStream now avoids to allocate buffer if buffer->chars is not NULL
Added useClientsMemoryPtr; if not-NULL, and the provided memory can be used as is, this is set to true
__CFStringDecodeByteStream2() is kept around for any internal clients who might be using it; it should be deprecated
!!! converterFlags is only used for the UTF8 converter at this point
*/
Boolean __CFStringDecodeByteStream2(const uint8_t *bytes, UInt32 len, CFStringEncoding encoding, Boolean alwaysUnicode, CFVarWidthCharBuffer *buffer, Boolean *useClientsMemoryPtr) {
    return __CFStringDecodeByteStream3(bytes, len, encoding, alwaysUnicode, buffer, useClientsMemoryPtr, 0);
}

enum {
    __NSNonLossyErrorMode = -1,
    __NSNonLossyASCIIMode = 0,
    __NSNonLossyBackslashMode = 1,
    __NSNonLossyHexInitialMode = __NSNonLossyBackslashMode + 1,
    __NSNonLossyHexFinalMode = __NSNonLossyHexInitialMode + 4,
    __NSNonLossyOctalInitialMode = __NSNonLossyHexFinalMode + 1,
    __NSNonLossyOctalFinalMode = __NSNonLossyHexFinalMode + 3
};

Boolean __CFStringDecodeByteStream3(const uint8_t *bytes, UInt32 len, CFStringEncoding encoding, Boolean alwaysUnicode, CFVarWidthCharBuffer *buffer, Boolean *useClientsMemoryPtr, UInt32 converterFlags) {

    if (useClientsMemoryPtr) *useClientsMemoryPtr = false;

    buffer->isASCII = !alwaysUnicode;
    buffer->shouldFreeChars = false;
    buffer->numChars = 0;

    if (0 == len) return true;

    buffer->allocator = (buffer->allocator ? buffer->allocator : __CFGetDefaultAllocator());

    if ((encoding == kCFStringEncodingUTF16) || (encoding == kCFStringEncodingUTF16BE) || (encoding == kCFStringEncodingUTF16LE)) { // UTF-16
        const UTF16Char *src = (const UTF16Char *)bytes;
        const UTF16Char *limit = (const UTF16Char *)(bytes + len);
        bool swap = false;

        if (kCFStringEncodingUTF16 == encoding) {
            UTF16Char bom = ((*src == 0xFFFE) || (*src == 0xFEFF) ? *(src++) : 0);

#if defined(__BIG_ENDIAN__)
            if (bom == 0xFFFE) swap = true;
#else
            if (bom != 0xFEFF) swap = true;
#endif
            if (bom) useClientsMemoryPtr = NULL;
        } else {
#if defined(__BIG_ENDIAN__)
            if (kCFStringEncodingUTF16LE == encoding) swap = true;
#else
            if (kCFStringEncodingUTF16BE == encoding) swap = true;
#endif
        }

        buffer->numChars = limit - src;

        if (useClientsMemoryPtr && !swap) { // If the caller is ready to deal with no-copy situation, and the situation is possible, indicate it...
            *useClientsMemoryPtr = true;
            buffer->chars.unicode = (UniChar *)src;
            buffer->isASCII = false;
        } else {
            if (buffer->isASCII) {	// Let's see if we can reduce the Unicode down to ASCII...
                const UTF16Char *characters = src;
                UTF16Char mask = (swap ? 0x80FF : 0xFF80);
    
                while (characters < limit) {
                    if (*(characters++) & mask) {
                        buffer->isASCII = false;
                        break;
                    }
                }
            }
    
            if (buffer->isASCII) {
                uint8_t *dst;
                if (NULL == buffer->chars.ascii) { // we never reallocate when buffer is supplied
                    if (buffer->numChars > MAX_LOCAL_CHARS) {
                        buffer->chars.ascii = CFAllocatorAllocate(buffer->allocator, (buffer->numChars * sizeof(uint8_t)), 0);
                        buffer->shouldFreeChars = true;
                    } else {
                        buffer->chars.ascii = (uint8_t *)buffer->localBuffer;
                    }
                }
                dst = buffer->chars.ascii;
    
                if (swap) {
                    while (src < limit) *(dst++) = (*(src++) >> 8);
                } else {
                    while (src < limit) *(dst++) = *(src++);
                }
            } else {
                UTF16Char *dst;

                if (NULL == buffer->chars.unicode) { // we never reallocate when buffer is supplied
                    if (buffer->numChars > MAX_LOCAL_UNICHARS) {
                        buffer->chars.unicode = CFAllocatorAllocate(buffer->allocator, (buffer->numChars * sizeof(UTF16Char)), 0);
                        buffer->shouldFreeChars = true;
                    } else {
                        buffer->chars.unicode = (UTF16Char *)buffer->localBuffer;
                    }
                }
                dst = buffer->chars.unicode;

                if (swap) {
                    while (src < limit) *(dst++) = CFSwapInt16(*(src++));
                } else {
                    memmove(dst, src, buffer->numChars * sizeof(UTF16Char));
                }
            }
        }
    } else if ((encoding == kCFStringEncodingUTF32) || (encoding == kCFStringEncodingUTF32BE) || (encoding == kCFStringEncodingUTF32LE)) {
        const UTF32Char *src = (const UTF32Char *)bytes;
        const UTF32Char *limit = (const UTF32Char *)(bytes + len);
        bool swap = false;

        if (kCFStringEncodingUTF32 == encoding) {
            UTF32Char bom = ((*src == 0xFFFE0000) || (*src == 0x0000FEFF) ? *(src++) : 0);

#if defined(__BIG_ENDIAN__)
            if (bom == 0xFFFE0000) swap = true;
#else
            if (bom != 0x0000FEFF) swap = true;
#endif
        } else {
#if defined(__BIG_ENDIAN__)
            if (kCFStringEncodingUTF32LE == encoding) swap = true;
#else
            if (kCFStringEncodingUTF32BE == encoding) swap = true;
#endif
        }

        buffer->numChars = limit - src;

        {
            // Let's see if we have non-ASCII or non-BMP
            const UTF32Char *characters = src;
            UTF32Char asciiMask = (swap ? 0x80FFFFFF : 0xFFFFFF80);
            UTF32Char bmpMask = (swap ? 0x0000FFFF : 0xFFFF0000);
    
            while (characters < limit) {
                if (*characters & asciiMask) {
                    buffer->isASCII = false;
                    if (*characters & bmpMask) ++(buffer->numChars);
                }
                ++characters;
            }
        }
    
        if (buffer->isASCII) {
            uint8_t *dst;
            if (NULL == buffer->chars.ascii) { // we never reallocate when buffer is supplied
                if (buffer->numChars > MAX_LOCAL_CHARS) {
                    buffer->chars.ascii = CFAllocatorAllocate(buffer->allocator, (buffer->numChars * sizeof(uint8_t)), 0);
                    buffer->shouldFreeChars = true;
                } else {
                    buffer->chars.ascii = (uint8_t *)buffer->localBuffer;
                }
            }
            dst = buffer->chars.ascii;

            if (swap) {
                while (src < limit) *(dst++) = (*(src++) >> 24);
            } else {
                while (src < limit) *(dst++) = *(src++);
            }
        } else {
            if (NULL == buffer->chars.unicode) { // we never reallocate when buffer is supplied
                if (buffer->numChars > MAX_LOCAL_UNICHARS) {
                    buffer->chars.unicode = CFAllocatorAllocate(buffer->allocator, (buffer->numChars * sizeof(UTF16Char)), 0);
                    buffer->shouldFreeChars = true;
                } else {
                    buffer->chars.unicode = (UTF16Char *)buffer->localBuffer;
                }
            }
            CFUniCharFromUTF32(src, limit - src, buffer->chars.unicode, false,
#if defined(__BIG_ENDIAN__)
            !swap
#else
            swap
#endif
            );
        }
    } else {
        UInt32 idx;
        const uint8_t *chars = (const uint8_t *)bytes;
        const uint8_t *end = chars + len;
    
        switch (encoding) {
        case kCFStringEncodingNonLossyASCII: {
            UTF16Char currentValue = 0;
            uint8_t character;
            int8_t mode = __NSNonLossyASCIIMode;
    
            buffer->isASCII = false;
            buffer->shouldFreeChars = !buffer->chars.unicode && (len <= MAX_LOCAL_UNICHARS) ? false : true;
            buffer->chars.unicode = (buffer->chars.unicode ? buffer->chars.unicode : (len <= MAX_LOCAL_UNICHARS) ? (UniChar *)buffer->localBuffer : CFAllocatorAllocate(buffer->allocator, len * sizeof(UniChar), 0));
            buffer->numChars = 0;
    
            while (chars < end) {
                character = (*chars++);
    
                switch (mode) {
                case __NSNonLossyASCIIMode:
                    if (character == '\\') {
                        mode = __NSNonLossyBackslashMode;
                    } else if (character < 0x80) {
                        currentValue = character;
                    } else {
                        mode = __NSNonLossyErrorMode;
                    }
                    break;
    
                case __NSNonLossyBackslashMode:
                    if ((character == 'U') || (character == 'u')) {
                        mode = __NSNonLossyHexInitialMode;
                        currentValue = 0;
                    } else if ((character >= '0') && (character <= '9')) {
                        mode = __NSNonLossyOctalInitialMode;
                        currentValue = character - '0';
                    } else if (character == '\\') {
                        mode = __NSNonLossyASCIIMode;
                        currentValue = character;
                    } else {
                        mode = __NSNonLossyErrorMode;
                    }
                    break;
    
                default:
                    if (mode < __NSNonLossyHexFinalMode) {
                        if ((character >= '0') && (character <= '9')) {
                            currentValue = (currentValue << 4) | (character - '0');
                            if (++mode == __NSNonLossyHexFinalMode) mode = __NSNonLossyASCIIMode;
                        } else {
                            if (character >= 'a') character -= ('a' - 'A');
                            if ((character >= 'A') && (character <= 'F')) {
                                currentValue = (currentValue << 4) | ((character - 'A') + 10);
                                if (++mode == __NSNonLossyHexFinalMode) mode = __NSNonLossyASCIIMode;
                            } else {
                                mode = __NSNonLossyErrorMode;
                            }
                        }
                    } else {
                        if ((character >= '0') && (character <= '9')) {
                            currentValue = (currentValue << 3) | (character - '0');
                            if (++mode == __NSNonLossyOctalFinalMode) mode = __NSNonLossyASCIIMode;
                        } else {
                            mode = __NSNonLossyErrorMode;
                        }
                    }
                    break;
                }
    
                if (mode == __NSNonLossyASCIIMode) {
                    buffer->chars.unicode[buffer->numChars++] = currentValue;
                } else if (mode == __NSNonLossyErrorMode) {
                    return false;
                }
            }
            return (mode == __NSNonLossyASCIIMode);
        }
    
        case kCFStringEncodingUTF8:
            if ((len >= 3) && (chars[0] == 0xef) && (chars[1] == 0xbb) && (chars[2] == 0xbf)) {	// If UTF8 BOM, skip
                chars += 3;
                len -= 3;
                if (0 == len) return true;
            }
            if (buffer->isASCII) {
                for (idx = 0; idx < len; idx++) {
                    if (128 <= chars[idx]) {
                        buffer->isASCII = false;
                        break;
                    }
                }
            }
            if (buffer->isASCII) {
                buffer->numChars = len;
                buffer->shouldFreeChars = !buffer->chars.ascii && (len <= MAX_LOCAL_CHARS) ? false : true;
                buffer->chars.ascii = (buffer->chars.ascii ? buffer->chars.ascii : (len <= MAX_LOCAL_CHARS) ? (uint8_t *)buffer->localBuffer : CFAllocatorAllocate(buffer->allocator, len * sizeof(uint8_t), 0));
                memmove(buffer->chars.ascii, chars, len * sizeof(uint8_t));
            } else {
                UInt32 numDone;
                static CFStringEncodingToUnicodeProc __CFFromUTF8 = NULL;
    
                if (!__CFFromUTF8) {
                    const CFStringEncodingConverter *converter = CFStringEncodingGetConverter(kCFStringEncodingUTF8);
                    __CFFromUTF8 = (CFStringEncodingToUnicodeProc)converter->toUnicode;
                }
    
                buffer->shouldFreeChars = !buffer->chars.unicode && (len <= MAX_LOCAL_UNICHARS) ? false : true;
                buffer->chars.unicode = (buffer->chars.unicode ? buffer->chars.unicode : (len <= MAX_LOCAL_UNICHARS) ? (UniChar *)buffer->localBuffer : CFAllocatorAllocate(buffer->allocator, len * sizeof(UniChar), 0));
                buffer->numChars = 0;
                while (chars < end) {
                    numDone = 0;
                    chars += __CFFromUTF8(converterFlags, chars, end - chars, &(buffer->chars.unicode[buffer->numChars]), len - buffer->numChars, &numDone);
    
                    if (0 == numDone) {
                        if (buffer->shouldFreeChars) CFAllocatorDeallocate(buffer->allocator, buffer->chars.unicode);
                        buffer->isASCII = !alwaysUnicode;
                        buffer->shouldFreeChars = false;
                        buffer->chars.ascii = NULL;
                        buffer->numChars = 0;
                        return false;
                    }
                    buffer->numChars += numDone;
                }
            }
            break;
    
        default:
            if (CFStringEncodingIsValidEncoding(encoding)) {
                const CFStringEncodingConverter *converter = CFStringEncodingGetConverter(encoding);
                Boolean isASCIISuperset = __CFStringEncodingIsSupersetOfASCII(encoding);
                
                if (!converter) return false;
    
                if (!isASCIISuperset) buffer->isASCII = false;

                if (buffer->isASCII) {
                    for (idx = 0; idx < len; idx++) {
                        if (128 <= chars[idx]) {
                            buffer->isASCII = false;
                            break;
                        }
                    }
                }

                if (converter->encodingClass == kCFStringEncodingConverterCheapEightBit) {
                    if (buffer->isASCII) {
                        buffer->numChars = len;
                        buffer->shouldFreeChars = !buffer->chars.ascii && (len <= MAX_LOCAL_CHARS) ? false : true;
                        buffer->chars.ascii = (buffer->chars.ascii ? buffer->chars.ascii : (len <= MAX_LOCAL_CHARS) ? (uint8_t *)buffer->localBuffer : CFAllocatorAllocate(buffer->allocator, len * sizeof(uint8_t), 0));
                        memmove(buffer->chars.ascii, chars, len * sizeof(uint8_t));
                    } else {
                        buffer->shouldFreeChars = !buffer->chars.unicode && (len <= MAX_LOCAL_UNICHARS) ? false : true;
                        buffer->chars.unicode = (buffer->chars.unicode ? buffer->chars.unicode : (len <= MAX_LOCAL_UNICHARS) ? (UniChar *)buffer->localBuffer : CFAllocatorAllocate(buffer->allocator, len * sizeof(UniChar), 0));
                        buffer->numChars = len;
                        if (kCFStringEncodingASCII == encoding || kCFStringEncodingISOLatin1 == encoding) {
                            for (idx = 0; idx < len; idx++) buffer->chars.unicode[idx] = (UniChar)chars[idx];
                        } else {
                            for (idx = 0; idx < len; idx++)
                                if (chars[idx] < 0x80 && isASCIISuperset)
                                    buffer->chars.unicode[idx] = (UniChar)chars[idx];
                                else if (!((CFStringEncodingCheapEightBitToUnicodeProc)converter->toUnicode)(0, chars[idx], buffer->chars.unicode + idx))
                                    return false;
                        }
                    }
                } else {
                    if (buffer->isASCII) {
                        buffer->numChars = len;
                        buffer->shouldFreeChars = !buffer->chars.ascii && (len <= MAX_LOCAL_CHARS) ? false : true;
                        buffer->chars.ascii = (buffer->chars.ascii ? buffer->chars.ascii : (len <= MAX_LOCAL_CHARS) ? (uint8_t *)buffer->localBuffer : CFAllocatorAllocate(buffer->allocator, len * sizeof(uint8_t), 0));
                        memmove(buffer->chars.ascii, chars, len * sizeof(uint8_t));
                    } else {
                        UInt32 guessedLength = CFStringEncodingCharLengthForBytes(encoding, 0, bytes, len);
                        static UInt32 lossyFlag = (UInt32)-1;
    
                        buffer->shouldFreeChars = !buffer->chars.unicode && (guessedLength <= MAX_LOCAL_UNICHARS) ? false : true;
                        buffer->chars.unicode = (buffer->chars.unicode ? buffer->chars.unicode : (guessedLength <= MAX_LOCAL_UNICHARS) ? (UniChar *)buffer->localBuffer : CFAllocatorAllocate(buffer->allocator, guessedLength * sizeof(UniChar), 0));
    
                        if (lossyFlag == (UInt32)-1) lossyFlag = (_CFExecutableLinkedOnOrAfter(CFSystemVersionPanther) ? 0 : kCFStringEncodingAllowLossyConversion);
    
                        if (CFStringEncodingBytesToUnicode(encoding, lossyFlag|__CFGetASCIICompatibleFlag(), bytes, len, NULL, buffer->chars.unicode, (guessedLength > MAX_LOCAL_UNICHARS ? guessedLength : MAX_LOCAL_UNICHARS), &(buffer->numChars))) {
                            if (buffer->shouldFreeChars) CFAllocatorDeallocate(buffer->allocator, buffer->chars.unicode);
                            buffer->isASCII = !alwaysUnicode;
                            buffer->shouldFreeChars = false;
                            buffer->chars.ascii = NULL;
                            buffer->numChars = 0;
                            return false;
                        }
                    }
                }
            } else {
                return false;
            }
        }
    }

    return true;
}


/* Create a byte stream from a CFString backing. Can convert a string piece at a time
   into a fixed size buffer. Returns number of characters converted. 
   Characters that cannot be converted to the specified encoding are represented
   with the char specified by lossByte; if 0, then lossy conversion is not allowed
   and conversion stops, returning partial results.
   Pass buffer==NULL if you don't care about the converted string (but just the convertability,
   or number of bytes required, indicated by usedBufLen). 
   Does not zero-terminate. If you want to create Pascal or C string, allow one extra byte at start or end. 

   Note: This function is intended to work through CFString functions, so it should work
   with NSStrings as well as CFStrings.
*/
CFIndex __CFStringEncodeByteStream(CFStringRef string, CFIndex rangeLoc, CFIndex rangeLen, Boolean generatingExternalFile, CFStringEncoding encoding, char lossByte, uint8_t *buffer, CFIndex max, CFIndex *usedBufLen) {
    CFIndex totalBytesWritten = 0;	/* Number of written bytes */
    CFIndex numCharsProcessed = 0;	/* Number of processed chars */
    const UniChar *unichars;

    if (encoding == kCFStringEncodingUTF8 && (unichars = CFStringGetCharactersPtr(string))) {
        static CFStringEncodingToBytesProc __CFToUTF8 = NULL;

        if (!__CFToUTF8) {
            const CFStringEncodingConverter *utf8Converter = CFStringEncodingGetConverter(kCFStringEncodingUTF8);
            __CFToUTF8 = (CFStringEncodingToBytesProc)utf8Converter->toBytes;
        }
        numCharsProcessed = __CFToUTF8((generatingExternalFile ? kCFStringEncodingPrependBOM : 0), unichars + rangeLoc, rangeLen, buffer, (buffer ? max : 0), &totalBytesWritten);

    } else if (encoding == kCFStringEncodingNonLossyASCII) {
	const char *hex = "0123456789abcdef";
	UniChar ch;
	CFStringInlineBuffer buf;
	CFStringInitInlineBuffer(string, &buf, CFRangeMake(rangeLoc, rangeLen));
	while (numCharsProcessed < rangeLen) {
	    CFIndex reqLength; /* Required number of chars to encode this UniChar */
	    CFIndex cnt;
	    char tmp[6];
	    ch = CFStringGetCharacterFromInlineBuffer(&buf, numCharsProcessed);
	    if ((ch >= ' ' && ch <= '~' && ch != '\\') || (ch == '\n' || ch == '\r' || ch == '\t')) {
		reqLength = 1;
		tmp[0] = ch;
	    } else {
		if (ch == '\\') {
		    tmp[1] = '\\';
		    reqLength = 2;
		} else if (ch < 256) {	/* \nnn; note that this is not NEXTSTEP encoding but a (small) UniChar */
		    tmp[1] = '0' + (ch >> 6);
		    tmp[2] = '0' + ((ch >> 3) & 7);
		    tmp[3] = '0' + (ch & 7);
		    reqLength = 4;
		} else {	/* \Unnnn */
		    tmp[1] = 'u'; // Changed to small+u in order to be aligned with Java
		    tmp[2] = hex[(ch >> 12) & 0x0f];
		    tmp[3] = hex[(ch >> 8) & 0x0f];
		    tmp[4] = hex[(ch >> 4) & 0x0f];
		    tmp[5] = hex[ch & 0x0f];
		    reqLength = 6;
		}
		tmp[0] = '\\';
	    }
            if (buffer) {
                if (totalBytesWritten + reqLength > max) break; /* Doesn't fit..
.*/
                for (cnt = 0; cnt < reqLength; cnt++) {
                    buffer[totalBytesWritten + cnt] = tmp[cnt];
                }
            }
	    totalBytesWritten += reqLength;
	    numCharsProcessed++;
	}
    } else if ((encoding == kCFStringEncodingUTF16) || (encoding == kCFStringEncodingUTF16BE) || (encoding == kCFStringEncodingUTF16LE)) {
   	CFIndex extraForBOM = (generatingExternalFile && (encoding == kCFStringEncodingUTF16) ? sizeof(UniChar) : 0);
        numCharsProcessed = rangeLen;
        if (buffer && (numCharsProcessed * (CFIndex)sizeof(UniChar) + extraForBOM > max)) {
            numCharsProcessed = (max > extraForBOM) ? ((max - extraForBOM) / sizeof(UniChar)) : 0;
        }
        totalBytesWritten = (numCharsProcessed * sizeof(UniChar)) + extraForBOM;
	if (buffer) {
	    if (extraForBOM) {	/* Generate BOM */
#if defined(__BIG_ENDIAN__)
		*buffer++ = 0xfe; *buffer++ = 0xff;
#else
		*buffer++ = 0xff; *buffer++ = 0xfe;
#endif
	    }
	    CFStringGetCharacters(string, CFRangeMake(rangeLoc, numCharsProcessed), (UniChar *)buffer);
            if (
#if defined(__BIG_ENDIAN__)
                kCFStringEncodingUTF16LE
#else
                kCFStringEncodingUTF16BE
#endif
                == encoding) { // Need to swap
                UTF16Char *characters = (UTF16Char *)buffer;
                const UTF16Char *limit = characters + numCharsProcessed;

                while (characters < limit) {
                    *characters = CFSwapInt16(*characters);
                    ++characters;
                }
            }
	}
    } else if ((encoding == kCFStringEncodingUTF32) || (encoding == kCFStringEncodingUTF32BE) || (encoding == kCFStringEncodingUTF32LE)) {
        UTF32Char character;
        CFStringInlineBuffer buf;
        UTF32Char *characters = (UTF32Char *)buffer;

#if defined(__BIG_ENDIAN__)
        bool swap = (encoding == kCFStringEncodingUTF32LE ? true : false);
#else
        bool swap = (encoding == kCFStringEncodingUTF32BE ? true : false);
#endif

        if (generatingExternalFile && (encoding == kCFStringEncodingUTF32)) {
            totalBytesWritten += sizeof(UTF32Char);
            if (characters) {
                if (totalBytesWritten > max) { // insufficient buffer
                    totalBytesWritten = 0;
                } else {
#if defined(__BIG_ENDIAN__)
                    *(characters++) = 0x0000FEFF;
#else
                    *(characters++) = 0xFFFE0000;
#endif
                }
            }
        }

        CFStringInitInlineBuffer(string, &buf, CFRangeMake(rangeLoc, rangeLen));
        while (numCharsProcessed < rangeLen) {
            character = CFStringGetCharacterFromInlineBuffer(&buf, numCharsProcessed);

            if (CFUniCharIsSurrogateHighCharacter(character)) {
                UTF16Char otherCharacter;

                if (((numCharsProcessed + 1) < rangeLen) && CFUniCharIsSurrogateLowCharacter((otherCharacter = CFStringGetCharacterFromInlineBuffer(&buf, numCharsProcessed + 1)))) {
                    character = CFUniCharGetLongCharacterForSurrogatePair(character, otherCharacter);
                } else if (lossByte) {
                    character = lossByte;
                } else {
                    break;
                }
            } else if (CFUniCharIsSurrogateLowCharacter(character)) {
                if (lossByte) {
                    character = lossByte;
                } else {
                    break;
                }
            }

            totalBytesWritten += sizeof(UTF32Char);

            if (characters) {
                if (totalBytesWritten > max) {
                    totalBytesWritten -= sizeof(UTF32Char);
                    break;
                }
                *(characters++) = (swap ? CFSwapInt32(character) : character);
            }

            numCharsProcessed += (character > 0xFFFF ? 2 : 1);
        }
    } else {
        CFIndex numChars;
        UInt32 flags;
        const unsigned char *cString = NULL;
        BOOL isASCIISuperset = __CFStringEncodingIsSupersetOfASCII(encoding);

        if (!CF_IS_OBJC(CFStringGetTypeID(), string) && isASCIISuperset) { // Checking for NSString to avoid infinite recursion
            const unsigned char *ptr;
            if ((cString = CFStringGetCStringPtr(string, __CFStringGetEightBitStringEncoding()))) {
                ptr = (cString += rangeLoc);
                if (__CFStringGetEightBitStringEncoding() == encoding) {
                    numCharsProcessed = (rangeLen < max || buffer == NULL ? rangeLen : max);
                    if (buffer) memmove(buffer, cString, numCharsProcessed);
                    if (usedBufLen) *usedBufLen = numCharsProcessed;
                    return numCharsProcessed;
                }
                while (*ptr < 0x80 && rangeLen > 0) {
                    ++ptr;
                    --rangeLen;
                }
                numCharsProcessed = ptr - cString;
                if (buffer) {
                    numCharsProcessed = (numCharsProcessed < max ? numCharsProcessed : max);
                    memmove(buffer, cString, numCharsProcessed);
                    buffer += numCharsProcessed;
		    max -= numCharsProcessed;
                }
                if (!rangeLen || (buffer && (max == 0))) {
                    if (usedBufLen) *usedBufLen = numCharsProcessed;
                    return numCharsProcessed;
                }
                rangeLoc += numCharsProcessed;
                totalBytesWritten += numCharsProcessed;
            }
            if (!cString && (cString = CFStringGetPascalStringPtr(string, __CFStringGetEightBitStringEncoding()))) {
                ptr = (cString += (rangeLoc + 1));
                if (__CFStringGetEightBitStringEncoding() == encoding) {
                    numCharsProcessed = (rangeLen < max || buffer == NULL ? rangeLen : max);
                    if (buffer) memmove(buffer, cString, numCharsProcessed);
                    if (usedBufLen) *usedBufLen = numCharsProcessed;
                    return numCharsProcessed;
                }
                while (*ptr < 0x80 && rangeLen > 0) {
                    ++ptr;
                    --rangeLen;
                }
                numCharsProcessed = ptr - cString;
                if (buffer) {
                    numCharsProcessed = (numCharsProcessed < max ? numCharsProcessed : max);
                    memmove(buffer, cString, numCharsProcessed);
                    buffer += numCharsProcessed;
		    max -= numCharsProcessed;
                }
                if (!rangeLen || (buffer && (max == 0))) {
                    if (usedBufLen) *usedBufLen = numCharsProcessed;
                    return numCharsProcessed;
                }
                rangeLoc += numCharsProcessed;
                totalBytesWritten += numCharsProcessed;
            }
        }

        if (!buffer) max = 0;

        // Special case for Foundation. When lossByte == 0xFF && encoding kCFStringEncodingASCII, we do the default ASCII fallback conversion
        // Aki 11/24/04 __CFGetASCIICompatibleFlag() is called only for non-ASCII superset encodings. Otherwise, it could lead to a deadlock (see 3890536).
        flags = (lossByte ? ((unsigned char)lossByte == 0xFF && encoding == kCFStringEncodingASCII ? kCFStringEncodingAllowLossyConversion : CFStringEncodingLossyByteToMask(lossByte)) : 0) | (generatingExternalFile ? kCFStringEncodingPrependBOM : 0) | (isASCIISuperset ? 0 : __CFGetASCIICompatibleFlag());

        if (!cString && (cString = (const char*)CFStringGetCharactersPtr(string))) { // Must be Unicode string
            if (CFStringEncodingIsValidEncoding(encoding)) { // Converter available in CF
                CFStringEncodingUnicodeToBytes(encoding, flags, (const UniChar*)cString + rangeLoc, rangeLen, &numCharsProcessed, buffer, max, &totalBytesWritten);
            } else {
                return 0;
            }
        } else {
            UniChar charBuf[kCFCharConversionBufferLength];
            UInt32 currentLength;
            UInt32 usedLen;
            uint32_t lastUsedLen = 0, lastNumChars = 0;
            uint32_t result;
            Boolean isCFBuiltin = CFStringEncodingIsValidEncoding(encoding);
#define MAX_DECOMP_LEN (6)

            while (rangeLen > 0) {
                currentLength = (rangeLen > kCFCharConversionBufferLength ? kCFCharConversionBufferLength : rangeLen);
                CFStringGetCharacters(string, CFRangeMake(rangeLoc, currentLength), charBuf);

                // could be in the middle of surrogate pair; back up.
                if ((rangeLen > kCFCharConversionBufferLength) && CFUniCharIsSurrogateHighCharacter(charBuf[kCFCharConversionBufferLength - 1])) --currentLength;

                if (isCFBuiltin) { // Converter available in CF
                    if ((result = CFStringEncodingUnicodeToBytes(encoding, flags, charBuf, currentLength, &numChars, buffer, max, &usedLen)) != kCFStringEncodingConversionSuccess) {
                        if (kCFStringEncodingInvalidInputStream == result) {
                            CFRange composedRange;
                            // Check the tail
                            if ((rangeLen > kCFCharConversionBufferLength) && ((currentLength - numChars) < MAX_DECOMP_LEN)) {
                                composedRange = CFStringGetRangeOfComposedCharactersAtIndex(string, rangeLoc + currentLength);

                                if ((composedRange.length <= MAX_DECOMP_LEN) && (composedRange.location < (rangeLoc + numChars))) {
                                    result = CFStringEncodingUnicodeToBytes(encoding, flags, charBuf, composedRange.location - rangeLoc, &numChars, buffer, max, &usedLen);
                                }
                            }

                            // Check the head
                            if ((kCFStringEncodingConversionSuccess != result) && (lastNumChars > 0) && (numChars < MAX_DECOMP_LEN)) {
                                composedRange = CFStringGetRangeOfComposedCharactersAtIndex(string, rangeLoc);

                                if ((composedRange.length <= MAX_DECOMP_LEN) && (composedRange.location < rangeLoc)) {
                                    // Try if the composed range can be converted
                                    CFStringGetCharacters(string, composedRange, charBuf);

                                    if (CFStringEncodingUnicodeToBytes(encoding, flags, charBuf, composedRange.length, &numChars, NULL, 0, &usedLen) == kCFStringEncodingConversionSuccess) { // OK let's try the last run
                                        CFIndex lastRangeLoc = rangeLoc - lastNumChars;

                                        currentLength = composedRange.location - lastRangeLoc;
                                        CFStringGetCharacters(string, CFRangeMake(lastRangeLoc, currentLength), charBuf);

                                        if ((result = CFStringEncodingUnicodeToBytes(encoding, flags, charBuf, currentLength, &numChars, (max ? buffer - lastUsedLen : NULL), (max ? max + lastUsedLen : 0), &usedLen)) == kCFStringEncodingConversionSuccess) { // OK let's try the last run
                                            // Looks good. back up
                                            totalBytesWritten -= lastUsedLen;
                                            numCharsProcessed -= lastNumChars;

                                            rangeLoc = lastRangeLoc;
                                            rangeLen += lastNumChars;

                                            if (max) {
                                                buffer -= lastUsedLen;
                                                max += lastUsedLen;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (kCFStringEncodingConversionSuccess != result) { // really failed
                            totalBytesWritten += usedLen;
                            numCharsProcessed += numChars;
                            break;
                        }
                    }
                } else {
                    return 0;
                }

                totalBytesWritten += usedLen;
                numCharsProcessed += numChars;

                rangeLoc += numChars;
                rangeLen -= numChars;
                if (max) {
                    buffer += usedLen;
                    max -= usedLen;
                    if (max <= 0) break;
                }
                lastUsedLen = usedLen; lastNumChars = numChars;
                flags &= ~kCFStringEncodingPrependBOM;
            }
        }
    }
    if (usedBufLen) *usedBufLen = totalBytesWritten;
    return numCharsProcessed;
}

CFStringRef CFStringCreateWithFileSystemRepresentation(CFAllocatorRef alloc, const char *buffer) {
    return CFStringCreateWithCString(alloc, buffer, CFStringFileSystemEncoding());
}

CFIndex CFStringGetMaximumSizeOfFileSystemRepresentation(CFStringRef string) {
    CFIndex len = CFStringGetLength(string);
    CFStringEncoding enc = CFStringGetFastestEncoding(string);
    switch (enc) {
	case kCFStringEncodingASCII:
	case kCFStringEncodingMacRoman:
	    return len * 3 + 1;
	default:
	    return len * 9 + 1;
    }
} 

Boolean CFStringGetFileSystemRepresentation(CFStringRef string, char *buffer, CFIndex maxBufLen) {
#if defined(__MACH__)
#define MAX_STACK_BUFFER_LEN	(255)
    const UTF16Char *characters = CFStringGetCharactersPtr(string);
    CFIndex length = CFStringGetLength(string);
    uint32_t usedBufLen;

    if (maxBufLen < length) return false; // Since we're using UTF-8, the byte length is never shorter than the char length. Also, it filters out 0 == maxBufLen

    if (NULL == characters) {
        if (length > MAX_STACK_BUFFER_LEN) {
            UTF16Char charactersBuffer[MAX_STACK_BUFFER_LEN];
            CFRange range = CFRangeMake(0, MAX_STACK_BUFFER_LEN);
            uint32_t localUsedBufLen;

            usedBufLen = 0;

            while ((length > 0) && (maxBufLen > usedBufLen)) {
                CFStringGetCharacters(string, range, charactersBuffer);
                if (CFUniCharIsSurrogateHighCharacter(charactersBuffer[range.length - 1])) --range.length; // Backup for a high surrogate

                if (!CFUniCharDecompose(charactersBuffer, range.length, NULL, (void *)buffer, maxBufLen - usedBufLen, &localUsedBufLen, true, kCFUniCharUTF8Format, true)) return false;
                buffer += localUsedBufLen;
                usedBufLen += localUsedBufLen;

                length -= range.length;
                range.location += range.length;
                range.length = (length < MAX_STACK_BUFFER_LEN ? length : MAX_STACK_BUFFER_LEN);
            }
        } else {
            UTF16Char charactersBuffer[MAX_STACK_BUFFER_LEN];

            CFStringGetCharacters(string, CFRangeMake(0, length), charactersBuffer);
            if (!CFUniCharDecompose(charactersBuffer, length, NULL, (void *)buffer, maxBufLen, &usedBufLen, true, kCFUniCharUTF8Format, true)) return false;
            buffer += usedBufLen;
        }
    } else {
        if (!CFUniCharDecompose(characters, length, NULL, (void *)buffer, maxBufLen, &usedBufLen, true, kCFUniCharUTF8Format, true)) return false;
        buffer += usedBufLen;
    }

    if (usedBufLen < (uint32_t)maxBufLen) { // Since the filename has its own limit, this is ok for now
        *buffer = '\0';
        return true;
    } else {
        return false;
    }
#else __MACH__
    return CFStringGetCString(string, buffer, maxBufLen, CFStringFileSystemEncoding());
#endif __MACH__
}

Boolean _CFStringGetFileSystemRepresentation(CFStringRef string, uint8_t *buffer, CFIndex maxBufLen) {
    return CFStringGetFileSystemRepresentation(string, buffer, maxBufLen);
}

