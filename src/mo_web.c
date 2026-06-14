/*
** Mocha web embedder + introspection.
**
** Self-contained Emscripten entry point for the modern playground.  It
** compiles a source string with the original 1995 engine and emits a
** single JSON document describing:
**
**   - tokens   : the scanner's token stream (type, text, line, column)
**   - scripts  : disassembled bytecode for every executed script/function
**   - trace    : a per-instruction execution trace (pc, value stack, frame)
**   - output   : everything written by print()
**   - error    : the first compile/runtime error, if any
**   - result   : the value of the final top-level expression
**
** Nothing here is gated behind -DDEBUG; the engine stays a black box apart
** from the single mocha_TraceHook call wired into the interpreter loop.
*/
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prlog.h"
#include "mo_atom.h"
#include "mo_bcode.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mo_parse.h"
#include "mo_scan.h"
#include "mo_scope.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"
#include "mo_introspect.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

/* ------------------------------------------------------------------ */
/* Growable string buffer                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char   *p;
    size_t  len;
    size_t  cap;
    int     oom;
} StrBuf;

static void
sb_reset(StrBuf *b)
{
    if (b->p) free(b->p);
    b->p = 0;
    b->len = 0;
    b->cap = 0;
    b->oom = 0;
}

static void
sb_need(StrBuf *b, size_t extra)
{
    size_t nc;
    char *np;

    if (b->oom)
        return;
    if (b->len + extra + 1 <= b->cap)
        return;
    nc = b->cap ? b->cap : 256;
    while (nc < b->len + extra + 1)
        nc *= 2;
    np = realloc(b->p, nc);
    if (!np) {
        b->oom = 1;
        return;
    }
    b->p = np;
    b->cap = nc;
}

static void
sb_putc(StrBuf *b, char c)
{
    sb_need(b, 1);
    if (b->oom)
        return;
    b->p[b->len++] = c;
    b->p[b->len] = 0;
}

static void
sb_puts(StrBuf *b, const char *s)
{
    size_t n;

    if (!s)
        return;
    n = strlen(s);
    sb_need(b, n);
    if (b->oom)
        return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

static void
sb_putf(StrBuf *b, const char *fmt, ...)
{
    char tmp[128];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof tmp) {
        sb_puts(b, tmp);
        return;
    }
    sb_need(b, (size_t)n);
    if (b->oom)
        return;
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

/* Append `s` as a JSON string literal, with escaping. */
static void
sb_jstr(StrBuf *b, const char *s)
{
    unsigned char c;

    sb_putc(b, '"');
    if (s) {
        for (; (c = (unsigned char)*s) != 0; s++) {
            switch (c) {
              case '"':  sb_puts(b, "\\\""); break;
              case '\\': sb_puts(b, "\\\\"); break;
              case '\n': sb_puts(b, "\\n");  break;
              case '\r': sb_puts(b, "\\r");  break;
              case '\t': sb_puts(b, "\\t");  break;
              default:
                if (c < 0x20)
                    sb_putf(b, "\\u%04x", c);
                else
                    sb_putc(b, (char)c);
            }
        }
    }
    sb_putc(b, '"');
}

/* ------------------------------------------------------------------ */
/* Static lookup tables                                               */
/* ------------------------------------------------------------------ */

static const char *token_names[] = {
    "EOF", "EOL", "SEMI", "LB", "RB", "LC", "RC", "LP", "RP", "COMMA",
    "ASSIGN", "HOOK", "COLON", "OR", "AND", "BITOR", "BITXOR", "BITAND",
    "EQOP", "RELOP", "SHOP", "PLUS", "MINUS", "MULOP", "UNARYOP", "INCOP",
    "DOT", "NAME", "NUMBER", "STRING", "PRIMARY", "FUNCTION", "IF", "ELSE",
    "SWITCH", "CASE", "DEFAULT", "WHILE", "DO", "FOR", "BREAK", "CONTINUE",
    "IN", "VAR", "WITH", "RETURN", "NEW", "RESERVED"
};

/* Representative lexeme for fixed-shape tokens (NAME/NUMBER/STRING are
   captured from source instead). */
static const char *token_lexemes[] = {
    "", "", ";", "[", "]", "{", "}", "(", ")", ",",
    "=", "?", ":", "||", "&&", "|", "^", "&",
    "==", "<", "<<", "+", "-", "*", "!", "++",
    ".", "", "", "", "", "function", "if", "else",
    "switch", "case", "default", "while", "do", "for", "break", "continue",
    "in", "var", "with", "return", "new", "reserved"
};

static const char *
datum_type_name(uint8 tag)
{
    switch (tag) {
      case MOCHA_UNDEF:    return "undefined";
      case MOCHA_INTERNAL: return "internal";
      case MOCHA_ATOM:     return "atom";
      case MOCHA_SYMBOL:   return "symbol";
      case MOCHA_FUNCTION: return "function";
      case MOCHA_OBJECT:   return "object";
      case MOCHA_NUMBER:   return "number";
      case MOCHA_BOOLEAN:  return "boolean";
      case MOCHA_STRING:   return "string";
      default:             return "unknown";
    }
}

static const char *
op_format_name(MochaOpFormat f)
{
    switch (f) {
      case MOF_BYTE:  return "byte";
      case MOF_JUMP:  return "jump";
      case MOF_INCOP: return "incop";
      case MOF_ARGC:  return "argc";
      case MOF_CONST: return "const";
      default:        return "byte";
    }
}

/*
** Serialize a datum without invoking any user code (no toString / valueOf),
** so it is safe to call from the per-instruction trace hook.
*/
static void
emit_datum(StrBuf *b, MochaDatum d)
{
    char num[40];

    sb_puts(b, "{\"type\":");
    sb_jstr(b, datum_type_name(d.tag));
    sb_puts(b, ",\"value\":");
    switch (d.tag) {
      case MOCHA_NUMBER:
        if (d.u.fval != d.u.fval) {
            sb_jstr(b, "NaN");
        } else {
            snprintf(num, sizeof num, "%.15g", (double)d.u.fval);
            sb_jstr(b, num);
        }
        break;
      case MOCHA_BOOLEAN:
        sb_jstr(b, d.u.bval ? "true" : "false");
        break;
      case MOCHA_STRING:
      case MOCHA_ATOM:
        sb_jstr(b, d.u.atom ? atom_name(d.u.atom) : "");
        break;
      case MOCHA_UNDEF:
        sb_jstr(b, "undefined");
        break;
      case MOCHA_FUNCTION:
        sb_jstr(b, (d.u.fun && d.u.fun->atom) ? atom_name(d.u.fun->atom)
                                              : "function");
        break;
      case MOCHA_OBJECT:
        sb_jstr(b, (d.u.obj && d.u.obj->clazz) ? d.u.obj->clazz->name
                                               : "object");
        break;
      case MOCHA_SYMBOL:
        sb_jstr(b, d.u.pair.sym ? atom_name(sym_atom(d.u.pair.sym)) : "symbol");
        break;
      default:
        sb_jstr(b, "?");
    }
    sb_putc(b, '}');
}

/* ------------------------------------------------------------------ */
/* Per-run global state                                               */
/* ------------------------------------------------------------------ */

#define MAX_SCRIPTS  256
#define MAX_STEPS    300000
#define MAX_TRACE_BYTES (24 * 1024 * 1024)
#define STACK_WINDOW 32

static MochaScript *g_scripts[MAX_SCRIPTS];
static const char  *g_script_names[MAX_SCRIPTS];
static int          g_nscripts;

static StrBuf g_trace;
static StrBuf g_output;
static StrBuf g_error;
static int    g_haserror;
static int    g_step;
static int    g_truncated;
static int    g_in_hook;

static int
script_id(MochaScript *s, const char *name)
{
    int i;

    for (i = 0; i < g_nscripts; i++)
        if (g_scripts[i] == s)
            return i;
    if (g_nscripts < MAX_SCRIPTS) {
        g_scripts[g_nscripts] = s;
        g_script_names[g_nscripts] = name;
        return g_nscripts++;
    }
    return g_nscripts - 1;
}

/* ------------------------------------------------------------------ */
/* The trace hook                                                     */
/* ------------------------------------------------------------------ */

static void
trace_hook(MochaContext *mc, MochaScript *script, MochaCode *pc,
           struct MochaStack *sp)
{
    MochaOp op;
    MochaCodeSpec *cs;
    MochaStackFrame *fr;
    MochaDatum *base, *top;
    const char *nm;
    long count, start, i;
    int sid, depth;
    unsigned argi;

    if (g_in_hook)
        return;
    if (g_step >= MAX_STEPS || g_trace.len >= MAX_TRACE_BYTES) {
        g_truncated = 1;
        return;
    }
    g_in_hook = 1;

    fr = sp->frame;
    nm = (fr && fr->fun && fr->fun->atom && fr->fun->script == script)
         ? atom_name(fr->fun->atom) : 0;
    sid = script_id(script, nm);

    op = *pc;
    cs = &mocha_CodeSpec[op];

    depth = 0;
    {
        MochaStackFrame *f;
        for (f = sp->frame; f; f = f->down)
            depth++;
    }

    if (g_step)
        sb_putc(&g_trace, ',');
    sb_putf(&g_trace, "{\"step\":%d,\"script\":%d,\"pc\":%u,\"line\":%u,\"op\":",
            g_step, sid, (unsigned)(pc - script->code),
            mocha_PCtoLineNumber(script, pc));
    sb_jstr(&g_trace, cs->name);
    sb_putf(&g_trace, ",\"depth\":%d,\"stack\":[", depth);

    base = sp->base;
    top = sp->ptr;
    count = (long)(top - base);
    start = (count > STACK_WINDOW) ? count - STACK_WINDOW : 0;
    for (i = start; i < count; i++) {
        if (i > start)
            sb_putc(&g_trace, ',');
        emit_datum(&g_trace, base[i]);
    }
    sb_puts(&g_trace, "]");

    if (fr) {
        sb_puts(&g_trace, ",\"frame\":{\"fn\":");
        sb_jstr(&g_trace, (fr->fun && fr->fun->atom)
                          ? atom_name(fr->fun->atom) : "anonymous");
        sb_puts(&g_trace, ",\"args\":[");
        for (argi = 0; argi < fr->argc; argi++) {
            if (argi)
                sb_putc(&g_trace, ',');
            emit_datum(&g_trace, fr->argv[argi]);
        }
        sb_puts(&g_trace, "],\"vars\":[");
        for (argi = 0; argi < fr->nvars; argi++) {
            if (argi)
                sb_putc(&g_trace, ',');
            emit_datum(&g_trace, fr->vars[argi]);
        }
        sb_puts(&g_trace, "]}");
    } else {
        sb_puts(&g_trace, ",\"frame\":null");
    }

    sb_putc(&g_trace, '}');
    g_step++;
    g_in_hook = 0;
}

static MochaBoolean
branch_cb(MochaContext *mc, MochaScript *script)
{
    if (g_step >= MAX_STEPS) {
        g_truncated = 1;
        return MOCHA_FALSE;     /* abort runaway loops cleanly */
    }
    return MOCHA_TRUE;
}

/* ------------------------------------------------------------------ */
/* Bytecode disassembly (one executed script)                        */
/* ------------------------------------------------------------------ */

static void
emit_script_bytecode(StrBuf *b, MochaContext *mc, MochaScript *script)
{
    MochaCode *pc, *end;
    MochaOp op;
    MochaCodeSpec *cs;
    MochaAtom *atom;
    unsigned off, len;
    int first = 1;
    char num[40];

    pc = script->code;
    end = pc + script->length;
    sb_putc(b, '[');
    while (pc < end) {
        op = *pc;
        if (op >= MOP_MAX)
            break;
        cs = &mocha_CodeSpec[op];
        if (!first)
            sb_putc(b, ',');
        first = 0;
        off = (unsigned)(pc - script->code);
        sb_putf(b, "{\"offset\":%u,\"op\":", off);
        sb_jstr(b, cs->name);
        sb_putf(b, ",\"length\":%u,\"nuses\":%d,\"ndefs\":%d,\"line\":%u,"
                   "\"format\":",
                cs->length, cs->nuses, cs->ndefs,
                mocha_PCtoLineNumber(script, pc));
        sb_jstr(b, op_format_name(cs->format));
        sb_puts(b, ",\"operand\":");
        switch (cs->format) {
          case MOF_JUMP: {
            int delta = GET_JUMP_OFFSET(pc);
            sb_putf(b, "{\"target\":%u,\"delta\":%d}",
                    (unsigned)(off + delta), delta);
            break;
          }
          case MOF_INCOP:
            sb_jstr(b, pc[1] ? "post" : "pre");
            break;
          case MOF_ARGC:
            sb_putf(b, "%u", pc[1]);
            break;
          case MOF_CONST:
            atom = GET_CONST_ATOM(mc, script, pc);
            if (op == MOP_NUMBER && atom) {
                snprintf(num, sizeof num, "%.15g", (double)atom->fval);
                sb_jstr(b, num);
            } else {
                sb_jstr(b, atom ? atom_name(atom) : "");
            }
            break;
          default:
            sb_puts(b, "null");
        }
        sb_putc(b, '}');
        len = cs->length ? cs->length : 1;
        pc += len;
    }
    sb_putc(b, ']');
}

/* ------------------------------------------------------------------ */
/* Token stream                                                       */
/* ------------------------------------------------------------------ */

static void
emit_tokens(StrBuf *b, MochaContext *mc, const char *src)
{
    MochaTokenStream *ts;
    CodeGenerator cg;
    MochaTokenType tt;
    int first = 1, idx = 0;

    sb_putc(b, '[');
    ts = mocha_NewBufferTokenStream(mc, src, strlen(src));
    if (!ts) {
        sb_putc(b, ']');
        return;
    }
    mocha_InitCodeGenerator(mc, &cg, &mc->tempPool);
    CG_RESET(&cg);

    for (;;) {
        unsigned line;
        int col;

        tt = mocha_GetToken(mc, ts, &cg);
        if (tt == TOK_EOF || (int)tt < 0 || tt >= TOK_MAX)
            break;
        if (tt == TOK_EOL)
            continue;
        if (ts->flags & TSF_EOF)
            ;   /* still record this final token */

        line = ts->lineno;
        col = (ts->token.ptr && ts->linebuf.base)
              ? (int)(ts->token.ptr - ts->linebuf.base) : 0;

        if (!first)
            sb_putc(b, ',');
        first = 0;
        sb_putf(b, "{\"index\":%d,\"type\":", idx++);
        sb_jstr(b, token_names[tt]);
        sb_putf(b, ",\"line\":%u,\"col\":%d,\"text\":", line, col);
        if (tt == TOK_NAME || tt == TOK_STRING) {
            sb_jstr(b, ts->token.u.atom ? atom_name(ts->token.u.atom)
                       : (ts->tokenbuf.base ? ts->tokenbuf.base : ""));
        } else if (tt == TOK_NUMBER) {
            char num[40];
            snprintf(num, sizeof num, "%.15g",
                     ts->token.u.atom ? (double)ts->token.u.atom->fval : 0.0);
            sb_jstr(b, num);
        } else {
            sb_jstr(b, token_lexemes[tt]);
        }
        sb_putc(b, '}');

        if (b->oom || idx > 200000 || (ts->flags & TSF_EOF))
            break;
    }
    sb_putc(b, ']');
    mocha_CloseTokenStream(ts);
}

/* ------------------------------------------------------------------ */
/* Native print + error reporter (capture, no stdout)                */
/* ------------------------------------------------------------------ */

static MochaBoolean
js_print(MochaContext *mc, MochaObject *obj,
         unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    unsigned i;
    MochaAtom *atom;

    for (i = 0; i < argc; i++) {
        if (!MOCHA_DatumToString(mc, argv[i], &atom))
            return MOCHA_FALSE;
        if (i)
            sb_putc(&g_output, ' ');
        sb_puts(&g_output, atom_name(atom));
        mocha_DropAtom(mc, atom);
    }
    sb_putc(&g_output, '\n');
    return MOCHA_TRUE;
}

static void
js_error_reporter(MochaContext *mc, const char *message,
                  MochaErrorReport *report)
{
    g_haserror = 1;
    if (g_error.len)
        return;     /* keep the first error */
    sb_puts(&g_error, "{\"message\":");
    sb_jstr(&g_error, message ? message : "error");
    if (report) {
        int col = (report->linebuf && report->tokenptr)
                  ? (int)(report->tokenptr - report->linebuf) : -1;
        sb_putf(&g_error, ",\"line\":%u,\"col\":%d,\"linebuf\":",
                report->lineno, col);
        sb_jstr(&g_error, report->linebuf ? report->linebuf : "");
    } else {
        sb_puts(&g_error, ",\"line\":0,\"col\":-1,\"linebuf\":\"\"");
    }
    sb_putc(&g_error, '}');
}

static MochaClass global_class = {
    "global",
    MOCHA_PropertyStub, MOCHA_PropertyStub, MOCHA_ListPropStub,
    MOCHA_ResolveStub,  MOCHA_ConvertStub,  MOCHA_FinalizeStub
};

static MochaFunctionSpec web_functions[] = {
    {"print", js_print, 0},
    {0}
};

/* Stubs so we need not link the Java glue (mirrors mo_shell.c). */
MochaBoolean
mocha_InitJava(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_TRUE;
}

void
mocha_DestroyJavaContext(MochaContext *mc)
{
}

/* The trace-hook global the interpreter calls (default: disabled). */
MochaTraceHook mocha_TraceHook = 0;

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

EXPORT char *
mocha_run_json(const char *source)
{
    MochaContext *mc;
    MochaObject *glob;
    MochaScript *script;
    MochaDatum result;
    StrBuf tokens = {0};
    StrBuf out = {0};
    MochaBoolean ok = MOCHA_FALSE;
    int i;

    /* Reset per-run state. */
    sb_reset(&g_trace);
    sb_reset(&g_output);
    sb_reset(&g_error);
    g_haserror = 0;
    g_step = 0;
    g_truncated = 0;
    g_in_hook = 0;
    g_nscripts = 0;

    if (!source)
        source = "";

    mc = MOCHA_NewContext(8192);
    if (!mc) {
        char *e = malloc(64);
        if (e) strcpy(e, "{\"ok\":false,\"error\":{\"message\":\"no context\"}}");
        return e;
    }

    glob = MOCHA_NewObject(mc, &global_class, 0, 0, 0, 0, 0);
    if (!glob) {
        MOCHA_DestroyContext(mc);
        return strdup("{\"ok\":false,\"error\":{\"message\":\"no global\"}}");
    }
    MOCHA_HoldObject(mc, glob);
    MOCHA_SetGlobalObject(mc, glob);
    MOCHA_SetErrorReporter(mc, js_error_reporter);
    MOCHA_DefineFunctions(mc, glob, web_functions);

    /* Phase 1: token stream (independent scan). */
    emit_tokens(&tokens, mc, source);

    /* Phase 2: compile. */
    result = MOCHA_void;
    script = MOCHA_CompileBuffer(mc, glob, source, strlen(source),
                                 "<playground>", 1);
    if (script) {
        script_id(script, "<main>");
        mocha_TraceHook = trace_hook;
        MOCHA_SetBranchCallback(mc, branch_cb);
        ok = MOCHA_ExecuteScript(mc, glob, script, &result);
        mocha_TraceHook = 0;
        MOCHA_SetBranchCallback(mc, 0);
    }

    /* Phase 3: assemble JSON. */
    sb_putc(&out, '{');
    sb_puts(&out, "\"ok\":");
    sb_puts(&out, (ok && !g_haserror) ? "true" : "false");

    sb_puts(&out, ",\"output\":");
    sb_jstr(&out, g_output.p ? g_output.p : "");

    sb_puts(&out, ",\"error\":");
    if (g_haserror && g_error.p)
        sb_puts(&out, g_error.p);
    else
        sb_puts(&out, "null");

    sb_puts(&out, ",\"result\":");
    if (ok)
        emit_datum(&out, result);
    else
        sb_puts(&out, "null");

    sb_puts(&out, ",\"tokens\":");
    sb_puts(&out, tokens.p ? tokens.p : "[]");

    sb_puts(&out, ",\"scripts\":[");
    for (i = 0; i < g_nscripts; i++) {
        if (i)
            sb_putc(&out, ',');
        sb_putf(&out, "{\"id\":%d,\"name\":", i);
        sb_jstr(&out, g_script_names[i] ? g_script_names[i] : "fn");
        sb_puts(&out, ",\"bytecode\":");
        emit_script_bytecode(&out, mc, g_scripts[i]);
        sb_putc(&out, '}');
    }
    sb_puts(&out, "]");

    sb_puts(&out, ",\"trace\":[");
    if (g_trace.p)
        sb_puts(&out, g_trace.p);
    sb_puts(&out, "]");

    sb_puts(&out, ",\"truncated\":");
    sb_puts(&out, g_truncated ? "true" : "false");
    sb_putf(&out, ",\"stats\":{\"steps\":%d,\"scripts\":%d}", g_step, g_nscripts);
    sb_putc(&out, '}');

    if (script)
        MOCHA_DestroyScript(mc, script);
    if (ok)
        mocha_DropRef(mc, &result);
    MOCHA_DropObject(mc, glob);
    MOCHA_DestroyContext(mc);

    sb_reset(&tokens);
    sb_reset(&g_trace);
    sb_reset(&g_output);
    sb_reset(&g_error);

    if (out.oom) {
        sb_reset(&out);
        return strdup("{\"ok\":false,\"error\":{\"message\":\"out of memory\"}}");
    }
    return out.p;   /* caller frees */
}

/* Allow standalone native testing: ./mo_web 'print(1+2)' */
#ifndef __EMSCRIPTEN__
int
main(int argc, char **argv)
{
    char *json;
    const char *src = (argc > 1) ? argv[1] : "print('hello mocha')";
    json = mocha_run_json(src);
    if (json) {
        fputs(json, stdout);
        fputc('\n', stdout);
        free(json);
    }
    return 0;
}
#endif
