#ifndef prtypes_h___
#define prtypes_h___

/* jwz: to get uint on linux */
#ifdef __linux
#include <fcntl.h>
#endif

#if defined(XP_PC)

#if defined(_WIN32)

#define PR_PUBLIC_API(__x)      _declspec(dllexport) __x
#define PR_CALLBACK
#define PR_CALLBACK_DECL
#define PR_STATIC_CALLBACK(__x) static __x

#else  /* !_WIN32 */

#define PR_CALLBACK_DECL        _cdecl

#if defined(_WINDLL)
#define PR_PUBLIC_API(__x)      __x _cdecl _loadds _export
#define PR_CALLBACK             __cdecl __loadds
#define PR_STATIC_CALLBACK(__x) static __x PR_CALLBACK

#else   /* !_WINDLL */

#define PR_PUBLIC_API(__x)      __x _cdecl _export
#define PR_CALLBACK             __cdecl __export
#define PR_STATIC_CALLBACK(__x) __x PR_CALLBACK
#endif  /* ! WINDLL */

#endif  /* !_WIN32 */

#else  /* Mac or Unix */

#define PR_PUBLIC_API(__x)      __x
#define PR_CALLBACK
#define PR_CALLBACK_DECL
#define PR_STATIC_CALLBACK(__x) static __x

#endif /* Mac or Unix */

/* Typedefs */
typedef struct PRCListStr PRCList;
typedef struct PRMonitorStr PRMonitor;
typedef struct PRThreadStr PRThread;
typedef struct PRTimeStr PRTime;
typedef struct PREventQueueStr PREventQueue;
typedef struct PREventStr PREvent;

/*
** A prword_t is an integer that is the same size as a void*
*/
typedef long prword_t;
typedef unsigned long uprword_t;

#define BYTES_PER_WORD sizeof(prword_t)
#define BITS_PER_WORD (sizeof(prword_t) * 8)

#if 0
#ifdef IS_64
#define BITS_PER_WORD_LOG2 6
#define BYTES_PER_WORD_LOG2 3
#define WORDS_PER_DWORD_LOG2 0
#else
#define BITS_PER_WORD_LOG2 5
#define BYTES_PER_WORD_LOG2 2
#define WORDS_PER_DWORD_LOG2 1
#endif
#endif

#define BITS_PER_WORD_LOG2 PR_BITS_PER_WORD_LOG2
#define BYTES_PER_WORD_LOG2 PR_BYTES_PER_WORD_LOG2
#define WORDS_PER_DWORD_LOG2 PR_WORDS_PER_DWORD_LOG2 

#define WORDS_PER_DWORD (1L << WORDS_PER_DWORD_LOG2)

#define BYTES_PER_DWORD PR_BYTES_PER_DWORD
#define BYTES_PER_DWORD_LOG2 PR_BYTES_PER_DWORD_LOG2
#define BITS_PER_DWORD PR_BITS_PER_DWORD
#define BITS_PER_DWORD_LOG2 PR_BITS_PER_DWORD_LOG2

#define DWORDS_TO_WORDS(_dw) ((_dw) << WORDS_PER_DWORD_LOG2)

/* NOTE: Rounds up */
#define WORDS_TO_DWORDS(_dw) \
    (((_dw) + WORDS_PER_DWORD - 1L) >> WORDS_PER_DWORD_LOG2)

/*
** A prbitmap_t is an integer that can be used for bitmaps
*/
typedef unsigned long prbitmap_t;

#define TEST_BIT(_map,_bit) \
    ((_map)[(_bit)>>BITS_PER_WORD_LOG2] & (1L << ((_bit) & (BITS_PER_WORD-1))))
#define SET_BIT(_map,_bit) \
    ((_map)[(_bit)>>BITS_PER_WORD_LOG2] |= (1L << ((_bit) & (BITS_PER_WORD-1))))
#define CLEAR_BIT(_map,_bit) \
    ((_map)[(_bit)>>BITS_PER_WORD_LOG2] &= ~(1L << ((_bit) & (BITS_PER_WORD-1))))

#define BITS_PER_BITMAP BITS_PER_WORD
#define BYTES_PER_BITMAP BYTES_PER_WORD
#define BITS_PER_BITMAP_LOG2 BITS_PER_WORD_LOG2
#define BYTES_PER_BITMAP_LOG2 BYTES_PER_WORD_LOG2


/* SVR4 typedef of uint is commonly found on UNIX machines. */
#if !defined(__SVR4)  && !defined(_AIX)    && !defined(__sgi) && \
    !defined(__sun__) && !defined(__linux) && !defined(__hpux) && \
    !defined(__osf__) && !defined(NEC) && !defined(SONY) && \
    !defined(SYSV) && !defined(SNI) && !defined(BSDI_2)
typedef unsigned int uint;
#endif

typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef signed short int16;
#ifdef IS_64 /* XXX ok for alpha, but not right on all 64-bit architectures */
typedef unsigned int uint32;
typedef signed int int32;
#else
typedef unsigned long uint32;
typedef signed long int32;
#endif

/*
** Use PRBool for variables and parameter types.  Use PRPackedBool within
** structs where bitfields are not desirable but minimum overhead matters.
**
** Use PR_FALSE and PR_TRUE for clarity of target type in assignments and
** actual args.  Use 'if (bool)', 'while (!bool)', '(bool ? x : y)', etc.
** to test Booleans just as you would C int-valued conditions.
*/
typedef enum { PR_FALSE, PR_TRUE } PRBool;
typedef uint8 PRPackedBool;

#endif /* prtypes_h___ */
