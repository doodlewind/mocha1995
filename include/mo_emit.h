#ifndef _mo_emit_h_
#define _mo_emit_h_
/*
** Mocha bytecode generation.
**
** Brendan Eich, 6/21/95
*/
#include <stddef.h>
#include "prmacros.h"
#include "mo_bcode.h"
#include "mo_prvtd.h"
#include "mo_pubtd.h"

NSPR_BEGIN_EXTERN_C

typedef struct LoopInfo LoopInfo;

struct LoopInfo {
    ptrdiff_t           top;            /* offset of loop top from cg base */
    ptrdiff_t           update;         /* loop update offset (top if none) */
    ptrdiff_t           breaks;         /* offset of last break in loop */
    ptrdiff_t           continues;      /* offset of last continue in loop */
    int                 withDepth;      /* with statement depth counter */
    LoopInfo            *down;          /* info for enclosing loop */
};

#define SET_LOOPINFO_TOP(li, top) \
    ((li)->top = (li)->update = (li)->breaks = (li)->continues = (top))

struct CodeGenerator {
    struct PRArenaPool  *pool;          /* pool in which to allocate code */
    MochaCode           *base;          /* base of Mocha bytecode vector */
    MochaCode           *limit;         /* one byte beyond end of bytecode */
    MochaCode           *ptr;           /* pointer to next free bytecode */
    MochaAtom           *atomList;      /* literals indexed for mapping */
    MochaAtomNumber     atomCount;      /* count of indexed literals */
    MochaOp             lastOpcode;     /* last bytecode emited */
    LoopInfo            *loopInfo;      /* LoopInfo stack for break/continue */
    uint32              depthTypeSet;   /* bitset of statement depth types */
    int                 withDepth;      /* with/for-in statement depth count */
    int                 stackDepth;     /* current stack depth in basic block */
    int                 maxStackDepth;  /* maximum stack depth so far */
    SourceNote          *notes;         /* source notes, see below */
    unsigned            noteCount;      /* number of source notes so far */
    ptrdiff_t           lastOffset;     /* pc offset of last source note */
};

#define CG_CODE(cg,offset)      ((cg)->base + (offset))
#define CG_OFFSET(cg)           ((cg)->ptr - (cg)->base)
#define CG_RESET(cg)            ((cg)->ptr = (cg)->base,                      \
				 (cg)->atomList = 0, (cg)->atomCount = 0,     \
                                 (cg)->lastOpcode = 0, (cg)->loopInfo = 0,    \
                                 (cg)->depthTypeSet = 0, (cg)->withDepth = 0, \
                                 (cg)->stackDepth = (cg)->maxStackDepth = 0,  \
                                 CG_RESET_NOTES(cg))
#define CG_RESET_NOTES(cg)      ((cg)->notes = 0, (cg)->noteCount = 0,        \
                                 (cg)->lastOffset = 0)

/*
** Initialize cg to allocate from arena pool ap.  Return true on success.
** Report an exception and return false if the initial code segment can't
** be allocated.
*/
extern MochaBoolean
mocha_InitCodeGenerator(MochaContext *mc, CodeGenerator *cg,
                        struct PRArenaPool *ap);

/*
** Emit one bytecode.
*/
extern ptrdiff_t
mocha_Emit1(MochaContext *mc, CodeGenerator *cg, MochaOp op);

/*
** Emit two bytecodes, an opcode (op) with a byte of immediate operand (op1).
*/
extern ptrdiff_t
mocha_Emit2(MochaContext *mc, CodeGenerator *cg, MochaOp op, MochaCode op1);

/*
** Emit three bytecodes, an opcode with two bytes of immediate operands.
*/
extern ptrdiff_t
mocha_Emit3(MochaContext *mc, CodeGenerator *cg, MochaOp op, MochaCode op1,
            MochaCode op2);

/*
** Update cg's stack depth budget for the opcode located at offset in cg.
*/
extern void
mocha_UpdateDepth(MochaContext *mc, CodeGenerator *cg, ptrdiff_t offset);

/*
** Support for return from within nested with and for-in statements.
** Return MOCHA_FALSE if too much nesting, MOCHA_TRUE otherwise.
*/
extern MochaBoolean
mocha_IsWithStatementDepth(CodeGenerator *cg, int depth);

extern MochaBoolean
mocha_SetStatementDepthType(MochaContext *mc, CodeGenerator *cg,
                            MochaBoolean isWith);

/*
** Copy code from code generator from starting at fromOffset and running to
** CG_OFFSET(from) into code generator to, starting at toOffset.
**
** NB: this function does not move source notes; the caller must do that.
*/
extern ptrdiff_t
mocha_MoveCode(MochaContext *mc, CodeGenerator *from, ptrdiff_t fromOffset,
               CodeGenerator *to, ptrdiff_t toOffset);

/*
** Push the stack-allocated struct at li onto the cg->loopInfo stack.
*/
extern void
mocha_PushLoopInfo(CodeGenerator *cg, LoopInfo *li, ptrdiff_t top);

/*
** Emit a break instruction, recording it for backpatching.
*/
extern ptrdiff_t
mocha_EmitBreak(MochaContext *mc, CodeGenerator *cg);

/*
** Emit a continue instruction, recording it for backpatching.
*/
extern ptrdiff_t
mocha_EmitContinue(MochaContext *mc, CodeGenerator *cg);

/*
** Pop cg->loopInfo.  If the top LoopInfo struct is not stack-allocated, it
** is up to the caller to free it.
*/
extern void
mocha_PopLoopInfo(CodeGenerator *cg);

/*
** Source notes generated along with bytecode for decompiling and debugging.
** A source note is a uint16 with 4 bits of type and 12 of offset.
**
** NB: order matters -- note types > SRC_SETLINE are "gettable", but the line
**     number types and null are not.
*/
typedef enum SourceNoteType {
    SRC_NULL     = 0,           /* if delta == 0, terminate SourceNote list */
    SRC_NEWLINE  = 1,           /* bytecode follows a source newline */
    SRC_SETLINE  = 2,           /* a file-absolute source line number note */
    SRC_IF       = 3,           /* MOP_IFEQ bytecode is from an if-then */
    SRC_IF_ELSE  = 4,           /* MOP_IFEQ bytecode is from an if-then-else */
    SRC_WHILE    = 5,           /* MOP_IFEQ is from a while loop */
    SRC_FOR      = 6,           /* MOP_NOP or MOP_POP in for loop head */
    SRC_CONTINUE = 7,           /* MOP_GOTO is a continue, not a break */
    SRC_VAR      = 8,           /* MOP_NAME of a local variable declaration */
    SRC_COMMA    = 9,           /* MOP_POP representing a comma operator */
    SRC_ASSIGNOP = 10,          /* += or another assign-op follows */
    SRC_COND     = 11,          /* MOP_IFEQ is from conditional (?:) operator */
    SRC_PAREN    = 12,          /* MOP_NOP generated to mark user parentheses */
    SRC_HIDDEN   = 13           /* MOP_LEAVE for break/cont/return in a with */
} SourceNoteType;

#define SN_TYPE_BITS            4
#define SN_DELTA_BITS           12
#define SN_TYPE_MASK            (PR_BITMASK(SN_TYPE_BITS) << SN_DELTA_BITS)
#define SN_DELTA_MASK           (PR_BITMASK(SN_DELTA_BITS))

#define SN_TYPE(sn)             (*(sn) >> SN_DELTA_BITS)
#define SN_SET_TYPE(sn,_type)   (*(sn) = (SourceNote)                         \
                                         (((_type) << SN_DELTA_BITS)          \
                                          | (*(sn) & SN_DELTA_MASK)))
#define SN_DELTA(sn)            (*(sn) & SN_DELTA_MASK)
#define SN_SET_DELTA(sn,_delta) (*(sn) = (SourceNote)                         \
                                         ((*(sn) & SN_TYPE_MASK)              \
                                          | ((_delta) & SN_DELTA_MASK)))
#define SN_OFFSET(sn)           (*(sn))
#define SN_SET_OFFSET(sn,_off)  (*(sn) = (SourceNote)(_off))

#define SN_DELTA_MAX            (1 << SN_DELTA_BITS)

/* A for loop has 3 extra notes for cond, next, and tail offsets. */
#define SN_LENGTH(sn)           ((SN_TYPE(sn) == SRC_SETLINE) ? 2 :           \
                                    ((SN_TYPE(sn) == SRC_FOR) ? 4 : 1))
#define SN_NEXT(sn)             ((sn) + SN_LENGTH(sn))

/* A source note array is terminated by an all-zero element. */
#define SN_MAKE_TERMINATOR(sn)  (*(sn) = 0)
#define SN_IS_TERMINATOR(sn)    (*(sn) == 0)

/*
** Append a new source note of the given type (and therefore size) to cg's
** notes dynamic array, updating cg->noteCount.  Return the new note's index
** within the array pointed at by cg->notes.  Return -1 if out of memory.
*/
extern int
mocha_NewSourceNote(MochaContext *mc, CodeGenerator *cg, SourceNoteType type);

/*
** Copy source notes from one code generator to another.  The code annotated
** by the copied notes must be moved *after* the notes are moved.
*/
extern MochaBoolean
mocha_MoveSourceNotes(MochaContext *mc, CodeGenerator *from, CodeGenerator *to);

/*
** Finish taking source notes in mc's tempPool by copying them to new stable
** store allocated from codePool.
*/
extern SourceNote *
mocha_FinishTakingSourceNotes(MochaContext *mc, CodeGenerator *cg);

/*
** Get a non-line-type source note for pc.  Return 0 if pc has no such note.
*/
extern SourceNote *
mocha_GetSourceNote(MochaScript *script, MochaCode *pc);

/*
** Return the number of the line from which pc's bytecode was generated.
*/
extern unsigned
mocha_PCtoLineNumber(MochaScript *script, MochaCode *pc);

/*
** Return a structure pointing to a safe copy of code generated in cg, to the
** filename string, and to source notes for debugging/decompilation.  Return
** null on allocation failure, which this function reports.
*/
extern MochaScript *
mocha_NewScript(MochaContext *mc, CodeGenerator *cg, const char *filename,
                unsigned lineno);

/*
** Destroy all dynamically allocated storage held by script.
*/
extern void
mocha_DestroyScript(MochaContext *mc, MochaScript *script);

NSPR_END_EXTERN_C

#endif /* _mo_emit_h_ */
