/*
** Mocha shell.
**
** Brendan Eich, 6/20/95
*/
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prarena.h"
#include "prlog.h"
#include "prprf.h"
#include "mo_atom.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mo_parse.h"
#include "mo_scan.h"
#include "mo_scope.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

#ifndef MOCHAFILE
# error "MOCHAFILE must be defined for this module to work."
#endif

extern MochaAtomState mocha_AtomState;

static void
Process(MochaContext *mc, MochaObject *obj, char *filename)
{
    MochaTokenStream *ts;
    MochaScript script;		/* XXX we know all about this struct */
    CodeGenerator cg;
    MochaDatum result;
    MochaAtom *atom;

    script.filename = filename;
    if (filename && strcmp(filename, "-") != 0) {
	ts = mocha_NewFileTokenStream(mc, filename);
    } else {
	ts = mocha_NewBufferTokenStream(mc, 0, 0);
	if (ts) ts->file = stdin;
    }
    if (!ts) goto out;
    if (isatty(fileno(ts->file)))
	ts->flags |= (TSF_INTERACTIVE | TSF_COMMAND);

    mocha_InitCodeGenerator(mc, &cg, &mc->codePool);
    script.notes = 0;
    while (!(ts->flags & TSF_EOF)) {
	script.lineno = ts->lineno;
	if (ts->flags & TSF_INTERACTIVE)
	    printf("mocha> ");

	CG_RESET(&cg);
	if (!mocha_Parse(mc, obj, ts, &cg) || CG_OFFSET(&cg) == 0)
	    continue;

	script.code = cg.base;
	script.length = CG_OFFSET(&cg);
	script.depth = cg.maxStackDepth;
	if (mocha_InitAtomMap(mc, &script.atomMap, &cg)) {
	    script.notes = mocha_FinishTakingSourceNotes(mc, &cg);
	    if (script.notes) {
		if (MOCHA_ExecuteScript(mc, obj, &script, &result)) {
		    if ((ts->flags & TSF_INTERACTIVE) &&
			result.tag != MOCHA_UNDEF &&
			MOCHA_DatumToString(mc, result, &atom)) {
			printf("%s\n", atom_name(atom));
			mocha_DropAtom(mc, atom);
		    }
		    mocha_DropRef(mc, &result);
		}
		free(script.notes);
	    }
	    mocha_FreeAtomMap(mc, &script.atomMap);
	}
    }

    mocha_CloseTokenStream(ts);
out:
    PR_FreeArenaPool(&mc->codePool);
    PR_FreeArenaPool(&mc->tempPool);
}

static MochaBoolean
Load(MochaContext *mc, MochaObject *obj,
     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    unsigned i;
    MochaAtom *atom;
    const char *filename;
    MochaScript *script;
    MochaDatum result;

    for (i = 0; i < argc; i++) {
	if (!MOCHA_DatumToString(mc, argv[i], &atom))
	    return MOCHA_FALSE;
	filename = atom_name(atom);
	errno = 0;
	script = MOCHA_CompileFile(mc, obj, filename);
	mocha_DropAtom(mc, atom);
	if (!script) {
	    fprintf(stderr, "Mocha: cannot load %s", filename);
	    if (errno)
		fprintf(stderr, ": %s", strerror(errno));
	    putc('\n', stderr);
	    continue;
	}
	if (MOCHA_ExecuteScript(mc, obj, script, &result))
	    mocha_DropRef(mc, &result);
	MOCHA_DestroyScript(mc, script);
    }
    return MOCHA_TRUE;
}

static MochaBoolean
Print(MochaContext *mc, MochaObject *obj,
      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    unsigned i, n;
    MochaAtom *atom;

    for (i = n = 0; i < argc; i++) {
	if (!MOCHA_DatumToString(mc, argv[i], &atom))
	    return MOCHA_FALSE;
	printf("%s%s", i ? " " : "", atom_name(atom));
	mocha_DropAtom(mc, atom);
	n++;
    }
    if (n)
	putchar('\n');
    return MOCHA_TRUE;
}

#ifdef DEBUG

static char *sourceNoteName[] = {
    "null", "newline", "setline", "if", "if-else", "while", "for", "continue",
    "var", "comma", "assignop", "cond", "paren", "hidden"
};

static MochaBoolean
Disassemble(MochaContext *mc, MochaObject *obj,
	    unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    unsigned i, offset, delta;
    MochaAtom *atom;
    MochaFunction *fun;
    SourceNote *notes, *sn;
    SourceNoteType type;

    for (i = 0; i < argc; i++) {
	if (argv[i].tag != MOCHA_FUNCTION) {
	    if (!MOCHA_DatumToString(mc, argv[i], &atom))
		return MOCHA_FALSE;
	    fprintf(stderr, "Mocha: cannot disassemble %s\n", atom_name(atom));
	    mocha_DropAtom(mc, atom);
	    continue;
	}

	fun = argv[i].u.fun;
	mocha_Disassemble(mc, fun->script, stdout);
	notes = fun->script->notes;
	if (notes) {
	    printf("\nSource notes:\n");
	    offset = 0;
	    for (sn = notes; !SN_IS_TERMINATOR(sn); sn = SN_NEXT(sn)) {
		delta = SN_DELTA(sn);
		offset += delta;
		printf("%3u: %5u [%4u] %-8s",
		       sn - notes, offset, delta, sourceNoteName[SN_TYPE(sn)]);
		type = SN_TYPE(sn);
		if (type == SRC_SETLINE) {
		    printf(" lineno %u", SN_OFFSET(&sn[1]));
		} else if (type == SRC_FOR) {
		    printf(" cond %u update %u tail %u",
			   SN_OFFSET(&sn[1]), SN_OFFSET(&sn[2]),
			   SN_OFFSET(&sn[3]));
		}
		putchar('\n');
	    }
	}
    }
    return MOCHA_TRUE;
}

static MochaBoolean
Tracing(MochaContext *mc, MochaObject *obj,
	unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaBoolean bval;
    MochaAtom *atom;

    if (argc == 0) {
	MOCHA_INIT_DATUM(mc, rval, MOCHA_BOOLEAN, u.bval, mc->tracefp != 0);
	return MOCHA_TRUE;
    }

    switch (argv[0].tag) {
      case MOCHA_NUMBER:
	bval = argv[0].u.fval != 0;
	break;
      case MOCHA_BOOLEAN:
	bval = argv[0].u.bval;
	break;
      default:
	if (!MOCHA_DatumToString(mc, argv[0], &atom))
	    return MOCHA_FALSE;
	fprintf(stderr, "tracing: illegal argument %s\n", atom_name(atom));
	mocha_DropAtom(mc, atom);
	return MOCHA_TRUE;
    }
    mc->tracefp = bval ? stdout : 0;
    return MOCHA_TRUE;
}

static char *symbolTypeName[] = {
    "undef", "argument", "variable", "property", "constructor"
};

int
DumpAtom(PRHashEntry *he, int i, void *arg)
{
    FILE *fp = arg;
    MochaAtom *atom = (MochaAtom *)he;

    fprintf(fp, "%3d %08x %5u %5ld %.16g \"%s\"\n",
	    i, (unsigned)he->keyHash, atom->index, atom->number, atom->fval,
	    atom_name(atom));
    return HT_ENUMERATE_NEXT;
}

int
DumpSymbol(PRHashEntry *he, int i, void *arg)
{
    FILE *fp = arg;
    MochaSymbol *sym = (MochaSymbol *)he;
    MochaAtom *atom = sym_atom(sym);

    fprintf(fp, "%3d %08x %-8s \"%s\"\n",
	    i, (unsigned)he->keyHash, symbolTypeName[sym->type],
	    atom_name(atom));
    return HT_ENUMERATE_NEXT;
}

void
DumpScope(MochaScope *scope, PRHashEnumerator dump, FILE *fp)
{
    fprintf(fp, "\n%s scope contents:\n", scope->object->clazz->name);
    PR_HashTableDump(scope->table, dump, fp);
}

/* These are callable from gdb. */
void Dsym(MochaSymbol *sym) { if (sym) DumpSymbol(&sym->entry, 0, stderr); }
void Datom(MochaAtom *atom) { if (atom) DumpAtom(&atom->entry, 0, stderr); }

static MochaBoolean
DumpStats(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    unsigned i;
    MochaAtom *atom, *atom2;
    const char *which;
    MochaScope *scope;
    MochaPair pair;
    MochaObject *obj2;

    for (i = 0; i < argc; i++) {
	if (!MOCHA_DatumToString(mc, argv[i], &atom))
	    return MOCHA_FALSE;
	which = atom_name(atom);
	if (strcmp(which, "arena") == 0) {
#ifdef ARENAMETER
	    PR_DumpArenaStats(stdout);
#endif
	} else if (strcmp(which, "atom") == 0) {
	    printf("\natom table contents:\n");
	    PR_HashTableDump(mocha_AtomState.table, DumpAtom, stdout);
	} else if (strcmp(which, "global") == 0) {
	    for (scope = mc->staticLink->scope; scope->object->parent;
		 scope = scope->object->parent->scope)
		;
	    DumpScope(scope, DumpSymbol, stdout);
	} else {
	    atom2 = mocha_Atomize(mc, which, ATOM_STRING);
	    if (atom2) {
		if (!mocha_SearchScopes(mc, atom2, MLF_GET, &pair))
		    pair.sym = 0;
		mocha_DropAtom(mc, atom2);
		if (!pair.sym ||
		    pair.sym->type != SYM_PROPERTY ||
		    sym_datum(pair.sym)->tag != MOCHA_OBJECT) {
		    fprintf(stderr, "Mocha: invalid stats argument %s\n",
			    which);
		} else {
		    obj2 = sym_datum(pair.sym)->u.obj;
		    DumpScope(obj2->scope, DumpSymbol, stdout);
		}
	    }
	}
	mocha_DropAtom(mc, atom);
    }
    return MOCHA_TRUE;
}

#endif /* DEBUG */

static MochaBoolean
Help(MochaContext *mc, MochaObject *obj,
     unsigned argc, MochaDatum *argv, MochaDatum *rval);

static MochaBoolean
Quit(MochaContext *mc, MochaObject *obj,
     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    exit(0);
}

static MochaFunctionSpec shell_functions[] = {
    {"load",            Load,           1},
    {"print",           Print,          0},
    {"help",            Help,           0},
    {"quit",            Quit,           0},
#ifdef DEBUG
    {"dis",             Disassemble,    1},
    {"tracing",         Tracing,        0},
    {"stats",           DumpStats,      1},
#endif
    {0}
};

/* *** NOTE: These must be kept in sync with the above***  */

static char *shell_help_messages[] = {
    "Load files named by string arguments",
    "Evaluate and print expressions",
    "Display usage and help messages",
    "Quit mocha",
#ifdef DEBUG
    "Disassemble functions into bytecodes",
    "Turn tracing on or off",
    "Dump 'arena', 'atom', or 'global' stats",
#endif
    0
};

static void
ShowHelpHeader(void)
{
    printf("%-9.9s %s\n", "Command", "description");
    printf("%-9.9s %s\n", "=======", "===========");
}

static void
ShowHelpForCommand(unsigned n)
{
    printf("%-9.9s %s\n", shell_functions[n].name, shell_help_messages[n]);
}

static MochaBoolean
Help(MochaContext *mc, MochaObject *obj,
     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    unsigned i, j;
    int did_header, did_something;
    MochaAtom *atom;

    if (argc == 0) {
	ShowHelpHeader();
	for (i = 0; shell_functions[i].name; i++)
	    ShowHelpForCommand(i);
    } else {
	did_header = 0;
	for (i = 0; i < argc; i++) {
	    did_something = 0;
	    if (argv[i].tag == MOCHA_FUNCTION) {
		atom = mocha_HoldAtom(mc, argv[i].u.fun->atom);
	    } else if (argv[i].tag == MOCHA_STRING) {
		atom = mocha_HoldAtom(mc, argv[i].u.atom);
	    } else {
		atom = 0;
	    }
	    if (atom) {
		for (j = 0; shell_functions[j].name; j++) {
		    if (!strcmp(atom_name(atom), shell_functions[j].name)) {
			if (!did_header) {
			    did_header = 1;
			    ShowHelpHeader();
			}
			did_something = 1;
			ShowHelpForCommand(j);
			break;
		    }
		}
		mocha_DropAtom(mc, atom);
	    }
	    if (!did_something) {
		if (!MOCHA_DatumToString(mc, argv[i], &atom))
		    return MOCHA_FALSE;
		fprintf(stderr, "Sorry, no help for %s\n", atom_name(atom));
		mocha_DropAtom(mc, atom);
	    }
	}
    }
    return MOCHA_TRUE;
}

/*
** Define a Mocha object called "it".
*/
enum its_slot {
    ITS_COLOR, ITS_HEIGHT, ITS_WIDTH, ITS_FUNNY, ITS_ARRAY, ITS_MAX
};

static MochaPropertySpec its_props[] = {
    {"color",           ITS_COLOR},
    {"height",          ITS_HEIGHT},
    {"width",           ITS_WIDTH},
    {"funny",           ITS_FUNNY},
    {"array",           ITS_ARRAY},
    {0}
};

static MochaClass its_class = {
    "It",
    MOCHA_PropertyStub, MOCHA_PropertyStub, MOCHA_ListPropStub,
    MOCHA_ResolveStub,  MOCHA_ConvertStub, MOCHA_FinalizeStub
};

static void
my_ErrorReporter(MochaContext *mc, const char *message,
		 MochaErrorReport *report)
{
    int i, j, k, n;

    fputs("Mocha: ", stderr);
    if (!report) {
	fprintf(stderr, "%s\n", message);
	return;
    }

    if (report->filename)
	fprintf(stderr, "%s, ", report->filename);
    if (report->lineno)
	fprintf(stderr, "line %u: ", report->lineno);
    fputs(message, stderr);
    if (!report->linebuf) {
	putc('\n', stderr);
	return;
    }

    fprintf(stderr, ":\n%s\n", report->linebuf);
    n = report->tokenptr - report->linebuf;
    for (i = j = 0; i < n; i++) {
	if (report->linebuf[i] == '\t') {
	    for (k = (j + 8) & ~7; j < k; j++)
		putc('.', stderr);
	    continue;
	}
	putc('.', stderr);
	j++;
    }
    fputs("^\n", stderr);
}

static MochaClass global_class = {
    "global",
    MOCHA_PropertyStub, MOCHA_PropertyStub, MOCHA_ListPropStub,
    MOCHA_ResolveStub,  MOCHA_ConvertStub,  MOCHA_FinalizeStub
};

/* Stub to avoid linking with half the known universe. */
MochaBoolean
mocha_InitJava(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_TRUE;
}

/* Another stub to avoid linking with half the known universe. */
void
mocha_DestroyJavaContext(MochaContext *mc)
{
}

int
main(int argc, char **argv)
{
    MochaContext *mc;
    MochaObject *glob;
    int i;

    mc = MOCHA_NewContext(8192);
    if (!mc) return 1;

    glob = MOCHA_NewObject(mc, &global_class, 0, 0, 0, 0, 0);
    if (!glob) return 1;
    MOCHA_HoldObject(mc, glob);
    MOCHA_SetGlobalObject(mc, glob);

    MOCHA_SetErrorReporter(mc, my_ErrorReporter);
    MOCHA_DefineFunctions(mc, glob, shell_functions);
    MOCHA_DefineNewObject(mc, glob, "it", &its_class, 0, 0, 0, its_props, 0);

    if (argc > 1) {
	for (i = 1; i < argc; i++)
	    Process(mc, glob, argv[i]);
    } else {
	Process(mc, glob, 0);
    }

    MOCHA_DropObject(mc, glob);
    MOCHA_DestroyContext(mc);
    return 0;
}
