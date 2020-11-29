/*
** Mocha bytecode descriptors, disassemblers, and decompilers.
**
** Brendan Eich, 6/24/95
*/
#include <ctype.h>
#include <memory.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prarena.h"
#include "prlog.h"
#include "prmem.h"
#include "prprf.h"
#include "alloca.h"
#include "mo_atom.h"
#include "mo_bcode.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mochaapi.h"

char mocha_new[]    = "new";
#ifdef MOCHA_HAS_DELETE_OPERATOR
char mocha_delete[] = "delete";
#endif
char mocha_typeof[] = "typeof";
char mocha_void[]   = "void";
char mocha_null[]   = "null";
char mocha_this[]   = "this";
char mocha_false[]  = "false";
char mocha_true[]   = "true";

/* Pollute the namespace locally for Win16 :-(  */
#ifndef FAR
#define FAR 
#endif

MochaCodeSpec FAR mocha_CodeSpec[] = {
#define MOPDEF(op,val,name,image,length,nuses,ndefs,pretty,format) \
    {name,image,length,nuses,ndefs,pretty,format},
#include "mocha.def"
#undef MOPDEF
};

unsigned mocha_NumCodeSpecs = sizeof mocha_CodeSpec
			    / sizeof mocha_CodeSpec[0];

/* ------------------------------------------------------------------------ */

#ifdef DEBUG

void
mocha_Disassemble(MochaContext *mc, MochaScript *script, FILE *fp)
{
    MochaCode *pc, *end;
    unsigned len;

    pc = script->code;
    end = pc + script->length;
    while (pc < end) {
	len = mocha_Disassemble1(mc, script, pc, pc - script->code, fp);
	pc += len;
    }
}

unsigned
mocha_Disassemble1(MochaContext *mc, MochaScript *script, MochaCode *pc,
		   unsigned loc, FILE *fp)
{
    MochaOp op;
    MochaCodeSpec *cs;
    int off;
    MochaAtom *atom;

    op = *pc;
    if (op >= MOP_MAX) {
	MOCHA_ReportError(mc, "bytecode %d too large", op);
	return 0;
    }
    cs = &mocha_CodeSpec[op];
    fprintf(fp, "%05u:  %s", loc, cs->name);
    switch (cs->format) {
      case MOF_BYTE:
	break;
      case MOF_JUMP:
	off = GET_JUMP_OFFSET(pc);
	fprintf(fp, " %u (%d)", loc + off, off);
	break;
      case MOF_INCOP:
	fprintf(fp, " (%s)", pc[1] ? "post" : "pre");
	break;
      case MOF_ARGC:
	fprintf(fp, " %u", pc[1]);
	break;
      case MOF_CONST:
	atom = GET_CONST_ATOM(mc, script, pc);
	fprintf(fp, (op == MOP_STRING) ? " \"%s\"" : " %s", atom_name(atom));
	break;
      default:
	MOCHA_ReportError(mc, "unknown bytecode format %d", cs->format);
	return 0;
    }
    fputs("\n", fp);
    return cs->length;
}

#endif /* DEBUG */

/* ------------------------------------------------------------------------ */

/*
** Compute a worst-case format-converted string length.
*/
size_t
GuessFormatConversionSize(const char *format, va_list ap)
{
    size_t nb, len;
    const char *s, *t;
    unsigned width;

    nb = strlen(format) + 1;
    for (t = format; (t = strchr(t, '%')) != 0; t++) {
	if (*++t == '%')
	    continue;
	for (width = 0; *t && !isalpha(*t); t++) {
	    if (*t == '*')
		width = va_arg(ap, int);
	}
	if (*t == 's') {
	    s = va_arg(ap, char *);
	    len = s ? strlen(s) : 6;
	    if (len < width) len = width;
	    nb += len;
	} else if (*t == 'e' || *t == 'f' || *t == 'g') {
	    (void) va_arg(ap, double);
	    nb += 32;
	} else if (*t == 'l') {
	    (void) va_arg(ap, long);
	    nb += 32;
	} else {
	    (void) va_arg(ap, int);
	    nb += 16;
	}
    }
    return nb;
}

/*
** Sprintf, but with unlimited and automatically allocated buffering.
*/
typedef struct Sprinter {
    MochaContext    *context;       /* recursion invariant argument */
    PRArenaPool     *pool;          /* string allocation pool */
    char            *base;          /* base address of buffer in pool */
    size_t          size;           /* size of buffer allocated at base */
    ptrdiff_t       offset;         /* offset of next free char in buffer */
} Sprinter;

#define INIT_SPRINTER(mc, sp, ap, off) \
    ((sp)->context = mc, (sp)->pool = ap, (sp)->base = 0, (sp)->size = 0, \
     (sp)->offset = off)

#define OFF2STR(sp,off) ((sp)->base + (off))
#define STR2OFF(sp,str) ((str) - (sp)->base)

static MochaBoolean
SprintAlloc(Sprinter *sp, size_t nb)
{
    if (!sp->base) {
	PR_ARENA_ALLOCATE(sp->base, sp->pool, nb);
    } else {
	PR_ARENA_GROW(sp->base, sp->pool, sp->size, nb);
    }
    if (!sp->base) {
	MOCHA_ReportOutOfMemory(sp->context);
	return MOCHA_FALSE;
    }
    sp->size += nb;
    return MOCHA_TRUE;
}

static ptrdiff_t
SprintPut(Sprinter *sp, const char *s, size_t len)
{
    ptrdiff_t nb, offset;
    char *bp;

    /* Allocate space for s, including the '\0' at the end. */
    nb = (sp->offset + len + 1) - sp->size;
    if (nb > 0 && !SprintAlloc(sp, nb))
	return -1;

    /* Advance offset and copy s into sp's buffer. */
    offset = sp->offset;
    sp->offset += len;
    bp = sp->base + offset;
    memcpy(bp, s, len);
    bp[len] = '\0';
    return offset;
}

static ptrdiff_t
Sprint(Sprinter *sp, const char *format, ...)
{
    va_list ap;
    int nb, cc;
    char *bp;

    va_start(ap, format);
    nb = GuessFormatConversionSize(format, ap);
    bp = alloca(nb);
    cc = PR_vsnprintf(bp, nb, format, ap);
    va_end(ap);
    if (cc < 0)
	return cc;
    return SprintPut(sp, bp, cc);
}

static char escapeMap[] = "\bb\ff\nn\rr\tt\vv\"\"";

static char *
EscapeString(Sprinter *sp, const char *s)
{
    ptrdiff_t offset;
    const char *t, *u;
    char c;
    MochaBoolean ok;

    offset = sp->offset;
    t = s;
    do {
	while (isprint(*t) && *t != '"')
	    t++;
	if (SprintPut(sp, s, t - s) < 0)
	    return 0;
	if ((c = *t) == '\0')
	    break;
	if ((u = strchr(escapeMap, c)) != 0)
	    ok = Sprint(sp, "\\%c", u[1]) >= 0;
	else
	    ok = Sprint(sp, "\\%03o", c) >= 0;
	if (!ok)
	    return 0;
    } while (*(s = ++t) != '\0');
    return OFF2STR(sp, offset);
}

/* ------------------------------------------------------------------------ */

struct MochaPrinter {
    Sprinter        sprinter;       /* base class state */
    PRArenaPool     pool;           /* string allocation pool */
    unsigned        indent;         /* indentation in spaces */
    MochaScript     *script;        /* script being printed */
};

MochaPrinter *
mocha_NewPrinter(MochaContext *mc, const char *name, unsigned indent)
{
    MochaPrinter *mp;

    mp = PR_NEW(MochaPrinter);
    if (!mp) {
	MOCHA_ReportOutOfMemory(mc);
	return 0;
    }
    INIT_SPRINTER(mc, &mp->sprinter, &mp->pool, 0);
    PR_InitArenaPool(&mp->pool, name, 256, 1);
    mp->indent = indent;
    mp->script = 0;
    return mp;
}

MochaBoolean
mocha_GetPrinterOutput(MochaPrinter *mp, char **sp)
{
    char *str;

    if (!mp->sprinter.base)
	return MOCHA_TRUE;
    str = MOCHA_strdup(mp->sprinter.context, mp->sprinter.base);
    if (!str)
	return MOCHA_FALSE;
    *sp = str;
    return MOCHA_TRUE;
}

void
mocha_DestroyPrinter(MochaPrinter *mp)
{
    PR_FinishArenaPool(&mp->pool);
    PR_DELETE(mp);
}

int
mocha_printf(MochaPrinter *mp, char *format, ...)
{
    va_list ap;
    int nb, cc;
    char *bp;

    va_start(ap, format);

    /* Expand magic tab into a run of mp->indent spaces. */
    if (*format == '\t') {
	if (Sprint(&mp->sprinter, "%*s", mp->indent, "") < 0)
	    return -1;
	format++;
    }

    /* Allocate temp space, convert format, and put. */
    nb = GuessFormatConversionSize(format, ap);
    bp = (char *)alloca(nb);
    cc = PR_vsnprintf(bp, nb, format, ap);
    if (cc > 0 && SprintPut(&mp->sprinter, bp, cc) < 0)
	return -1;

    va_end(ap);
    return cc;
}

MochaBoolean
mocha_puts(MochaPrinter *mp, char *s)
{
    return SprintPut(&mp->sprinter, s, strlen(s)) >= 0;
}

/* ------------------------------------------------------------------------ */

typedef struct SprintStack {
    Sprinter    sprinter;       /* base class state */
    ptrdiff_t   *offsets;       /* stack of postfix string offsets */
    MochaCode   *opcodes;       /* parallel stack of Mocha opcodes */
    unsigned    top;            /* top of stack index */
} SprintStack;

/* Gap between stacked strings to allow for insertion of parens and commas. */
#define PARENSLOP       (2 + 1)

static MochaBoolean
PushOff(SprintStack *ss, ptrdiff_t off, MochaOp op)
{
    if (!SprintAlloc(&ss->sprinter, PARENSLOP))
	return MOCHA_FALSE;
    ss->offsets[ss->top] = off;
    ss->opcodes[ss->top++] = op;
    ss->sprinter.offset += PARENSLOP;
    return MOCHA_TRUE;
}

static ptrdiff_t
PopOff(SprintStack *ss, MochaOp op)
{
    ptrdiff_t off;

    if (ss->opcodes[--ss->top] < (MochaCode)op) {
	ss->offsets[ss->top] -= 2;
	ss->sprinter.offset = ss->offsets[ss->top];
	off = Sprint(&ss->sprinter, "(%s)",
		     OFF2STR(&ss->sprinter, ss->sprinter.offset + 2));
    } else {
	off = ss->sprinter.offset = ss->offsets[ss->top];
    }
    return off;
}

static MochaBoolean
Decompile(MochaCode *pc, int nb, SprintStack *ss, MochaPrinter *mp)
{
    unsigned len;
    MochaCode *end, *ifeq, *ifne, *done;
    ptrdiff_t todo, cond, next, tail;
    MochaOp op, lastop;
    MochaCodeSpec *cs;
    SourceNote *sn;
    char *lval, *rval, **argv;
    int i, argc;
    MochaAtom *atom;
    MochaBoolean ok;

/*
** Local macros
*/
#define DECOMPILE_CODE(pc,nb)	if (!Decompile(pc,nb,ss,mp)) return MOCHA_FALSE
#define POP_STR()		OFF2STR(&ss->sprinter, PopOff(ss, op))
#define LOCAL_ASSERT(expr)	PR_ASSERT(expr); if (!(expr)) return MOCHA_FALSE

    end = pc + nb;
    todo = -1;
    op = MOP_NOP;
    while (pc < end) {
	lastop = op;
	op = *pc;
	cs = &mocha_CodeSpec[op];
	len = cs->length;

	if (cs->pretty) {
	    switch (cs->nuses) {
	      case 2:
		rval = POP_STR();
		lval = POP_STR();
		if (cs->pretty == 2 &&
		    (sn = mocha_GetSourceNote(mp->script, pc)) &&
		    SN_TYPE(sn) == SRC_ASSIGNOP) {
		    /* Print only the right operand of the assignment-op. */
		    todo = Sprint(&ss->sprinter, "%s", rval);
		} else {
		    todo = Sprint(&ss->sprinter, "%s %s %s",
				  lval, cs->image, rval);
		}
		break;

	      case 1:
		rval = POP_STR();
		todo = Sprint(&ss->sprinter, "%s%s", cs->image, rval);
		break;

	      case 0:
		todo = Sprint(&ss->sprinter, "%s", cs->image);
		break;

	      default:
		todo = -1;
	    }
	} else {
	    switch (op) {
	      case MOP_NOP:
		/*
		** Check for extra user parenthesization, or a for-loop with
		** an empty initializer part.
		*/
		sn = mocha_GetSourceNote(mp->script, pc);
		todo = -1;
		switch (sn ? SN_TYPE(sn) : SRC_NULL) {
		  case SRC_PAREN:
		    /* Use last real op so PopOff adds parens if needed. */
		    op = lastop;
		    todo = PopOff(ss, op);

		    /* Now add user-supplied parens only if PopOff did not. */
		    if (ss->opcodes[ss->top] >= (MochaCode)op) {
			todo = Sprint(&ss->sprinter, "(%s)",
				      OFF2STR(&ss->sprinter, todo));
		    }

		    /* Set op so the next Pop won't add extra parens. */
		    op = MOP_MAX;
		    break;

		  case SRC_FOR:
		    rval = "";

		forloop:
		    /* Skip the MOP_NOP or MOP_POP bytecode. */
		    pc++;

		    /* Get the cond, next, and loop-closing tail offsets. */
		    cond = SN_OFFSET(&sn[1]);
		    next = SN_OFFSET(&sn[2]);
		    tail = SN_OFFSET(&sn[3]);
		    LOCAL_ASSERT(tail + GET_JUMP_OFFSET(pc + tail) == 0);

		    /* Print the keyword and the possibly empty init-part. */
		    mocha_printf(mp, "\tfor (%s;", rval);

		    if (pc[cond] == MOP_IFEQ) {
			/* Decompile the loop condition. */
			DECOMPILE_CODE(pc, cond);
			mocha_printf(mp, " %s", POP_STR());
		    }

		    /* Need a semicolon whether or not there was a cond. */
		    mocha_printf(mp, ";");

		    if (pc[next] != MOP_GOTO) {
			/* Decompile the loop updater. */
			DECOMPILE_CODE(pc + next, tail - next - 1);
			mocha_printf(mp, " %s", POP_STR());
		    }

		    /* Do the loop body. */
		    mocha_printf(mp, ") {\n");
		    mp->indent += 4;
		    DECOMPILE_CODE(pc + cond + 3, next - cond - 3);
		    mp->indent -= 4;
		    mocha_printf(mp, "\t}\n");

		    /* Set len so pc skips over the entire loop. */
		    len = tail + 3;
		    break;
		}
		break;

	      case MOP_PUSH:
		todo = Sprint(&ss->sprinter, "");
		break;

	      case MOP_POP:
		sn = mocha_GetSourceNote(mp->script, pc);
		switch (sn ? SN_TYPE(sn) : SRC_NULL) {
		  case SRC_FOR:
		    rval = POP_STR();
		    goto forloop;
		  case SRC_COMMA:
		    pc += len;
		    for (done = pc; pc < end && *pc != MOP_POP; pc += len)
			len = mocha_CodeSpec[*pc].length;
		    DECOMPILE_CODE(done, pc - done);
		    rval = POP_STR();
		    lval = POP_STR();
		    todo = Sprint(&ss->sprinter, "%s, %s", lval, rval);
		    len = 0;
		    break;
		  case SRC_HIDDEN:
		    /* hide this pop, it's for return nested in with/for-in */
		    break;
		  default:
		    op = MOP_NOP;	/* XXX should reorder bytecodes? */
		    rval = POP_STR();
		    if (*rval != '\0')
			mocha_printf(mp, "\t%s;\n", rval);
		    todo = -1;
		    break;
		}
		break;

	      case MOP_ENTER:
		rval = POP_STR();
		mocha_printf(mp, "\twith %s {\n", rval);
		mp->indent += 4;
		todo = -1;
		break;

	      case MOP_LEAVE:
		sn = mocha_GetSourceNote(mp->script, pc);
		if (sn && SN_TYPE(sn) == SRC_HIDDEN)
		    break;
		mp->indent -= 4;
		mocha_printf(mp, "\t}\n");
		todo = -1;
		break;

	      case MOP_RETURN:
		op = MOP_NOP;	/* XXX should reorder bytecodes? */
		rval = POP_STR();
		mocha_printf(mp, "\t%s%s%s;\n",
			     cs->name, *rval != '\0' ? " " : "", rval);
		todo = -1;
		break;

	      case MOP_GOTO:
		sn = mocha_GetSourceNote(mp->script, pc);
		if (sn && SN_TYPE(sn) == SRC_CONTINUE)
		    mocha_printf(mp, "\tcontinue;\n");
		else
		    mocha_printf(mp, "\tbreak;\n");
		todo = -1;
		break;

	      case MOP_IFEQ:
		len = GET_JUMP_OFFSET(pc);
		sn = mocha_GetSourceNote(mp->script, pc);

		switch (sn ? SN_TYPE(sn) : SRC_NULL) {
		  case SRC_IF:
		  case SRC_IF_ELSE:
		    rval = POP_STR();
		    mocha_printf(mp, "\tif (%s) {\n", rval);
		    mp->indent += 4;
		    if (SN_TYPE(sn) == SRC_IF) {
			DECOMPILE_CODE(pc + 3, len - 3);
		    } else {
			DECOMPILE_CODE(pc + 3, len - 6);
			mp->indent -= 4;
			pc += len - 3;
			len = GET_JUMP_OFFSET(pc);
			mocha_printf(mp, "\t} else {\n");
			mp->indent += 4;
			DECOMPILE_CODE(pc + 3, len - 3);
		    }
		    mp->indent -= 4;
		    mocha_printf(mp, "\t}\n");
		    todo = -1;
		    break;

		  case SRC_WHILE:
		    rval = POP_STR();
		    mocha_printf(mp, "\twhile (%s) {\n", rval);
		    mp->indent += 4;
		    DECOMPILE_CODE(pc + 3, len - 6);
		    mp->indent -= 4;
		    mocha_printf(mp, "\t}\n");
		    todo = -1;
		    break;

		  case SRC_COND:
		    DECOMPILE_CODE(pc + 3, len - 6);
		    pc += len - 3;
		    LOCAL_ASSERT(*pc == MOP_GOTO);
		    len = GET_JUMP_OFFSET(pc);
		    DECOMPILE_CODE(pc + 3, len - 3);
		    rval = POP_STR();
		    lval = POP_STR();
		    todo = Sprint(&ss->sprinter, "%s ? %s : %s",
				  POP_STR(), lval, rval);
		    break;

		  default:
		    /* top is the first clause in a disjunction (||). */
		    ifeq = pc + len;
		    LOCAL_ASSERT(pc[3] == MOP_TRUE);
		    pc += 4;
		    LOCAL_ASSERT(*pc == MOP_GOTO);
		    done = pc + GET_JUMP_OFFSET(pc);
		    pc += 3;
		    DECOMPILE_CODE(pc, done - ifeq);
		    rval = POP_STR();
		    lval = POP_STR();
		    todo = Sprint(&ss->sprinter, "%s || %s", lval, rval);
		    len = done - pc;
		    break;
		}
		break;

	      case MOP_IFNE:
		/* This bytecode is used only for conjunction (&&). */
		ifne = pc + GET_JUMP_OFFSET(pc);
		len++;
		pc += len;
		LOCAL_ASSERT(pc[-1] == MOP_FALSE);
		LOCAL_ASSERT(*pc == MOP_GOTO);
		done = pc + GET_JUMP_OFFSET(pc);
		pc += 3;
		DECOMPILE_CODE(pc, done - ifne);
		rval = POP_STR();
		lval = POP_STR();
		todo = Sprint(&ss->sprinter, "%s && %s", lval, rval);
		len = done - pc;
		break;

	      case MOP_IN:
		rval = POP_STR();
		lval = POP_STR();
		pc++;
		LOCAL_ASSERT(*pc == MOP_IFEQ);
		len = GET_JUMP_OFFSET(pc);
		mocha_printf(mp, "\tfor (%s in %s) {\n", lval, rval);
		mp->indent += 4;
		DECOMPILE_CODE(pc + 3, len - 6);
		mp->indent -= 4;
		mocha_printf(mp, "\t}\n");
		todo = -1;
		break;

	      case MOP_DUP:
		todo = Sprint(&ss->sprinter, "%s",
			      OFF2STR(&ss->sprinter, ss->offsets[ss->top-1]));
		break;

	      case MOP_ASSIGN:
		rval = POP_STR();
		lval = POP_STR();
		if ((sn = mocha_GetSourceNote(mp->script, pc - 1)) &&
		    SN_TYPE(sn) == SRC_ASSIGNOP &&
		    (cs = &mocha_CodeSpec[pc[-1]])->pretty == 2) {
		    todo = Sprint(&ss->sprinter, "%s %s= %s",
				  lval, cs->image, rval);
		} else {
		    todo = Sprint(&ss->sprinter, "%s = %s", lval, rval);
		}
		break;

	      case MOP_TYPEOF:
	      case MOP_VOID:
		rval = POP_STR();
		todo = Sprint(&ss->sprinter, "%s %s", cs->image, rval);
		break;

	      case MOP_INC:
	      case MOP_DEC:
		lval = POP_STR();
		if (pc[1]) {
		    todo = Sprint(&ss->sprinter, "%s%s", lval, cs->image);
		} else {
		    todo = Sprint(&ss->sprinter, "%s%s", cs->image, lval);
		}
		break;

	      case MOP_MEMBER:
	      case MOP_LMEMBER:
		rval = POP_STR();
		lval = POP_STR();
		todo = Sprint(&ss->sprinter, "%s.%s", lval, rval);
		break;

	      case MOP_INDEX:
	      case MOP_LINDEX:
		op = MOP_NOP;           /* turn off parens */
		rval = POP_STR();
		op = MOP_INDEX;
		lval = POP_STR();
		todo = Sprint(&ss->sprinter, "%s[%s]", lval, rval);
		break;

	      case MOP_NEW:
	      case MOP_CALL:
		op = MOP_NOP;           /* turn off parens */
		argc = pc[1];
		argv = MOCHA_malloc(mp->sprinter.context,
				    (argc + 1) * sizeof *argv);
		if (!argv)
		    return MOCHA_FALSE;

		ok = MOCHA_TRUE;
		for (i = argc; i >= 0; i--) {
		    argv[i] = MOCHA_strdup(mp->sprinter.context, POP_STR());
		    if (!argv[i]) {
			ok = MOCHA_FALSE;
			break;
		    }
		}

		if (cs->image) {
		    todo = Sprint(&ss->sprinter, "%s %s(", cs->image, argv[0]);
		    /* balance) */
		} else {
		    todo = Sprint(&ss->sprinter, "%s(", argv[0]);
		}
		if (todo < 0)
		    ok = MOCHA_FALSE;

		for (i = 1; i <= argc; i++) {
		    if (!argv[i] ||
			Sprint(&ss->sprinter, "%s%s",
			       argv[i], (i < argc) ? ", " : "") < 0) {
			ok = MOCHA_FALSE;
			break;
		    }
		}
		if (Sprint(&ss->sprinter, ")") < 0)
		    ok = MOCHA_FALSE;

		for (i = 0; i <= argc; i++) {
		    if (argv[i])
			MOCHA_free(mp->sprinter.context, argv[i]);
		}
		MOCHA_free(mp->sprinter.context, argv);
		if (!ok)
		    return MOCHA_FALSE;
		op = MOP_CALL;
		break;

	      case MOP_NAME:
		atom = GET_CONST_ATOM(mp->sprinter.context, mp->script, pc);
		sn = mocha_GetSourceNote(mp->script, pc);
		todo = Sprint(&ss->sprinter,
			      (sn && SN_TYPE(sn) == SRC_VAR) ? "var %s" : "%s",
			      atom_name(atom));
		break;

	      case MOP_NUMBER:
		atom = GET_CONST_ATOM(mp->sprinter.context, mp->script, pc);
		todo = Sprint(&ss->sprinter, atom_name(atom));
		break;

	      case MOP_STRING:
		atom = GET_CONST_ATOM(mp->sprinter.context, mp->script, pc);
		rval = EscapeString(&ss->sprinter, atom_name(atom));
		if (!rval)
		    return MOCHA_FALSE;
		todo = Sprint(&ss->sprinter, "\"%s\"", rval);
		break;

	      default:
		todo = -1;
	    }
	}

	if (todo >= 0 && !PushOff(ss, todo, op)) {
	    return MOCHA_FALSE;
	}
	pc += len;
    }

/*
** Undefine local macros.
*/
#undef DECOMPILE_CODE
#undef POP_STR
#undef LOCAL_ASSERT

    return MOCHA_TRUE;
}

MochaBoolean
mocha_DecompileScript(MochaScript *script, MochaPrinter *mp)
{
    MochaContext *mc;
    void *mark;
    SprintStack ss;
    MochaBoolean ok;

    /* Initialize a sprinter for use with the offset stack. */
    mc = mp->sprinter.context;
    mark = PR_ARENA_MARK(&mc->tempPool);
    INIT_SPRINTER(mc, &ss.sprinter, &mc->tempPool, PARENSLOP);

    /* Initialize the offset and opcode stacks. */
    ss.offsets = (ptrdiff_t *)alloca(script->depth * sizeof *ss.offsets);
    ss.opcodes = (MochaCode *)alloca(script->depth * sizeof *ss.opcodes);
    ss.top = 0;

    /* Set mp->script for source note referencing. */
    mp->script = script;

    /* Call recursive subroutine to do the hard work. */
    ok = Decompile(script->code, script->length, &ss, mp);
    PR_ARENA_RELEASE(&mc->tempPool, mark);
    return ok;
}

MochaBoolean
mocha_DecompileFunction(MochaFunction *fun, MochaPrinter *mp)
{
    MochaSymbol *arg;
    const MochaAtom *atom;
    unsigned indent;

    mocha_printf(mp, "\nfunction %s(", atom_name(fun->atom));
    if (fun->script) {
	for (arg = fun->script->args; arg; arg = arg->next) {
	    atom = sym_atom(arg);
	    mocha_printf(mp, "%s%s", atom_name(atom), arg->next ? ", " : "");
	}
    }
    mocha_printf(mp, ") {\n");
    if (fun->call) {
	mocha_printf(mp, "    [native code]\n");
    } else {
	indent = mp->indent;
	mp->indent += 4;
	if (!mocha_DecompileScript(fun->script, mp)) {
	    mp->indent = indent;
	    return MOCHA_FALSE;
	}
	mp->indent -= 4;
    }
    mocha_printf(mp, "}\n");
    return MOCHA_TRUE;
}
