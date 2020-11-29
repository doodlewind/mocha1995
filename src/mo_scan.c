/*
** Mocha lexical scanner.
**
** Brendan Eich, 6/15/95
*/
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <memory.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "prarena.h"
#include "prlog.h"
#include "mo_atom.h"
#include "mo_bcode.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mo_scan.h"
#include "mochaapi.h"

#define RESERVE_JAVA_KEYWORDS

static struct keyword {
    char        *name;
    uint16      tokentype;      /* MochaTokenType */
    uint16      op;             /* MochaOp */
} keywords[] = {
    {"break",           TOK_BREAK,              MOP_NOP},
    {"case",            TOK_CASE,               MOP_NOP},
    {"continue",        TOK_CONTINUE,           MOP_NOP},
    {"default",         TOK_DEFAULT,            MOP_NOP},
#ifdef MOCHA_HAS_DELETE_OPERATOR
    {"delete",          TOK_UNARYOP,            MOP_DELETE},
#endif
    {"do",              TOK_DO,                 MOP_NOP},
    {"else",            TOK_ELSE,               MOP_NOP},
    {"false",           TOK_PRIMARY,            MOP_FALSE},
    {"for",             TOK_FOR,                MOP_NOP},
    {"function",        TOK_FUNCTION,           MOP_NOP},
    {"if",              TOK_IF,                 MOP_NOP},
    {"in",              TOK_IN,                 MOP_NOP},
    {"new",             TOK_NEW,                MOP_NEW},
    {"null",            TOK_PRIMARY,            MOP_NULL},
    {"return",          TOK_RETURN,             MOP_NOP},
    {"switch",          TOK_SWITCH,             MOP_NOP},
    {"this",            TOK_PRIMARY,            MOP_THIS},
    {"true",            TOK_PRIMARY,            MOP_TRUE},
    {"typeof",          TOK_UNARYOP,            MOP_TYPEOF},
    {"var",             TOK_VAR,                MOP_NOP},
    {"void",            TOK_UNARYOP,            MOP_VOID},
    {"while",           TOK_WHILE,              MOP_NOP},
    {"with",            TOK_WITH,               MOP_NOP},

#ifdef RESERVE_JAVA_KEYWORDS
    {"abstract",        TOK_RESERVED,           MOP_NOP},
    {"boolean",         TOK_RESERVED,           MOP_NOP},
    {"byte",            TOK_RESERVED,           MOP_NOP},
    {"catch",           TOK_RESERVED,           MOP_NOP},
    {"char",            TOK_RESERVED,           MOP_NOP},
    {"class",           TOK_RESERVED,           MOP_NOP},
    {"const",           TOK_RESERVED,           MOP_NOP},
    {"double",          TOK_RESERVED,           MOP_NOP},
    {"extends",         TOK_RESERVED,           MOP_NOP},
    {"final",           TOK_RESERVED,           MOP_NOP},
    {"finally",         TOK_RESERVED,           MOP_NOP},
    {"float",           TOK_RESERVED,           MOP_NOP},
    {"goto",            TOK_RESERVED,           MOP_NOP},
    {"implements",      TOK_RESERVED,           MOP_NOP},
    {"import",          TOK_RESERVED,           MOP_NOP},
    {"instanceof",      TOK_RESERVED,           MOP_NOP},
    {"int",             TOK_RESERVED,           MOP_NOP},
    {"interface",       TOK_RESERVED,           MOP_NOP},
    {"long",            TOK_RESERVED,           MOP_NOP},
    {"native",          TOK_RESERVED,           MOP_NOP},
    {"package",         TOK_RESERVED,           MOP_NOP},
    {"private",         TOK_RESERVED,           MOP_NOP},
    {"protected",       TOK_RESERVED,           MOP_NOP},
    {"public",          TOK_RESERVED,           MOP_NOP},
    {"short",           TOK_RESERVED,           MOP_NOP},
    {"static",          TOK_RESERVED,           MOP_NOP},
    {"super",           TOK_PRIMARY,            MOP_NOP},
    {"synchronized",    TOK_RESERVED,           MOP_NOP},
    {"throw",           TOK_RESERVED,           MOP_NOP},
    {"throws",          TOK_RESERVED,           MOP_NOP},
    {"transient",       TOK_RESERVED,           MOP_NOP},
    {"try",             TOK_RESERVED,           MOP_NOP},
    {"volatile",        TOK_RESERVED,           MOP_NOP},
#endif

    {0}
};

int
mocha_InitScanner(MochaContext *mc)
{
    struct keyword *kw;
    MochaAtom *atom;

    for (kw = keywords; kw->name; kw++) {
	atom = mocha_Atomize(mc, kw->name, ATOM_HELD | ATOM_KEYWORD);
	if (!atom)
	    return 0;
	atom->keyIndex = kw - keywords;
    }
    return 1;
}

MochaTokenStream *
mocha_NewTokenStream(MochaContext *mc, const char *base, size_t length,
		     const char *filename, unsigned lineno)
{
    MochaTokenStream *ts;

    ts = mocha_NewBufferTokenStream(mc, base, length);
    if (!ts)
	return 0;
    ts->filename = filename;
    ts->lineno = lineno;
    return ts;
}

MochaTokenStream *
mocha_NewBufferTokenStream(MochaContext *mc, const char *base, size_t length)
{
    size_t nb;
    MochaTokenStream *ts;

    nb = sizeof(MochaTokenStream) + MOCHA_LINE_MAX;
    PR_ARENA_ALLOCATE(ts, &mc->tempPool, nb);
    if (!ts) {
	MOCHA_ReportOutOfMemory(mc);
	return 0;
    }
    memset(ts, 0, nb);
    CLEAR_PUSHBACK(ts);
    ts->linebuf.base = ts->linebuf.limit = ts->linebuf.ptr = (char *)(ts + 1);
    ts->userbuf.base = (char *)base;
    ts->userbuf.limit = (char *)base + length;
    ts->userbuf.ptr = (char *)base;
    return ts;
}

#ifdef MOCHAFILE
MochaTokenStream *
mocha_NewFileTokenStream(MochaContext *mc, const char *filename)
{
    MochaTokenStream *ts;
    FILE *file;

    ts = mocha_NewBufferTokenStream(mc, 0, 0);
    if (!ts)
	return 0;
    file = fopen(filename, "r");
    if (!file) {
	MOCHA_ReportError(mc, "can't open %s: %s", filename, strerror(errno));
	return 0;
    }
    ts->file = file;
    ts->filename = filename;
    return ts;
}
#endif

MochaBoolean
mocha_CloseTokenStream(MochaTokenStream *ts)
{
#ifdef MOCHAFILE
    return !ts->file || fclose(ts->file) == 0;
#else
    return MOCHA_TRUE;
#endif
}

static int
GetChar(MochaTokenStream *ts)
{
    ptrdiff_t length;
    char *nl;

    if (ts->linebuf.ptr >= ts->linebuf.limit) {
#ifdef MOCHAFILE
	if (ts->file) {
	    if (!fgets(ts->linebuf.base, MOCHA_LINE_MAX, ts->file)) {
		ts->flags |= TSF_EOF;
		return EOF;
	    }
	    length = strlen(ts->linebuf.base);
	} else
#endif
	{
	    length = ts->userbuf.limit - ts->userbuf.ptr;
	    if (length <= 0) {
		ts->flags |= TSF_EOF;
		return EOF;
	    }

	    /*
	    ** Any one of \n, \r, or \r\n ends a line (longest match wins).
	    */
	    for (nl = ts->userbuf.ptr; nl < ts->userbuf.limit; nl++) {
		if (*nl == '\n')
		    break;
		if (*nl == '\r') {
		    if (nl[1] == '\n')
			nl++;
		    break;
		}
	    }

	    /*
	    ** If there was an end of line char, copy through it into linebuf.
	    ** Else copy MOCHA_LINE_MAX-1 bytes into linebuf.
	    */
	    if (nl < ts->userbuf.limit)
		length = nl - ts->userbuf.ptr + 1;
	    if (length >= MOCHA_LINE_MAX)
		length = MOCHA_LINE_MAX - 1;
	    memcpy(ts->linebuf.base, ts->userbuf.ptr, length);
	    ts->userbuf.ptr += length;

	    /*
	    ** Make sure linebuf contains \n for EOL (don't do this in userbuf
	    ** because the user's input string might readonly).
	    */
	    if (*nl == '\r' && ts->linebuf.base[length-1] == '\r')
		ts->linebuf.base[length-1] = '\n';
	}
	if (ts->linebuf.base[length-1] == '\n' ||
	    ts->userbuf.ptr == ts->userbuf.limit) {
	    ts->lineno++;
	}
	ts->linebuf.limit = ts->linebuf.base + length;
	ts->linebuf.ptr = ts->linebuf.base;
    }
    return *ts->linebuf.ptr++;
}

static void
UngetChar(MochaTokenStream *ts, int c)
{
    if (c == EOF)
	return;
    PR_ASSERT(ts->linebuf.ptr > ts->linebuf.base);
    *--ts->linebuf.ptr = c;
}

static int
PeekChar(MochaTokenStream *ts)
{
    int c;

    c = GetChar(ts);
    UngetChar(ts, c);
    return c;
}

static int
MatchChar(MochaTokenStream *ts, int nextChar)
{
    int c;

    c = GetChar(ts);
    if (c == nextChar)
	return 1;
    UngetChar(ts, c);
    return 0;
}

void
mocha_ReportSyntaxError(MochaContext *mc, MochaTokenStream *ts,
			const char *message)
{
    char *limit, lastc;
    MochaErrorReporter onError;
    MochaErrorReport report;

    PR_ASSERT(ts->linebuf.limit < ts->linebuf.base + MOCHA_LINE_MAX);
    limit = ts->linebuf.limit;
    lastc = limit[-1];
    limit[(lastc == '\n') ? -1 : 0] = '\0';
    onError = mc->errorReporter;
    if (onError) {
	report.filename = ts->filename;
	report.lineno = ts->lineno;
	report.linebuf = ts->linebuf.base;
	report.tokenptr = ts->token.ptr;
	(*onError)(mc, message, &report);
    } else {
	if (!(ts->flags & TSF_INTERACTIVE))
	    fprintf(stderr, "Mocha: ");
	if (ts->filename)
	    fprintf(stderr, "%s, ", ts->filename);
	if (ts->lineno)
	    fprintf(stderr, "line %u: ", ts->lineno);
	fprintf(stderr, "%s:\n%s\n", message, ts->linebuf.base);
    }
    if (lastc == '\n')
	limit[-1] = lastc;
}

MochaTokenType
mocha_PeekToken(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaTokenType tt;

    tt = ts->pushback.type;
    if (tt == TOK_EOF) {
	tt = mocha_GetToken(mc, ts, cg);
	mocha_UngetToken(ts);
    }
    return tt;
}

#define SBINCR  64

static MochaBoolean
GrowTokenBuf(MochaContext *mc, MochaTokenBuf *tb)
{
    char *base;
    ptrdiff_t offset, length;
    PRArenaPool *pool;

    base = tb->base;
    offset = tb->ptr - base;
    length = tb->limit - base;
    pool = &mc->tempPool;
    if (!base) {
	PR_ARENA_ALLOCATE(base, pool, SBINCR);
    } else {
	PR_ARENA_GROW(base, pool, length, SBINCR);
    }
    if (!base) {
	MOCHA_ReportOutOfMemory(mc);
	return MOCHA_FALSE;
    }
    tb->base = base;
    tb->limit = base + length + SBINCR;
    tb->ptr = base + offset;
    return MOCHA_TRUE;
}

static MochaBoolean
AppendToTokenBuf(MochaContext *mc, MochaTokenBuf *tb, char c)
{
    if (tb->ptr == tb->limit && !GrowTokenBuf(mc, tb))
	return MOCHA_FALSE;
    *tb->ptr++ = c;
    return MOCHA_TRUE;
}

MochaTokenType
mocha_GetToken(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    int c;
    MochaAtom *atom;

    if (ts->pushback.type != TOK_EOF) {
	ts->token = ts->pushback;
	CLEAR_PUSHBACK(ts);
	return ts->token.type;
    }

#define INIT_TOKENBUF(tb)   ((tb)->ptr = (tb)->base)
#define FINISH_TOKENBUF(tb) if (!AppendToTokenBuf(mc, tb, '\0')) RETURN(TOK_EOF)
#define RETURN(tt)          return (ts->token.type = tt)

retry:
    do {
	c = GetChar(ts);
	if (c == '\n') {
	    if (cg && mocha_NewSourceNote(mc, cg, SRC_NEWLINE) < 0)
		RETURN(TOK_EOF);
	    if (ts->flags & TSF_NEWLINES)
		break;
	}
    } while (isspace(c));
    if (c == EOF)
	RETURN(TOK_EOF);

    ts->token.ptr = ts->linebuf.ptr - 1;

    if (isalpha(c) || c == '_' || c == '$') {
	INIT_TOKENBUF(&ts->tokenbuf);
	do {
	    if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
		RETURN(TOK_EOF);
	    c = GetChar(ts);
	} while (isalnum(c) || c == '_' || c == '$');
	UngetChar(ts, c);
	FINISH_TOKENBUF(&ts->tokenbuf);

	atom = mocha_Atomize(mc, ts->tokenbuf.base, ATOM_NAME);
	if (!atom) RETURN(TOK_EOF);
	if (atom->flags & ATOM_KEYWORD) {
	    struct keyword *kw;

	    kw = &keywords[atom->keyIndex];
	    ts->token.u.op = kw->op;
	    RETURN(kw->tokentype);
	}
	ts->token.u.atom = atom;
	RETURN(TOK_NAME);
    }

    if (isdigit(c) || (c == '.' && isdigit(PeekChar(ts)))) {
	int base;
	char *endptr;
	unsigned long ulval;
	MochaFloat fval;

	base = 10;
	INIT_TOKENBUF(&ts->tokenbuf);

	if (c == '0') {
	    if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
		RETURN(TOK_EOF);
	    c = GetChar(ts);
	    if (tolower(c) == 'x') {
		if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
		    RETURN(TOK_EOF);
		c = GetChar(ts);
		base = 16;
	    } else if (isdigit(c) && c < '8') {
		base = 8;
	    }
	}

	while (isxdigit(c)) {
	    if (base < 16 && (isalpha(c) || (base == 8 && c >= '8')))
		break;
	    if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
		RETURN(TOK_EOF);
	    c = GetChar(ts);
	}

	if (base == 10 && (c == '.' || tolower(c) == 'e')) {
	    if (c == '.') {
		do {
		    if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
			RETURN(TOK_EOF);
		    c = GetChar(ts);
		} while (isdigit(c));
	    }
	    if (tolower(c) == 'e') {
		if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
		    RETURN(TOK_EOF);
		c = GetChar(ts);
		if (c == '+' || c == '-') {
		    if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
			RETURN(TOK_EOF);
		    c = GetChar(ts);
		}
		if (!isdigit(c)) {
		    mocha_ReportSyntaxError(mc, ts, "missing exponent");
		} else {
		    do {
			if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
			    RETURN(TOK_EOF);
			c = GetChar(ts);
		    } while (isdigit(c));
		}
	    }
	}

	UngetChar(ts, c);
	FINISH_TOKENBUF(&ts->tokenbuf);

	if (base == 10) {
	    /* Let strtod() do the hard work and validity checks. */
	    fval = strtod(ts->tokenbuf.base, &endptr);
	    if (endptr == ts->tokenbuf.base) {
		mocha_ReportSyntaxError(mc, ts,
					"malformed floating point literal");
	    }
	} else {
	    /* Let strtoul() do the hard work, then check for overflow */
	    ulval = strtoul(ts->tokenbuf.base, &endptr, base);
	    PR_ASSERT(endptr == ts->tokenbuf.ptr - 1);
	    if (ulval == ULONG_MAX && errno == ERANGE)
		mocha_ReportSyntaxError(mc, ts, "integer literal too large");
	    fval = ulval;
	}
	atom = mocha_Atomize(mc, ts->tokenbuf.base, ATOM_NUMBER);
	if (!atom) RETURN(TOK_EOF);
	atom->fval = fval;
	ts->token.u.atom = atom;
	RETURN(TOK_NUMBER);
    }

    if (c == '"' || c == '\'') {
	int val, qc = c;

	INIT_TOKENBUF(&ts->tokenbuf);
	while ((c = GetChar(ts)) != qc) {
	    if (c == '\n' || c == EOF) {
	badstr:
		UngetChar(ts, c);
		mocha_ReportSyntaxError(mc, ts, "unterminated string literal");
		break;
	    }
	    if (mc->charFilter) {
		int skip = mc->charFilter(mc->charFilterArg, c);
		if (skip > 0) {
		    if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
			RETURN(TOK_EOF);
		    while (--skip >= 0) {
			c = GetChar(ts);
			if (c == '\n' || c == EOF)
			    goto badstr;
			if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
			    RETURN(TOK_EOF);
		    }
		    continue;
		}
	    }
	    if (c == '\\') {
		switch (c = GetChar(ts)) {
		  case 'b': c = '\b'; break;
		  case 'f': c = '\f'; break;
		  case 'n': c = '\n'; break;
		  case 'r': c = '\r'; break;
		  case 't': c = '\t'; break;
		  case 'v': c = '\v'; break;

		  default:
		    if (isdigit(c) && c < '8') {
			val = c - '0';
			while (isdigit(c = GetChar(ts)) && c < '8')
			    val = 8 * val + c - '0';
			UngetChar(ts, c);
			c = val;
		    } else if (c == 'u') {
			/* TODO: \uxxxx Unicode escapes */
		    } else if (c == 'x') {
			val = 0;
			while (isxdigit(c = GetChar(ts))) {
			    val = 16 * val
				+ (isdigit(c) ? c - '0'
					      : 10 + tolower(c) - 'a');
			}
			UngetChar(ts, c);
			c = val;
		    }
		    break;
		}
	    }
	    if (!AppendToTokenBuf(mc, &ts->tokenbuf, (char)c))
		RETURN(TOK_EOF);
	}
	FINISH_TOKENBUF(&ts->tokenbuf);
	atom = mocha_Atomize(mc, ts->tokenbuf.base, ATOM_STRING);
	if (!atom) RETURN(TOK_EOF);
	ts->token.u.atom = atom;
	RETURN(TOK_STRING);
    }

    switch (c) {
      case '\n': c = TOK_EOL; break;
      case ';': c = TOK_SEMI; break;
      case '[': c = TOK_LB; break;
      case ']': c = TOK_RB; break;
      case '{': c = TOK_LC; break;
      case '}': c = TOK_RC; break;
      case '(': c = TOK_LP; break;
      case ')': c = TOK_RP; break;
      case ',': c = TOK_COMMA; break;
      case '?': c = TOK_HOOK; break;
      case ':': c = TOK_COLON; break;
      case '.': c = TOK_DOT; break;

      case '|':
	if (MatchChar(ts, c)) {
	    c = TOK_OR;
	} else if (MatchChar(ts, '=')) {
	    ts->token.u.op = MOP_BITOR;
	    c = TOK_ASSIGN;
	} else {
	    c = TOK_BITOR;
	}
	break;

      case '^':
	if (MatchChar(ts, '=')) {
	    ts->token.u.op = MOP_BITXOR;
	    c = TOK_ASSIGN;
	} else {
	    c = TOK_BITXOR;
	}
	break;

      case '&':
	if (MatchChar(ts, c)) {
	    c = TOK_AND;
	} else if (MatchChar(ts, '=')) {
	    ts->token.u.op = MOP_BITAND;
	    c = TOK_ASSIGN;
	} else {
	    c = TOK_BITAND;
	}
	break;

      case '=':
	if (MatchChar(ts, c)) {
	    ts->token.u.op = MOP_EQ;
	    c = TOK_EQOP;
	} else {
	    ts->token.u.op = MOP_NOP;
	    c = TOK_ASSIGN;
	}
	break;

      case '!':
	if (MatchChar(ts, '=')) {
	    ts->token.u.op = MOP_NE;
	    c = TOK_EQOP;
	} else {
	    ts->token.u.op = MOP_NOT;
	    c = TOK_UNARYOP;
	}
	break;

      case '<':
	/* XXX treat HTML begin-comment as comment-till-end-of-line */
	if (MatchChar(ts, '!')) {
	    if (MatchChar(ts, '-')) {
		if (MatchChar(ts, '-'))
		    goto skipline;
		UngetChar(ts, '-');
	    }
	    UngetChar(ts, '!');
	}
	if (MatchChar(ts, c)) {
	    ts->token.u.op = MOP_LSH;
	    c = MatchChar(ts, '=') ? TOK_ASSIGN : TOK_SHOP;
	} else {
	    ts->token.u.op = MatchChar(ts, '=') ? MOP_LE : MOP_LT;
	    c = TOK_RELOP;
	}
	break;

      case '>':
	if (MatchChar(ts, c)) {
	    ts->token.u.op = MatchChar(ts, c) ? MOP_URSH : MOP_RSH;
	    c = MatchChar(ts, '=') ? TOK_ASSIGN : TOK_SHOP;
	} else {
	    ts->token.u.op = MatchChar(ts, '=') ? MOP_GE : MOP_GT;
	    c = TOK_RELOP;
	}
	break;

      case '*':
	ts->token.u.op = MOP_MUL;
	c = MatchChar(ts, '=') ? TOK_ASSIGN : TOK_MULOP;
	break;

      case '/':
	if (MatchChar(ts, '/')) {
skipline:
	    while ((c = GetChar(ts)) != EOF && c != '\n')
		/* skip to end of line */;
	    UngetChar(ts, c);
	    goto retry;
	}
	if (MatchChar(ts, '*')) {
	    while ((c = GetChar(ts)) != EOF
		&& !(c == '*' && MatchChar(ts, '/'))) {
		if (c == '/' && MatchChar(ts, '*')) {
		    if (MatchChar(ts, '/'))
			goto retry;
		    mocha_ReportSyntaxError(mc, ts, "nested comment");
		}
	    }
	    if (c == EOF)
		mocha_ReportSyntaxError(mc, ts, "unterminated comment");
	    goto retry;
	}
	ts->token.u.op = MOP_DIV;
	c = MatchChar(ts, '=') ? TOK_ASSIGN : TOK_MULOP;
	break;

      case '%':
	ts->token.u.op = MOP_MOD;
	c = MatchChar(ts, '=') ? TOK_ASSIGN : TOK_MULOP;
	break;

      case '~':
	ts->token.u.op = MOP_BITNOT;
	c = TOK_UNARYOP;
	break;

      case '+':
      case '-':
	if (MatchChar(ts, '=')) {
	    ts->token.u.op = (c == '+') ? MOP_ADD : MOP_SUB;
	    c = TOK_ASSIGN;
	} else if (MatchChar(ts, c)) {
	    ts->token.u.op = (c == '+') ? MOP_INC : MOP_DEC;
	    c = TOK_INCOP;
	} else if (c == '-') {
	    ts->token.u.op = MOP_NEG;
	    c = TOK_MINUS;
	} else {
	    c = TOK_PLUS;
	}
	break;

      default:
	mocha_ReportSyntaxError(mc, ts, "illegal character");
	goto retry;
    }

    PR_ASSERT(c < TOK_MAX);
    RETURN(c);

#undef INIT_TOKENBUF
#undef FINISH_TOKENBUF
#undef RETURN
}

void
mocha_UngetToken(MochaTokenStream *ts)
{
    PR_ASSERT(ts->pushback.type == TOK_EOF);
    ts->pushback = ts->token;
}

MochaBoolean
mocha_MatchToken(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg,
		 MochaTokenType tt)
{
    if (mocha_GetToken(mc, ts, cg) == tt)
	return MOCHA_TRUE;
    mocha_UngetToken(ts);
    return MOCHA_FALSE;
}
