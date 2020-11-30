#ifndef prunixos_h___
#define prunixos_h___

#define DIRECTORY_SEPARATOR	'/'
#define DIRECTORY_SEPARATOR_STR	"/"
#define PATH_SEPARATOR		':'

#include <unistd.h>
#include <stdlib.h>

#define GCPTR
#define CALLBACK
typedef int (*FARPROC)();
#define gcmemcpy(a,b,c) memcpy(a,b,c)

#define FlipSlashes(a,b)

#ifdef SUNOS4
#include "sunos4.h"
#endif

typedef int PROSFD;

#endif /* prunixos_h___ */
