/*
** Mocha reflection of Java objects and vice versa
**
**   javapackage is a java namespace
**   java is a java class or object
**   javaarray is a java array
**   javaslot represents an object+name that may resolve to
**     either a field or a method depending on later usage.
**
**   netscape.javascript.JSObject is a java reflection of a mocha object
*/

/* XXX these entry points to java need to use mocha_CallJava
 *  which should set env->mochaContext to the current MochaContext.
 * makeJavaString?
 * allocCString?
 * exceptiondescribe?
 */

/* better exception handling needed in mocha<->java calls */

#ifdef JAVA

#include "mo_cntxt.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

/* java joy */
#include "oobj.h"
#include "interpreter.h"
#include "tree.h"
#include "opcodes.h"
#include "javaString.h"
#include "exceptions.h"
#include "jri.h"

/* libjava */
#include "lj.h"

/* nspr stuph */
#include "prgc.h"
#include "prhash.h"
#include "prmon.h"	/* for PR_XLock and PR_XUNlock */
#include "prlog.h"
#include "prprf.h"

#include "mo_java.h"

/*
** Those namespace terrorists up in Redmond are holding "exception" hostage
** Free it now !!!
*/
#if defined(exception)
#undef exception
#endif

/* JSObject generated header */
#define IMPLEMENT_netscape_javascript_JSObject
#include "netscape_javascript_JSObject.h"
#define IMPLEMENT_netscape_javascript_JSException
#ifdef XP_MAC
#include "n_javascript_JSException.h"
#else
#include "netscape_javascript_JSException.h"
#endif
#include "java_lang_Throwable.h"
#include "java_applet_Applet.h"

/*
 * types of reflected java objects
 */

/* a package is basically just a name, since the jdk doesn't
 * allow you to find out whether a particular name represents
 * a package or not */
typedef struct MochaJavaPackage MochaJavaPackage;

/* either a java class or a java object.
 *   class.field is a static field or method
 *   class(...) constructs an object which is an instance of the class
 *   object.field is a field or method */
typedef struct MochaJava MochaJava;

/* type associated with a MochaJava structure */
/* the MochaClass java_class uses OBJECT and CLASS
 * javaarray_class uses ARRAY */
typedef enum {
    JAVA_UNDEF,
    JAVA_OBJECT,
    JAVA_CLASS,
    JAVA_ARRAY
} MochaJavaType;

/* fields and methods are initially represented with a slot object:
 * this allows us to cope with fields and methods that have the same
 * name.  if the slot is used in a function context it will call the
 * appropriate method (dynamically choosing between overloaded methods).
 * if it is used in any other context it will convert itself to the
 * value of the field at the time it was looked up. */
typedef struct MochaJavaSlot MochaJavaSlot;

/*
 * globals for convenience, set up in the init routine
 */
static ClassClass * JSObjectClassBlock = 0;
static ClassClass * StringClassBlock = 0;
static ClassClass * BooleanClassBlock = 0;
static ClassClass * DoubleClassBlock = 0;
static ClassClass * ThrowableClassBlock = 0;
/*
 *  this is used to ensure that there is at most one reflection
 *  of any java object.  objects are keyed by handle, classes are
 *  keyed by classblock pointer.  the value for each is the mocha
 *  object reflection.  there is a root finder registered with the
 *  gc that marks all the java objects (keys); the mocha objects
 *  are not referenced and are responsible for removing themselves
 *  from the table upon finalization.
 */
static PRHashTable *javaReflections = NULL;
static PRMonitor *javaReflectionsMonitor = 0;

/*
 *  similarly for java reflections of mocha objects - in this case
 *  the keys are mocha objects.  when the corresponding JSObject is
 *  finalized the entry is removed from the table, and a reference
 *  to the mocha object is dropped.
 */
static PRHashTable *mochaReflections = NULL;
static PRMonitor *mochaReflectionsMonitor = 0;

PR_LOG_DEFINE(Moja);


/* lazy initialization */
static int mocha_java_initialized = 0;
static void mocha_FinishInitJava(MochaContext *mc);
#define FINISH_MOCHA_JAVA_INIT(mc)  \
    if (!mocha_java_initialized) {  \
	mocha_FinishInitJava(mc);   \
    }

/* check if a field is static/nonstatic */
#define CHECK_STATIC(isStatic, fb) (((fb)->access & ACC_STATIC) \
                                    ? (isStatic) : !(isStatic))

/* forward declarations */
static MochaBoolean
mocha_CallJava(MochaContext *mc, LJCallback doit, void *d,
               MochaBoolean pushSafeFrame);
static ClassClass *
mocha_FindJavaClass(MochaContext *mc, char *name, ClassClass *from);
static MochaBoolean
mocha_ExecuteJavaMethod(MochaContext *mc, void *raddr, size_t rsize,
                        HObject *ho, char *name, char *sig,
                        struct methodblock *mb, bool_t isStaticCall, ...);
static HObject *
mocha_ConstructJava(MochaContext *mc, char *name, ClassClass *cb,
                    char *sig, ...);
static HObject *
mocha_ConstructJavaPrivileged(MochaContext *mc, char *name, ClassClass *cb,
                              char *sig, ...);



static MochaObject *
mocha_reflectJavaSlot(MochaContext *mc, MochaObject *obj, MochaAtom *atom);
static MochaBoolean
mocha_javaMethodWrapper(MochaContext *mc, MochaObject *obj,
		     unsigned argc, MochaDatum *argv, MochaDatum *rval);
static MochaBoolean
mocha_javaConstructorWrapper(MochaContext *mc, MochaObject *obj,
			  unsigned argc, MochaDatum *argv,
			  MochaDatum *rval);

static MochaBoolean mocha_JArrayElementType(HObject *handle, char **sig,
                                            ClassClass **clazz);

static MochaBoolean
mocha_convertMDatumToJSObject(HObject **objp, MochaContext *mc,
                              MochaObject *mpo, ClassClass *paramcb,
                              MochaBoolean checkOnly);
static MochaBoolean
mocha_convertMDatumToJElement(MochaContext *mc, MochaDatum *dp,
                              char *addr, char *sig, ClassClass *fromClass,
                              char **sigRest);
static MochaBoolean
mocha_convertMDatumToJValue(MochaContext *mc, MochaDatum *dp,
			    OBJECT *addr, char *sig, ClassClass *fromClass,
                            MochaBoolean checkOnly, char **sigRestPtr,
                            int *cost);
static MochaBoolean
mocha_convertMDatumToJField(MochaContext *mc, MochaDatum *dp,
			 HObject *ho, struct fieldblock *fb);
static MochaBoolean
mocha_convertJElementToMDatum(MochaContext *mc, char *addr, char *sig,
			   MochaDatum *dp, MochaTag desired);
static MochaBoolean
mocha_convertJValueToMDatum(MochaContext *mc, OBJECT *addr, char *sig,
			 MochaDatum *dp, MochaTag desired);
static MochaBoolean
mocha_convertJFieldToMDatum(MochaContext *mc, HObject *ho, struct fieldblock *fb,
			 MochaDatum *dp, MochaTag desired);
static MochaBoolean
mocha_convertJObjectToMString(MochaContext *mc, HObject *ho,
                               bool_t isClass, MochaDatum *dp);
static MochaBoolean
mocha_convertJObjectToMNumber(MochaContext *mc, HObject *ho,
                               bool_t isClass, MochaDatum *dp);
static MochaBoolean
mocha_convertJObjectToMBoolean(MochaContext *mc, HObject *ho,
                               bool_t isClass, MochaDatum *dp);


static struct methodblock *
mocha_findSafeMethod(MochaContext *mc);
static MochaBoolean
mocha_pushSafeFrame(MochaContext *mc, ExecEnv *ee);
static void
mocha_popSafeFrame(MochaContext *mc, ExecEnv *ee);

/* handy macro from agent.c */
#define obj_getoffset(o, off) (*(OBJECT *)((char *)unhand(o)+(off)))



/****	****	****	****	****	****	****	****	****
 *
 *   java packages are just strings
 *
 ****	****	****	****	****	****	****	****	****/

struct MochaJavaPackage {
    int lastslot;		/* current slot index */
    char *name;			/* e.g. "java/lang" or 0 if it's the top level */
};

/* javapackage uses standard get_property */

static MochaBoolean
javapackage_set_property(MochaContext *mc, MochaObject *obj,
			 MochaSlot slot, MochaDatum *dp)
{
    MochaJavaPackage *package = obj->data;
    MOCHA_ReportError(mc, "%s doesn't refer to any Java value",
		      package->name);
    return MOCHA_FALSE;
}

static MochaBoolean
javapackage_list_properties(MochaContext *mc, MochaObject *obj)
{
    /* XXX can't do this without reading directories... */
    return MOCHA_TRUE;
}

/* forward declaration */
static MochaBoolean
javapackage_resolve_name(MochaContext *mc, MochaObject *obj, const char *name);

static MochaBoolean
javapackage_convert(MochaContext *mc, MochaObject *obj,
                    MochaTag tag, MochaDatum *dp)
{
    MochaJavaPackage *package = obj->data;
    MochaAtom *atom;
    char *str, *cp;

    switch(tag) {
    case MOCHA_STRING:
        /* convert '/' to '.' so it looks like the entry syntax */
	if (!package->name)
	    break;
	str = PR_smprintf("[JavaPackage %s]", package->name);
	if (!str) {
	    MOCHA_ReportOutOfMemory(mc);
	    return MOCHA_FALSE;
	}
	for (cp = str; *cp != '\0'; cp++)
	    if (*cp == '/')
		*cp = '.';
        atom = MOCHA_Atomize(mc, str);
        if (!atom) {
	    MOCHA_ReportOutOfMemory(mc);
            return MOCHA_FALSE;
        }

        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_STRING,
			      MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.atom, atom);
	free(str);
        break;
    case MOCHA_OBJECT:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_OBJECT,
			      MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.obj, obj);
        break;
    default:
	MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_UNDEF,
			      MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.ptr, 0);
        break;
    }
    return MOCHA_TRUE;
}

static void
javapackage_finalize(MochaContext *mc, MochaObject *obj)
{
    MochaJavaPackage *package = obj->data;

    /* get rid of the private data */
    if (package->name)
	free(package->name);
    free(package);
    obj->data = 0;
}

static MochaClass javapackage_class = {
    "JavaPackage",
    MOCHA_PropertyStub, javapackage_set_property, javapackage_list_properties,
    javapackage_resolve_name, javapackage_convert, javapackage_finalize
};

/* needs pointer to javapackage_class */
static MochaBoolean
javapackage_resolve_name(MochaContext *mc, MochaObject *obj, const char *name)
{
    MochaJavaPackage *package = obj->data;
    char *fullname;
    int namelen = strlen(name);
    ClassClass *cb;
    MochaObject *mo;
    MochaDatum d;

    if (package->name) {
	int packagelen = strlen(package->name);
	fullname = (char*) malloc(packagelen + namelen + 2);
        if (!fullname) {
	    MOCHA_ReportOutOfMemory(mc);
            return MOCHA_FALSE;
        }
	strcpy(fullname, package->name);
	fullname[packagelen] = '/';
	strcpy(fullname + packagelen + 1, name);
	fullname[packagelen + namelen + 1] = '\0';
    } else {
	fullname = (char *) malloc(namelen + 1);
        if (!fullname) {
            MOCHA_ReportOutOfMemory(mc);
            return MOCHA_FALSE;
        }
	strcpy(fullname, name);
	fullname[namelen] = '\0';
    }

    PR_LOG(Moja, debug, ("looking for java class \"%s\"", fullname));

    /* see if the name is a class */
    cb = mocha_FindJavaClass(mc, fullname, 0);

    /* if not, it's a package */
    if (!cb) {
	MochaJavaPackage *newpackage = malloc(sizeof(MochaJavaPackage));
        if (!newpackage) {
            MOCHA_ReportOutOfMemory(mc);
            free(fullname);
            return MOCHA_FALSE;
        }
        PR_LOG(Moja, debug, ("creating package %s", fullname));
	newpackage->name = fullname;
	newpackage->lastslot = 0;
        mo = MOCHA_NewObject(mc, &javapackage_class, newpackage, 0, 0, 0, 0);
    } else {
	/* reflect the Class object */
	mo = MOCHA_ReflectJClassToMObject(mc, cb);
    }

    if (!mo) {
        /* XXX error message? */
        return MOCHA_FALSE;
    }
    MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_OBJECT,
			  MDF_READONLY | MDF_ENUMERATE | MDF_TAINTED,
			  MOCHA_TAINT_JAVA, u.obj, mo);
    return MOCHA_SetProperty(mc, obj, name, ++package->lastslot, d);
}

/****	****	****	****	****	****	****	****	****/

struct MochaJava {
    MochaJavaType       type;			/* object / array / class */
    HObject *           handle;                 /* handle to the java Object */
    ClassClass *        cb;                     /* classblock, or element
                                                 * classblock for array of
                                                 * object */
    char *		signature;		/* array element signature */
    MochaSlot		nextSlot;		/* next slot to assign */
};

/* look up the slot name by index - this is used to pass the
 * property name from java_resolve_name to java_get_property */
static MochaBoolean
java_slot_to_name(MochaContext *mc, MochaObject *obj, MochaSlot slot,
                  MochaAtom **atomp)
{
    char buf[20];
    MochaProperty *prop;
    MochaAtom *atom;
    MochaSymbol *sym;

    PR_snprintf(buf, sizeof(buf), "%d", slot);
    atom = mocha_Atomize(mc, buf, ATOM_NUMBER);
    if (!atom) {
        MOCHA_ReportOutOfMemory(mc);
        return MOCHA_FALSE;
    }
    atom->fval = slot;
    if (!mocha_LookupSymbol(mc, obj->scope, atom, MLF_GET, &sym)) {
        /* this should never happen, since resolve_name should have
         * added a property with this slot number already */
        PR_ASSERT(0);
        return MOCHA_FALSE;
    }
    PR_ASSERT(sym && sym->type == SYM_PROPERTY);
    prop = sym_property(sym);
    for (sym = prop->lastsym; sym; sym = sym->next) {
        if (sym_atom(sym) != atom) {
            /* must be a named property symbol */
            *atomp = sym_atom(sym);
            return MOCHA_TRUE;
        }
    }
    return MOCHA_FALSE;
}

static struct fieldblock *
java_lookup_field(MochaContext *mc, ClassClass *cb,
                  PRBool isStatic, const char *name)
{
    while (cb) {
        int i;
        for (i = 0; i < cb->fields_count; i++) {
            struct fieldblock *fb = cbFields(cb) + i;
            if (CHECK_STATIC(isStatic, fb)
                && !strcmp(fieldname(fb), name)) {
                return fb;
            }
        }
        /* check the parent */
        if (cbSuperclass(cb))
            cb = unhand(cbSuperclass(cb));
        else
            cb = 0;
    }

    return 0;
}

static MochaBoolean
java_get_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		 MochaDatum *dp)
{
    MochaAtom *atom;
    MochaObject *slotobj;

    /* XXX reflect a getter/setter pair as a property! */

    if (!java_slot_to_name(mc, obj, slot, &atom)) {
        PR_LOG(Moja, error, ("couldn't find a name for slot %d",
                             slot));
        return MOCHA_FALSE;
    }

    PR_LOG(Moja, debug, ("looked up slot \"%s\" with index %d",
                         MOCHA_GetAtomName(mc, atom), slot));

    slotobj = mocha_reflectJavaSlot(mc, obj, atom);
    if (!slotobj)
        return MOCHA_FALSE;
    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_OBJECT, MDF_ENUMERATE | MDF_TAINTED,
			  MOCHA_TAINT_JAVA, u.obj, slotobj);
    return MOCHA_TRUE;
}

static MochaBoolean
java_set_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		 MochaDatum *dp)
{
    MochaJava *java = obj->data;
    ClassClass *cb = java->cb;
    struct fieldblock *fb = 0;
    MochaAtom *atom;
    const char *name;

    if (!java_slot_to_name(mc, obj, slot, &atom)) {
        PR_LOG(Moja, error, ("couldn't find a name for slot %d",
                             slot));
        return MOCHA_FALSE;
    }

    name = MOCHA_GetAtomName(mc, atom);
    PR_LOG(Moja, debug, ("looked up slot \"%s\" with index %d",
                         name, slot));

    fb = java_lookup_field(mc, cb, java->type == JAVA_CLASS, name);

    if (!fb) {
	MOCHA_ReportError(mc, "no Java %sfield found with name %s",
                          (java->type == JAVA_CLASS ? "static " : ""),
                          name);
        return MOCHA_FALSE;
    }

    if (mocha_convertMDatumToJField(mc, dp, java->handle, fb))
        return MOCHA_TRUE;

    MOCHA_ReportError(mc, "can't set Java field %s", name);
    return MOCHA_FALSE;
}

static MochaBoolean
java_list_one_property(MochaContext *mc, MochaObject *obj,
		       struct fieldblock *fb)
{
    MochaJava *java = obj->data;
    MochaAtom *atom;
    MochaSymbol *sym;

    PR_ASSERT(java->type == JAVA_OBJECT || java->type == JAVA_CLASS);
    if (!(fb->access & ACC_PUBLIC) ||
	!CHECK_STATIC(java->type == JAVA_CLASS, fb)) {
	return MOCHA_TRUE;
    }

    atom = mocha_Atomize(mc, fieldname(fb), ATOM_HELD | ATOM_NAME);
    if (!atom) {
        MOCHA_ReportOutOfMemory(mc);
	return MOCHA_FALSE;
    }
    if (!mocha_LookupSymbol(mc, obj->scope, atom, MLF_GET, &sym)) {
	mocha_DropAtom(mc, atom);
	return MOCHA_FALSE;
    }
    if (!sym) {
	MochaSlot slot = java->nextSlot++;
	MochaDatum d = MOCHA_null;
	d.flags |= MDF_ENUMERATE;
	mocha_SetProperty(mc, obj->scope, atom, slot, d);
    }
    mocha_DropAtom(mc, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
java_list_properties(MochaContext *mc, MochaObject *obj)
{
    MochaJava *java = obj->data;
    ClassClass *cb = java->cb;

    while (cb) {
        int i;
        for (i = 0; i < cb->fields_count; i++) {
            struct fieldblock *fb = cbFields(cb) + i;
	    if (!java_list_one_property(mc, obj, fb))
		return MOCHA_FALSE;
        }
        for (i = cb->methods_count; i--;) {
            struct methodblock *mb = cbMethods(cb) + i;
            if (!java_list_one_property(mc, obj, &mb->fb))
		return MOCHA_FALSE;
	}
        /* check the parent */
        if (cbSuperclass(cb))
            cb = unhand(cbSuperclass(cb));
        else
            cb = 0;
    }
    return MOCHA_TRUE;
}

static MochaBoolean
java_resolve_name(MochaContext *mc, MochaObject *obj, const char *name)
{
    MochaJava *java = obj->data;
    MochaSlot slot;
    MochaDatum d;

    PR_LOG(Moja, debug, ("resolve_name(0x%x) (handle 0x%x)",
                         obj, java->handle));

    /* the slot number we choose for this property is unimportant:
     * when we arrive in java_get_property or java_set_property we
     * will look at the properties to convert it back into a name */
    slot = java->nextSlot++;
    PR_LOG(Moja, debug, ("field \"%s\" -> slot %d", name, slot));
    d = MOCHA_null;
    d.flags |= MDF_ENUMERATE;
    return MOCHA_SetProperty(mc, obj, name, slot, d);
}

static void
java_finalize(MochaContext *mc, MochaObject *obj)
{
    MochaJava *java = obj->data;

    /* remove it from the reflection table */
    if (java->type==JAVA_CLASS) {
        PR_LOG(Moja, debug, ("removing class 0x%x from table", java->cb));
        PR_EnterMonitor(javaReflectionsMonitor);
        PR_HashTableRemove(javaReflections, java->cb);
        PR_ExitMonitor(javaReflectionsMonitor);
    } else {
        PR_LOG(Moja, debug, ("removing handle 0x%x from table", java->handle));
        PR_EnterMonitor(javaReflectionsMonitor);
        PR_HashTableRemove(javaReflections, java->handle);
        PR_ExitMonitor(javaReflectionsMonitor);
    }

    /* get rid of the private data */
    free(java);
    obj->data = 0;
#ifdef DEBUG
    obj->data = (void*)0xdeadbeef;
#endif
}

static MochaBoolean
java_convert(MochaContext *mc, MochaObject *obj, MochaTag tag,
             MochaDatum *dp)
{
    MochaJava *java = obj->data;

    PR_LOG(Moja, debug, ("java_convert to %d", tag));

    switch (tag) {
    case MOCHA_OBJECT:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_OBJECT,
			      MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.obj, obj);
        break;
    case MOCHA_FUNCTION:
        /* only classes convert to functions (constructors) */
        if (java->type != JAVA_CLASS) {
            /* random java objects do not convert to functions */
            MOCHA_ReportError(mc, "can't convert Java object %s to function",
                              "XXX" /* XXX obj->scope.name */);
            return MOCHA_FALSE;
	} else {
            MochaFunction *fun;
            MochaAtom *atom;
            PR_LOG(Moja, debug, ("making a constructor\n"));
            atom = MOCHA_Atomize(mc, classname(java->cb));
            if (!atom) {
                MOCHA_ReportOutOfMemory(mc);
                return MOCHA_FALSE;
            }
            fun = mocha_NewFunction(mc, mocha_javaConstructorWrapper, 0,
                                    0, atom);
            /* XXX the private data for the function object is the
             * classblock: gc problem? */
            fun->object.data = java->cb;
            MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_FUNCTION,
				  MDF_TAINTED, MOCHA_TAINT_JAVA,
				  u.fun, fun);

        }
        break;
    case MOCHA_STRING:
        /* either pull out the string or call toString */
        return mocha_convertJObjectToMString(mc, java->handle,
                                             java->type==JAVA_CLASS, dp);
    case MOCHA_NUMBER:
        /* call doubleValue() */
        return mocha_convertJObjectToMNumber(mc, java->handle,
					     java->type==JAVA_CLASS, dp);
    case MOCHA_BOOLEAN:
        /* call booleanValue() */
        return mocha_convertJObjectToMBoolean(mc, java->handle,
                                              java->type==JAVA_CLASS, dp);
    default:
	MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_UNDEF,
			      MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.ptr, 0);
        break;
    }
    return MOCHA_TRUE;
}

static MochaClass java_class = {
    "Java",
    java_get_property, java_set_property, java_list_properties,
    java_resolve_name, java_convert, java_finalize
};

/****	****	****	****	****	****	****	****	****/

#define ARRAY_LENGTH_SLOT	-1

static MochaBoolean
javaarray_get_property(MochaContext *mc, MochaObject *obj,
		       MochaSlot slot, MochaDatum *dp)
{
    MochaJava *array = obj->data;
    HObject *ho = array->handle;

    if (slot == ARRAY_LENGTH_SLOT) {
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.fval, (MochaFloat) obj_length(ho));
	return MOCHA_TRUE;
    } else if (slot >= 0 && slot < obj_length(ho)) {
        int size = sizearray(obj_flags(ho), 1);
        char *addr = ((char*) unhand(ho)) + slot * size;

	if (!mocha_convertJElementToMDatum(mc, addr, array->signature,
                                           dp, MOCHA_UNDEF)) {
            MOCHA_ReportError(mc,
                      "can't convert Java array element to JavaScript datum");
            return MOCHA_FALSE;
        }
        return MOCHA_TRUE;
    }

    MOCHA_ReportError(mc, "Java array index %d out of range", slot);

    return MOCHA_FALSE;
}

static MochaBoolean
javaarray_set_property(MochaContext *mc, MochaObject *obj,
		       MochaSlot slot, MochaDatum *dp)
{
    MochaJava *array = obj->data;
    HObject *ho = array->handle;

    if (slot == ARRAY_LENGTH_SLOT) {
        MOCHA_ReportError(mc, "can't set length of a Java array");
	return MOCHA_FALSE;
    } else if (slot >= 0 && slot < obj_length(ho)) {
        int size = sizearray(obj_flags(ho), 1);
        char *addr = ((char*) unhand(ho)) + slot * size;

	if (!mocha_convertMDatumToJElement(mc, dp, addr,
                                           array->signature, array->cb, 0)) {

            MOCHA_ReportError(mc, "illegal assignment to Java array element");
            return MOCHA_FALSE;
        }
        return MOCHA_TRUE;
    }

    MOCHA_ReportError(mc, "Java array index %d out of range", slot);
    return MOCHA_FALSE;
}

static MochaBoolean
javaarray_list_properties(MochaContext *mc, MochaObject *obj)
{
    MochaJava *array = obj->data;
    HObject *ho = array->handle;
    MochaSlot slot;
    MochaDatum d;

    for (slot = 0; slot < obj_length(ho); slot++) {
        int size = sizearray(obj_flags(ho), 1);
        char *addr = ((char*) unhand(ho)) + slot * size;

	if (!mocha_convertJElementToMDatum(mc, addr, array->signature,
					   &d, MOCHA_UNDEF)) {
	    return MOCHA_FALSE;
	}
	if (!mocha_SetProperty(mc, obj->scope, 0, slot, d)) {
	    return MOCHA_FALSE;
	}
    }
    return MOCHA_TRUE;
}

static MochaBoolean
javaarray_resolve_name(MochaContext *mc, MochaObject *obj, const char *name)
{
    MOCHA_ReportError(mc, "Java array doesn't have a field named \"%s\"", name);
    return MOCHA_FALSE;
}

static void
javaarray_finalize(MochaContext *mc, MochaObject *obj)
{
    MochaJava *array = obj->data;

    /* get rid of the private data */
    /* array->signature is a static string */ ;
    array->signature = 0;

    /* remove it from the reflection table */
    java_finalize(mc, obj);
}

static MochaBoolean
javaarray_convert(MochaContext *mc, MochaObject *obj, MochaTag tag,
		  MochaDatum *dp)
{
    switch (tag) {
    case MOCHA_OBJECT:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_OBJECT,
			      MDF_TAINTED, MOCHA_TAINT_JAVA, u.obj, obj);
        break;
    case MOCHA_STRING:
        /* XXX how should arrays convert to strings? */
    default:
	MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_UNDEF,
			      MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.ptr, 0);
        break;
    }
    return MOCHA_TRUE;
}


static MochaClass javaarray_class = {
    "JavaArray",
    javaarray_get_property, javaarray_set_property, javaarray_list_properties,
    javaarray_resolve_name, javaarray_convert, javaarray_finalize
};

static MochaPropertySpec javaarray_props[] = {
    {"length", ARRAY_LENGTH_SLOT },
    {0}
};


/****	****	****	****	****	****	****	****	****/

/* this lameness is brought to you by the decision
 * to use strings for signatures in the JDK */
static char *
getSignatureBase(char *sig)
{
    int len;
    char *ret;
    char end;

    switch (*sig) {
    case SIGNATURE_CLASS:
        end = SIGNATURE_ENDCLASS;
        break;
    case SIGNATURE_FUNC:
        end = SIGNATURE_ENDFUNC;
        break;
    default:
        return 0;
        break;
    }

    len = strchr(sig+1, end) - (sig+1);
    ret = malloc(len+1);
    if (!ret)
        return 0;
    strncpy(ret, sig+1, len);
    ret[len] = '\0';

    return ret;
}

static ClassClass *
getSignatureClass(MochaContext *mc, char *sig, ClassClass *from)
{
    if (sig[1] != '\0') {
        char *name = getSignatureBase(sig);
        ClassClass *cb;
        if (!name) {
            MOCHA_ReportOutOfMemory(mc);
            return 0;
        }
        cb = mocha_FindJavaClass(mc, name, from);
        free(name);
        return cb;
    } else {
        /* special hack: if the signature is "L" it means
         * just use the fromClass */
        return from;
    }
}

/* this doesn't need to run in a java env, but it may throw an
 * exception so we need a temporary one */
static MochaBoolean
mocha_isSubclassOf(MochaContext *mc, ClassClass *cb1, ClassClass *cb2)
{
    ExecEnv *ee = (ExecEnv *)mozenv;
    Bool ret;

    exceptionClear(ee);
    ret = is_subclass_of(cb1, cb2, ee);
    if (exceptionOccurred(ee)) {
#ifdef DEBUG
        char *message;

        /* exceptionDescribe(ee); */
        /* XXX this could fail: we don't check if it's throwable,
         * but i assume that is_subclass_of is well-behaved */
        HString *hdetail =
          unhand((Hjava_lang_Throwable *)
                 ee->exception.exc)->detailMessage;
        ClassClass *cb = obj_array_classblock(ee->exception.exc);

        message = allocCString(hdetail);

        PR_LOG(Moja, error,
               ("exception in is_subclass_of %s (\"%s\")",
                classname(cb), message));

        free(message);
#endif
        exceptionClear(ee);
        return MOCHA_FALSE;
    }
    return ret ? MOCHA_TRUE : MOCHA_FALSE;
}

static MochaBoolean
mocha_convertMDatumToJSObject(HObject **objp, MochaContext *mc,
                              MochaObject *mpo, ClassClass *paramcb,
                              MochaBoolean checkOnly)
{
    MochaObject *mo = mpo;

    /* check if a JSObject would be an acceptable argument */
    if (mocha_isSubclassOf(mc, JSObjectClassBlock, paramcb)) {
        /* JSObject is ok, convert to one */
        /* a mocha object which is not really a java object
         * converts to JSObject */
        if (!checkOnly) {
            /* reflect the object as a JSObject */
            *objp = MOCHA_ReflectMObjectToJObject(mc, mo);
            if (!objp)
                return MOCHA_FALSE;
        }
        return MOCHA_TRUE;
    }
    return MOCHA_FALSE;
}

static MochaBoolean
mocha_convertMDatumToJArray(HObject **objp, MochaContext *mc,
                            MochaDatum *arg, char *sig, ClassClass *fromClass,
                            MochaBoolean checkOnly, int *cost)
{
    /* XXX bump the cost counter when necessary */
    MochaJava *java;
    HArrayOfObject *harr;
    ClassClass *acb;
    ClassClass *paramcb;
    MochaObject *mo;
    char *elementsig;
    ClassClass *elementClazz;

    /* the only legal conversions are from null or from a java
     * array */
    if (arg->tag != MOCHA_OBJECT)
        return MOCHA_FALSE;

    mo = arg->u.obj;

    /* can always pass null */
    if (!mo) {
        if (!checkOnly)
            *objp = 0;
        return MOCHA_TRUE;
    }

    /* otherwise, it must be a mocha reflection of a java array */
    if (mo->clazz != &javaarray_class)
        return MOCHA_FALSE;

    java = (MochaJava *) mo->data;

    sig++; /* skip to array element type */

    switch (*sig) {
    case SIGNATURE_CLASS:	/* object array */
        paramcb = getSignatureClass(mc, sig, fromClass);
        PR_LOG(Moja, debug, ("desired array element signature \"%s\"(0x%x)",
                             sig, paramcb));
        if (!paramcb) {
            PR_LOG(Moja, warn,
                   ("couldn't find class for signature \"%s\"\n", sig));
            return MOCHA_FALSE;
        }
        harr = (HArrayOfObject *) java->handle;
        acb = (ClassClass *)(unhand(harr)->body[obj_length(harr)]);
        if (!acb) {
            PR_LOG(Moja, warn,
                   ("couldn't find class of array element\n"));
            return MOCHA_FALSE;
        }

        /* elements must convert */
        if (mocha_isSubclassOf(mc, acb, paramcb)) {
            if (!checkOnly)
                *objp = (HObject *) harr;
            return MOCHA_TRUE;
        }
        break;
    case SIGNATURE_ARRAY:	/* nested array */
        /* XXX nested arrays can't be supported, because the
         * jdk runtime treats them all as flat
	 *
	 * This just in... Actually, you can get the dimensions of a
	 * java array because they aren't flattened. */
        /* XXX throw an exception */
        break;
    default:			/* primitive array */
        /* for any other array, the signature must match exactly */
        if (mocha_JArrayElementType(java->handle,
                                    &elementsig, &elementClazz)) {
            if (elementsig[0] == sig[0]) {
                if (!checkOnly)
                    *objp = java->handle;
                return MOCHA_TRUE;
            }
        }
        break;
    }

    return MOCHA_FALSE;
}

MochaBoolean
mocha_convertMDatumToJObject(HObject **objp, MochaContext *mc,
                             MochaDatum *arg, char *sig, ClassClass *fromClass,
                             MochaBoolean checkOnly, int *cost)
{
    /* XXX bump the cost counter when necessary */

    ClassClass *paramcb = sig ? getSignatureClass(mc, sig, fromClass)
      : mocha_FindJavaClass(mc, "java/lang/Object", 0); /* XXX */

    PR_LOG(Moja, debug, ("desired argument class signature \"%s\"(0x%x)",
	    sig, paramcb));

    if (!paramcb) {
	PR_LOG(Moja, warn,
               ("couldn't find class for signature \"%s\"", sig));
	return MOCHA_FALSE;
    }

#if 0
    if (arg->taint != MOCHA_TAINT_IDENTITY && arg->taint != MOCHA_TAINT_JAVA) {
	MOCHA_ReportError(mc,
	    "can't convert tainted JavaScript datum to Java object");
	return MOCHA_FALSE;
    }
#endif

    /* mocha wrappers around java objects do the same check
     * as the java compiler */
    /* XXX except classes, which become JSObject (circular problem?) */
    switch (arg->tag) {
      case MOCHA_OBJECT: {
        MochaObject *mo = arg->u.obj;
        /* null is always a valid object */
        if (!mo) {
            if (!checkOnly)
                *objp = 0;
            return MOCHA_TRUE;
        }
	if (mo->clazz == &java_class || mo->clazz == &javaarray_class) {
	    MochaJava *java = mo->data;
	    HObject *ho = java->handle;
	    ClassClass *cb;

            /* class reflections convert to JSObject or String only */
            if (java->type == JAVA_CLASS) {
                if (mocha_convertMDatumToJSObject(objp, mc, mo,
                                                  paramcb, checkOnly))
                    return MOCHA_TRUE;
            } else {
                /* get the classblock for the java argument type */
                cb = obj_array_classblock(ho);
                PR_LOG(Moja, debug, ("actual argument 0x%x, class 0x%x",
                          ho, cb));

                /* check against the expected class */
                if (mocha_isSubclassOf(mc, cb, paramcb)) {
                    if (!checkOnly)
                        *objp = ho;
                    return MOCHA_TRUE;
                }
            }
        } else {
            /* otherwise see if it will take a JSObject */
            if (mocha_convertMDatumToJSObject(objp, mc, mo,
                                              paramcb, checkOnly))
                return MOCHA_TRUE;
        }
	}
        break;
    case MOCHA_FUNCTION: {
        MochaObject *mo = arg->u.obj;
        if (mocha_convertMDatumToJSObject(objp, mc, mo, paramcb, checkOnly))
            return MOCHA_TRUE;
        }
	break;
    case MOCHA_NUMBER:    /* java.lang.Double */
        if (mocha_isSubclassOf(mc, DoubleClassBlock, paramcb)) {
            /* Float is ok */
            if (!checkOnly)
                *objp = mocha_ConstructJava(mc, "java/lang/Double",
                                            0, "(D)", arg->u.fval);
            return MOCHA_TRUE;
        }
	break;
    case MOCHA_BOOLEAN:   /* java.lang.Boolean */
	/* XXX this should return Boolean.TRUE or Boolean.FALSE
	 * instead of constructing a new one? */
        if (mocha_isSubclassOf(mc, BooleanClassBlock, paramcb)) {
            if (!checkOnly)
                *objp = mocha_ConstructJava(mc,
                                            "java/lang/Boolean",
                                            0, "(Z)", arg->u.bval);
            return MOCHA_TRUE;
        }
	break;

    }

    /* last ditch attempt: is a String acceptable? */
    {
	if (mocha_isSubclassOf(mc, StringClassBlock, paramcb)) {
            MochaAtom *atom;
	    /* string is ok, convert to one */

	    if (mocha_DatumToString(mc, *arg, &atom)) {
		/* make a java String from str */
                if (!checkOnly) {
		    *objp = (JHandle *)
                      makeJavaString((char*)MOCHA_GetAtomName(mc, atom),
                                     strlen(MOCHA_GetAtomName(mc, atom)));
                }
                mocha_DropAtom(mc, atom);
                return MOCHA_TRUE;
	    }
	}
    }
    return MOCHA_FALSE;
}

MochaBoolean
mocha_convertJObjectToMDatum(MochaContext *mc, MochaDatum *dp,
                              HObject *ho)
{
    MochaObject *mo;

    FINISH_MOCHA_JAVA_INIT(mc);

    if (!ho) {
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_OBJECT,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.obj, 0);
        return MOCHA_TRUE;
    }

    /* if it's a JSObject, pull out the original mocha
     * object when it comes back in to the mocha world */
    if (obj_array_classblock(ho) == JSObjectClassBlock) {
        jref jso = (jref) ho /* XXX */;
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_OBJECT,
	  MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA, u.obj,
          (MochaObject *)
	  get_netscape_javascript_JSObject_internal((JRIEnv*)EE(), jso));
        return MOCHA_TRUE;
    }

    /* instances of java.lang.String are wrapped so we can
     * call methods on them, but they convert to a mocha string
     * if used in a string context */

    /* otherwise, wrap it */
    mo = MOCHA_ReflectJObjectToMObject(mc, ho);
    if (!mo) {
        MOCHA_ReportError(mc,
                          "can't convert Java object to JavaScript object");
        return MOCHA_FALSE;
    }
    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_OBJECT,
			  MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			  u.obj, mo);
    return MOCHA_TRUE;
}


PR_STATIC_CALLBACK(PRHashNumber)
java_hashHandle(void *key)
{
    return (PRHashNumber)key;
}

static int
java_pointerEq(void *v1, void *v2)
{
    return v1 == v2;
}

HObject *
MOCHA_ReflectMObjectToJObject(MochaContext *mc, MochaObject *mo)
{
    jref jso, tmp;

    FINISH_MOCHA_JAVA_INIT(mc);

    /* see if it's already there */
    PR_EnterMonitor(mochaReflectionsMonitor);

    jso = /* XXX */ PR_HashTableLookup(mochaReflections, mo);

    /* release the monitor temporarily while we call the
     * java constructor, which could contain arbitrary
     * scariness */
    /*
    ** XXX Actually, the right thing to do here if the lookup fails is to
    ** put a placeholder in the hash table, saying it's in progress. And
    ** if you get the placeholder, you wait on the monitor.
    */
    PR_ExitMonitor(mochaReflectionsMonitor);

    if (jso)
        return (HObject *) jso /*XXX*/;

    /* nope, reflect it */
    jso = (jref) /*XXX*/
        mocha_ConstructJavaPrivileged(mc, 0,
                                      JSObjectClassBlock, "()");

    /* XXX check for exceptions */
    if (!jso) {
        return 0;
    }

    set_netscape_javascript_JSObject_internal((JRIEnv*)EE(), jso, 0);

    /* need to check again since we released the monitor */
    PR_EnterMonitor(mochaReflectionsMonitor);
    tmp = /* XXX */ PR_HashTableLookup(mochaReflections, mo);

    if (tmp) {
        PR_ExitMonitor(mochaReflectionsMonitor);

        /* let the one we constructed die in gc and use the one from
         * the table */
        return (HObject *) tmp;
    }

    MOCHA_HoldObject(mc, mo);
    set_netscape_javascript_JSObject_internal((JRIEnv*)EE(), jso,
					      (jint) mo);

    /* add it to the table */
    PR_HashTableAdd(mochaReflections, mo, jso);

    PR_ExitMonitor(mochaReflectionsMonitor);

    return (HObject *) jso;/*XXX*/
}

void
mocha_removeReflection(MochaObject *mo)
{
    /* remove it from the reflection table */
    PR_LOG(Moja, debug, ("removing mocha object 0x%x from table", mo));
    PR_EnterMonitor(mochaReflectionsMonitor);
    PR_HashTableRemove(mochaReflections, mo);
    PR_ExitMonitor(mochaReflectionsMonitor);
}



static MochaBoolean
mocha_convertMDatumToJValue(MochaContext *mc, MochaDatum *dp,
			    OBJECT *addr, char *sig, ClassClass *fromClass,
                            MochaBoolean checkOnly, char **sigRestPtr,
                            int *cost)
{
    Java8 tdub;
    char *p = sig;
    MochaDatum td;

#if 0
    if (dp->taint != MOCHA_TAINT_IDENTITY && dp->taint != MOCHA_TAINT_JAVA) {
	MOCHA_ReportError(mc,
	    "can't convert tainted JavaScript datum to Java value");
	return MOCHA_FALSE;
    }
#endif
    switch (*p) {
    case SIGNATURE_BOOLEAN:
	if (dp->tag == MOCHA_BOOLEAN)
            td = *dp;
        else {
            /* XXX we could convert other things to boolean too,
             * but until cost checking is done this will cause us
             * to choose the wrong method if there is method overloading */
            /* XXX */ return MOCHA_FALSE;
            (*cost)++;
            if (!MOCHA_ConvertDatum(mc, *dp, MOCHA_BOOLEAN, &td))
                return MOCHA_FALSE;
        }
        if (!checkOnly)
            *(long*)addr = (td.u.bval == MOCHA_TRUE);
	break;
    case SIGNATURE_SHORT:
    case SIGNATURE_BYTE:
    case SIGNATURE_CHAR:
    case SIGNATURE_INT:
        /* XXX should really do a range check... */
	if (dp->tag == MOCHA_NUMBER)
            td = *dp;
        else {
            /* XXX */ return MOCHA_FALSE;
            (*cost)++;
            if (!MOCHA_ConvertDatum(mc, *dp, MOCHA_NUMBER, &td))
                return MOCHA_FALSE;
        }
        if (!checkOnly)
            *(long*)addr = (long) td.u.fval;
	break;

    case SIGNATURE_LONG:
	if (dp->tag == MOCHA_NUMBER)
            td = *dp;
        else {
            /* XXX */ return MOCHA_FALSE;
            (*cost)++;
            if (!MOCHA_ConvertDatum(mc, *dp, MOCHA_NUMBER, &td))
                return MOCHA_FALSE;
        }
	if (!checkOnly) {
	    double dval;
	    int64 llval;

	    dval = (double) td.u.fval;
	    LL_D2L(llval, dval);
	    SET_INT64(tdub, addr, llval);
	}
	break;

    case SIGNATURE_FLOAT:
	if (dp->tag == MOCHA_NUMBER)
            td = *dp;
        else {
            /* XXX */ return MOCHA_FALSE;
            (*cost)++;
            if (!MOCHA_ConvertDatum(mc, *dp, MOCHA_NUMBER, &td))
                return MOCHA_FALSE;
        }
        if (!checkOnly)
	    *(float*)addr = td.u.fval;
	break;

    case SIGNATURE_DOUBLE:
	if (dp->tag == MOCHA_NUMBER)
            td = *dp;
        else {
            /* XXX */ return MOCHA_FALSE;
            (*cost)++;
            if (!MOCHA_ConvertDatum(mc, *dp, MOCHA_NUMBER, &td))
                return MOCHA_FALSE;
        }
        if (!checkOnly)
            SET_DOUBLE(tdub, addr, (double) td.u.fval);
	break;

    case SIGNATURE_CLASS:
        /* XXX cost? */
        if (mocha_convertMDatumToJObject((HObject **)/*XXX*/addr,
                                         mc, dp, p, fromClass,
                                         checkOnly, cost)) {
            while (*p != SIGNATURE_ENDCLASS) p++;
        } else
            return MOCHA_FALSE;
        break;

    case SIGNATURE_ARRAY:
        /* XXX cost? */
        if (mocha_convertMDatumToJArray((HObject **)/*XXX*/addr,
                                        mc, dp, p, fromClass,
                                        checkOnly, cost)) {
            while (*p == SIGNATURE_ARRAY) p++; /* skip array beginning */
            /* skip the element type */
            if (*p == SIGNATURE_CLASS) {
                while (*p != SIGNATURE_ENDCLASS) p++;
            }
        } else
            return MOCHA_FALSE;
        break;

    default:
        PR_LOG(Moja, warn,
               ("unknown value signature '%s'", p));
        return MOCHA_FALSE;
        break;
    }

    if (sigRestPtr)
        *sigRestPtr = p+1;
    return MOCHA_TRUE;
}


/*
 *  this code mimics the method overloading resolution
 *  done in java, for use in mocha->java calls
 */


/* push a MochaDatum array onto the java stack for use by
 *  the given method.
 * sigRest gets a pointer to the remainder of the signature (the
 *   rest of the arguments in the list).
 * if checkOnly is true, don't actually convert, just check that it's ok.
 */
static MochaBoolean
mocha_convertMToJArgs(MochaContext *mc, stack_item *optop,
                      struct methodblock *mb, int argc, MochaDatum *argv,
                      MochaBoolean checkOnly, char **sigRestPtr,
                      int *cost)
{
    struct fieldblock *fb = &mb->fb;
    char *sig = fieldsig(fb);
    MochaDatum *dp = argv;
    int argsleft = argc;
    char *p;
    void *addr;

    *cost = 0;

    for (p = sig + 1; *p != SIGNATURE_ENDFUNC; dp++,argsleft--) {
        if (argsleft == 0)		/* not enough arguments passed */
            return MOCHA_FALSE;
        if (checkOnly)
            addr = 0;
        else switch (*p) {
	case SIGNATURE_BOOLEAN:
	case SIGNATURE_SHORT:
	case SIGNATURE_BYTE:
	case SIGNATURE_CHAR:
	case SIGNATURE_INT:
            addr = &(optop++)->i;
            break;
	case SIGNATURE_LONG:
            addr = optop;
            optop += 2;
            break;
	case SIGNATURE_FLOAT:
            addr = &(optop++)->f;
            break;
	case SIGNATURE_DOUBLE:
            addr = optop;
            optop += 2;
            break;
	case SIGNATURE_CLASS:
	case SIGNATURE_ARRAY:
            addr = &(optop++)->h;
            break;
	default:
            PR_LOG(Moja, warn,
                   ("Invalid method signature '%s' for method '%s'\n",
                    fieldsig(fb), fieldname(fb)));
            return MOCHA_FALSE;
            break;
        }
        /* this bumps p to the next argument */
        if (!mocha_convertMDatumToJValue(mc, dp, addr, p, fieldclass(&mb->fb),
                                         checkOnly, &p, cost)) {
            return MOCHA_FALSE;
        }
    }
    if (argsleft > 0) {
        /* too many arguments */
        return MOCHA_FALSE;
    }

    if (sigRestPtr)
        *sigRestPtr = p+1 /* go past the SIGNATURE_ENDFUNC */;
    return MOCHA_TRUE;
}

/* java array elements are packed with smaller sizes */
static MochaBoolean
mocha_convertMDatumToJElement(MochaContext *mc, MochaDatum *dp,
                              char *addr, char *sig, ClassClass *fromClass,
                              char **sigRestPtr)
{
    long tmp[2];
    int cost = 0;
    if (!mocha_convertMDatumToJValue(mc, dp, (OBJECT *)&tmp,
                                     sig, fromClass,
                                     MOCHA_FALSE, sigRestPtr, &cost))
        return MOCHA_FALSE;

    switch (sig[0]) {
    case SIGNATURE_BOOLEAN:
        *(char*)addr = *(long*)&tmp;
        break;
    case SIGNATURE_BYTE:
        *(char*)addr = *(long*)&tmp;
        break;
    case SIGNATURE_CHAR:
        *(unicode*)addr = *(long*)&tmp;
        break;
    case SIGNATURE_SHORT:
        *(signed short*)addr = *(long*)&tmp;
        break;
    case SIGNATURE_INT:
        *(int32_t*)addr = *(long*)&tmp;
        break;
    case SIGNATURE_LONG:
        *(int64_t*)addr = *(int64_t*)&tmp;
        break;
    case SIGNATURE_FLOAT:
        *(float*)addr = *(float*)&tmp;
        break;
    case SIGNATURE_DOUBLE:
        *(double*)addr = *(double*)&tmp;
        break;
    case SIGNATURE_CLASS:
    case SIGNATURE_ARRAY:
        *(HObject**)addr = *(HObject**)&tmp;
        break;
    default:
        PR_LOG(Moja, warn,
               ("unknown value signature '%s'\n", sig[0]));
        return MOCHA_FALSE;
	break;
    }
    return MOCHA_TRUE;
}


static OBJECT *
getJavaFieldAddress(HObject *ho, struct fieldblock *fb)
{
    OBJECT *addr;			/* address of the java value */
    char *sig = fieldsig(fb);

    if (fb->access & ACC_PUBLIC) {
        if (fb->access & ACC_STATIC) {
            if (sig[0] == SIGNATURE_LONG ||
                sig[0] == SIGNATURE_DOUBLE)
                addr = (long *)twoword_static_address(fb);
            else
                addr = (long *)normal_static_address(fb);
        } else {
            addr = &obj_getoffset(ho, fb->u.offset);
        }
        return addr;
    } else {
        return 0;
    }
}

static MochaBoolean
mocha_convertMDatumToJField(MochaContext *mc, MochaDatum *dp,
			     HObject *ho, struct fieldblock *fb)
{
    OBJECT *addr = getJavaFieldAddress(ho, fb);
    int cost = 0;

    if (!addr) {
        MOCHA_ReportError(mc, "can't access field %s", fieldname(fb));
	return MOCHA_FALSE;
    }
    return mocha_convertMDatumToJValue(mc, dp, addr,
                                       fieldsig(fb), fieldclass(fb),
                                       MOCHA_FALSE, 0, &cost);
}

/* returns -1 if the given method is not applicable to the arguments,
 * or a cost if it is */
static int
methodIsApplicable(MochaContext *mc,
                   struct methodblock *mb, MochaBoolean isStatic,
                   const char *name, int argc, MochaDatum *argv)
{
    struct fieldblock *fb = &mb->fb;
    int cost = 0;

    /* name and access must match */
    if (!CHECK_STATIC(isStatic, fb) ||
        strcmp(fieldname(fb), name))
        return -1;

    if (mocha_convertMToJArgs(mc, 0, mb, argc, argv,
			      MOCHA_TRUE /* checkOnly */,
			      0 /* &sigRest */, &cost))
        return cost;
    return -1;
}

/* XXX this routine doesn't work yet - its purpose is to choose
 * the best method when java methods are overloaded, or to detect
 * ambiguous method calls */
static MochaBoolean
methodIsMoreSpecific(MochaContext *mc,
                     struct methodblock *mb1, struct methodblock *mb2)
{
    char *sig1 = fieldsig(&mb1->fb) + 1;        /* skip '(' */
    char *sig2 = fieldsig(&mb2->fb) + 1;

    /* XXX fill this in! */
    return MOCHA_TRUE;

    /* go through the args */
    while (*sig1 != SIGNATURE_ENDFUNC && *sig2 != SIGNATURE_ENDFUNC) {
        if (*sig1 == SIGNATURE_CLASS) {
            if (*sig2 == SIGNATURE_CLASS) {
                ClassClass *cb1 =
                  getSignatureClass(mc, sig1, fieldclass(&mb1->fb));
                ClassClass *cb2 =
                  getSignatureClass(mc, sig2, fieldclass(&mb2->fb));

                if (! mocha_isSubclassOf(mc, cb1, cb2))
                    return MOCHA_FALSE;

                /* next argument */
                while (*sig1++ != SIGNATURE_ENDCLASS);
                while (*sig2++ != SIGNATURE_ENDCLASS);
            }
        }
    }
    if (*sig1 != *sig2) {
        return MOCHA_FALSE;
        /* arg number mismatch */
    }
    return MOCHA_TRUE;
}

/* based on sun.tools.java.Environment */
/**
 * Return true if an implicit cast from this type to
 * the given type is allowed.
 */
#if 0
static bool_t
implicitCast(MochaContext *mc, char *sigfrom, char *sigto)
{
    switch(*sigfrom) {
    case SIGNATURE_BYTE:
        if (*sigto == SIGNATURE_SHORT) return TRUE;
    case SIGNATURE_SHORT:
    case SIGNATURE_CHAR:
        if (*sigto == SIGNATURE_INT) return TRUE;
    case SIGNATURE_INT:
        if (*sigto == SIGNATURE_LONG) return TRUE;
    case SIGNATURE_LONG:
        if (*sigto == SIGNATURE_FLOAT) return TRUE;
    case SIGNATURE_FLOAT:
        if (*sigto == SIGNATURE_DOUBLE) return TRUE;
    case SIGNATURE_DOUBLE:
        if (*sigfrom == *sigto) return TRUE;
        return FALSE;
        break;

#if 0
    case SIGNATURE_NULL:
        return (*sigto == SIGNATURE_NULL ||
                *sigto == SIGNATURE_CLASS ||
                *sigto == SIGNATURE_ARRAY);
        break;
#endif

    case SIGNATURE_ARRAY:
        switch (*sigto) {
	case SIGNATURE_ARRAY: {
	    /* find the base types */
	    char *basefrom = strdup(sigfrom);
	    char *baseto = strdup(sigto);
	    do {
		char *tmpfrom = basefrom;
		char *tmpto = baseto;
		basefrom = getSignatureBase(basefrom);
                if (!basefrom)
                    return FALSE;
		baseto = getSignatureBase(baseto);
                if (!baseto)
                    return FALSE;
		free(tmpfrom);
		free(tmpto);
	    } while (*basefrom == SIGNATURE_ARRAY &&
		     *baseto == SIGNATURE_ARRAY);
	    if ((*basefrom == SIGNATURE_ARRAY ||
		 (*basefrom == SIGNATURE_CLASS))
		&&
		(*baseto == SIGNATURE_ARRAY ||
		 (*baseto == SIGNATURE_CLASS))) {
		bool_t ret = methodIsMoreSpecific(mc, basefrom, baseto);
		free(basefrom); free(baseto);
		return ret;
	    } else {
		bool_t ret = (*basefrom == *baseto);
		free(basefrom); free(baseto);
		return ret;
	    }
	}
	break;
	case SIGNATURE_CLASS:
            /* must be Object or Cloneable */
            /* XXX is the string test safe? */
            return (! strncmp(sigto, "LObject;", strlen("LObject;")) ||
                    ! strncmp(sigto, "LCloneable;", strlen("LCloneable;")));
            break;
	default:
            return FALSE;
            break;
        }
        break;

#if 0 /* XXX */
    case SIGNATURE_CLASS:
	if (*sigto == SIGNATURE_CLASS) {
            ClassClass *cbfrom = getSignatureClass(mc, sigfrom);
            ClassClass *cbto = getSignatureClass(mc, sigto);
            return implementedBy(cbto, cbfrom);
        } else {
            return FALSE;
        }
        break;
#endif
    default:
        return FALSE;
        break;
    }
}
#endif

static MochaBoolean
mocha_convertJValueToMDatum(MochaContext *mc, OBJECT *addr, char *sig,
                             MochaDatum *dp, MochaTag desired)
{
    Java8 tmp;

    switch (sig[0]) {
    case SIGNATURE_VOID:
        *dp = MOCHA_void;
        break;
    case SIGNATURE_BYTE:
    case SIGNATURE_CHAR:
    case SIGNATURE_INT:
    case SIGNATURE_SHORT:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.fval, (long)*addr);
        break;
    case SIGNATURE_BOOLEAN:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_BOOLEAN,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.bval, (long)*addr);
        break;
    case SIGNATURE_LONG:
	{
	    int64 llval;
	    double dval;

	    llval = GET_INT64(tmp, addr);
	    LL_L2D(dval, llval);
	    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
				  MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
				  u.fval, (MochaFloat) dval);
	}
        break;
    case SIGNATURE_FLOAT:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.fval, *(float*)addr);
        break;
    case SIGNATURE_DOUBLE:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.fval, (MochaFloat)GET_DOUBLE(tmp, addr));
        break;
    case SIGNATURE_CLASS:
    case SIGNATURE_ARRAY:
        return mocha_convertJObjectToMDatum(mc, dp, *(HObject **) addr);
        break;
    default:
        MOCHA_ReportError(mc, "unknown Java signature character '%c'", sig[0]);
        return MOCHA_FALSE;
        break;
    }

    if (desired != MOCHA_UNDEF) {
        MochaDatum tmp = *dp;
        return MOCHA_ConvertDatum(mc, tmp, desired, dp);
    }
    return MOCHA_TRUE;
}

/* java array elements are packed with smaller sizes */
static MochaBoolean
mocha_convertJElementToMDatum(MochaContext *mc, char *addr, char *sig,
			      MochaDatum *dp, MochaTag desired)
{
    switch (sig[0]) {
    case SIGNATURE_BYTE:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.fval, *(char*)addr);
        break;
    case SIGNATURE_CHAR:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.fval, *(unicode*)addr);
        break;
    case SIGNATURE_SHORT:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.fval, *(signed short*)addr);
        break;
    case SIGNATURE_INT:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.fval, *(int32_t*)addr);
        break;
    case SIGNATURE_BOOLEAN:
        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_BOOLEAN,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.bval, *(int32_t*)addr);
        break;
    default:
	return mocha_convertJValueToMDatum(mc, (OBJECT *) addr, sig,
					   dp, desired);
	break;
    }

    if (desired != MOCHA_UNDEF) {
        MochaDatum tmp = *dp;
        return MOCHA_ConvertDatum(mc, tmp, desired, dp);
    }
    return MOCHA_TRUE;
}

static MochaBoolean
mocha_convertJFieldToMDatum(MochaContext *mc, HObject *ho,
                             struct fieldblock *fb, MochaDatum *dp,
                             MochaTag desired)
{
    OBJECT *addr = getJavaFieldAddress(ho, fb);
    char *sig = fieldsig(fb);

    if (addr)
        return mocha_convertJValueToMDatum(mc, addr, sig, dp, desired);
    else {
        MOCHA_ReportError(mc, "can't access Java field %s",
                          fieldname(fb));
        return MOCHA_FALSE;
    }
}

static MochaBoolean
mocha_convertJObjectToMString(MochaContext *mc, HObject *ho,
                              bool_t isClass, MochaDatum *dp)
{
    MochaAtom *atom;

    if (isClass) {
        char buf[256];
        PR_snprintf(buf, sizeof(buf), "[JavaClass %s]",
		    classname(unhand((HClass*)ho)));
        atom = MOCHA_Atomize(mc, buf);
        if (!atom) {
            MOCHA_ReportOutOfMemory(mc);
            return MOCHA_FALSE;
        }
    } else {
        HString *hstr;
        char *str;

        if (!ho)
            return MOCHA_FALSE;

        if (obj_classblock(ho) == StringClassBlock) {
            /* it's a string already */
            hstr = (HString*) ho;
        } else {
            /* call toString() to convert to a string */
            if (!mocha_ExecuteJavaMethod(mc, &hstr, sizeof(hstr), ho,
                                   "toString", "()Ljava/lang/String;",
                                   0, FALSE))
                return MOCHA_FALSE;
        }

        /* convert the java string to a mocha string */
        str = allocCString(hstr);
        atom = MOCHA_Atomize(mc, str);
        if (!atom) {
            sysFree(str);
            MOCHA_ReportOutOfMemory(mc);
            return MOCHA_FALSE;
        }
        sysFree(str);
    }
    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_STRING,
			  MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			  u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
mocha_convertJObjectToMNumber(MochaContext *mc, HObject *ho,
                               bool_t isClass, MochaDatum *dp)
{
    long foo[2], swap;
    JRI_JDK_Java8 tmp;
    double d;

    if (isClass || !ho)
        return MOCHA_FALSE;

    if (!mocha_ExecuteJavaMethod(mc, foo, sizeof(foo), ho,
				 "doubleValue", "()D",
				 0, FALSE))
        return MOCHA_FALSE;

    swap = foo[0];
    foo[0] = foo[1];
    foo[1] = swap;
    d = JRI_GET_DOUBLE(tmp, &foo[0]);
    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_NUMBER,
			  MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			  u.fval, d);
    return MOCHA_TRUE;
}

static MochaBoolean
mocha_convertJObjectToMBoolean(MochaContext *mc, HObject *ho,
                               bool_t isClass, MochaDatum *dp)
{
    long b;

    if (isClass || !ho)
        return MOCHA_FALSE;

    if (!mocha_ExecuteJavaMethod(mc, &b, sizeof(b), ho,
				 "booleanValue", "()Z",
				 0, FALSE))
        return MOCHA_FALSE;

    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_BOOLEAN,
			  MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			  u.bval, b);
    return MOCHA_TRUE;
}

static MochaBoolean
java_returnAsMochaDatum(MochaContext *mc, MochaDatum *dp,
                        ExecEnv *ee, char *sig)
{
    OBJECT *addr;

    if ((sig[0] == SIGNATURE_DOUBLE || sig[0] == SIGNATURE_LONG)) {
        addr = (OBJECT*) &ee->current_frame->optop[-2];
    } else
        addr = (OBJECT *) &ee->current_frame->optop[-1];

    return mocha_convertJValueToMDatum(mc, addr, sig, dp, MOCHA_UNDEF);
}

/*
 * find the best method to call for a name and a bunch of mocha
 * arguments.  try each possible method, then find the most
 * specific of the applicable methods.  "most specific" is
 * determined without reference to the mocha arguments: it is
 * the same as "most specific" in the java sense of the word.
 * XXX this could cause trouble since more methods will be applicable
 *  to a mocha call than for a similar java call!
 */
static struct methodblock *
matchMethod(MochaContext *mc, ClassClass *cb, bool_t isStatic,
            const char *name, int argc, MochaDatum *argv)
{
    int mindex;
    int *isApplicable = 0;		/* array holding costs */
    struct methodblock *bestmb = 0;
    PRBool isOverloaded = PR_FALSE;

    while (cb) {
        /* find all applicable methods.  keep track of which ones are
         * applicable using the isApplicable array, and the best one
         * found so far in bestmb.  set isOverloaded if there is more
         * than one applicable method */
        isApplicable = (int *) malloc(cb->methods_count * sizeof(int));
        if (!isApplicable) {
            MOCHA_ReportOutOfMemory(mc);
            return 0;
        }
        for (mindex = cb->methods_count; mindex--;) {
            struct methodblock *mb = cbMethods(cb) + mindex;
            struct fieldblock *fb = &mb->fb;

            isApplicable[mindex] = methodIsApplicable(mc, mb, isStatic,
                                                      name, argc, argv);
            if (isApplicable[mindex] == -1)
                continue;

            PR_LOG(Moja, debug, ("found applicable method %s with sig %s",
                                 fieldname(fb), fieldsig(fb)));

            if (!bestmb) {		/* first one found */
                bestmb = mb;
                continue;
            }

            isOverloaded = PR_TRUE;;

            if (methodIsMoreSpecific(mc, mb, bestmb)) {
                bestmb = mb;
            }
        }

        /* if we've found something applicable in the current class,
         * no need to go any further */
	/* this is the only exit from the loop in which isApplicable
         * is live */
        if (bestmb)
            break;

        /* otherwise, check the parent */
        if (cbSuperclass(cb))
            cb = unhand(cbSuperclass(cb));
        else
            cb = 0;
        free(isApplicable);
        isApplicable = 0;
    }

    /* if we didn't find anything, bail */
    if (!bestmb)
        return 0;

    /* second pass: check that bestmb is more specific than all other
     * applicable methods */
/* XXX if mb is equally specific, this is an ambiguous lookup */
#if 0
    if (isOverloaded)
        for (mindex = cb->methods_count; mindex--;) {
            struct methodblock *mb = cbMethods(cb) + mindex;
            struct fieldblock *fb = &mb->fb;

            if (!isApplicable[mindex] || mb == bestmb)
                continue;

            /* hopefully we can disambiguate by the cost */
        }
#endif

    free(isApplicable);
    return bestmb;
}

/* this is the callback to execute java bytecodes in the
 * appropriate java env */
typedef struct {
    JRIEnv *env;
    char *pc;
    bool_t ok;
} mocha_ExecuteJava_data;

static void
mocha_ExecuteJava_stub(void *d)
{
    mocha_ExecuteJava_data *data = d;
    extern bool_t ExecuteJava(unsigned char  *, ExecEnv *ee);

    data->ok = ExecuteJava((unsigned char*) data->pc, /*XXX*/(ExecEnv*)data->env);
}

/*  this is a copy of do_execute_java_method_vararg modified for
 *   a MochaDatum* argument list instead of a va_list
 */
static MochaBoolean
do_mocha_execute_java_method(MochaContext *mc, void *obj,
                             struct methodblock *mb, bool_t isStaticCall,
                             int argc, MochaDatum *argv,
                             MochaDatum *rval)
{
    uint16 taint;
    int i;
    ExecEnv *ee;
    char *method_name;
    char *method_signature;
    JavaFrame *current_frame, *previous_frame;
    JavaStack *current_stack;
    char *sigRest;
    int cost = 0;
    stack_item *firstarg;
    JRIEnv *saved;
    PRBool success = PR_TRUE;

    /* Don't let taint flow into Java. */
    taint = mc->taintInfo->accum;
    for (i = 0; i < argc; i++)
	MOCHA_MIX_TAINT(mc, taint, argv[i].taint);
#if 0
    if (taint != MOCHA_TAINT_IDENTITY && taint != MOCHA_TAINT_JAVA) {
	MOCHA_ReportError(mc, "can't call Java method with tainted arguments");
	return MOCHA_FALSE;
    }
#endif

    /* get the real ee that we are going to use */
    /* if this is mozenv everything is fine since we're on the moz thread
     * otherwise we are safe because it shouldn't be in the mochacontext
     * unless the appropriate java thread is waiting for us to call it
     * back (so we can walk all over it without fear */
    saved = mc->javaEnv;
    if (mc->javaEnv) {
        ee = /*XXX*/(ExecEnv *)mc->javaEnv;
    } else {
        ee = /*XXX*/(ExecEnv *)(mc->javaEnv = mozenv);
    }

    /* push the safety frame before the call frame */
    if (!mocha_pushSafeFrame(mc, ee))
        return MOCHA_FALSE;

    method_name = fieldname(&mb->fb);
    method_signature = fieldsig(&mb->fb);

    previous_frame = ee->current_frame;
    if (previous_frame == 0) {
        /* bottommost frame on this Exec Env. */
        current_stack = ee->initial_stack;
        current_frame = (JavaFrame *)(current_stack->data); /* no vars */
    } else {
        int args_size = mb->args_size;
        current_stack = previous_frame->javastack; /* assume same stack */
        if (previous_frame->current_method) {
            int size = previous_frame->current_method->maxstack;
            current_frame = (JavaFrame *)(&previous_frame->ostack[size]);
        } else {
            /* The only frames that don't have a mb are pseudo frames like
             * this one and they don't really touch their stack. */
            current_frame = (JavaFrame *)(previous_frame->optop + 3);
        }
        if (current_frame->ostack + args_size > current_stack->end_data) {
            /* Ooops.  The current stack isn't big enough.  */
            if (current_stack->next != 0) {
                current_stack = current_stack->next;
            } else {
                current_stack = CreateNewJavaStack(ee, current_stack);
                if (current_stack == 0) {
                    MOCHA_ReportOutOfMemory(mc);
                    success = PR_FALSE;
                    goto done;
                }
            }
            /* no vars */
            current_frame = (JavaFrame *)(current_stack->data);
        }
    }
    ee->current_frame = current_frame;

    current_frame->prev = previous_frame;
    current_frame->javastack = current_stack;
    current_frame->optop = current_frame->ostack;
    current_frame->vars = 0;	/* better not reference any! */
    current_frame->monitor = 0;	/* not monitoring anything */
    current_frame->current_method = 0;

    /* allocate space for all the operands before they are actually
     * converted, because conversion may need to use this stack */
    firstarg = current_frame->optop;
    current_frame->optop += mb->args_size;

    /* Push the target object, if not a static call */
    if (!isStaticCall)
        (firstarg++)->p = obj;

    /* now convert the args into the space on the stack */
    if (!mocha_convertMToJArgs(mc, firstarg, mb, argc, argv,
                               MOCHA_FALSE /* actually convert */,
                               &sigRest, &cost)) {
        /* the method shouldn't have matched if this was going
         * to happen! */
        MOCHA_ReportError(mc, "internal error: argument conversion failed");
        success = PR_FALSE;
        goto done;
    }

    /* build the bytecodes and constant table for the call */
{
    unsigned char pc[6];
    cp_item_type  constant_pool[10];
    unsigned char cpt[10];
    bool_t ok;

    constant_pool[CONSTANT_POOL_TYPE_TABLE_INDEX].p = cpt;
    cpt[0] = CONSTANT_POOL_ENTRY_RESOLVED;

    pc[0] = isStaticCall ? opc_invokestatic_quick
        : opc_invokenonvirtual_quick;
    pc[1] = 0; pc[2] = 1;	/* constant pool entry #1 */
    pc[3] = opc_return;

    constant_pool[1].p = mb;
    cpt[1] = CONSTANT_POOL_ENTRY_RESOLVED | CONSTANT_Methodref;

    current_frame->constant_pool = constant_pool;

    /* Run the byte codes in java-land catch any exceptions. */
    ee->exceptionKind = EXCKIND_NONE;

    {
        mocha_ExecuteJava_data data;
        data.pc = (char*) pc;
        mocha_CallJava(mc, mocha_ExecuteJava_stub, &data,
                       MOCHA_FALSE /* we pushed the safety frame already */);
        ok = data.ok;
    }

    if (ok) {
        MochaBoolean ret;
        PR_LOG(Moja, debug, ("method call succeeded\n"));
        ret = java_returnAsMochaDatum(mc, rval, ee, sigRest);
        if (!ret) {
            MOCHA_ReportError(mc,
                    "can't convert Java return value with signature %s",
                    sigRest);
            success = PR_FALSE;
        }
    } else
        success = PR_FALSE;
}
  done:
    /* Our caller can look at ee->exceptionKind and ee->exception. */
    ee->current_frame = previous_frame;
    /* pop the safety frame */
    mocha_popSafeFrame(mc, ee);

    mc->javaEnv = saved;

    return success;
}


static MochaBoolean
mocha_javaMethodWrapper(MochaContext *mc, MochaObject *obj,
unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFunction *fun;
    const char *name;
    struct methodblock *realmb;
    MochaJava *java;
    ClassClass *cb;
    MochaBoolean success;
    MochaAtom *atom;

    PR_ASSERT(argv[-1].tag == MOCHA_FUNCTION);
    fun = argv[-1].u.fun;

    atom = fun->atom;
    name = MOCHA_GetAtomName(mc, atom);

    if (!MOCHA_InstanceOf(mc, obj, &java_class, fun))
	return MOCHA_FALSE;
    java = obj->data;

    cb = java->cb;

    PR_LOG(Moja, debug, ("entered methodwrap, fun=0x%x, name=\"%s\"(0x%x)",
            fun, name, name));

    /* check that the object which we are being invoked on has
     * the same java class as the one we were extracted from */
/* XXX oh well
    if (fb->clazz != cb) {
        MOCHA_ReportError(mc, "Java method doesn't match object");
        return MOCHA_FALSE;
    }
 */

    /* match argc,argv against the signatures of the java methods */
    realmb = matchMethod(mc, cb, java->type==JAVA_CLASS, name, argc, argv);

    if (!realmb) {
        char buf[256];
        PR_snprintf(buf, sizeof(buf), "no Java %s.%s method matching JavaScript arguments (%d)", 
                    cb->name, name, argc);
        MOCHA_ReportError(mc, buf);
        return MOCHA_FALSE;
    }

    PR_LOG(Moja, debug, ("calling %sjava method %s with signature %s",
                         java->type==JAVA_CLASS ? "static " : "",
                         realmb->fb.name, realmb->fb.signature));

    success = do_mocha_execute_java_method(mc,
                  java->type==JAVA_CLASS ? 0 : java->handle, realmb,
                  java->type==JAVA_CLASS /* isStaticCall */,
                  argc, argv, rval);

    return success;
}


static MochaBoolean
mocha_javaConstructorWrapper(MochaContext *mc, MochaObject *obj,
                     unsigned argc, MochaDatum *argv,
                     MochaDatum *rval)
{
    MochaFunction *fun;
    ClassClass *cb;
    HObject *ho;
    struct methodblock *realmb;
    MochaBoolean success;
    MochaDatum tmp;

    PR_LOG(Moja, debug, ("entered java constructor wrapper\n"));

    PR_ASSERT(argv[-1].tag == MOCHA_FUNCTION);
    fun = argv[-1].u.fun;
    cb = fun->object.data;

    /* XXX these are copied from interpreter.c without much understanding */
    if (cbAccess(cb) & (ACC_INTERFACE | ACC_ABSTRACT)) {
        MOCHA_ReportError(mc, "can't instantiate Java class");
        return MOCHA_FALSE;
    }
    if (!VerifyClassAccess(0, cb, FALSE)) {
        MOCHA_ReportError(mc, "illegal access to Java class");
        return MOCHA_FALSE;
    }

    /* match argc,argv against the signatures of the java constructors */
    realmb = matchMethod(mc, cb, FALSE, "<init>", argc, argv);

    if (!realmb) {
        MOCHA_ReportError(mc, "no Java method matching arguments");
        return MOCHA_FALSE;
    }

    if (!VerifyFieldAccess(0, fieldclass(&realmb->fb),
                           realmb->fb.access, FALSE)) {
        MOCHA_ReportError(mc, "illegal access to Java constructor");
        return MOCHA_FALSE;
    }

    PR_LOG(Moja, debug, ("calling java constructor with signature %s",
            realmb->fb.signature));

/************* XXX RUN ME ON JAVA THREAD ***************/
    /*
    ** Because newobject can fail, and call SignalError, which calls
    ** FindClassFromClass, etc.
    */
    /* Allocate the object */
    if ((ho = newobject(cb, 0, EE())) == 0) {
        MOCHA_ReportOutOfMemory(mc);
        return MOCHA_FALSE;
    }
/************* END RUN ME ON JAVA THREAD ***************/

    success = do_mocha_execute_java_method(mc, ho, realmb,
                                           FALSE /* isStaticCall */,
                                           argc, argv,
                                           &tmp  /* gets set to void */);

    /* XXX the interpreter.c code calls cbDecRef(cb) at the end,
     * why?  should we? */

    if (success)
        return mocha_convertJObjectToMDatum(mc, rval, ho);
    return MOCHA_FALSE;
}

/*   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *
 *
 *   mocha/java reflection tables
 *
 *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   *   */

/* 16 bit needs to export this as a callback, but shouldn't do _loadds
 * as not in a DLL as the PR API macros do.
 * THIS NEEDS TO BE FIXED UP IN A GENERIC WAY USING THOSE MACROS,
 *   BY DECIDING WETHER OR NOT WE'RE COMPILING A DLL, THIS IS ONLY A
 *   QUICK HACK TO SHIP 3.0 BETA 4 */
#if defined(_WINDOWS) && !defined(_WIN32)
int __export
#else
static int
#endif
scanMochaJavaReflectionEntry(PRHashEntry *he, int i, void *gc)
{
    GCInfo *gcInfo = gc;
    MochaObject *mo = he->value;
    MochaJava *java = mo->data;

    /* scan the handle */
    gcInfo->liveObject((void **)&java->handle, 1);

    return HT_ENUMERATE_NEXT;
}

static void
scanMochaJavaReflections(void *notused)
{
    PR_PROCESS_ROOT_LOG(("Scanning mocha reflections of java objects"));

    /* XXX this is kind of scary long-term access inside the
     * monitor - is there any alternative? */
    PR_EnterMonitor(javaReflectionsMonitor);
    PR_HashTableEnumerateEntries(javaReflections,
                                 scanMochaJavaReflectionEntry,
                                 PR_GetGCInfo());
    PR_ExitMonitor(javaReflectionsMonitor);
}

static MochaFunctionSpec java_methods[] = {
    {mocha_toStringStr,	mocha_javaMethodWrapper,	0},
    {mocha_valueOfStr,	mocha_javaMethodWrapper,	0},
    {0}
};

static MochaObject *
mocha_ReflectJava(MochaContext *mc, MochaJavaType type, HObject *handle,
		  ClassClass *cb, char *sig)
{
    MochaObject *mo;
    MochaJava *java;
    void *key;

    /* see if it's already been reflected */
    switch (type) {
      case JAVA_CLASS:
	key = (void *) cb;
	break;
      case JAVA_OBJECT:
	key = (void *) handle;
	break;
      case JAVA_ARRAY:
	key = (void *) handle;
	break;
      default:
	break;
    }

    PR_EnterMonitor(javaReflectionsMonitor);
    mo = PR_HashTableLookup(javaReflections, key);

    /* nope, reflect it */
    if (!mo) {
        java = (MochaJava *) malloc(sizeof(MochaJava));
        if (!java) {
            MOCHA_ReportOutOfMemory(mc);
            return 0;
        }
	java->type = type;
	java->handle = handle;
        java->nextSlot = 1;

	switch (type) {
	  case JAVA_CLASS:
	    java->cb = cb;
	    java->signature = sig;
	    mo = MOCHA_NewObject(mc, &java_class, java, 0, 0,
				 0, java_methods);
	    break;
	  case JAVA_OBJECT:
	    java->cb = cb;
	    java->signature = sig;
	    mo = MOCHA_NewObject(mc, &java_class, java, 0, 0,
				 0, java_methods);
	    break;
          case JAVA_ARRAY:
	    java->cb = cb;
	    java->signature = sig;
	    mo = MOCHA_NewObject(mc, &javaarray_class, java, 0, 0,
				 javaarray_props, 0);
	    break;
	  default:
	    break;
	}

        /* add it to the table */
        PR_HashTableAdd(javaReflections, key, mo);
    }
    PR_ExitMonitor(javaReflectionsMonitor);

    return mo;
}

/* get the element type for a java array.  clazz will be set to null
 * if it's not of object type */
static MochaBoolean
mocha_JArrayElementType(HObject *handle, char **sig, ClassClass **clazz)
{
    /* figure out the signature from the type */
    int elementtype = obj_flags(handle);
    char *elementsig;

    *sig = 0;
    *clazz = 0;

    switch (elementtype) {
      case T_CLASS:
        *clazz = (ClassClass*)
          unhand((HArrayOfObject *)handle)->body[obj_length(handle)];
        elementsig = SIGNATURE_CLASS_STRING;
        break;
      case T_BOOLEAN:
        elementsig = SIGNATURE_BOOLEAN_STRING;
        break;
      case T_CHAR:
        elementsig = SIGNATURE_CHAR_STRING;
        break;
      case T_FLOAT:
        elementsig = SIGNATURE_FLOAT_STRING;
        break;
      case T_DOUBLE:
        elementsig = SIGNATURE_DOUBLE_STRING;
        break;
      case T_BYTE:
        elementsig = SIGNATURE_BYTE_STRING;
        break;
      case T_SHORT:
        elementsig = SIGNATURE_SHORT_STRING;
        break;
      case T_INT:
        elementsig = SIGNATURE_INT_STRING;
        break;
      case T_LONG:
        elementsig = SIGNATURE_LONG_STRING;
        break;
      default:
        PR_ASSERT(0);
        return MOCHA_FALSE;
        break;
    }

    *sig = elementsig;
    return MOCHA_TRUE;
}

static MochaObject *
mocha_ReflectJArrayToMObject(MochaContext *mc, HObject *handle)
{
    MochaObject *mo;
    char *elementSig;
    ClassClass *elementClazz;

    FINISH_MOCHA_JAVA_INIT(mc);

    if (!mocha_JArrayElementType(handle, &elementSig, &elementClazz))
        return 0;

    mo = mocha_ReflectJava(mc, JAVA_ARRAY, handle, elementClazz,
                           elementSig);

    PR_LOG(Moja, debug, ("reflected array[%s] 0x%x as MochaObject* 0x%x",
            elementSig, handle, mo));

    return mo;
}

MochaObject *
MOCHA_ReflectJObjectToMObject(MochaContext *mc, HObject *handle)
{
    MochaObject *mo = 0;

    FINISH_MOCHA_JAVA_INIT(mc);

    if (handle) {
        if (obj_flags(handle)) {
            mo = mocha_ReflectJArrayToMObject(mc, handle);
        } else {
            mo = mocha_ReflectJava(mc, JAVA_OBJECT, handle,
                                   obj_classblock(handle), 0);
            PR_LOG(Moja, debug, ("reflected HObject* 0x%x as MochaObject* 0x%x",
                      handle, mo));
        }
    }

    return mo;
}

MochaObject *
MOCHA_ReflectJClassToMObject(MochaContext *mc, ClassClass *cb)
{
    MochaObject *mo;

    FINISH_MOCHA_JAVA_INIT(mc);

    mo = mocha_ReflectJava(mc, JAVA_CLASS, (HObject *) cbHandle(cb), cb, 0);

    PR_LOG(Moja, debug, ("reflected ClassClass* 0x%x as MochaObject* 0x%x",
            cb, mo));

    return mo;
}

/*	*	*	*	*	*	*	*	*	*/

/* javaslot is a java slot which will be resolved as a method
 * or a field depending on context */

struct MochaJavaSlot {
    MochaObject *obj;           /* the object or class reflection */
    MochaDatum	datum;		/* the field value when created */
    struct fieldblock *fb;      /* fieldblock if there is a field */
    MochaAtom   *atom;          /* name of the field or method */
};

/* none of these should ever be called, since javaslot_convert will
 * first turn the slot into the underlying object if there is one */
static MochaBoolean
javaslot_get_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
                      MochaDatum *dp)
{
    MOCHA_ReportError(mc, "Java slots have no properties");
    return MOCHA_FALSE;
}
static MochaBoolean
javaslot_set_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		 MochaDatum *dp)
{
    MOCHA_ReportError(mc, "Java slots have no properties");
    return MOCHA_FALSE;
}
static MochaBoolean
javaslot_resolve_name(MochaContext *mc, MochaObject *obj, const char *name)
{
    MOCHA_ReportError(mc, "Java slots have no properties");
    return MOCHA_FALSE;
}

static void
javaslot_finalize(MochaContext *mc, MochaObject *obj)
{
    MochaJavaSlot *slot = obj->data;

    /* the datum may be holding a reference to something... */
    MOCHA_DropRef(mc, &slot->datum);

    /* drop the object of which this is a slot */
    MOCHA_DropObject(mc, slot->obj);

    /* drop the name */
    MOCHA_DropAtom(mc, slot->atom);

    free(slot);

    PR_LOG(Moja, debug, ("finalizing MochaJavaSlot 0x%x", obj));
}

static MochaBoolean
javaslot_convert(MochaContext *mc, MochaObject *obj, MochaTag tag,
                 MochaDatum *dp)
{
    MochaJavaSlot *slot = obj->data;
    const char *name = MOCHA_GetAtomName(mc, slot->atom);
    MochaFunction *fun;
    MochaAtom *atom;

    switch (tag) {
      case MOCHA_FUNCTION:
        fun = mocha_NewFunction(mc, mocha_javaMethodWrapper, 0,
                                slot->obj, slot->atom);
        if (!fun)
            return MOCHA_FALSE;

        PR_LOG(Moja, debug,
               ("converted slot to function 0x%x with name %s(0x%x)\n",
                fun, name, name));

	/* Hold slot's parent object and flag fun as a bound method. */
	MOCHA_HoldObject(mc, fun->object.parent);
	fun->bound = MOCHA_TRUE;

        MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_FUNCTION,
			      MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.fun, fun);
        return MOCHA_TRUE;
        break;

      case MOCHA_OBJECT:
        PR_LOG(Moja, debug, ("converting java slot 0x%x to object", obj));
	/* FALL THROUGH */
      case MOCHA_ATOM:
      case MOCHA_SYMBOL:
      case MOCHA_NUMBER:
      case MOCHA_BOOLEAN:
      case MOCHA_STRING:
        if (slot->fb) {
            return MOCHA_ConvertDatum(mc, slot->datum, tag, dp);
	}
	if (tag == MOCHA_STRING) {
	    MochaJava *java = slot->obj->data;
	    char *str, *cp;

	    PR_ASSERT(java->type == JAVA_OBJECT || java->type == JAVA_CLASS);
	    str = PR_smprintf("[JavaMethod %s.%s]", classname(java->cb), name);
	    if (!str) {
		atom = 0;
	    } else {
		for (cp = str; *cp != '\0'; cp++)
		    if (*cp == '/')
			*cp = '.';
		atom = MOCHA_Atomize(mc, str);
		free(str);
	    }
	    if (!atom) {
                MOCHA_ReportOutOfMemory(mc);
                return MOCHA_FALSE;
            }
	    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_STRING,
				  MDF_ENUMERATE | MDF_TAINTED, MOCHA_TAINT_JAVA,
				  u.atom, atom);
	    return MOCHA_TRUE;
	}
	if (tag != MOCHA_OBJECT) {
	    MOCHA_ReportError(mc, "no field with name \"%s\"", name);
            /* XXX drop atom */
	    return MOCHA_FALSE;
	}
        break;
      default:
	MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_UNDEF,
			      MDF_TAINTED, MOCHA_TAINT_JAVA,
			      u.ptr, 0);
        break;
    }
    return MOCHA_TRUE;
}

static MochaClass javaslot_class = {
    "JavaSlot",
    javaslot_get_property, javaslot_set_property, MOCHA_ListPropStub,
    javaslot_resolve_name, javaslot_convert, javaslot_finalize
};

static MochaObject *
mocha_reflectJavaSlot(MochaContext *mc, MochaObject *obj, MochaAtom *atom)
{
    MochaJava *java = obj->data;
    ClassClass *cb = java->cb;
    MochaJavaSlot *slot;
    MochaObject *mo;
    const char *name;

    PR_ASSERT(obj->clazz == &java_class);

    slot = (MochaJavaSlot *) malloc(sizeof(MochaJavaSlot));
    if (!slot) {
        MOCHA_ReportOutOfMemory(mc);
        return 0;
    }
    /* corresponding drops are in javaslot_finalize */
    slot->atom = MOCHA_HoldAtom(mc, atom);
    slot->obj = MOCHA_HoldObject(mc, obj);
    slot->datum = MOCHA_void;

    name = MOCHA_GetAtomName(mc, atom);

    /* if there's a field get its value at reflection time */
    slot->fb = java_lookup_field(mc, cb, java->type == JAVA_CLASS, name);
    if (slot->fb) {
        if (!mocha_convertJFieldToMDatum(mc, java->handle, slot->fb,
                                         &slot->datum, MOCHA_UNDEF)) {
            /* if this happens, the field had a value that couldn't
             * be represented in mocha. */
            PR_LOG(Moja, error,
                   ("looking up initial field value failed!"));
            /* XXX should really set a flag that will cause an error
             * only if the slot is accessed as a field.  for now we
             * make it look like there wasn't any field by that name,
             * which is less informative */
            slot->fb = 0;
        }
    }

    /* we're hanging on to this datum, hold it if necessary.
     * corresponding drop is in javaslot_finalize */
    MOCHA_HoldRef(mc, &slot->datum);

    mo = MOCHA_NewObject(mc, &javaslot_class, slot, 0, 0, 0, 0);

    PR_LOG(Moja, debug, ("reflected slot %s of 0x%x as 0x%x",
            name, java->handle, mo));
    return mo;
}

/***********************************************************************/

/* a saved mocha error state */
typedef struct SavedMochaError SavedMochaError;
struct SavedMochaError {
    char *message;
    MochaErrorReport report;
    SavedMochaError *next;
};

/*
 *  capture a mocha error that occurred in mocha code called by java.
 *  makes a copy of the mocha error data and hangs it off the mocha
 *  environment.  when the mocha code returns, this is checked and
 *  used to generate a JSException.  if the JSException is uncaught
 *  and makes it up to another layer of mocha, the error will be
 *  reinstated with MOCHA_ReportError
 */
void
mocha_js_ErrorReporter(MochaContext *mc, const char *message,
                       MochaErrorReport *error)
{
    /* save the error state */
    SavedMochaError *newerr;
    newerr = (SavedMochaError *) malloc(sizeof(SavedMochaError));
    if (!newerr) {
        /* XXX not much we can do here, abort? */
        return;
    }
    newerr->message = strdup(message);
    if (!newerr->message) {
        /* XXX not much we can do here, abort? */
        free(newerr);
        return;
    }

    if (error) {
	newerr->report.filename = strdup(error->filename);
	if (!newerr->report.filename) {
	    /* XXX not much we can do here, abort? */
	    free(newerr->message);
	    free(newerr);
	    return;
	}
	newerr->report.lineno = error->lineno;
	if (error->linebuf) {
	    newerr->report.linebuf = strdup(error->linebuf);
	    if (!newerr->report.linebuf) {
		/* XXX not much we can do here, abort? */
		free((void*)newerr->report.filename);
		free(newerr->message);
		free(newerr);
		return;
	    }
	    newerr->report.tokenptr = newerr->report.linebuf +
				      (error->tokenptr - error->linebuf);
	} else {
	    newerr->report.linebuf = newerr->report.tokenptr = 0;
	}
    }

    /* push this error */
    newerr->next = mc->mochaErrors;
    mc->mochaErrors = newerr;
}

static SavedMochaError *
mocha_js_FreeError(SavedMochaError *err)
{
    SavedMochaError *next = err->next;

    free(err->message);
    free((char*)err->report.filename);/*XXX*/
    free((char*)err->report.linebuf);
    free(err);

    return next;
}

/* this is called upon returning from mocha to java.  one possibility
 * is that the mocha error was actually triggered by java at some point -
 * if so we throw the original java exception.  otherwise, each mocha
 * error will have pushed something on MochaContext->mochaErrors, so
 * we convert them all to a string and throw a JSException with that
 * info.
 */
void
mocha_MErrorToJException(MochaContext *mc, ExecEnv *ee)
{
    SavedMochaError *err = 0;

    if (!mc->mochaErrors) {
        exceptionClear(ee);
        PR_LOG(Moja, debug,
               ("j-m succeeded with no exception mc=0x%x ee=0x%x", mc, ee));
        return;
    }

    /* if there's a pending exception in the java env, assume it
     * needs to be propagated (since mocha couldn't have caught
     * it and done something with it) */
    if (exceptionOccurred(ee)) {
        PR_LOG(Moja, debug,
               ("j-m propagated exception through mocha mc=0x%x ee=0x%x",
                mc, ee));
        return;		/* propagating is easy! */
    }

    /* otherwise, throw a JSException */
    /* get the message from the deepest saved mocha error */
    err = mc->mochaErrors;
    if (err) {
        while (err->next)
            err = err->next;
    }

    /* propagate any pending mocha errors upward with a java exception */
    {
        JRIEnv *env = (JRIEnv*) ee;
        jref message =
          JRI_NewStringUTF(env, err->message,
                           strlen(err->message));
        jref filename = err->report.filename
          ? JRI_NewStringUTF(env, err->report.filename,
                              strlen(err->report.filename))
          : NULL;
        int lineno = err->report.lineno;
        jref source = err->report.linebuf
          ? JRI_NewStringUTF(env, err->report.linebuf,
                              strlen(err->report.linebuf))
          : NULL;
        int index = err->report.linebuf
          ? err->report.tokenptr - err->report.linebuf
          : 0;
        jref exc =
            netscape_javascript_JSException_new_2(env,
                         class_netscape_javascript_JSException(env),
                         /*Java_getGlobalRef(env, js_JSExceptionClass),*/
                         message, filename, lineno, source, index);
        exceptionThrow(ee, (HObject *)exc);
        PR_LOG(Moja, debug,
               ("j-m raised JSException \"%s\" mc=0x%x ee=0x%x",
                err->message, mc, ee));
    }
}

static MochaBoolean
mocha_isJSException(ExecEnv *ee, HObject *exc)
{
    return strcmp(classname(obj_array_classblock(exc)),
                  "netscape/javascript/JSException")
      ? MOCHA_TRUE : MOCHA_FALSE;
}

/* this is called after returning from java to mocha.  if the exception
 * is actually a JSException, we pull the original mocha error state
 * out of the MochaContext and use that.  Otherwise we turn the
 * JSException into a string and pass it up as a Mocha error
 */
static MochaBoolean
mocha_JExceptionToMError(MochaContext *mc, ExecEnv *ee)
{
    SavedMochaError *err = 0;
    char *message;
    MochaBoolean success;

    /* get rid of any mocha errors so far, but save the deepest
     * one in case this was a JSException and we re-report it */
    /* XXX the deepest one is the most interesting? */
    err = mc->mochaErrors;
    if (err) {
        while (err->next)
            err = mocha_js_FreeError(err);
    }

    /* if no exception reached us, continue on our merry way */
    if (!exceptionOccurred(ee)) {
        PR_LOG(Moja, debug,
               ("m-j succeeded, no exceptions mc=0x%x ee=0x%x",
                mc, ee));
        success = MOCHA_TRUE;
        goto done;
    }

    /* if we got this far there was an error for sure */
    success = MOCHA_FALSE;

    switch (ee->exceptionKind) {
      case EXCKIND_THROW:
        if (err && mocha_isJSException(ee, ee->exception.exc)) {
            mocha_ReportErrorAgain(mc, err->message, &err->report);
	    PR_LOG(Moja, debug,
                   ("m-j re-reported error \"%s\" mc=0x%x ee=0x%x",
                    err->message, mc, ee));
        }

        /* otherwise, describe the exception to a string */
        else if (mocha_isSubclassOf(mc,
                                    obj_array_classblock(ee->exception.exc),
                                    ThrowableClassBlock)) {
            HString *hdetail =
              unhand((Hjava_lang_Throwable *)
                     ee->exception.exc)->detailMessage;
	    ClassClass *cb = obj_array_classblock(ee->exception.exc);

            message = allocCString(hdetail);
	    PR_LOG(Moja, debug,
                   ("m-j converted exception %s, \"%s\" to error mc=0x%x ee=0x%x",
                    classname(cb), message, mc, ee));
            /* pass the string to MOCHA_ReportError */
            MOCHA_ReportError(mc, "uncaught Java exception %s (\"%s\")",
                              classname(cb), message);
            free(message);
        }

        /* it's not a Throwable, somebody in java-land is being
         * lame */
        else {
	    ClassClass *cb = obj_array_classblock(ee->exception.exc);
            MOCHA_ReportError(mc, "uncaught Java exception of class %s",
                              classname(cb));
	    PR_LOG(Moja, debug,
                   ("m-j converted exception %s to error mc=0x%x ee=0x%x",
                    classname(cb), mc, ee));
        }
        break;
      case EXCKIND_STKOVRFLW:
        MOCHA_ReportError(mc, "Java stack overflow, pc=0x%x",
                          ee->exception.addr);
        break;
      default:
        MOCHA_ReportError(mc,
                          "internal error: Java exception of unknown type %d",
                          ee->exceptionKind);
        break;
    }

  done:
    if (err) {
        mocha_js_FreeError(err);
        mc->mochaErrors = 0;
    }
    PR_LOG(Moja, debug,
           ("m-j cleared mocha errors mc=0x%x", mc));
    return success;
}

/***********************************************************************/


/*
 *  All mocha_CallJava calls must use a data pointer that starts like
 *  this one:
 */
typedef struct {
    JRIEnv *env;
} mocha_CallJava_data;

static MochaBoolean
mocha_CallJava(MochaContext *mc, LJCallback doit, void *d,
               MochaBoolean pushSafeFrame)
{
    mocha_CallJava_data *data = d;
    MochaContext *saved;
    MochaBoolean success = MOCHA_TRUE;
    ExecEnv *ee;

    FINISH_MOCHA_JAVA_INIT(mc);

    if (mc->javaEnv)
        data->env = mc->javaEnv;
    else
        data->env = mozenv;

    ee = (ExecEnv*) data->env; /*XXX*/
    if (!ee) {
	MOCHA_ReportError(mc, "can't call Java from JavaScript");
	return MOCHA_FALSE;
    }

    saved = ee->mochaContext;
    ee->mochaContext = mc;

    /* security: push the safety frame onto the java stack */
    if (pushSafeFrame)
        if (!mocha_pushSafeFrame(mc, ee)) {
            return MOCHA_FALSE;
        }

    PR_LOG_BEGIN(Moja, debug, ("entering java ee=0x%x mc=0x%x", ee, mc));
    LJ_CallJava(data->env, doit, data);
    PR_LOG_END(Moja, debug, ("left java ee=0x%x mc=0x%x", ee, mc));

    /* it's only safe to call this on the mozilla thread */
    success = mocha_JExceptionToMError(mc, ee);

    /* pop the safety frame */
    if (pushSafeFrame)
        mocha_popSafeFrame(mc, ee);

    ((ExecEnv*)(data->env))->mochaContext = saved;

    return success;
}


/***********************************************************************/

typedef struct {
    JRIEnv *env;
    HObject *self;
    char *name;
    char *sig;
    struct methodblock *mb;
    bool_t isStaticCall;
    va_list args;
    long *raddr;
    size_t rsize;
} mocha_ExecuteJavaMethod_data;

static void
mocha_ExecuteJavaMethod_stub(void *d)
{
    mocha_ExecuteJavaMethod_data *data = (mocha_ExecuteJavaMethod_data *) d;

    data->raddr[0] =
	do_execute_java_method_vararg(/*XXX*/(ExecEnv*)data->env,
				      data->self,
				      data->name, data->sig,
				      data->mb, data->isStaticCall,
				      data->args,
				      (data->rsize > sizeof(long))
					  ? &data->raddr[1] : NULL,
				      FALSE);
}

static MochaBoolean
mocha_ExecuteJavaMethod(MochaContext *mc, void *raddr, size_t rsize,
                        HObject *ho, char *name, char *sig,
                        struct methodblock *mb, bool_t isStaticCall, ...)
{
    mocha_ExecuteJavaMethod_data data;
    MochaBoolean success;
    va_list args;
    va_start(args, isStaticCall);

    data.self = ho;
    data.name = name;
    data.sig = sig;
    data.mb = mb;
    data.isStaticCall = isStaticCall;
    data.args = args;
    data.raddr = raddr;
    data.rsize = rsize;

    success = mocha_CallJava(mc, mocha_ExecuteJavaMethod_stub, &data,
                             MOCHA_TRUE);
    va_end(args);

    return success;
}

/***********************************************************************/

typedef struct {
    JRIEnv *env;
    char *name;
    ClassClass *fromClass;
    ClassClass *ret;
    char *errstr;
} mocha_FindJavaClass_data;

static void
mocha_FindJavaClass_stub(void *d)
{
    mocha_FindJavaClass_data *data = d;
    ExecEnv *ee = /*XXX*/(ExecEnv*)data->env;

    exceptionClear(ee);

    /* XXX need to push a stack frame with the classloader
     * of data->fromClass or the security check on opening
     * the url for the class will fail */

    data->ret = FindClassFromClass(ee, data->name, TRUE,
                                   data->fromClass);

    /* we clear the exception state, because when mocha
     * fails to find a class it assumes it's a package instead
     * of an error. */
    /* XXX can we report an error if the problem is accessing
     * bogus-codebase? */

    if (exceptionOccurred(ee)) {
        ClassClass *cb = obj_array_classblock(ee->exception.exc);

#ifdef DEBUG
        char *message;
        /* XXX this could fail: we don't check if it's Throwable,
         * but i assume that FindClass is well-behaved */
        HString *hdetail =
          unhand((Hjava_lang_Throwable *)
                 ee->exception.exc)->detailMessage;

        message = allocCString(hdetail);

        PR_LOG(Moja, debug,
               ("exception in is_subclass_of %s (\"%s\")",
                classname(cb), message));

        free(message);
#endif

        if (0 == strcmp(classname(cb),
                        "netscape/applet/AppletSecurityException")) {
            /* looks like we tried to get it from the net, and failed
             * because we don't have the right classloader set up */

            /* XXX fixme when it's possible to load java classes from
             * mocha we'll have to push the right classloader to make
             * this work. */
            data->errstr
              = PR_smprintf("can't load class %s from JavaScript",
                            data->name);
            PR_LOG(Moja, debug, ("%s", data->errstr));
	}

#ifdef DEBUG_nix
        /* take a look at the exception to see if we can narrow
         * down the kinds of failures that cause a package to
         * be created? */
        exceptionDescribe(ee);
#endif
        /* XXX other exceptions don't matter? narrow this down... */
        exceptionClear(ee);
    }
}

/* this can call arbitrary java code in class initialization */
/* pass 0 for the "from" argument for system classes, but if
 * you want to check the applet first pass its class */
static ClassClass *
mocha_FindJavaClass(MochaContext *mc, char *name, ClassClass *from)
{
    mocha_FindJavaClass_data data;

    data.name = name;
    data.fromClass = from;
    data.errstr = 0;
    if (mocha_CallJava(mc, mocha_FindJavaClass_stub, &data,
                       MOCHA_TRUE)) {
        if (data.errstr) {
            MOCHA_ReportError(mc, "%s", data. errstr);
            free(data.errstr);
            /* XXX need to propagate error condition differently */
            return 0;
        }
        return data.ret;
    } else
        return 0;
}

/***********************************************************************/


typedef struct {
    JRIEnv *env;
    bool_t privileged;
    char *name;
    ClassClass *cb;
    char *sig;
    va_list args;
    HObject *ret;
} mocha_ConstructJava_data;

static void
mocha_ConstructJava_stub(void *d)
{
    mocha_ConstructJava_data *data = d;
    ExecEnv *ee = /*XXX*/(ExecEnv*)data->env;

    if (data->privileged) {
        /* XXX extremely fucking lame - there should be a security
         * flag to execute_java_constructor_vararg instead.
         * the effect of this is that the JSObject constructor may
         * get called on the wrong thread, but this probably won't
         * do any damage.  JRI will fix this, right? */
        ee = PRIVILEGED_EE;
    }
    data->ret =
      execute_java_constructor_vararg(ee,
                                      data->name, data->cb,
                                      data->sig, data->args);
}

/* this can call arbitrary java code in class initialization */
static HObject *
mocha_ConstructJava(MochaContext *mc, char *name, ClassClass *cb,
                    char *sig, ...)
{
    mocha_ConstructJava_data data;
    va_list args;
    MochaBoolean success;

    va_start(args, sig);
    data.name = name;
    data.privileged = FALSE;
    data.cb = cb;
    data.sig = sig;
    data.args = args;
    data.env = JRI_GetCurrentEnv();

    success = mocha_CallJava(mc, mocha_ConstructJava_stub, &data,
                             MOCHA_TRUE);
    va_end(args);

    if (success) return data.ret;
    else return 0;
}

/* for private constructors, i.e. JSObject */
static HObject *
mocha_ConstructJavaPrivileged(MochaContext *mc, char *name, ClassClass *cb,
                              char *sig, ...)
{
    mocha_ConstructJava_data data;
    va_list args;
    MochaBoolean success;

    va_start(args, sig);
    data.privileged = TRUE;
    data.name = name;
    data.cb = cb;
    data.sig = sig;
    data.args = args;
    data.env = JRI_GetCurrentEnv();

    success = mocha_CallJava(mc, mocha_ConstructJava_stub, &data,
                             MOCHA_TRUE);
    va_end(args);

    if (success) return data.ret;
    else return 0;
}


/***********************************************************************/

PRBool moja_enabled = PR_TRUE;

/*
 * most of the initialization is done lazily by this function
 *  this is never called more than once because it is called
 *  through a macro that tests if it's already been called...
 */
/* XXX this must be run on the mozilla thread */
static void
mocha_FinishInitJava(MochaContext *mc)
{
    int tmp;

    if (mocha_java_initialized)
        return;

    mocha_java_initialized = 1;

    if (!MOCHA_JavaGlueEnabled())
        return;

    PR_LOG(Moja, debug, ("mocha_FinishInitJava()\n"));

    /* the argument to LJ_Java_init is unused */
    if (LJ_INIT_OK != (tmp = LJ_StartupJava())) {
        PR_LOG(Moja, error, ("LJ_StartupJava returned %d", tmp));
	goto fail;
    }

    /* initialize the reflection tables */
    javaReflections =
      PR_NewHashTable(256, (PRHashFunction) java_hashHandle,
                      (PRHashComparator) java_pointerEq,
                      (PRHashComparator) java_pointerEq, 0, 0);
    PR_RegisterRootFinder(scanMochaJavaReflections,
                          "scan mocha reflections of java objects", 0);
    javaReflectionsMonitor = PR_NewNamedMonitor(0, "javaReflections");
    mochaReflections =
      PR_NewHashTable(256, (PRHashFunction) java_hashHandle,
                      (PRHashComparator) java_pointerEq,
                      (PRHashComparator) java_pointerEq, 0, 0);
    mochaReflectionsMonitor = PR_NewNamedMonitor(0, "mochaReflections");

    /*
    ** Keep track of a few well-known classes for convenience. Setting
    ** the sticky bit keeps the classes from being collected.
    */
    JSObjectClassBlock =
      mocha_FindJavaClass(mc, "netscape/javascript/JSObject", 0);
    if (!JSObjectClassBlock) {
        PR_LOG(Moja, error,
               ("couldn't find class \"netscape/javascript/JSObject\"\n"));
	goto fail;
    }
    JSObjectClassBlock->flags |= CCF_Sticky;

    StringClassBlock = mocha_FindJavaClass(mc, "java/lang/String", 0);
    if (!StringClassBlock) {
        PR_LOG(Moja, error, ("couldn't find class \"java/lang/String\"\n"));
	goto fail;
    }
    StringClassBlock->flags |= CCF_Sticky;

    BooleanClassBlock = mocha_FindJavaClass(mc, "java/lang/Boolean", 0);
    if (!BooleanClassBlock) {
        PR_LOG(Moja, error, ("couldn't find class \"java/lang/Boolean\"\n"));
	goto fail;
    }
    BooleanClassBlock->flags |= CCF_Sticky;

    DoubleClassBlock = mocha_FindJavaClass(mc, "java/lang/Double", 0);
    if (!DoubleClassBlock) {
        PR_LOG(Moja, error, ("couldn't find class \"java/lang/Double\"\n"));
	goto fail;
    }
    DoubleClassBlock->flags |= CCF_Sticky;

    ThrowableClassBlock = mocha_FindJavaClass(mc, "java/lang/Throwable", 0);
    if (!ThrowableClassBlock) {
        PR_LOG(Moja, error, ("couldn't find class \"java/lang/Throwable\"\n"));
	goto fail;
    }
    ThrowableClassBlock->flags |= CCF_Sticky;

    {
	JRIEnv* env = JRI_GetCurrentEnv();
	use_netscape_javascript_JSObject(env);
	use_java_applet_Applet(env);
    }
    return;

  fail:
    moja_enabled = PR_FALSE;
    return;
}

PRBool
MOCHA_JavaGlueEnabled()
{
    extern MochaBoolean LM_GetMochaEnabled(void);
    return moja_enabled && LJ_GetJavaEnabled() && LM_GetMochaEnabled();
#if 0
    /* we can supposedly do getenv on all platforms */
    if (getenv("NS_ENABLE_MOJA"))
        return MOCHA_TRUE;
    return PR_FALSE;
#endif
}

/* hook from mocha_DestroyContext */
void
mocha_DestroyJavaContext(MochaContext *mc)
{
    /* XXX do anything with the env? */
    mc->javaEnv = 0;
}

/* get the java class associated with an instance, useful for
 * access to static fields and methods of applets */
static MochaBoolean
mocha_getJavaClass(MochaContext *mc, MochaObject *obj, unsigned argc,
                   MochaDatum *argv, MochaDatum *rval)
{
    MochaObject *mo;
    MochaObject *moclass;
    MochaJava *java;

    /* XXX this could accept strings as well i suppose */
    if (argc != 1
        || argv[0].tag != MOCHA_OBJECT
        || !argv[0].u.obj
        || ((mo = argv[0].u.obj)->clazz != &java_class)
        || ((java = mo->data)->type != JAVA_OBJECT)) {
	MOCHA_ReportError(mc, "getClass expects a Java object argument");
	return MOCHA_FALSE;
    }

    if (!(moclass = MOCHA_ReflectJClassToMObject(mc, java->cb))) {
        MOCHA_ReportError(mc, "getClass can't find Java class reflection");
        return MOCHA_FALSE;
    }

    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, moclass);
    return MOCHA_TRUE;
}

static MochaObject *
mocha_DefineJavaPackage(MochaContext *mc, MochaObject *obj,
                        char *mname, char *package)
{
    MochaJavaPackage *pack;

    pack = malloc(sizeof(MochaJavaPackage));
    if (!pack) {
        MOCHA_ReportOutOfMemory(mc);
        return 0;
    }

    pack->lastslot = 0;
    if (package) {
        pack->name = strdup(package);
        if (!pack->name) {
            MOCHA_ReportOutOfMemory(mc);
            free(pack);
            return 0;
        }
    } else
        pack->name = 0;
    return MOCHA_DefineNewObject(mc, obj, mname, &javapackage_class,
                                 pack, 0, 0, 0, 0);

    /* XXX can we make the package read-only? */
}

MochaBoolean
mocha_InitJava(MochaContext *mc, MochaObject *obj)
{
    MochaObject *mo;

    if (!MOCHA_JavaGlueEnabled()) {
        PR_LOG(Moja, warn, ("Moja disabled\n"));
        return MOCHA_TRUE;
    }

    PR_LOG(Moja, debug, ("mocha_InitJava(0x%x,0x%x)", mc, obj));

    /* define the top of the java package namespace as "Packages" */
    mo = mocha_DefineJavaPackage(mc, obj, "Packages", 0);

    /* some convenience packages */
    /* XXX these should be properties of the top-level package
     * too.  as it is there will be two different objects for
     * "java" and "Packages.java" which is unfortunate but mostly
     * invisible */
    mocha_DefineJavaPackage(mc, obj, "java", "java");
    mocha_DefineJavaPackage(mc, obj, "sun", "sun");
    mocha_DefineJavaPackage(mc, obj, "netscape", "netscape");

    MOCHA_DefineFunction(mc, obj, "getClass", mocha_getJavaClass, 0,
			 MDF_READONLY);

    /* any initialization that depends on java running is done in
     * mocha_FinishInitJava */

    return MOCHA_TRUE;
}


/***********************************************************************
 * these are the bytes for the class "JavaScript", defined as follows.
 * the bytes were generated with javac, od, and emacs
 ***********************************************************************/

#if 0
/* XXX should this be in a package? */

/**
 * Internal class for secure JavaScript->Java calls.
 * We load this class with a crippled ClassLoader, and make sure
 * that one of the methods from this class is on the Java stack
 * when JavaScript calls into Java.
 */
class JavaScript {
    /* can't be constructed */
    private JavaScript() {};

    /* this is the method that will be on the Java stack when
    // JavaScript calls Java.  it is never actually called,
    // but we put a reference to it on the stack as if it had
    // been. */
    static void callJava() {};
}
#endif /* 0 */

static jbyte JavaScript_class_bytes[] = {
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x03, 0x00, 0x2d, 0x00, 0x14, 0x07, 0x00, 0x07, 0x07, 0x00, 0x10,
    0x0a, 0x00, 0x02, 0x00, 0x04, 0x0c, 0x00, 0x11, 0x00, 0x13, 0x01, 0x00, 0x04, 0x74, 0x68, 0x69,
    0x73, 0x01, 0x00, 0x0d, 0x43, 0x6f, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x74, 0x56, 0x61, 0x6c, 0x75,
    0x65, 0x01, 0x00, 0x0a, 0x4a, 0x61, 0x76, 0x61, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x01, 0x00,
    0x12, 0x4c, 0x6f, 0x63, 0x61, 0x6c, 0x56, 0x61, 0x72, 0x69, 0x61, 0x62, 0x6c, 0x65, 0x54, 0x61,
    0x62, 0x6c, 0x65, 0x01, 0x00, 0x08, 0x63, 0x61, 0x6c, 0x6c, 0x4a, 0x61, 0x76, 0x61, 0x01, 0x00,
    0x0a, 0x45, 0x78, 0x63, 0x65, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x01, 0x00, 0x0f, 0x4c, 0x69,
    0x6e, 0x65, 0x4e, 0x75, 0x6d, 0x62, 0x65, 0x72, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x01, 0x00, 0x0a,
    0x53, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x46, 0x69, 0x6c, 0x65, 0x01, 0x00, 0x0e, 0x4c, 0x6f, 0x63,
    0x61, 0x6c, 0x56, 0x61, 0x72, 0x69, 0x61, 0x62, 0x6c, 0x65, 0x73, 0x01, 0x00, 0x04, 0x43, 0x6f,
    0x64, 0x65, 0x01, 0x00, 0x0f, 0x4a, 0x61, 0x76, 0x61, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x2e,
    0x6a, 0x61, 0x76, 0x61, 0x01, 0x00, 0x10, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67,
    0x2f, 0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e, 0x69, 0x74, 0x3e,
    0x01, 0x00, 0x0c, 0x4c, 0x4a, 0x61, 0x76, 0x61, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x3b, 0x01,
    0x00, 0x03, 0x28, 0x29, 0x56, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x02, 0x00, 0x11, 0x00, 0x13, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x2f, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x2a, 0xb7, 0x00, 0x03, 0xb1, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x0b, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x11, 0x00, 0x08, 0x00, 0x00,
    0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00, 0x12, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x09, 0x00, 0x13, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0xb1, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x06, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x17, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0f, 0x00,
};

/* now something to make a classloader and get a JavaScript object
 * from it. */

/* XXX garbage collection of these classes?  how should they be
 * protected? */

#include "java_lang_ClassLoader.h"

#define USELESS_CODEBASE_URL "http://bogus-codebase.netscape.com/"
/* XXX for now there is only one class loader for all
 * javascript->java calls, which has a bad codebase.
 * this is undesirable because it would be nice to be able
 * to pull in java classes from mocha, but the details of
 * which classloader to use (probably the decoder's origin_url)
 * are tricky, the netlib damage prevents us from loading new
 * classes over the net from the moz thread anyway, and i
 * need to go to sleep. */
/* XXX revisit this when we can load classes from the moz thread */
static JRIGlobalRef crippledJavaScriptClassLoader;

typedef struct {
    JRIEnv *env;
    MochaContext *mc;
    char *origin_url;
    JRIGlobalRef clazz;
    char *err;
} mocha_findSafeMethod_data;

static void
mocha_findSafeMethod_stub(void *d)
{
    mocha_findSafeMethod_data *data = (mocha_findSafeMethod_data *) d;
    int length = sizeof(JavaScript_class_bytes);
    HArrayOfByte *bytes;
    HClass *clazz;
    HObject *loader;
    char *err;
    ExecEnv *ee;
    HObject *url;

    ee = (ExecEnv*) data->env; /*XXX*/

    /* turn the origin_url into an AppletClassLoader from that site */

    /* make a java.net.URL from the origin_url */
    if (data->origin_url) {
        HString * str = makeJavaString(data->origin_url,
                                       strlen(data->origin_url));
	url =
          execute_java_constructor(ee, "java/net/URL", 0,
                                   "(Ljava/lang/String;)", str);
    } else {
        PR_LOG(Moja, warn,
               ("passing null codebaseURL for javascript classloader"));
        url = 0;
    }

    /* make the new AppletClassLoader */
    loader =
      execute_java_constructor(PRIVILEGED_EE,
                  "netscape/applet/AppletClassLoader", 0,
                  "(Lnetscape/applet/MozillaAppletContext;Ljava/net/URL;Ljava/net/URL;)",
                  0, url, url);
    if (exceptionOccurred(ee) || !loader) {
        err = "couldn't create AppletClassLoader for JavaScript";
        goto err;
    }

    /* make the array of bytes for class JavaScript */
    bytes = (HArrayOfByte *) ArrayAlloc(T_BYTE, length);
    if (!bytes) {
        err = "couldn't load allocate bytes for JavaScript.class";
        goto err;
    }
    memmove(unhand(bytes)->body, JavaScript_class_bytes, length);

    /* make class JavaScript */
    clazz = (HClass*)
      do_execute_java_method(ee, loader,
                             "defineClass", "(Ljava/lang/String;[BII)Ljava/lang/Class;", 
                             0, FALSE, NULL /* no required name */, bytes, 0, length);

    if (exceptionOccurred(ee) || !clazz) {
        err = "couldn't load class JavaScript";
        goto err;
    }

    data->clazz = JRI_NewGlobalRef(data->env, (jref) clazz);
    data->err = 0;

    return;

err:
    PR_LOG(Moja, error, ("%s", err));
    data->clazz = 0;
    data->err = err;
    return;
}

/* security: this method returns a methodblock which can be
 * used for secure mocha->java calls.  the methodblock is
 * guaranteed to have an associated AppletClassLoader which will
 * allow the SecurityManager to determine what permissions to
 * give to the Java code being called.
 */
static struct methodblock *
mocha_findSafeMethod(MochaContext *mc)
{
    mocha_findSafeMethod_data data;
    JRIGlobalRef clazz;
    ClassClass *cb;
    struct methodblock *mb;
    int i;

    /* first see if this MochaContext already has one */
    FINISH_MOCHA_JAVA_INIT(mc);

    /* first see if this MochaContext already has one */
    if (crippledJavaScriptClassLoader) {
        clazz = crippledJavaScriptClassLoader;
    } else {
#if 0
        char *tmp;
#endif
        FINISH_MOCHA_JAVA_INIT(mc);

        /* call java to get the class */
        data.env = mozenv;
        data.mc = mc;

#if 0
        /* strip off the last '/' and anything trailing it
         * to get a codebase */
        if (mc->script && mc->script->filename
            && (tmp = strrchr(mc->script->filename, '/'))) {
            int len = tmp - mc->script->filename;
            data.origin_url = (char*) malloc(len + 1);
            strncpy(data.origin_url, mc->script->filename, len);
            data.origin_url[len] = '\0';
        } else
#endif
        data.origin_url = strdup(USELESS_CODEBASE_URL); /* XXX */
        if (!data.origin_url) {
            MOCHA_ReportOutOfMemory(mc);
            return 0;
        }
        PR_LOG(Moja, debug, ("using origin_url \"%s\"", data.origin_url));

        LJ_CallJava(mozenv, mocha_findSafeMethod_stub, &data);

        clazz = data.clazz;
        free(data.origin_url);

        crippledJavaScriptClassLoader = clazz;
    }

    if (!clazz) {
        MOCHA_ReportError(mc, "%s", data.err);
        return 0;
    }

    cb = unhand((HClass*)JRI_GetGlobalRef(mozenv, clazz));
    /* find the static method callJava() for the class */
    for (i = 0; i < cb->methods_count; i++) {
        mb = cbMethods(cb) + i;
        if (!strcmp(fieldname(&mb->fb), "callJava")
            && !strcmp(fieldsig(&mb->fb), "()V")) {
            /* found it... */
            return mb;
        }
    }
    MOCHA_ReportError(mc, "can't find method JavaScript.callJava");
    return NULL;
}

/*
 * push a frame onto the java stack which does nothing except
 * provide a classloader for the security manager
 */
static MochaBoolean
mocha_pushSafeFrame(MochaContext *mc, ExecEnv *ee)
{
    JavaFrame *current_frame, *previous_frame;
    JavaStack *current_stack;
    struct methodblock *mb = mocha_findSafeMethod(mc);

    if (!mb)
        return MOCHA_FALSE;

    previous_frame = ee->current_frame;
    if (previous_frame == 0) {
        /* bottommost frame on this Exec Env. */
        current_stack = ee->initial_stack;
        current_frame = (JavaFrame *)(current_stack->data); /* no vars */
    } else {
        int args_size = mb->args_size;
        current_stack = previous_frame->javastack; /* assume same stack */
        if (previous_frame->current_method) {
            int size = previous_frame->current_method->maxstack;
            current_frame = (JavaFrame *)(&previous_frame->ostack[size]);
        } else {
            /* The only frames that don't have a mb are pseudo frames like
             * this one and they don't really touch their stack. */
            current_frame = (JavaFrame *)(previous_frame->optop + 3);
        }
        if (current_frame->ostack + args_size > current_stack->end_data) {
            /* Ooops.  The current stack isn't big enough.  */
            if (current_stack->next != 0) {
                current_stack = current_stack->next;
            } else {
                current_stack = CreateNewJavaStack(ee, current_stack);
                if (current_stack == 0) {
                    MOCHA_ReportOutOfMemory(mc);
                    return MOCHA_FALSE;
                }
            }
            /* no vars */
            current_frame = (JavaFrame *)(current_stack->data);
        }
    }
    ee->current_frame = current_frame;

    current_frame->prev = previous_frame;
    current_frame->javastack = current_stack;
    current_frame->optop = current_frame->ostack;
    current_frame->vars = 0;	/* better not reference any! */
    current_frame->monitor = 0;	/* not monitoring anything */

    /* make this be a method with the JS classloader */
    current_frame->current_method = mb;

    current_frame->optop += current_frame->current_method->args_size;
    current_frame->constant_pool = 0;

    return MOCHA_TRUE;
}

/*
 * pop the safety frame from the java stack
 */
static void
mocha_popSafeFrame(MochaContext *mc, ExecEnv *ee)
{
    ee->current_frame = ee->current_frame->prev;
}

#ifdef DEBUG_nix

static int
printToStdio(void* env,
             void* dest, size_t destLen,
             const char* srcBuf, size_t srcLen)
{
    /* ignore env, context, destLen */
    fwrite(srcBuf, 1, srcLen, (FILE*)dest);
    return 0;
}

void
stackToStderr(jref obj)
{
    printStackTrace(obj, printToStdio, stderr, 0);
}

#endif /* DEBUG_nix */

#endif /* defined(JAVA) */
