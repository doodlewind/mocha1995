/*
** Mocha bytecode generation.
**
** Brendan Eich, 6/20/95
*/
#include <memory.h>
#include <stddef.h>
#include <string.h>
#include "prarena.h"
#include "prlog.h"
#include "mo_bcode.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mochaapi.h"

#define CGINCR  256     /* code generation bytecode allocation increment */
#define SNINCR  64      /* source note allocation increment */

MochaBoolean
mocha_InitCodeGenerator(MochaContext *mc, CodeGenerator *cg, PRArenaPool *ap)
{
    cg->pool = ap;
    PR_ARENA_ALLOCATE(cg->base, ap, CGINCR);
    if (!cg->base) {
	MOCHA_ReportOutOfMemory(mc);
	return MOCHA_FALSE;
    }
    cg->limit = CG_CODE(cg, CGINCR);
    CG_RESET(cg);
    return MOCHA_TRUE;
}

static ptrdiff_t
EmitCheck(MochaContext *mc, CodeGenerator *cg, MochaOp op, int delta)
{
    ptrdiff_t offset, length;

    PR_ASSERT(delta < CGINCR);
    cg->lastOpcode = op;
    offset = CG_OFFSET(cg);
    if ((uprword_t)cg->ptr + delta >= (uprword_t)cg->limit) {
        length = cg->limit - cg->base;
        PR_ARENA_GROW(cg->base, cg->pool, length, CGINCR);
	if (!cg->base) {
	    MOCHA_ReportOutOfMemory(mc);
	    return -1;
	}
        cg->limit = CG_CODE(cg, length + CGINCR);
        cg->ptr = CG_CODE(cg, offset);
    }
    return offset;
}

void
mocha_UpdateDepth(MochaContext *mc, CodeGenerator *cg, ptrdiff_t target)
{
    MochaCode *pc;
    SourceNote *sn, *last;
    ptrdiff_t offset;
    MochaCodeSpec *cs;
    int nuses;

    pc = CG_CODE(cg, target);

    /* Don't worry about hidden MOP_LEAVEs, we guarantee stack balance. */
    if (cg->noteCount) {
	sn = cg->notes;
	last = &sn[cg->noteCount-1];
	if (SN_TYPE(last) == SRC_HIDDEN) {
	    /* We must ensure that this source note applies to target. */
	    for (offset = 0; sn <= last; sn = SN_NEXT(sn)) {
		offset += SN_DELTA(sn);
		if (offset == target && sn == last)
		    return;
	    }
	}
    }

    cs = &mocha_CodeSpec[pc[0]];
    nuses = cs->nuses;
    if (nuses < 0)
        nuses = pc[1] + 1;
    cg->stackDepth -= nuses;
    if (cg->stackDepth < 0) {
        MOCHA_ReportError(mc, "internal error: stack underflow at pc %d",
			  target);
    }
    cg->stackDepth += cs->ndefs;
    if (cg->stackDepth > cg->maxStackDepth)
        cg->maxStackDepth = cg->stackDepth;
}

ptrdiff_t
mocha_Emit1(MochaContext *mc, CodeGenerator *cg, MochaOp op)
{
    ptrdiff_t offset = EmitCheck(mc, cg, op, 1);

    if (offset >= 0) {
	*cg->ptr++ = op;
	mocha_UpdateDepth(mc, cg, offset);
    }
    return offset;
}

ptrdiff_t
mocha_Emit2(MochaContext *mc, CodeGenerator *cg, MochaOp op, MochaCode op1)
{
    ptrdiff_t offset = EmitCheck(mc, cg, op, 2);

    if (offset >= 0) {
	cg->ptr[0] = op;
	cg->ptr[1] = op1;
	cg->ptr += 2;
	mocha_UpdateDepth(mc, cg, offset);
    }
    return offset;
}

ptrdiff_t
mocha_Emit3(MochaContext *mc, CodeGenerator *cg, MochaOp op, MochaCode op1,
	    MochaCode op2)
{
    ptrdiff_t offset = EmitCheck(mc, cg, op, 3);

    if (offset >= 0) {
	cg->ptr[0] = op;
	cg->ptr[1] = op1;
	cg->ptr[2] = op2;
	cg->ptr += 3;
	mocha_UpdateDepth(mc, cg, offset);
    }
    return offset;
}

MochaBoolean
mocha_IsWithStatementDepth(CodeGenerator *cg, int depth)
{
    uint32 bit;

    PR_ASSERT((unsigned)depth < 32);
    bit = 1 << depth;
    return (cg->depthTypeSet & bit) ? MOCHA_TRUE : MOCHA_FALSE;
}

MochaBoolean
mocha_SetStatementDepthType(MochaContext *mc, CodeGenerator *cg,
			    MochaBoolean isWith)
{
    uint32 bit;

    bit = (uint32)cg->withDepth;
    if (bit >= 32) {
        MOCHA_ReportError(mc, "too much with and for-in statement nesting");
	return MOCHA_FALSE;
    }
    bit = 1 << bit;
    if (isWith)
	cg->depthTypeSet |= bit;
    else
	cg->depthTypeSet &= ~bit;
    return MOCHA_TRUE;
}

ptrdiff_t
mocha_MoveCode(MochaContext *mc,
	       CodeGenerator *from, ptrdiff_t fromOffset,
	       CodeGenerator *to, ptrdiff_t toOffset)
{
    ptrdiff_t length, growth, offset;
    
    length = CG_OFFSET(from) - fromOffset;
    growth = (toOffset + length) - CG_OFFSET(to);
    offset = EmitCheck(mc, to, from->lastOpcode, growth);
    if (offset >= 0) {
	memmove(CG_CODE(to, toOffset), CG_CODE(from, fromOffset), length);
	to->ptr += growth;
	if (from->maxStackDepth > to->maxStackDepth)
	    to->maxStackDepth = from->maxStackDepth;
    }
    return offset;
}

void
mocha_PushLoopInfo(CodeGenerator *cg, LoopInfo *li, ptrdiff_t top)
{
    SET_LOOPINFO_TOP(li, top);
    li->withDepth = 0;
    li->down = cg->loopInfo;
    cg->loopInfo = li;
}

static ptrdiff_t
EmitLoopGoto(MochaContext *mc, CodeGenerator *cg, LoopInfo *li, ptrdiff_t *last)
{
    int i;
    ptrdiff_t offset, delta;

    for (i = 0; i < li->withDepth; i++) {
	if (mocha_NewSourceNote(mc, cg, SRC_HIDDEN) < 0 ||
	    mocha_Emit1(mc, cg, MOP_LEAVE) < 0) {
	    return -1;
	}
    }

    offset = CG_OFFSET(cg);
    delta = offset - *last;
    *last = offset;
    return mocha_Emit3(mc, cg, MOP_GOTO,
		       (MochaCode)(delta >> 8), (MochaCode)delta);
}

static void
PatchLoopGotos(CodeGenerator *cg, LoopInfo *li, ptrdiff_t last,
	       MochaCode *target)
{
    ptrdiff_t delta, jumpOffset;
    MochaCode *pc, *top;

    pc = CG_CODE(cg, last);
    top = CG_CODE(cg, li->top);
    while (pc > top) {
        delta = GET_JUMP_OFFSET(pc);
        jumpOffset = target - pc;
        SET_JUMP_OFFSET(pc, jumpOffset);
        pc -= delta;
    }
}

ptrdiff_t
mocha_EmitBreak(MochaContext *mc, CodeGenerator *cg)
{
    return EmitLoopGoto(mc, cg, cg->loopInfo, &cg->loopInfo->breaks);
}

ptrdiff_t
mocha_EmitContinue(MochaContext *mc, CodeGenerator *cg)
{
    return mocha_NewSourceNote(mc, cg, SRC_CONTINUE) >= 0 &&
	   EmitLoopGoto(mc, cg, cg->loopInfo, &cg->loopInfo->continues);
}

void
mocha_PopLoopInfo(CodeGenerator *cg)
{
    LoopInfo *li;

    li = cg->loopInfo;
    PatchLoopGotos(cg, li, li->breaks, cg->ptr);
    PatchLoopGotos(cg, li, li->continues, CG_CODE(cg, li->update));
    cg->loopInfo = li->down;
}

int
mocha_NewSourceNote(MochaContext *mc, CodeGenerator *cg, SourceNoteType type)
{
    int index, index2;
    PRArenaPool *ap;
    SourceNote *sn;
    size_t incr, size;
    ptrdiff_t offset, delta;

    index = cg->noteCount;
    if (index % SNINCR == 0) {
        ap = &mc->tempPool;
	incr = SNINCR * sizeof(SourceNote);
        if (!cg->notes) {
            PR_ARENA_ALLOCATE(cg->notes, ap, incr);
        } else {
	    size = cg->noteCount * sizeof(SourceNote);
            PR_ARENA_GROW(cg->notes, ap, size, incr);
        }
	if (!cg->notes) {
	    MOCHA_ReportOutOfMemory(mc);
	    return -1;
	}
    }
    sn = &cg->notes[cg->noteCount++];
    offset = CG_OFFSET(cg);
    delta = offset - cg->lastOffset;
    cg->lastOffset = offset;
    if (delta >= SN_DELTA_MAX) {
	SN_SET_TYPE(sn, SRC_NULL);
	do {
	    SN_SET_DELTA(sn, SN_DELTA_MAX - 1);
	    delta -= SN_DELTA(sn);
	    index2 = mocha_NewSourceNote(mc, cg, SRC_NULL);
	    if (index2 < 0)
		return -1;
	    sn = &cg->notes[index2];
	} while (delta >= SN_DELTA_MAX);
    }
    SN_SET_TYPE(sn, type);
    SN_SET_DELTA(sn, delta);
    switch (type) {
      case SRC_FOR:
        if (mocha_NewSourceNote(mc, cg, SRC_NULL) < 0 ||
	    mocha_NewSourceNote(mc, cg, SRC_NULL) < 0) {
	    return -1;
	}
	/* FALL THROUGH */
      case SRC_SETLINE:
        if (mocha_NewSourceNote(mc, cg, SRC_NULL) < 0)
	    return -1;
	break;
      default:;
    }
    return index;
}

MochaBoolean
mocha_MoveSourceNotes(MochaContext *mc, CodeGenerator *from, CodeGenerator *to)
{
    int i, len, index;
    SourceNote *sn;
    SourceNoteType type;
    ptrdiff_t delta;

    for (i = 0; i < from->noteCount; i += len) {
	len = SN_LENGTH(&from->notes[i]);
	type = SN_TYPE(&from->notes[i]);
	index = mocha_NewSourceNote(mc, to, type);
	if (index < 0)
	    return MOCHA_FALSE;
	sn = &to->notes[index];
	switch (type) {
	  case SRC_FOR:
	    SN_SET_OFFSET(&sn[3], SN_OFFSET(&from->notes[i + 3]));
	    SN_SET_OFFSET(&sn[2], SN_OFFSET(&from->notes[i + 2]));
	    /* FALL THROUGH */
	  case SRC_SETLINE:
	    SN_SET_OFFSET(&sn[1], SN_OFFSET(&from->notes[i + 1]));
	    break;
	  default:
	    PR_ASSERT(len == 1);
	}
	delta = SN_DELTA(&from->notes[i]);
	SN_SET_DELTA(&sn[0], delta);
	to->lastOffset += delta;
    }
    return MOCHA_TRUE;
}

SourceNote *
mocha_FinishTakingSourceNotes(MochaContext *mc, CodeGenerator *cg)
{
    unsigned len;
    SourceNote *tmp, *final;

    len = cg->noteCount;
    tmp = cg->notes;
    final = MOCHA_malloc(mc, (len + 1) * sizeof(SourceNote));
    if (!final)
	return 0;
    memcpy(final, tmp, len * sizeof(SourceNote));
    SN_MAKE_TERMINATOR(&final[len]);
    CG_RESET_NOTES(cg);
    return final;
}

SourceNote *
mocha_GetSourceNote(MochaScript *script, MochaCode *pc)
{
    SourceNote *sn;
    ptrdiff_t offset, target;

    sn = script->notes;
    if (!sn) return 0;
    target = pc - script->code;
    if ((unsigned)target >= script->length)
        return 0;
    for (offset = 0; !SN_IS_TERMINATOR(sn); sn = SN_NEXT(sn)) {
        offset += SN_DELTA(sn);
        if (offset == target && SN_TYPE(sn) > (unsigned)SRC_SETLINE)
            return sn;
    }
    return 0;
}

unsigned
mocha_PCtoLineNumber(MochaScript *script, MochaCode *pc)
{
    SourceNote *sn;
    ptrdiff_t offset, target;
    unsigned lineno;
    SourceNoteType type;
    MochaBoolean found;

    sn = script->notes;
    if (!sn) return 0;
    target = pc - script->code;
    if ((unsigned)target >= script->length)
        return 0;
    lineno = script->lineno;
    found = MOCHA_FALSE;
    for (offset = 0; !SN_IS_TERMINATOR(sn); sn = SN_NEXT(sn)) {
        offset += SN_DELTA(sn);
        if (offset > target)
            found = MOCHA_TRUE;
	type = SN_TYPE(sn);
        if (type == SRC_SETLINE) {
            lineno = SN_OFFSET(&sn[1]);
        } else if (type == SRC_NEWLINE) {
	    lineno++;
	    if (found)
		break;
	}
    }
    return lineno;
}

MochaScript *
mocha_NewScript(MochaContext *mc, CodeGenerator *cg, const char *filename,
		unsigned lineno)
{
    MochaScript *script;
    ptrdiff_t length;

    length = CG_OFFSET(cg);
    script = MOCHA_malloc(mc, sizeof(MochaScript) + length);
    if (!script)
	return 0;
    memset(script, 0, sizeof(MochaScript));
    if (!mocha_InitAtomMap(mc, &script->atomMap, cg)) {
	MOCHA_free(mc, script);
	return 0;
    }
    if (filename) {
	script->filename = MOCHA_strdup(mc, filename);
	if (!script->filename) {
	    mocha_DestroyScript(mc, script);
	    return 0;
	}
    }
    script->notes = mocha_FinishTakingSourceNotes(mc, cg);
    if (!script->notes) {
	mocha_DestroyScript(mc, script);
	return 0;
    }
    script->code = (MochaCode *)(script + 1);
    memcpy(script->code, cg->base, length);
    script->length = length;
    script->depth = cg->maxStackDepth;
    script->lineno = lineno;
    return script;
}

void
mocha_DestroyScript(MochaContext *mc, MochaScript *script)
{
    mocha_FreeAtomMap(mc, &script->atomMap);
    if (script->filename)
	MOCHA_free(mc, script->filename);
    if (script->notes)
	MOCHA_free(mc, script->notes);
    MOCHA_free(mc, script);
}
