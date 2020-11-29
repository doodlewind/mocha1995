#ifndef _mo_bcode_h_
#define _mo_bcode_h_
/*
** Mocha bytecode definitions.
**
** Brendan Eich, 7/2/95
*/
#include <stddef.h>
#include "prmacros.h"
#include "mo_prvtd.h"
#include "mo_pubtd.h"

NSPR_BEGIN_EXTERN_C

/*
** Mocha operation bytecodes.
*/
typedef enum MochaOp {
#define MOPDEF(op,val,name,image,length,nuses,ndefs,pretty,format) \
    op = val,
#include "mocha.def"
#undef MOPDEF
    MOP_MAX
} MochaOp;

/*
** Mocha bytecode formats
*/
typedef enum MochaOpFormat {
    MOF_BYTE,                   /* single bytecode, no immediates */
    MOF_JUMP,                   /* signed 16-bit jump offset immediate */
    MOF_INCOP,                  /* 1-byte increment pre/post-order flag */
    MOF_ARGC,                   /* unsigned 8-bit argument count */
    MOF_CONST,                  /* unsigned 16-bit constant pool index */
    MOF_MAX
} MochaOpFormat;

#define GET_JUMP_OFFSET(pc)     ((int16)(((pc)[1] << 8) | (pc)[2]))
#define SET_JUMP_OFFSET(pc,off) ((pc)[1] = (off) >> 8, (pc)[2] = (off))

#define GET_CONST_ATOM(mc,script,pc) \
	mocha_GetAtom((mc), &(script)->atomMap, ((pc)[1] << 8) | (pc)[2])
#define SET_CONST_ATOM(pc,atom) \
	((pc)[1] = (atom)->number >> 8, (pc)[2] = (atom)->number)

#define MOCHA_ATOM_INDEX_MAX   (1L << 16)

struct MochaCodeSpec {
    char                *name;          /* Mocha bytecode name */
    char                *image;         /* Mocha source literal or null */
    uint8               length;         /* length including opcode byte */
    int8                nuses;          /* arity, -1 if variadic */
    int8                ndefs;          /* number of stack results */
    uint8               pretty;         /* degree of pretty-printability */
    MochaOpFormat       format;         /* immediate operand format */
};

extern char             mocha_new[];
#ifdef MOCHA_HAS_DELETE_OPERATOR
extern char             mocha_delete[];
#endif
extern char             mocha_typeof[];
extern char             mocha_void[];
extern char             mocha_null[];
extern char             mocha_this[];
extern char             mocha_false[];
extern char             mocha_true[];
extern MochaCodeSpec    mocha_CodeSpec[];
extern unsigned         mocha_NumCodeSpecs;

/*
** MochaPrinter operations, for printf style message formatting.  The return
** value from mocha_GetPrinterOutput() is the printer's output, in a malloc'd
** string that the caller must free.
*/
extern MochaPrinter *
mocha_NewPrinter(MochaContext *mc, const char *name, unsigned indent);

extern MochaBoolean
mocha_GetPrinterOutput(MochaPrinter *mp, char **sp);

extern void
mocha_DestroyPrinter(MochaPrinter *mp);

extern int
mocha_printf(MochaPrinter *mp, char *format, ...);

extern MochaBoolean
mocha_puts(MochaPrinter *mp, char *s);

#ifdef DEBUG
/*
** Disassemblers, for debugging only.
*/
#include <stdio.h>

extern void
mocha_Disassemble(MochaContext *mc, MochaScript *script, FILE *fp);

extern unsigned
mocha_Disassemble1(MochaContext *mc, MochaScript *script, MochaCode *pc,
		   unsigned loc, FILE *fp);
#endif /* DEBUG */

/*
** Decompilers, for function pretty-printing.
*/
extern MochaBoolean
mocha_DecompileScript(MochaScript *script, MochaPrinter *mp);

extern MochaBoolean
mocha_DecompileFunction(MochaFunction *fun, MochaPrinter *mp);

NSPR_END_EXTERN_C

#endif /* _mo_bcode_h_ */
