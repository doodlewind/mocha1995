/*
** Mocha string methods.
**
** Brendan Eich, 10/20/95
*/
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "prmem.h"
#include "prprf.h"
#include "alloca.h"
#include "mo_cntxt.h"
#include "mochaapi.h"
#include "mochalib.h"

MochaBoolean
mocha_RawDatumToString(MochaContext *mc, MochaDatum d, MochaAtom **atomp)
{
    char buf[32];
    char *str, *path;
    const char *name;
    MochaObject *obj;
    MochaAtom *atom;

    switch (d.tag) {
      default:
	*atomp = mocha_HoldAtom(mc, mocha_typeAtoms[MOCHA_UNDEF]);
	break;

      case MOCHA_INTERNAL:
	PR_snprintf(buf, sizeof buf, "[internal %p]", d.u.ptr);
	atom = mocha_Atomize(mc, buf, ATOM_HELD | ATOM_STRING);
	if (!atom)
	    return MOCHA_FALSE;
	*atomp = atom;
	break;

      case MOCHA_ATOM:
	*atomp = mocha_HoldAtom(mc, d.u.atom);
	break;

      case MOCHA_SYMBOL:
	atom = sym_atom(d.u.pair.sym);
	str = (char *)atom_name(atom);
	if (!isalpha(*str) && *str != '_')
	    path = PR_smprintf("[%s]", str);
	else
	    path = strdup(str);
	for (obj = d.u.pair.obj;
	     path && obj && obj != mc->staticLink;
	     obj = obj->parent) {
	    name = obj->clazz->name;
	    if (!isalpha(*name) && *name != '_')
		str = PR_smprintf("[%s]", name);
	    else
		str = PR_smprintf("%s", name);
	    str = PR_sprintf_append(str, "%s%s",
				    (*path == '[') ? "" : ".",	/* balance] */
				    path);
	    free(path);
	    path = str;
	}
	if (path && *path == '[' && mc->staticLink) {	/* balance] */
	    str = PR_smprintf("%s%s", mc->staticLink->clazz->name, path);
	    free(path);
	    path = str;
	}
	if (!path) {
	    MOCHA_ReportOutOfMemory(mc);
	    return MOCHA_FALSE;
	}
	atom = mocha_Atomize(mc, path, ATOM_STRING);
	PR_FREEIF(path);
	if (!atom)
	    return MOCHA_FALSE;
	*atomp = mocha_HoldAtom(mc, atom);
	break;

      case MOCHA_FUNCTION:
	return mocha_FunctionToString(mc, d.u.fun, atomp);

      case MOCHA_OBJECT:
	return mocha_ObjectToString(mc, d.u.obj, atomp);

      case MOCHA_NUMBER:
	return mocha_NumberToString(mc, d.u.fval, atomp);

      case MOCHA_BOOLEAN:
	return mocha_BooleanToString(mc, d.u.bval, atomp);

      case MOCHA_STRING:
	*atomp = mocha_HoldAtom(mc, d.u.atom);
	/* FALL THROUGH */
    }
    return MOCHA_TRUE;
}

MochaBoolean
mocha_DatumToString(MochaContext *mc, MochaDatum d, MochaAtom **atomp)
{
    if (!mocha_ResolveValue(mc, &d))
	return MOCHA_FALSE;
    return mocha_RawDatumToString(mc, d, atomp);
}

/*
** String object ops.
*/
enum string_slot { STRING_LENGTH = -1 };

static MochaPropertySpec string_props[] = {
    {"length",		STRING_LENGTH},
    {0}
};

static MochaBoolean
str_get_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		 MochaDatum *dp)
{
    MochaAtom *atom;

    atom = obj->data;
    switch (slot) {
      case STRING_LENGTH:
	MOCHA_INIT_DATUM(mc, dp, MOCHA_NUMBER, u.fval, atom->length);
	break;
      default:;
    }
    return MOCHA_TRUE;
}

static void
str_finalize(MochaContext *mc, MochaObject *obj)
{
    MochaAtom *atom;

    atom = obj->data;
    mocha_DropAtom(mc, atom);
}

static MochaClass string_class = {
    "String",
    str_get_property, MOCHA_PropertyStub, MOCHA_ListPropStub,
    MOCHA_ResolveStub, MOCHA_ConvertStub, str_finalize
};

/*
** Java-like string native methods.
*/
static MochaBoolean
str_substring(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;
    const char *str;
    char *sub;
    int len, begin, end;

    if (!MOCHA_InstanceOf(mc, obj, &string_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;

    str = atom_name(atom);
    if (argc != 0) {
	len = atom->length;
	begin = (int) argv[0].u.fval;
	if (begin < 0) begin = 0;
	if (begin > len) begin = len;

	if (argc == 1) {
	    end = len;
	} else {
	    end = (int) argv[1].u.fval;
	    if (end > len) end = len;
	    if (end < begin) {
		/* Emulate java.lang.String.substring(). */
		int tmp = begin;
		begin = end;
		end = tmp;
	    }
	}

	len = end - begin;
	sub = (char *)alloca(len + 1);
	strncpy(sub, str + begin, len);
	sub[len] = '\0';
	atom = mocha_Atomize(mc, sub, ATOM_STRING);
	if (!atom)
	    return MOCHA_FALSE;
    }
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_value_of(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    if (!MOCHA_InstanceOf(mc, obj, &string_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_to_lowercase(MochaContext *mc, MochaObject *obj,
		 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;
    char *str, *str1;
    const char *str2;

    if (!MOCHA_InstanceOf(mc, obj, &string_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    str = (char *)alloca(atom->length + 1);
    for (str1 = str, str2 = atom_name(atom); (*str1 = tolower(*str2)) != '\0';
	 str1++, str2++)
	;
    atom = mocha_Atomize(mc, str, ATOM_STRING);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_to_uppercase(MochaContext *mc, MochaObject *obj,
		 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;
    char *str, *str1;
    const char *str2;

    if (!MOCHA_InstanceOf(mc, obj, &string_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    str = (char *)alloca(atom->length + 1);
    for (str1 = str, str2 = atom_name(atom); (*str1 = toupper(*str2)) != '\0';
	 str1++, str2++)
	;
    atom = mocha_Atomize(mc, str, ATOM_STRING);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_char_at(MochaContext *mc, MochaObject *obj,
	    unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;
    MochaFloat fval;
    int index;
    const char *str;
    char buf[2];

    if (!MOCHA_InstanceOf(mc, obj, &string_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    if (!mocha_DatumToNumber(mc, argv[0], &fval))
	return MOCHA_FALSE;

    index = (int)fval;
    if (index >= atom->length) {
	*rval = MOCHA_empty;
    } else {
	str = atom_name(atom);
	buf[0] = str[index];
	buf[1] = '\0';
	atom = mocha_Atomize(mc, buf, ATOM_STRING);
	if (!atom)
	    return MOCHA_FALSE;
	MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    }
    return MOCHA_TRUE;
}

static MochaBoolean
str_index_of(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom, *atom2;
    const char *str, *str1, *str2;
    MochaFloat fval;
    int index;

    if (!MOCHA_InstanceOf(mc, obj, &string_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    if (argc > 1) {
	if (!mocha_DatumToNumber(mc, argv[1], &fval))
	    return MOCHA_FALSE;
	index = (int)fval;
    } else {
	index = 0;
    }
    if (index >= atom->length) {
	MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, -1);
    } else {
	if (!mocha_DatumToString(mc, argv[0], &atom2))
	    return MOCHA_FALSE;
	str1 = atom_name(atom);
	str2 = atom_name(atom2);
	str = strstr(str1 + index, str2);
	index = str ? str - str1 : -1;
	mocha_DropAtom(mc, atom2);
	MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, index);
    }
    return MOCHA_TRUE;
}

static MochaBoolean
str_last_index_of(MochaContext *mc, MochaObject *obj,
		  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom, *atom2;
    const char *str, *str2;
    MochaFloat fval;
    int from, len2, i;

    if (!MOCHA_InstanceOf(mc, obj, &string_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    if (argc > 1) {
	if (!mocha_DatumToNumber(mc, argv[1], &fval)) {
	    return MOCHA_FALSE;
	}
	from = (int)fval;
    } else {
	from = atom->length - 1;
    }
    if (!mocha_DatumToString(mc, argv[0], &atom2))
	return MOCHA_FALSE;
    str = atom_name(atom);
    str2 = atom_name(atom2);
    len2 = strlen(str2);
    for (i = (from >= atom->length) ? atom->length - 1 : from; i >= 0; i--) {
	if (strncmp(str + i, str2, len2) == 0)
	    break;
    }
    mocha_DropAtom(mc, atom2);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, i);
    return MOCHA_TRUE;
}

/*
** Perl-inspired string functions.
*/
static MochaBoolean
str_chop(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    return MOCHA_TRUE;
}

static MochaBoolean
str_match(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    return MOCHA_TRUE;
}

static MochaBoolean
str_ord(MochaContext *mc, MochaObject *obj,
	unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    return MOCHA_TRUE;
}

static MochaBoolean
str_split(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom, *arg;
    unsigned len, i;
    MochaDatum *vec, d;
    char *str, *tok, *end, save;
    const char *sep;
    MochaObject *aobj;

    atom = obj->data;
    if (argc == 0) {
	MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_STRING,
			      0, MOCHA_TAINT_IDENTITY,
			      u.atom, mocha_HoldAtom(mc, atom));
	len = 1;
	vec = &d;
    } else {
	str = MOCHA_strdup(mc, atom_name(atom));
	if (!str)
	    return MOCHA_FALSE;
	if (!mocha_DatumToString(mc, argv[0], &arg)) {
	    MOCHA_free(mc, str);
	    return MOCHA_FALSE;
	}
	sep = atom_name(arg);
	len = 1;
#define STRSTR(tok,sep)	(*(sep) ? strstr(tok, sep) : (tok[1] ? tok + 1 : 0))
	for (tok = str; (tok = STRSTR(tok, sep)) != 0; tok += arg->length)
	    len++;
	vec = MOCHA_malloc(mc, len * sizeof *vec);
	if (!vec) {
	    MOCHA_free(mc, str);
	    mocha_DropAtom(mc, arg);
	    return MOCHA_FALSE;
	}
	len = 0;
	for (tok = str; ; tok = end + arg->length) {
	    end = STRSTR(tok, sep);
	    if (end) {
		save = *end;
		*end = '\0';
	    }
	    atom = mocha_Atomize(mc, tok, ATOM_HELD | ATOM_STRING);
	    if (!atom)
		break;
	    MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_STRING,
				  0, MOCHA_TAINT_IDENTITY,
				  u.atom, atom);
	    vec[len++] = d;
	    if (!end) break;
	    *end = save;
	}
#undef STRSTR
	MOCHA_free(mc, str);
	mocha_DropAtom(mc, arg);
	if (!atom) {
	    for (i = 0; i < len; i++)
		mocha_DropRef(mc, &vec[i]);
	    return MOCHA_FALSE;
	}
    }
    aobj = mocha_NewArrayObject(mc, len, vec);
    for (i = 0; i < len; i++)
	mocha_DropRef(mc, &vec[i]);
    if (argc != 0)
	MOCHA_free(mc, vec);
    if (!aobj)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, aobj);
    return MOCHA_TRUE;
}

static MochaBoolean
str_substr(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    return MOCHA_TRUE;
}

static MochaBoolean
str_unpack(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    return MOCHA_TRUE;
}

/*
** HTML composition aids.
*/
static MochaAtom *
tagify(MochaContext *mc, MochaObject *obj, MochaDatum *argv,
       const char *begin, const char *param, const char *end)
{
    MochaAtom *atom;
    const char *markup, *tagbuf;

    if (!MOCHA_InstanceOf(mc, obj, &string_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;

    if (!end) end = begin;
    if (param) {
	markup = PR_smprintf("%s=\"%s\"", begin, param);
	if (!markup) {
	    MOCHA_ReportOutOfMemory(mc);
	    return 0;
	}
	begin = markup;
    }
    tagbuf = PR_smprintf("<%s>%s</%s>", begin, atom_name(atom), end);
    if (param)
	free((char *)markup);
    if (!tagbuf) {
	MOCHA_ReportOutOfMemory(mc);
	return 0;
    }
    atom = mocha_Atomize(mc, tagbuf, ATOM_STRING);
    free((char *)tagbuf);
    return atom;
}

static MochaAtom *
tagify_datum(MochaContext *mc, MochaObject *obj, MochaDatum *argv,
	     const char *begin, const char *end)
{
    MochaAtom *atom, *param;

    if (!mocha_DatumToString(mc, argv[0], &param))
	return 0;
    atom = tagify(mc, obj, argv, begin, atom_name(param), end);
    mocha_DropAtom(mc, param);
    return atom;
}

static MochaBoolean
str_bold(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify(mc, obj, argv, "B", 0, 0);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_italics(MochaContext *mc, MochaObject *obj,
	    unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify(mc, obj, argv, "I", 0, 0);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_fixed(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify(mc, obj, argv, "TT", 0, 0);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_fontsize(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify_datum(mc, obj, argv, "FONT SIZE", "FONT");
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_fontcolor(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify_datum(mc, obj, argv, "FONT COLOR", "FONT");
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_link(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify_datum(mc, obj, argv, "A HREF", "A");
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_anchor(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify_datum(mc, obj, argv, "A NAME", "A");
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_strike(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify(mc, obj, argv, "STRIKE", 0, 0);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_small(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify(mc, obj, argv, "SMALL", 0, 0);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_big(MochaContext *mc, MochaObject *obj,
	unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify(mc, obj, argv, "BIG", 0, 0);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_blink(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify(mc, obj, argv, "BLINK", 0, 0);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_sup(MochaContext *mc, MochaObject *obj,
	unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify(mc, obj, argv, "SUP", 0, 0);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
str_sub(MochaContext *mc, MochaObject *obj,
	unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    atom = tagify(mc, obj, argv, "SUB", 0, 0);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaFunctionSpec string_methods[] = {
    /* Java-like methods. */
    {mocha_toStringStr,	str_substring,		0},
    {mocha_valueOfStr,	str_value_of,		0},
    {"substring",	str_substring,		2},
    {"toLowerCase",	str_to_lowercase,	0},
    {"toUpperCase",	str_to_uppercase,	0},
    {"charAt",		str_char_at,		1},
    {"indexOf",		str_index_of,		2},
    {"lastIndexOf",	str_last_index_of,	2},

    /* Perl-ish methods. */
    {"chop",            str_chop,               1},
    {"match",		str_match,		1},
    {"ord",		str_ord,		0},
    {"split",           str_split,              1},
    {"substr",		str_substr,		2},
    {"unpack",		str_unpack,		1},

    /* HTML string methods. */
    {"bold",		str_bold,		0},
    {"italics",		str_italics,		0},
    {"fixed",		str_fixed,		0},
    {"fontsize",	str_fontsize,		1},
    {"fontcolor",	str_fontcolor,		1},
    {"link",		str_link,		1},
    {"anchor",		str_anchor,		1},
    {"strike",		str_strike,		0},
    {"small",		str_small,		0},
    {"big",		str_big,		0},
    {"blink",		str_blink,		0},
    {"sup",		str_sup,		0},
    {"sub",		str_sub,		0},
    {0}
};

static MochaBoolean
String(MochaContext *mc, MochaObject *obj,
       unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    if (argc > 0) {
	if (!mocha_DatumToString(mc, argv[0], &atom))
	    return MOCHA_FALSE;
    } else {
	atom = mocha_HoldAtom(mc, MOCHA_empty.u.atom);
    }
    if (obj->clazz != &string_class) {
	MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
	return MOCHA_TRUE;
    }
    obj->data = atom;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, obj);
    return MOCHA_TRUE;
}

MochaObject *
mocha_InitStringClass(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_InitClass(mc, obj, &string_class, 0, String, 1,
			   string_props, string_methods, 0, 0);
}

MochaObject *
mocha_StringToObject(MochaContext *mc, MochaAtom *atom)
{
    MochaObject *obj;

    obj = mocha_NewObjectByClass(mc, &string_class);
    if (!obj)
	return 0;
    obj->data = mocha_HoldAtom(mc, atom);
    return obj;
}
