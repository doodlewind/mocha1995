#ifndef _mo_scan_h_
#define _mo_scan_h_
/*
** Mocha lexical scanner interface.
**
** Brendan Eich, 6/15/95
*/
#include <stddef.h>
#ifdef MOCHAFILE
#include <stdio.h>
#endif
#include "prmacros.h"
#include "mo_prvtd.h"
#include "mo_bcode.h"
#include "mo_pubtd.h"

NSPR_BEGIN_EXTERN_C

typedef enum MochaTokenType {
    TOK_EOF,                            /* end of file */
    TOK_EOL,                            /* end of line */
    TOK_SEMI,                           /* semicolon */
    TOK_LB, TOK_RB,                     /* left and right brackets */
    TOK_LC, TOK_RC,                     /* left and right curlies */
    TOK_LP, TOK_RP,                     /* left and right parentheses */
    TOK_COMMA,                          /* comma operator */
    TOK_ASSIGN,                         /* assignment ops (= += -= etc.) */
    TOK_HOOK, TOK_COLON,                /* conditional (?:) */
    TOK_OR,                             /* logical or (||) */
    TOK_AND,                            /* logical and (&&) */
    TOK_BITOR,                          /* bitwise-or (|) */
    TOK_BITXOR,                         /* bitwise-xor (^) */
    TOK_BITAND,                         /* bitwise-and (&) */
    TOK_EQOP,                           /* equality ops (== !=) */
    TOK_RELOP,                          /* relational ops (< <= > >=) */
    TOK_SHOP,                           /* shift ops (<< >> >>>) */
    TOK_PLUS,                           /* plus */
    TOK_MINUS,                          /* minus */
    TOK_MULOP,                          /* multiply/divide ops (* / %) */
    TOK_UNARYOP,                        /* unary prefix operator */
    TOK_INCOP,                          /* increment/decrement (++ --) */
    TOK_DOT,                            /* member operator (.) */
    TOK_NAME,                           /* identifier */
    TOK_NUMBER,                         /* numeric constant */
    TOK_STRING,                         /* string constant */
    TOK_PRIMARY,                        /* true, false, null, this, super */
    TOK_FUNCTION,                       /* function keyword */
    TOK_IF,                             /* if keyword */
    TOK_ELSE,                           /* else keyword */
    TOK_SWITCH,                         /* switch keyword */
    TOK_CASE,                           /* case keyword */
    TOK_DEFAULT,                        /* default keyword */
    TOK_WHILE,                          /* while keyword */
    TOK_DO,                             /* do keyword */
    TOK_FOR,                            /* for keyword */
    TOK_BREAK,                          /* break keyword */
    TOK_CONTINUE,                       /* continue keyword */
    TOK_IN,                             /* in keyword */
    TOK_VAR,                            /* var keyword */
    TOK_WITH,                           /* with keyword */
    TOK_RETURN,                         /* return keyword */
    TOK_NEW,                            /* new keyword */
    TOK_RESERVED,                       /* reserved keywords */
    TOK_MAX                             /* domain size */
} MochaTokenType;

#define IS_PRIMARY_TOKEN(tt)    (TOK_NAME <= (tt) && (tt) <= TOK_PRIMARY)

struct MochaToken {
    MochaTokenType      type;           /* char value or above enumerator */
    char                *ptr;           /* beginning of token in line buffer */
    union {
        MochaAtom       *atom;          /* atom table entry */
        MochaFloat      fval;           /* floating point number */
        MochaOp         op;             /* operator, for minimal parsers */
    } u;
};

typedef struct MochaTokenBuf {
    char                *base;          /* base of line or stream buffer */
    char                *limit;         /* limit for quick bounds check */
    char                *ptr;           /* next char to get, or slot to use */
} MochaTokenBuf;

#define MOCHA_LINE_MAX  256             /* logical line buffer size limit --
                                           physical line length is unlimited */

struct MochaTokenStream {
    MochaToken          token;          /* last token scanned */
    MochaToken          pushback;       /* pushed-back already-scanned token */
    uint16              flags;          /* flags -- see below */
    uint16              lineno;         /* current line number */
    MochaTokenBuf       linebuf;        /* line buffer for diagnostics */
    MochaTokenBuf       userbuf;        /* user input buffer if !file */
    MochaTokenBuf       tokenbuf;       /* current token string buffer */
    const char          *filename;      /* input filename or null */
#ifdef MOCHAFILE
    FILE                *file;          /* stdio stream if reading from file */
#endif
};

/* MochaTokenStream flags */
#define TSF_EOF         0x01            /* hit end of file */
#define TSF_NEWLINES    0x02            /* tokenize newlines */
#define TSF_FUNCTION    0x04            /* scanning inside function body */
#define TSF_RETURN_EXPR 0x08            /* function has 'return expr;' */
#define TSF_RETURN_VOID 0x10            /* function has 'return;' */
#define TSF_INTERACTIVE 0x20            /* interactive parsing mode */
#define TSF_COMMAND     0x40            /* command parsing mode */

#define CLEAR_PUSHBACK(ts)  ((ts)->pushback.type = TOK_EOF)
#define SCAN_NEWLINES(ts)   ((ts)->flags |= TSF_NEWLINES)
#define HIDE_NEWLINES(ts)                                                     \
    NSPR_BEGIN_MACRO                                                          \
	(ts)->flags &= ~TSF_NEWLINES;                                         \
	if ((ts)->pushback.type == TOK_EOL)                                   \
	    (ts)->pushback.type = TOK_EOF;                                    \
    NSPR_END_MACRO

/*
** Create a new token stream, either from an input buffer or from a file.
** Return null on file-open or memory-allocation failure.
**
** NB: Both mocha_New{Buffer,File}TokenStream() return a pointer to transient
** memory in the current context's data pool.  This memory is deallocated via
** PR_ARENA_RELEASE() after parsing is finished.
*/
extern MochaTokenStream *
mocha_NewTokenStream(MochaContext *mc, const char *base, size_t length,
		     const char *filename, unsigned lineno);

extern MochaTokenStream *
mocha_NewBufferTokenStream(MochaContext *mc, const char *base, size_t length);

extern MochaTokenStream *
mocha_NewFileTokenStream(MochaContext *mc, const char *filename);

extern MochaBoolean
mocha_CloseTokenStream(MochaTokenStream *ts);

/*
** Initialize the scanner, installing Mocha keywords into mc's global scope.
*/
extern int
mocha_InitScanner(MochaContext *mc);

/*
** Report an error found while scanning ts to a window or other output device
** associated with mc.
*/
extern void
mocha_ReportSyntaxError(MochaContext *mc, MochaTokenStream *ts,
			const char *message);

/*
** Look ahead one token and return its type.
*/
extern MochaTokenType
mocha_PeekToken(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg);

/*
** Get the next token from ts.
*/
extern MochaTokenType
mocha_GetToken(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg);

/*
** Push back the last scanned token onto ts.
*/
extern void
mocha_UngetToken(MochaTokenStream *ts);

/*
** Get the next token from ts if its type is tt.
*/
extern MochaBoolean
mocha_MatchToken(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg,
		 MochaTokenType tt);

NSPR_END_EXTERN_C

#endif /* _mo_scan_h_ */
