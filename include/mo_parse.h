#ifndef _mo_parse_h_
#define _mo_parse_h_
/*
** Mocha parser definitions.
** Brendan Eich, 6/14/95
*/
#include "prmacros.h"
#include "mo_prvtd.h"
#include "mo_pubtd.h"

NSPR_BEGIN_EXTERN_C

extern MochaBoolean
mocha_Parse(MochaContext *mc, MochaObject *slink, MochaTokenStream *ts,
	    CodeGenerator *cg);

extern MochaBoolean
mocha_ParseFunctionBody(MochaContext *mc, MochaTokenStream *ts,
			MochaFunction *fun);

NSPR_END_EXTERN_C

#endif /* _mo_parse_h_ */
