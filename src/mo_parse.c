/*
** Mocha parser.
**
** This is a recursive descent parser for the extended BNF grammar in
** "The Mocha Language Report" (XXX need wnj JavaScript spec cite here).
** It generates Mocha bytecode and builds symbol table entries for functions
** as it parses.
**
** The parser attempts no error recovery yet -- you have to fix each error
** as you find it.
**
** Brendan Eich, 6/14/95
*/
#include <stdlib.h>
#include <string.h>
#include "prarena.h"
#include "prlog.h"
#include "mo_atom.h"
#include "mo_bcode.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mo_parse.h"
#include "mo_scan.h"
#include "mo_scope.h"
#include "mocha.h"
#include "mochalib.h"	/* XXX only for mocha_GetMutableScope() */

/*
** Mocha parsers, from lowest to highest precedence.
**
** Each parser takes a context, a static link, a token stream, and generates
** bytecode using the code generator argument.
*/
typedef MochaBoolean
MochaParser(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg);

static MochaParser FunctionDefinition;
static MochaParser Statements;
static MochaParser Statement;
static MochaParser Variables;
static MochaParser StmtExpr;
static MochaParser Expr;
static MochaParser AssignExpr;
static MochaParser CondExpr;
static MochaParser OrExpr;
static MochaParser AndExpr;
static MochaParser BitOrExpr;
static MochaParser BitXorExpr;
static MochaParser BitAndExpr;
static MochaParser EqExpr;
static MochaParser RelExpr;
static MochaParser ShiftExpr;
static MochaParser AddExpr;
static MochaParser MulExpr;
static MochaParser UnaryExpr;
static MochaParser MemberExpr;
static MochaParser PrimaryExpr;
static MochaParser NameExpr;

/* NB: this macro uses mc, ts, and cg from its lexical environment. */
#define MUST_MATCH_TOKEN(tt, err) {                                           \
    if (mocha_GetToken(mc, ts, cg) != tt) {                                   \
	mocha_ReportSyntaxError(mc, ts, err);                                 \
	return MOCHA_FALSE;                                                   \
    }                                                                         \
}

/*
** Parse a top-level Mocha script.
*/
MochaBoolean
mocha_Parse(MochaContext *mc, MochaObject *slink, MochaTokenStream *ts,
	    CodeGenerator *cg)
{
    MochaObject *oldslink;
    MochaTokenType stop, tt;
    MochaBoolean ok;

    oldslink = mc->staticLink;
    mc->staticLink = slink;

    if (ts->flags & TSF_INTERACTIVE) {
	SCAN_NEWLINES(ts);
	stop = TOK_EOL;
    } else {
	stop = TOK_EOF;
    }

    ok = MOCHA_TRUE;
    while (ok && (tt = mocha_GetToken(mc, ts, cg)) != stop && tt != TOK_EOF) {
	switch (tt) {
	  case TOK_FUNCTION:
	    if (!FunctionDefinition(mc, ts, cg))
		ok = MOCHA_FALSE;
	    break;
	  default:
	    mocha_UngetToken(ts);
	    if (!Statement(mc, ts, cg))
		ok = MOCHA_FALSE;
	}
    }

    mc->staticLink = oldslink;
    if (!ok) {
	CLEAR_PUSHBACK(ts);
	mocha_DropUnmappedAtoms(mc, cg);
    }
    return ok;
}

/*
** Parse a Mocha function body, which might appear as the value of an event
** handler attribute in a HTML <INPUT> tag.
*/
MochaBoolean
mocha_ParseFunctionBody(MochaContext *mc, MochaTokenStream *ts,
			MochaFunction *fun)
{
    unsigned lineno;
    CodeGenerator funcg;
    MochaObject *oldslink;
    MochaBoolean ok;

    if (ts->flags & TSF_FUNCTION) {
	mocha_ReportSyntaxError(mc, ts, "function defined inside a function");
	return MOCHA_FALSE;
    }

    lineno = ts->lineno - 1;
    if (!mocha_InitCodeGenerator(mc, &funcg, &mc->codePool))
	return MOCHA_FALSE;

    oldslink = mc->staticLink;
    mc->staticLink = &fun->object;
    ts->flags |= TSF_FUNCTION;
    ok = Statements(mc, ts, &funcg);
    ts->flags &= ~TSF_FUNCTION;
    mc->staticLink = oldslink;

    /* Check for falling off the end of a function that returns a value. */
    if (ok &&
	(ts->flags & TSF_RETURN_EXPR) &&
	funcg.lastOpcode != MOP_RETURN &&
	funcg.lastOpcode != MOP_LEAVE) {
	mocha_ReportSyntaxError(mc, ts,
				"function does not always return a value");
	ok = MOCHA_FALSE;
    }
    ts->flags &= ~(TSF_RETURN_EXPR | TSF_RETURN_VOID);

    if (!ok) {
	CLEAR_PUSHBACK(ts);
	mocha_DropUnmappedAtoms(mc, &funcg);
    } else {
	fun->script = mocha_NewScript(mc, &funcg, ts->filename, lineno);
	if (!fun->script)
	    ok = MOCHA_FALSE;
    }
    return ok;
}

static MochaBoolean
FunctionDefinition(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaAtom *atom;
    char *cp;
    unsigned nargs;
    MochaAtomMap map;
    MochaFunction *fun;
    MochaBoolean ok;
    MochaSymbol *arg, *args, **argp;
    void *mark;
    unsigned i;
    int snindex;

    /* Save atoms indexed but not mapped for the top-level script. */
    if (!mocha_InitAtomMap(mc, &map, cg))
	return MOCHA_FALSE;

    MUST_MATCH_TOKEN(TOK_NAME, "missing function name");
    atom = ts->token.u.atom;

    /* Estimate number of arguments for initial function scope size. */
    nargs = 1;
    for (cp = ts->token.ptr; *cp != '\0'; cp++) {
	if (*cp == ',')
	    nargs++;
    }

    fun = mocha_DefineFunction(mc, mc->staticLink, atom, 0, nargs, 0);
    if (!fun || !mocha_GetMutableScope(mc, &fun->object)) {
	ok = MOCHA_FALSE;
	goto out;
    }
    PR_ASSERT(fun->object.parent == mc->staticLink);

    /* Now parse formal argument list and compute the final fun->nargs. */
    nargs = 0;
    args = 0;
    MUST_MATCH_TOKEN(TOK_LP, "missing ( before formal parameters");
    /* balance) */
    if (!mocha_MatchToken(mc, ts, cg, TOK_RP)) {
	argp = &args;
	do {
	    MUST_MATCH_TOKEN(TOK_NAME, "missing formal parameter");
	    arg = mocha_DefineSymbol(mc, fun->object.scope, ts->token.u.atom,
				     SYM_ARGUMENT, 0);
	    if (!arg) {
		ok = MOCHA_FALSE;
		goto out;
	    }
	    arg->slot = nargs++;
	    *argp = arg;
	    argp = &arg->next;
	} while (mocha_MatchToken(mc, ts, cg, TOK_COMMA));

	/* (balance: */
	MUST_MATCH_TOKEN(TOK_RP, "missing ) after formal parameters");
    }
    fun->nargs = nargs;

    MUST_MATCH_TOKEN(TOK_LC, "missing { before function body");
    mark = PR_ARENA_MARK(&mc->codePool);
    ok = mocha_ParseFunctionBody(mc, ts, fun);
    if (ok) {
	MUST_MATCH_TOKEN(TOK_RC, "missing } after function body");
	fun->script->depth += fun->object.scope->freeslot;
	fun->script->args = args;

	/* Generate a setline note for script that follows this function. */
	snindex = mocha_NewSourceNote(mc, cg, SRC_SETLINE);
	if (snindex >= 0)
	    SN_SET_OFFSET(&cg->notes[snindex + 1], ts->lineno - 1);
	else
	    ok = MOCHA_FALSE;
    }
    PR_ARENA_RELEASE(&mc->codePool, mark);

out:
    if (!ok)
	mocha_RemoveSymbol(mc, mc->staticLink->scope, atom);
    for (i = 0; i < map.length; i++) {
	mocha_IndexAtom(mc, map.vector[i], cg);
	PR_ASSERT(map.vector[i]->index == i);
    }
    mocha_FreeAtomMap(mc, &map);
    return ok;
}

static MochaBoolean
Statements(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    int newlines;
    MochaBoolean ok;
    MochaTokenType tt;

    newlines = ts->flags & TSF_NEWLINES;
    if (newlines) HIDE_NEWLINES(ts);

    ok = MOCHA_TRUE;
    while ((tt = mocha_PeekToken(mc, ts, cg)) != TOK_EOF && tt != TOK_RC) {
	if (!Statement(mc, ts, cg)) {
	    ok = MOCHA_FALSE;
	    break;
	}
    }
    if (newlines) SCAN_NEWLINES(ts);
    return ok;
}

static MochaBoolean
Condition(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MUST_MATCH_TOKEN(TOK_LP, "missing ( before condition");
    if (!Expr(mc, ts, cg))
	return MOCHA_FALSE;
    MUST_MATCH_TOKEN(TOK_RP, "missing ) after condition");

    /* Check for an AssignExpr (see below) and "correct" it to an EqExpr */
    if (cg->lastOpcode == MOP_ASSIGN &&
	(cg->noteCount == 0 ||
	 SN_TYPE(&cg->notes[cg->noteCount-1]) != SRC_ASSIGNOP ||
	 cg->lastOffset < CG_OFFSET(cg) - 2)) {
	mocha_ReportSyntaxError(mc, ts,
	    "test for equality (==) mistyped as assignment (=)?\n"
	    "Assuming equality test");
	cg->ptr[-1] = MOP_EQ;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
Statement(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    int snindex, upindex, newlines;
    SourceNote *topsn;
    ptrdiff_t top, beq, jmp;
    LoopInfo loopInfo;
    unsigned topNoteCount;
    CodeGenerator updater;
    MochaTokenType tt;
    MochaBoolean forIn, ok;

    switch (mocha_GetToken(mc, ts, cg)) {
      case TOK_IF:
	if (!Condition(mc, ts, cg))
	    return MOCHA_FALSE;
	snindex = mocha_NewSourceNote(mc, cg, SRC_IF);
	if (snindex < 0)
	    return MOCHA_FALSE;
	beq = mocha_Emit3(mc, cg, MOP_IFEQ, 0, 0);
	if (beq < 0)
	    return MOCHA_FALSE;
	if (!Statement(mc, ts, cg))
	    return MOCHA_FALSE;
	if (mocha_MatchToken(mc, ts, cg, TOK_ELSE)) {
	    SN_SET_TYPE(&cg->notes[snindex], SRC_IF_ELSE);
	    jmp = mocha_Emit3(mc, cg, MOP_GOTO, 0, 0);
	    if (jmp < 0)
		return MOCHA_FALSE;
	    SET_JUMP_OFFSET(CG_CODE(cg, beq), CG_OFFSET(cg) - beq);
	    if (!Statement(mc, ts, cg))
		return MOCHA_FALSE;
	    SET_JUMP_OFFSET(CG_CODE(cg, jmp), CG_OFFSET(cg) - jmp);
	} else {
	    SET_JUMP_OFFSET(CG_CODE(cg, beq), CG_OFFSET(cg) - beq);
	}
	break;

      case TOK_WHILE:
	top = CG_OFFSET(cg);
	mocha_PushLoopInfo(cg, &loopInfo, top);
	if (!Condition(mc, ts, cg))
	    return MOCHA_FALSE;
	snindex = mocha_NewSourceNote(mc, cg, SRC_WHILE);
	if (snindex < 0)
	    return MOCHA_FALSE;
	beq = mocha_Emit3(mc, cg, MOP_IFEQ, 0, 0);
	if (beq < 0)
	    return MOCHA_FALSE;
	if (!Statement(mc, ts, cg))
	    return MOCHA_FALSE;
	jmp = mocha_Emit3(mc, cg, MOP_GOTO, 0, 0);
	if (jmp < 0)
	    return MOCHA_FALSE;
	SET_JUMP_OFFSET(CG_CODE(cg, jmp), top - jmp);
	SET_JUMP_OFFSET(CG_CODE(cg, beq), CG_OFFSET(cg) - beq);
	mocha_PopLoopInfo(cg);
	break;

      case TOK_FOR:
	top = CG_OFFSET(cg);
	mocha_PushLoopInfo(cg, &loopInfo, top);
	snindex = -1;
	topNoteCount = cg->noteCount;
	updater.base = 0;

	MUST_MATCH_TOKEN(TOK_LP, "missing ( after for");	/* balance) */
	tt = mocha_PeekToken(mc, ts, cg);
	if (tt == TOK_SEMI) {
	    /* No initializer -- emit an annotated nop for the decompiler. */
	    snindex = mocha_NewSourceNote(mc, cg, SRC_FOR);
	    if (snindex < 0 || mocha_Emit1(mc, cg, MOP_NOP) < 0)
		return MOCHA_FALSE;
	} else {
	    if (tt == TOK_VAR) {
		(void) mocha_GetToken(mc, ts, cg);
		if (!Variables(mc, ts, cg))
		    return MOCHA_FALSE;
	    } else {
		if (!Expr(mc, ts, cg))
		    return MOCHA_FALSE;
	    }
	    if (mocha_PeekToken(mc, ts, cg) != TOK_IN) {
		snindex = mocha_NewSourceNote(mc, cg, SRC_FOR);
		if (snindex < 0 || mocha_Emit1(mc, cg, MOP_POP) < 0)
		    return MOCHA_FALSE;
	    }
	}

	forIn = mocha_MatchToken(mc, ts, cg, TOK_IN);
	if (forIn) {
	    /* Insert an MOP_PUSH to allocate an iterator before this loop. */
	    if (!mocha_SetStatementDepthType(mc, cg, MOCHA_FALSE))
		return MOCHA_FALSE;
	    cg->withDepth++;
	    mocha_MoveCode(mc, cg, top, cg, top + 1);
	    *CG_CODE(cg, top) = MOP_PUSH;
	    mocha_UpdateDepth(mc, cg, top);
	    top++;
	    SET_LOOPINFO_TOP(&loopInfo, top);

	    /* If the iterator had source notes, bump the first one's delta. */
	    if (cg->noteCount > topNoteCount) {
		topsn = &cg->notes[topNoteCount];
		SN_SET_DELTA(topsn, SN_DELTA(topsn) + 1);
		cg->lastOffset++;
	    }

	    /* Now compile the object expression over which we're iterating. */
	    if (!Expr(mc, ts, cg))
		return MOCHA_FALSE;
	    if (mocha_Emit1(mc, cg, MOP_IN) < 0)
		return MOCHA_FALSE;
	    beq = mocha_Emit3(mc, cg, MOP_IFEQ, 0, 0);
	    if (beq < 0)
		return MOCHA_FALSE;
	} else {
	    MUST_MATCH_TOKEN(TOK_SEMI, "missing ; after for-loop initializer");
	    top = CG_OFFSET(cg);
	    SET_LOOPINFO_TOP(&loopInfo, top);
	    if (mocha_PeekToken(mc, ts, cg) == TOK_SEMI) {
		/* No loop condition -- flag this fact in the source note. */
		if (snindex >= 0)
		    SN_SET_OFFSET(&cg->notes[snindex + 1], 0);
		beq = 0;
	    } else {
		if (!Expr(mc, ts, cg))
		    return MOCHA_FALSE;
		if (snindex >= 0)
		    SN_SET_OFFSET(&cg->notes[snindex + 1], CG_OFFSET(cg) - top);
		beq = mocha_Emit3(mc, cg, MOP_IFEQ, 0, 0);
		if (beq < 0)
		    return MOCHA_FALSE;
	    }

	    MUST_MATCH_TOKEN(TOK_SEMI, "missing ; after for-loop condition");
	    if (mocha_PeekToken(mc, ts, cg) != TOK_RP) {
		if (!mocha_InitCodeGenerator(mc, &updater, &mc->tempPool))
		    return MOCHA_FALSE;
		upindex = mocha_NewSourceNote(mc, &updater, SRC_SETLINE);
		if (upindex < 0)
		    return MOCHA_FALSE;
		SN_SET_OFFSET(&updater.notes[upindex + 1], ts->lineno);
/* XXX egad */
updater.atomList = cg->atomList;
updater.atomCount = cg->atomCount;
		if (!Expr(mc, ts, &updater))
		    return MOCHA_FALSE;
/* XXX egad */
cg->atomList = updater.atomList;
cg->atomCount = updater.atomCount;
		if (mocha_Emit1(mc, &updater, MOP_POP) < 0)
		    return MOCHA_FALSE;
	    }
	}

	/* (balance: */
	MUST_MATCH_TOKEN(TOK_RP, "missing ) after for-loop control");
	if (!Statement(mc, ts, cg))
	    return MOCHA_FALSE;

	if (snindex != -1)
	    SN_SET_OFFSET(&cg->notes[snindex + 2], CG_OFFSET(cg) - top);

	if (updater.base) {
	    /* Set our loopInfo's "update code" offset, for continue. */
	    loopInfo.update = CG_OFFSET(cg);

	    /* Copy the update code from its temporary pool into cg's pool. */
	    mocha_MoveSourceNotes(mc, &updater, cg);
	    mocha_MoveCode(mc, &updater, 0, cg, loopInfo.update);

	    /* Restore the absolute line number for source note readers. */
	    upindex = mocha_NewSourceNote(mc, cg, SRC_SETLINE);
	    if (upindex < 0)
		return MOCHA_FALSE;
	    SN_SET_OFFSET(&cg->notes[upindex + 1], ts->lineno);
	}

	if (snindex != -1)
	    SN_SET_OFFSET(&cg->notes[snindex + 3], CG_OFFSET(cg) - top);

	jmp = mocha_Emit3(mc, cg, MOP_GOTO, 0, 0);
	if (jmp < 0)
	    return MOCHA_FALSE;
	SET_JUMP_OFFSET(CG_CODE(cg, jmp), top - jmp);
	if (beq > 0)
	    SET_JUMP_OFFSET(CG_CODE(cg, beq), CG_OFFSET(cg) - beq);
	mocha_PopLoopInfo(cg);
	if (forIn) {
	    if (mocha_Emit1(mc, cg, MOP_POP) < 0)
		return MOCHA_FALSE;
	    cg->withDepth--;
	}
	break;

      case TOK_BREAK:
	if (!cg->loopInfo) {
	    mocha_ReportSyntaxError(mc, ts, "break used outside a loop");
	    return MOCHA_FALSE;
	}
	if (mocha_EmitBreak(mc, cg) < 0)
	    return MOCHA_FALSE;
	break;

      case TOK_CONTINUE:
	if (!cg->loopInfo) {
	    mocha_ReportSyntaxError(mc, ts, "continue used outside a loop");
	    return MOCHA_FALSE;
	}
	if (mocha_EmitContinue(mc, cg) < 0)
	    return MOCHA_FALSE;
	break;

      case TOK_WITH:
	MUST_MATCH_TOKEN(TOK_LP, "missing ( before formal parameters");
	/* balance) */
	if (!Expr(mc, ts, cg))
	    return MOCHA_FALSE;
	/* (balance: */
	MUST_MATCH_TOKEN(TOK_RP, "missing ) after formal parameters");

	if (mocha_Emit1(mc, cg, MOP_ENTER) < 0)
	    return MOCHA_FALSE;
	if (!mocha_SetStatementDepthType(mc, cg, MOCHA_TRUE))
	    return MOCHA_FALSE;
	cg->withDepth++;
	if (cg->loopInfo)
	    cg->loopInfo->withDepth++;
	ok = Statement(mc, ts, cg);
	cg->withDepth--;
	if (cg->loopInfo)
	    cg->loopInfo->withDepth--;
	if (!ok || mocha_Emit1(mc, cg, MOP_LEAVE) < 0)
	    return MOCHA_FALSE;
	break;

      case TOK_VAR:
	if (!Variables(mc, ts, cg) || mocha_Emit1(mc, cg, MOP_POP) < 0)
	    return MOCHA_FALSE;
	break;

      case TOK_RETURN:
	if (!(ts->flags & TSF_FUNCTION)) {
	    mocha_ReportSyntaxError(mc, ts, "return used outside a function");
	    return MOCHA_FALSE;
	}

	/* This is ugly, but we don't want to require a semicolon. */
	newlines = ts->flags & TSF_NEWLINES;
	if (!newlines) SCAN_NEWLINES(ts);
	tt = mocha_PeekToken(mc, ts, cg);
	if (!newlines) HIDE_NEWLINES(ts);

	/* Arrange to leave all current with and for-in statements. */
	while (cg->withDepth > 0) {
	    if (mocha_NewSourceNote(mc, cg, SRC_HIDDEN) < 0)
		return MOCHA_FALSE;
	    cg->withDepth--;
	    if ((mocha_IsWithStatementDepth(cg, cg->withDepth)
		? mocha_Emit1(mc, cg, MOP_LEAVE)
		: mocha_Emit1(mc, cg, MOP_POP)) < 0) {
		return MOCHA_FALSE;
	    }
	}

	if (tt != TOK_EOF && tt != TOK_EOL && tt != TOK_SEMI && tt != TOK_RC) {
	    if (!StmtExpr(mc, ts, cg))
		return MOCHA_FALSE;
	    ts->flags |= TSF_RETURN_EXPR;
	} else {
	    if (mocha_Emit1(mc, cg, MOP_PUSH) < 0)
		return MOCHA_FALSE;
	    ts->flags |= TSF_RETURN_VOID;
	}
	if ((ts->flags & (TSF_RETURN_EXPR | TSF_RETURN_VOID)) ==
	    (TSF_RETURN_EXPR | TSF_RETURN_VOID)) {
	    mocha_ReportSyntaxError(mc, ts,
				    "function does not always return a value");
	    return MOCHA_FALSE;
	}
	if (mocha_Emit1(mc, cg, MOP_RETURN) < 0)
	    return MOCHA_FALSE;
	break;

      case TOK_LC:
	if (!Statements(mc, ts, cg))
	    return MOCHA_FALSE;

	/* {balance: */
	MUST_MATCH_TOKEN(TOK_RC, "missing } in compound statement");
	break;

      case TOK_EOL:
      case TOK_SEMI:
	return MOCHA_TRUE;

      default:
	mocha_UngetToken(ts);
	if (!StmtExpr(mc, ts, cg) || mocha_Emit1(mc, cg, MOP_POP) < 0)
	    return MOCHA_FALSE;
	break;
    }

    (void) mocha_MatchToken(mc, ts, cg, TOK_SEMI);
    return MOCHA_TRUE;
}

/*
** Emit a bytecode and its 2-byte constant (atom) index immediate operand.
** We use mc and cg from the caller's lexical scope, and return MOCHA_FALSE
** on error.
*/
#define EMIT_CONST_ATOM_OP(op, atomIndex) {                                   \
    if (mocha_Emit3(mc, cg, op, (MochaCode)((atomIndex) >> 8),                \
				(MochaCode)(atomIndex)) < 0) {                \
	return MOCHA_FALSE;                                                   \
    }                                                                         \
}

static MochaBoolean
Variables(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaBoolean ok;
    MochaAtom *atom;
    MochaAtomNumber atomIndex;
    MochaScope *scope;
    MochaSymbol *var;

    if (mocha_NewSourceNote(mc, cg, SRC_VAR) < 0)
	return MOCHA_FALSE;
    ok = MOCHA_TRUE;
    for (;;) {
	MUST_MATCH_TOKEN(TOK_NAME, "missing variable name");
	atom = ts->token.u.atom;
	atomIndex = mocha_IndexAtom(mc, atom, cg);
	EMIT_CONST_ATOM_OP(MOP_NAME, atomIndex);

	scope = mc->staticLink->scope;
	var = mocha_DefineSymbol(mc, scope, atom, SYM_VARIABLE, 0);
	if (!var)
	    return MOCHA_FALSE;
	var->slot = scope->freeslot++;

	if (mocha_MatchToken(mc, ts, cg, TOK_ASSIGN)) {
	    if (ts->token.u.op != MOP_NOP) {
		mocha_ReportSyntaxError(mc, ts,
					"illegal variable initialization");
		ok = MOCHA_FALSE;
	    }
	    if (!AssignExpr(mc, ts, cg) || mocha_Emit1(mc, cg, MOP_ASSIGN) < 0)
		return MOCHA_FALSE;
	}
	if (!mocha_MatchToken(mc, ts, cg, TOK_COMMA))
	    break;
	if (mocha_NewSourceNote(mc, cg, SRC_COMMA) < 0 ||
	    mocha_Emit1(mc, cg, MOP_POP) < 0) {
	    return MOCHA_FALSE;
	}
    }
    return ok;
}

/* Parse an expression followed by a statement separator or terminator. */
static MochaBoolean
StmtExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    unsigned lineno;
    MochaTokenType tt;

    lineno = ts->lineno;
    if (!Expr(mc, ts, cg))
	return MOCHA_FALSE;
    if (ts->lineno == lineno) {
	tt = ts->pushback.type;
	if (tt != TOK_EOF && tt != TOK_EOL && tt != TOK_SEMI && tt != TOK_RC) {
	    mocha_ReportSyntaxError(mc, ts,
				    (tt == TOK_LP || IS_PRIMARY_TOKEN(tt))
				    ? "missing operator in expression"
				    : "missing semicolon before statement");
	    return MOCHA_FALSE;
	}
    }
    return MOCHA_TRUE;
}

static MochaBoolean
Expr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    for (;;) {
	if (!AssignExpr(mc, ts, cg))
	    return MOCHA_FALSE;
	if (!mocha_MatchToken(mc, ts, cg, TOK_COMMA))
	    break;
	if (mocha_NewSourceNote(mc, cg, SRC_COMMA) < 0 ||
	    mocha_Emit1(mc, cg, MOP_POP) < 0) {
	    return MOCHA_FALSE;
	}
    }
    return MOCHA_TRUE;
}

static MochaBoolean
AssignExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaOp op;

    if (!CondExpr(mc, ts, cg))
	return MOCHA_FALSE;
    if (mocha_MatchToken(mc, ts, cg, TOK_ASSIGN)) {
	op = ts->token.u.op;
	if (op != MOP_NOP && mocha_Emit1(mc, cg, MOP_DUP) < 0)
	    return MOCHA_FALSE;
	if (!AssignExpr(mc, ts, cg))
	    return MOCHA_FALSE;
	if (op != MOP_NOP) {
	    if (mocha_NewSourceNote(mc, cg, SRC_ASSIGNOP) < 0 ||
		mocha_Emit1(mc, cg, op) < 0) {
		return MOCHA_FALSE;
	    }
	}
	if (mocha_Emit1(mc, cg, MOP_ASSIGN) < 0)
	    return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
CondExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    ptrdiff_t beq, jmp;

    if (!OrExpr(mc, ts, cg))
	return MOCHA_FALSE;
    if (mocha_MatchToken(mc, ts, cg, TOK_HOOK)) {
	if (mocha_NewSourceNote(mc, cg, SRC_COND) < 0)
	    return MOCHA_FALSE;
	beq = mocha_Emit3(mc, cg, MOP_IFEQ, 0, 0);
	if (beq < 0 || !AssignExpr(mc, ts, cg))
	    return MOCHA_FALSE;
	MUST_MATCH_TOKEN(TOK_COLON, "missing : in conditional expression");
	jmp = mocha_Emit3(mc, cg, MOP_GOTO, 0, 0);
	if (jmp < 0)
	    return MOCHA_FALSE;
	SET_JUMP_OFFSET(CG_CODE(cg, beq), CG_OFFSET(cg) - beq);
	if (!AssignExpr(mc, ts, cg))
	    return MOCHA_FALSE;
	SET_JUMP_OFFSET(CG_CODE(cg, jmp), CG_OFFSET(cg) - jmp);
    }
    return MOCHA_TRUE;
}

static MochaBoolean
OrExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    ptrdiff_t beq, tmp, jmp;

    if (!AndExpr(mc, ts, cg))
	return MOCHA_FALSE;
    if (mocha_MatchToken(mc, ts, cg, TOK_OR)) {
	beq = mocha_Emit3(mc, cg, MOP_IFEQ, 0, 0);
	tmp = mocha_Emit1(mc, cg, MOP_TRUE);
	jmp = mocha_Emit3(mc, cg, MOP_GOTO, 0, 0);
	if (beq < 0 || tmp < 0 || jmp < 0)
	    return MOCHA_FALSE;
	SET_JUMP_OFFSET(CG_CODE(cg, beq), CG_OFFSET(cg) - beq);
	if (!OrExpr(mc, ts, cg))
	    return MOCHA_FALSE;
	SET_JUMP_OFFSET(CG_CODE(cg, jmp), CG_OFFSET(cg) - jmp);
    }
    return MOCHA_TRUE;
}

static MochaBoolean
AndExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    ptrdiff_t bne, tmp, jmp;

    if (!BitOrExpr(mc, ts, cg))
	return MOCHA_FALSE;
    if (mocha_MatchToken(mc, ts, cg, TOK_AND)) {
	bne = mocha_Emit3(mc, cg, MOP_IFNE, 0, 0);
	tmp = mocha_Emit1(mc, cg, MOP_FALSE);
	jmp = mocha_Emit3(mc, cg, MOP_GOTO, 0, 0);
	if (bne < 0 || tmp < 0 || jmp < 0)
	    return MOCHA_FALSE;
	SET_JUMP_OFFSET(CG_CODE(cg, bne), CG_OFFSET(cg) - bne);
	if (!AndExpr(mc, ts, cg))
	    return MOCHA_FALSE;
	SET_JUMP_OFFSET(CG_CODE(cg, jmp), CG_OFFSET(cg) - jmp);
    }
    return MOCHA_TRUE;
}

static MochaBoolean
BitOrExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    if (!BitXorExpr(mc, ts, cg))
	return MOCHA_FALSE;
    while (mocha_MatchToken(mc, ts, cg, TOK_BITOR)) {
	if (!BitXorExpr(mc, ts, cg) ||
	    mocha_Emit1(mc, cg, MOP_BITOR) < 0) {
	    return MOCHA_FALSE;
	}
    }
    return MOCHA_TRUE;
}

static MochaBoolean
BitXorExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    if (!BitAndExpr(mc, ts, cg))
	return MOCHA_FALSE;
    while (mocha_MatchToken(mc, ts, cg, TOK_BITXOR)) {
	if (!BitAndExpr(mc, ts, cg) ||
	    mocha_Emit1(mc, cg, MOP_BITXOR) < 0) {
	    return MOCHA_FALSE;
	}
    }
    return MOCHA_TRUE;
}

static MochaBoolean
BitAndExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    if (!EqExpr(mc, ts, cg))
	return MOCHA_FALSE;
    while (mocha_MatchToken(mc, ts, cg, TOK_BITAND)) {
	if (!EqExpr(mc, ts, cg) || mocha_Emit1(mc, cg, MOP_BITAND) < 0)
	    return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
EqExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaOp op;

    if (!RelExpr(mc, ts, cg))
	return MOCHA_FALSE;
    while (mocha_MatchToken(mc, ts, cg, TOK_EQOP)) {
	op = ts->token.u.op;
	if (!RelExpr(mc, ts, cg) || mocha_Emit1(mc, cg, op) < 0)
	    return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
RelExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaOp op;

    if (!ShiftExpr(mc, ts, cg))
	return MOCHA_FALSE;
    while (mocha_MatchToken(mc, ts, cg, TOK_RELOP)) {
	op = ts->token.u.op;
	if (!ShiftExpr(mc, ts, cg) || mocha_Emit1(mc, cg, op) < 0)
	    return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
ShiftExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaOp op;

    if (!AddExpr(mc, ts, cg))
	return MOCHA_FALSE;
    while (mocha_MatchToken(mc, ts, cg, TOK_SHOP)) {
	op = ts->token.u.op;
	if (!AddExpr(mc, ts, cg) || mocha_Emit1(mc, cg, op) < 0)
	    return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
AddExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaTokenType tt;

    if (!MulExpr(mc, ts, cg))
	return MOCHA_FALSE;
    while ((tt = mocha_GetToken(mc, ts, cg)) == TOK_PLUS || tt == TOK_MINUS) {
	if (!MulExpr(mc, ts, cg) ||
	    mocha_Emit1(mc, cg, (tt == TOK_PLUS) ? MOP_ADD : MOP_SUB) < 0) {
	    return MOCHA_FALSE;
	}
    }
    mocha_UngetToken(ts);
    return MOCHA_TRUE;
}

static MochaBoolean
MulExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaOp op;

    if (!UnaryExpr(mc, ts, cg))
	return MOCHA_FALSE;
    while (mocha_MatchToken(mc, ts, cg, TOK_MULOP)) {
	op = ts->token.u.op;
	if (!UnaryExpr(mc, ts, cg) || mocha_Emit1(mc, cg, op) < 0)
	    return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
UnaryExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaTokenType tt;
    MochaOp op;
    int argc;
    unsigned lineno;

    tt = mocha_GetToken(mc, ts, cg);
    switch (tt) {
      case TOK_UNARYOP:
      case TOK_MINUS:
	op = ts->token.u.op;
	if (!UnaryExpr(mc, ts, cg) || mocha_Emit1(mc, cg, op) < 0)
	    return MOCHA_FALSE;
	break;
      case TOK_INCOP:
	op = ts->token.u.op;
	if (!MemberExpr(mc, ts, cg) || mocha_Emit2(mc, cg, op, 0) < 0)
	    return MOCHA_FALSE;
	break;
      case TOK_NEW:
	if (!NameExpr(mc, ts, cg))
	    return MOCHA_FALSE;
	while ((tt = mocha_GetToken(mc, ts, cg)) == TOK_DOT) {
	    if (!NameExpr(mc, ts, cg) || mocha_Emit1(mc, cg, MOP_MEMBER) < 0)
		return MOCHA_FALSE;
	}
	argc = 0;
	if (tt != TOK_LP) {
	    mocha_UngetToken(ts);
	} else if (!mocha_MatchToken(mc, ts, cg, TOK_RP)) {
	    do {
		if (!AssignExpr(mc, ts, cg))
		    return MOCHA_FALSE;
		argc++;
	    } while (mocha_MatchToken(mc, ts, cg, TOK_COMMA));

	    /* (balance: */
	    MUST_MATCH_TOKEN(TOK_RP, "missing ) after constructor argument list");
	}
	if (mocha_Emit2(mc, cg, MOP_NEW, (MochaCode)argc) < 0)
	    return MOCHA_FALSE;
	break;

      default:
	mocha_UngetToken(ts);
	lineno = ts->lineno;
	if (!MemberExpr(mc, ts, cg))
	    return MOCHA_FALSE;

	/* Don't look across a newline boundary looking for a postfix incop. */
	if (ts->lineno == lineno && mocha_MatchToken(mc, ts, cg, TOK_INCOP)) {
	    op = ts->token.u.op;
	    if (mocha_Emit2(mc, cg, op, 1) < 0)
		return MOCHA_FALSE;
	}
    }
    return MOCHA_TRUE;
}

static MochaBoolean
MemberExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaToken token;
    MochaTokenType tt;
    int argc;
    MochaSymbol *sym;

    if (!PrimaryExpr(mc, ts, cg))
	return MOCHA_FALSE;
    token = ts->token;
    while ((tt = mocha_GetToken(mc, ts, cg)) != TOK_EOF) {
	if (tt == TOK_DOT) {
	    if (!NameExpr(mc, ts, cg))
		return MOCHA_FALSE;
	    if (mocha_Emit1(mc, cg, (mocha_PeekToken(mc,ts,cg) == TOK_ASSIGN)
				    ? MOP_LMEMBER : MOP_MEMBER) < 0) {
		return MOCHA_FALSE;
	    }
	} else if (tt == TOK_LB) {
	    if (!Expr(mc, ts, cg))
		return MOCHA_FALSE;
	    /* [balance: */
	    MUST_MATCH_TOKEN(TOK_RB, "missing ] in index expression");
	    if (mocha_Emit1(mc, cg, (mocha_PeekToken(mc,ts,cg) == TOK_ASSIGN)
				    ? MOP_LINDEX : MOP_INDEX) < 0) {
		return MOCHA_FALSE;
	    }
	} else if (tt == TOK_LP) {
	    argc = 0;
	    if (!mocha_MatchToken(mc, ts, cg, TOK_RP)) {
		do {
		    if (!AssignExpr(mc, ts, cg))
			return MOCHA_FALSE;
		    argc++;
		} while (mocha_MatchToken(mc, ts, cg, TOK_COMMA));

		/* (balance: */
		MUST_MATCH_TOKEN(TOK_RP, "missing ) after argument list");
	    }
	    if (mocha_Emit2(mc, cg, MOP_CALL, (MochaCode)argc) < 0)
		return MOCHA_FALSE;
	} else {
	    mocha_UngetToken(ts);

	    /*
	    ** Look for a name, number, or string immediately after a name.
	    ** Such a token juxtaposition is likely to be a "command-style"
	    ** function call.
	    */
	    if ((ts->flags & TSF_COMMAND) && token.type == TOK_NAME &&
		(IS_PRIMARY_TOKEN(tt) || tt == TOK_EOL)) {
		if (!mocha_LookupSymbol(mc, mc->staticLink->scope, token.u.atom,
					MLF_GET, &sym)) {
		    return MOCHA_FALSE;
		}
		if (sym &&
		    sym->type == SYM_PROPERTY &&
		    sym_datum(sym)->tag == MOCHA_FUNCTION &&
		    (tt != TOK_EOL ||
		     sym_datum(sym)->u.fun->nargs == 0)) {
		    argc = 0;

		    while (tt != TOK_EOL && tt != TOK_SEMI && tt != TOK_RP &&
			   tt != TOK_RC) {
			if (!AssignExpr(mc, ts, cg))
			    return MOCHA_FALSE;
			argc++;
			mocha_MatchToken(mc, ts, cg, TOK_COMMA);
			tt = mocha_PeekToken(mc, ts, cg);
		    }
		    if (mocha_Emit2(mc, cg, MOP_CALL, (MochaCode)argc) < 0)
			return MOCHA_FALSE;
		}
	    }
	    break;
	}
	token = ts->token;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
PrimaryExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaTokenType tt;
    MochaAtomNumber atomIndex;

    tt = mocha_GetToken(mc, ts, cg);
    switch (tt) {
      case TOK_LP:
	if (!Expr(mc, ts, cg))
	    return MOCHA_FALSE;
	/* (balance: */
	MUST_MATCH_TOKEN(TOK_RP, "missing ) in parenthetical");

	/* XXX optimize to annotate expr's last instruction and avoid a NOP */
	if (mocha_NewSourceNote(mc, cg, SRC_PAREN) < 0 ||
	    mocha_Emit1(mc, cg, MOP_NOP) < 0) {
	    return MOCHA_FALSE;
	}
	break;

      case TOK_NAME:
	mocha_UngetToken(ts);
	return NameExpr(mc, ts, cg);

      case TOK_NUMBER:
	if (ts->token.u.atom->fval == 0) {
	    if (mocha_Emit1(mc, cg, MOP_ZERO) < 0)
		return MOCHA_FALSE;
	} else if (ts->token.u.atom->fval == 1) {
	    if (mocha_Emit1(mc, cg, MOP_ONE) < 0)
		return MOCHA_FALSE;
	} else {
	    atomIndex = mocha_IndexAtom(mc, ts->token.u.atom, cg);
	    EMIT_CONST_ATOM_OP(MOP_NUMBER, atomIndex);
	}
	break;

      case TOK_STRING:
	atomIndex = mocha_IndexAtom(mc, ts->token.u.atom, cg);
	EMIT_CONST_ATOM_OP(MOP_STRING, atomIndex);
	break;

      case TOK_PRIMARY:
	if (mocha_Emit1(mc, cg, ts->token.u.op) < 0)
	    return MOCHA_FALSE;
	break;

      case TOK_RESERVED:
	mocha_ReportSyntaxError(mc, ts, "identifier is a reserved word");
	return MOCHA_FALSE;

      default:
	mocha_ReportSyntaxError(mc, ts, IS_PRIMARY_TOKEN(tt)
					? "missing operand in expression"
					: "syntax error");
	return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
NameExpr(MochaContext *mc, MochaTokenStream *ts, CodeGenerator *cg)
{
    MochaTokenType tt;
    MochaAtomNumber atomIndex;

    tt = mocha_GetToken(mc, ts, cg);
    switch (tt) {
      case TOK_NAME:
	atomIndex = mocha_IndexAtom(mc, ts->token.u.atom, cg);
	EMIT_CONST_ATOM_OP(MOP_NAME, atomIndex);
	break;
      case TOK_PRIMARY:
	if (ts->token.u.op == MOP_THIS) {
	    if (mocha_Emit1(mc, cg, MOP_THIS) < 0)
		return MOCHA_FALSE;
	    break;
	}
	/* FALL THROUGH */
      default:
	mocha_ReportSyntaxError(mc, ts, "missing name in expression");
	return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}
