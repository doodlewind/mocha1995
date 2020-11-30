/* -*- Mode: C; tab-width: 4; -*- */

/* NSPR Logging

NSPR Logging allows you to compile logging messages into debug builds that
can be enabled and disabled by changing the PRLOG environment variable.
Log messages are associated with "log modules" that can be enabled or
disabled individually, allowing control over what gets logged, and
consequently how much output you have to wade through. Any number of modules
can be defined.

Logs are printed via the PR_LOG macro (example below). PR_LOG also associates
a "log level" with each log statement, allowing finer control over what gets
logged. There are 4 predefined levels: 

		out: always printed unless the log module is completely disabled, 
		error: an error message
		warn: a warning message
		debug: a debug message

These levels are progressive, meaning, if you enable errors, you get standard
output too; if you enable warnings, you get errors and output, etc. This
lets you tune the amount of detail for a particular module.

Here's an example file that uses logging:

  #include "prlog.h"

  PR_LOG_DEFINE(Foo);	// define a "log module"

  void main() {
	  int err;

	  err = doit();
	  if (err) {
		  // Use the log to write an error message:
		  PR_LOG(Foo, error, ("doit got %d", err));
	  }
  }

Then provided that in your shell you've defined Foo as one of the
log modules:

  setenv PRLOG "Foo:error, Bar:debug"

The following message will show up:

  main Foo error: doit got -1	[log.c:11]

where "main" is the name of the thread.

Nested log messages can also be printed by using the PR_LOG_BEGIN and 
PR_LOG_END macros. These are just like PR_LOG, but they specify a block
around which and entering and exiting message will be printed (a '>' symbol
is used to signify entering, and '<' for exiting -- recursive calls are
indented). For example:

  int fact(int n) {
      int result;
      PR_LOG_BEGIN(Fact, debug, ("calling fact(%d)", n));
      if (n == 1)
	      result = 1;
      else
	      result = n * fact(n - 1);
      PR_LOG_END(Fact, debug, ("fact(%d) returning %d", n, result));
      return result;
  }

Calling fact(3) with Fact debug logs enabled will produce the following:

  main Fact debug>	calling fact(3)	[log.c:8]
  main Fact debug>	2 calling fact(2)	[log.c:8]
  main Fact debug>	3 | calling fact(1)	[log.c:8]
  main Fact debug<	3 | fact(1) returning 1	[log.c:13]
  main Fact debug<	2 fact(2) returning 2	[log.c:13]
  main Fact debug<	fact(3) returning 6	[log.c:13]

There are two special parameters that can be passed via the PRLOG environment
variable: 'sync' and 'file'. (Note: you can't use 'sync' or 'file' for a log
module name.)

		file: specifies a file for logs to go to
		sync: value of 1 or 0 specifying whether logging is synchronous or
			  asynchronous (synchronous means a flush occurs after every 
			  message)

For example:

  setenv PRLOG "Foo:warn, sync:0, file:/tmp/output.txt"

This will cause Foo warnings, errors and output to be logged asynchronously
to the file /tmp/output.txt.

In the debugger, a log module is just a structure that has a name suffixed
by the word 'Log' that contains 3 fields: the name of the module,
the currently enabled level, and the current depth of recursion. So, for
example, if you want to change the level of the Foo module while in the
debugger you could say:

gdb: p FooLog.level = PRLogLevel_warn
$1 = PRLogLevel_warn
gdb: p FooLog
$2 = {
  name = 0x513cf0 "Foo", 
  level = PRLogLevel_warn, 
  depth = 0
}

*/

#ifndef prlog2_h___
#define prlog2_h___

#include "prmacros.h"
#include "prtypes.h"
#include "prglobal.h"

NSPR_BEGIN_EXTERN_C

typedef enum PRLogLevel {
    PRLogLevel_none,			/* 0: nothing */
    PRLogLevel_out,				/* 1: always printed */
    PRLogLevel_error,			/* 2: error messages */
    PRLogLevel_warn,			/* 3: warning messages */
    PRLogLevel_debug,			/* 4: debugging messages */

    /* make sure this is last: */
    PRLogLevel_uninitialized	/* uninitialized */
} PRLogLevel;

typedef struct PRLogModule {
	const char*			name;
	PRLogLevel			level;
	int					depth;
} PRLogModule;

/* Make logging conditional on debug compilation switch */
#if defined(DEBUG) || defined(FORCE_PR_LOG)

#define PR_LOG_DEFINE(moduleName)									   \
    PRLogModule moduleName##Log = {									   \
		#moduleName, PRLogLevel_uninitialized, -1					   \
    }																   \

#define PR_LOG_TEST(moduleName, levelName)							   \
    (moduleName##Log.level >= PRLogLevel_##levelName				   \
	 && moduleName##Log.level < PRLogLevel_uninitialized)			   \

#define PR_LOG1(moduleName, level, printfArgs, direction)			   \
	NSPR_BEGIN_MACRO												   \
		extern PRLogModule moduleName##Log;							   \
		if (PR_LOG_TEST(moduleName, level)) {						   \
			int _status_ = PR_LogEnter(&moduleName##Log,			   \
									   PRLogLevel_##level, direction); \
			if (_status_ != PR_LOG_SKIP) {							   \
				PR_LogPrint printfArgs;								   \
				PR_LogExit(_status_, __FILE__, __LINE__);			   \
			}														   \
		}															   \
	NSPR_END_MACRO													   \

#define PR_LOG(moduleName, level, printfArgs)						   \
		PR_LOG1(moduleName, level, printfArgs, 0)					   \

#define PR_LOG_BEGIN(moduleName, level, printfArgs)					   \
		PR_LOG1(moduleName, level, printfArgs, 1)					   \

#define PR_LOG_END(moduleName, level, printfArgs)					   \
		PR_LOG1(moduleName, level, printfArgs, -1)					   \

#define PR_LOG_FLUSH()					PR_LogFlush()

/* Private Functions: */

extern PR_PUBLIC_API(void)
PR_LogInit(PRLogModule* module);

extern PR_PUBLIC_API(int/*status*/)
PR_LogEnter(PRLogModule* module, PRLogLevel level, int direction);

extern PR_PUBLIC_API(void)
PR_LogPrint(const char* format, ...);

extern PR_PUBLIC_API(void)
PR_LogExit(int status, const char* file, int lineno);

extern PR_PUBLIC_API(void)
PR_LogFlush(void);

#else /* !DEBUG && !FORCE_PR_LOG */

#define PR_LOG_DEFINE(moduleName)
#define PR_LOG_TEST(moduleName, level)				0
#define PR_LOG(moduleName, level, printfArgs)
#define PR_LOG_BEGIN(moduleName, level, printfArgs)
#define PR_LOG_END(moduleName, level, printfArgs)
#define PR_LOG_FLUSH()

#endif /* !DEBUG && !FORCE_PR_LOG */

#define PR_LOG_SKIP		-99999

NSPR_END_EXTERN_C

#endif /* prlog2_h___ */
