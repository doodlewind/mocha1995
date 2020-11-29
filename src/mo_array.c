/*
** Mocha array class.
**
** Brendan Eich, 11/15/95
*/
#include <stdlib.h>
#include <string.h>
#include "prprf.h"
#include "mo_cntxt.h"
#include "mo_scope.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

enum array_slot {
    ARRAY_LENGTH = -1
};

static MochaPropertySpec array_props[] = {
    {"length",	ARRAY_LENGTH},
    {0}
};

static MochaBoolean
array_get_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		   MochaDatum *dp)
{
    switch (slot) {
      case ARRAY_LENGTH:
	MOCHA_INIT_DATUM(mc, dp, MOCHA_NUMBER, u.fval, obj->scope->freeslot);
	break;
      default:;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
array_set_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		   MochaDatum *dp)
{
    MochaSlot newlen, oldlen;

    switch (slot) {
      case ARRAY_LENGTH:
	if (dp->tag != MOCHA_NUMBER &&
	    !MOCHA_ConvertDatum(mc, *dp, MOCHA_NUMBER, dp)) {
	    return MOCHA_FALSE;
	}
	newlen = (MochaSlot)dp->u.fval;
	if (dp->u.fval != (MochaFloat)newlen || newlen < 0) {
	    MOCHA_ReportError(mc, "illegal array length %g", dp->u.fval);
	    return MOCHA_FALSE;
	}
	oldlen = obj->scope->freeslot;
	for (slot = newlen; slot < oldlen; slot++)
	    MOCHA_RemoveSlot(mc, obj, slot);
	obj->scope->freeslot = newlen;
	break;

      default:;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
array_convert(MochaContext *mc, MochaObject *obj, MochaTag tag, MochaDatum *dp)
{
    switch (tag) {
      case MOCHA_NUMBER:
	MOCHA_INIT_DATUM(mc, dp, MOCHA_NUMBER, u.fval, obj->scope->freeslot);
	return MOCHA_TRUE;
      case MOCHA_BOOLEAN:
	MOCHA_INIT_DATUM(mc, dp, MOCHA_BOOLEAN,
			 u.bval, obj->scope->freeslot != 0);
	return MOCHA_TRUE;
      default:
	return MOCHA_TRUE;
    }
}

static MochaClass array_class = {
    "Array",
    array_get_property, array_set_property, MOCHA_ListPropStub,
    MOCHA_ResolveStub, array_convert, MOCHA_FinalizeStub
};

static MochaBoolean
array_join_str(MochaContext *mc, MochaObject *obj, const char *separator,
	       MochaDatum *rval)
{
    char *last;
    MochaSlot slot;
    MochaDatum d;
    uint16 taint;
    MochaAtom *atom;

    last = 0;
    taint = MOCHA_TAINT_IDENTITY;
    for (slot = 0; slot < obj->scope->freeslot; slot++) {
	if (!MOCHA_GetSlot(mc, obj, slot, &d))
	    return MOCHA_FALSE;
	if (MOCHA_DATUM_IS_NULL(d)) {
	    atom = mocha_HoldAtom(mc, MOCHA_empty.u.atom);
	} else {
	    if (!mocha_RawDatumToString(mc, d, &atom))
		return MOCHA_FALSE;
	}
	last = PR_sprintf_append(last, "%s%s",
				 (slot == 0) ? "" : separator,
				 atom_name(atom));
	mocha_DropAtom(mc, atom);
	if (!last) {
	    MOCHA_ReportOutOfMemory(mc);
	    return MOCHA_FALSE;
	}
	MOCHA_MIX_TAINT(mc, taint, d.taint);
    }
    if (!last) {
	*rval = MOCHA_empty;
	return MOCHA_TRUE;
    }
    atom = mocha_Atomize(mc, last, ATOM_STRING);
    free(last);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_FULL_DATUM(mc, rval, MOCHA_STRING, 0, taint, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
array_to_string(MochaContext *mc, MochaObject *obj,
		unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    return array_join_str(mc, obj, ",", rval);
}

static MochaBoolean
array_join(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;
    MochaBoolean ok;

    if (argc == 0)
	return array_to_string(mc, obj, argc, argv, rval);
    if (!mocha_DatumToString(mc, argv[0], &atom))
	return MOCHA_FALSE;
    ok = array_join_str(mc, obj, atom_name(atom), rval);
    mocha_DropAtom(mc, atom);
    return ok;
}

static MochaBoolean
InitArrayObject(MochaContext *mc, MochaObject *obj, unsigned length,
		MochaDatum *base)
{
    MochaSlot slot;
    MochaDatum d;

    for (slot = 0; (unsigned)slot < length; slot++) {
	d = base ? base[slot] : MOCHA_null;
	d.flags |= MDF_ENUMERATE;
	if (!MOCHA_SetSlot(mc, obj, slot, d))
	    return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
array_reverse(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    size_t len, i;
    MochaDatum *vec, *dp;
    MochaProperty *prop;

    len = (size_t)obj->scope->freeslot;
    vec = MOCHA_malloc(mc, len * sizeof *vec);
    if (!vec)
	return MOCHA_FALSE;
    memset(vec, 0, len * sizeof *vec);
    for (prop = obj->scope->props; prop; prop = prop->next) {
	if (prop->slot >= 0) {
	    dp = &vec[len - prop->slot - 1];
	    *dp = prop->datum;
	    mocha_HoldRef(mc, dp);
	}
    }
    InitArrayObject(mc, obj, len, vec);
    for (i = 0; i < len; i++)
	mocha_DropRef(mc, &vec[i]);
    MOCHA_free(mc, vec);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, obj);
    return MOCHA_TRUE;
}

/* XXX begin move me to prqsort.h */
typedef int (*PRComparator)(const void *a, const void *b, void *arg);

extern PRBool
PR_qsort(void *vec, size_t nel, size_t elsize, PRComparator cmp, void *arg);
/* XXX end move me to prqsort.h */

/* XXX begin move me to prqsort.c */
typedef struct QSortArgs {
    void         *vec;
    size_t       elsize;
    void         *pivot;
    PRComparator cmp;
    void         *arg;
} QSortArgs;

static void
pr_qsort_r(QSortArgs *qa, int lo, int hi)
{
    void *pivot, *a, *b;
    int i, j;

    pivot = qa->pivot;
    while (lo < hi) {
	i = lo;
	j = hi;
	a = (char *)qa->vec + i * qa->elsize;
	memmove(pivot, a, qa->elsize);
	while (i < j) {
	    for (;;) {
		b = (char *)qa->vec + j * qa->elsize;
		if ((*qa->cmp)(b, pivot, qa->arg) <= 0)
		    break;
		j--;
	    }
	    memmove(a, b, qa->elsize);
	    while (i < j && (*qa->cmp)(a, pivot, qa->arg) <= 0) {
		i++;
		a = (char *)qa->vec + i * qa->elsize;
	    }
	    memmove(b, a, qa->elsize);
	}
	memmove(a, pivot, qa->elsize);
	if (i - lo < hi - i) {
	    pr_qsort_r(qa, lo, i - 1);
	    lo = i + 1;
	} else {
	    pr_qsort_r(qa, i + 1, hi);
	    hi = i - 1;
	}
    }
}

PRBool
PR_qsort(void *vec, size_t nel, size_t elsize, PRComparator cmp, void *arg)
{
    void *pivot;
    QSortArgs qa;

    pivot = malloc(elsize);
    if (!pivot)
	return PR_FALSE;
    qa.vec = vec;
    qa.elsize = elsize;
    qa.pivot = pivot;
    qa.cmp = cmp;
    qa.arg = arg;
    pr_qsort_r(&qa, 0, (int)(nel - 1));
    free(pivot);
    return PR_TRUE;
}
/* XXX end move me to prqsort.c */

typedef struct CompareArgs {
    MochaContext  *context;
    MochaFunction *fun;
    MochaBoolean  status;
} CompareArgs;

static int
sort_compare(const void *a, const void *b, void *arg)
{
    const MochaDatum *adp = a, *bdp = b;
    CompareArgs *ca = arg;
    MochaContext *mc = ca->context;
    MochaFloat fval = -1;
    MochaDatum fd, argv[2], rval;

    if (!ca->fun) {
	MochaAtom *aatom = 0, *batom = 0;

	if (mocha_RawDatumToString(mc, *adp, &aatom) &&
	    mocha_RawDatumToString(mc, *bdp, &batom)) {
	    fval = strcoll(atom_name(aatom), atom_name(batom));
	}
	if (aatom) mocha_DropAtom(mc, aatom);
	if (batom) mocha_DropAtom(mc, batom);
    } else {
	MOCHA_INIT_FULL_DATUM(mc, &fd, MOCHA_FUNCTION, 0,
			      MOCHA_TAINT_IDENTITY, u.fun, ca->fun);
	argv[0] = *adp;
	argv[1] = *bdp;
	ca->status = mocha_Call(ca->context, fd, 2, argv, &rval);
	if (ca->status) {
	    ca->status = mocha_DatumToNumber(mc, rval, &fval);
	    mocha_DropRef(mc, &rval);
	}
    }
    return (int)fval;
}

static MochaBoolean
array_sort(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFunction *fun;
    size_t len, i;
    MochaDatum *vec;
    MochaProperty *prop;
    CompareArgs ca;

    fun = 0;
    if (argc > 0 && !MOCHA_DatumToFunction(mc, argv[0], &fun))
	return MOCHA_FALSE;

    len = (size_t)obj->scope->freeslot;
    vec = MOCHA_malloc(mc, len * sizeof *vec);
    if (!vec) {
	MOCHA_DropObject(mc, &fun->object);
	return MOCHA_FALSE;
    }
    memset(vec, 0, len * sizeof *vec);
    for (prop = obj->scope->props; prop; prop = prop->next) {
	if (prop->slot >= 0) {
	    vec[prop->slot] = prop->datum;
	    mocha_HoldRef(mc, &vec[prop->slot]);
	}
    }

    ca.context = mc;
    ca.fun = fun;
    ca.status = MOCHA_TRUE;
    if (!PR_qsort(vec, len, sizeof *vec, sort_compare, &ca))
	ca.status = MOCHA_FALSE;
    MOCHA_DropObject(mc, &fun->object);
    if (ca.status)
	InitArrayObject(mc, obj, len, vec);
    for (i = 0; i < len; i++)
	mocha_DropRef(mc, &vec[i]);
    MOCHA_free(mc, vec);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, obj);
    return ca.status;
}

static MochaFunctionSpec array_methods[] = {
    {mocha_toStringStr,         array_to_string,        0},
    {"join",                    array_join,             1},
    {"reverse",                 array_reverse,          0},
    {"sort",                    array_sort,             1},
    {0}
};

static MochaBoolean
Array(MochaContext *mc, MochaObject *obj,
      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    unsigned length;
    MochaDatum *base;

    if (argc == 0) {
	length = 0;
	base = 0;
    } else if (argc == 1 && argv[0].tag == MOCHA_NUMBER) {
	length = (unsigned) argv[0].u.fval;
	base = 0;
    } else {
	length = argc;
	base = argv;
    }
    if (!InitArrayObject(mc, obj, length, base))
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, obj);
    return MOCHA_TRUE;
}

MochaObject *
mocha_InitArrayClass(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_InitClass(mc, obj, &array_class, 0, Array, 1,
			   array_props, array_methods, 0, 0);
}

MochaObject *
mocha_NewArrayObject(MochaContext *mc, unsigned length, MochaDatum *base)
{
    MochaObject *obj;

    obj = mocha_NewObjectByClass(mc, &array_class);
    if (!obj)
	return 0;
    if (!InitArrayObject(mc, obj, length, base)) {
	mocha_DestroyObject(mc, obj);
	return 0;
    }
    return obj;
}
