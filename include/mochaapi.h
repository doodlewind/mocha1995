#ifndef _mochaapi_h_
#define _mochaapi_h_
/*
** Mocha API.
**
** Brendan Eich, 6/21/95
*/
#include <stddef.h>
#include "prmacros.h"
#include "mo_pubtd.h"                   /* public typedefs */

NSPR_BEGIN_EXTERN_C

/*
** Mocha stack datum type.
**
** API clients should use only MOCHA_UNDEF, MOCHA_FUNCTION, MOCHA_OBJECT,
** MOCHA_NUMBER, MOCHA_BOOLEAN, and MOCHA_STRING.  Further, MochaFunction and
** MochaObject are mostly-opaque struct types, and the only method for getting
** valid pointers to instances of these types is to call MOCHA_DefineFunction()
** or MOCHA_NewObject(), e.g.
*/
typedef enum MochaTag {
    MOCHA_UNDEF,                        /* undefined value (void) */
    MOCHA_INTERNAL,                     /* internal handle (void *) */
    MOCHA_ATOM,                         /* unresolved identifier */
    MOCHA_SYMBOL,                       /* resolved symbol (lvalue) */
    MOCHA_FUNCTION,                     /* function pointer (rvalue) */
    MOCHA_OBJECT,                       /* object reference (rvalue) */
    MOCHA_NUMBER,                       /* floating point number (rvalue) */
    MOCHA_BOOLEAN,                      /* Boolean (rvalue) */
    MOCHA_STRING,                       /* character string (rvalue) */
    MOCHA_NTYPES
} MochaTag;

typedef struct MochaPair {
    MochaObject         *obj;           /* object containing this symbol */
    MochaSymbol         *sym;           /* member symbol in object scope */
} MochaPair;

/*
** API clients should not set or use nrefs.  All MochaDatum instances visible
** through this API are stack-allocated, either on the Mocha virtual machine
** interpreter's stack or on the client's C runtime stack.
**
** In other words, if you need to make a MochaDatum to return from a native
** function (see below for more), just declare it as a local variable in the
** function, set its members using MOCHA_INIT_DATUM, and return it.
*/
struct MochaDatum {
    MochaRefCount       nrefs;          /* reference count, must be first */
    uint8               tag;            /* union discriminant */
    uint8               flags;          /* flags, see below */
    uint16              taint;          /* taint code */
    union {
        void            *ptr;           /* internal pointer */
        MochaAtom       *atom;          /* literal */
        MochaPair       pair;           /* object/symbol pair */
        MochaFunction   *fun;           /* function pointer */
        MochaObject     *obj;           /* object pointer */
        MochaFloat      fval;           /* number */
        MochaBoolean    bval;           /* boolean */
    } u;
};

#define MDF_BACKEDGE    0x01            /* graph back-edge: do not refcount */
#define MDF_ENUMERATE   0x02            /* property is visible to for-in loop */
#define MDF_READONLY    0x04            /* set when property is read-only */
#define MDF_VISITED     0x08            /* visited bit for depth-first search */
#define MDF_TAINTED     0x10            /* for private/secret data tainting */
#define MDF_ALLFLAGS    0x1f            /* bit-set of all valid datum flags */

#ifdef DEBUG_brendan
#define MDF_TRACEBITS   0xa0            /* make sure these two bits propagate */
#else
#define MDF_TRACEBITS   0
#endif

#define MOCHA_ASSERT_VALID_DATUM_FLAGS(DP) \
    PR_ASSERT(((DP)->flags & ~MDF_ALLFLAGS) == MDF_TRACEBITS)

/* Well-known data tainting code values. */
#define MOCHA_TAINT_IDENTITY    0
#define MOCHA_TAINT_SHIST       ((uint16)0xfffd)
#define MOCHA_TAINT_JAVA        ((uint16)0xfffe)
#define MOCHA_TAINT_MAX         ((uint16)0xffff)

#define MOCHA_MIX_TAINT(MC, ACCUM, TAINT)                                     \
    NSPR_BEGIN_MACRO                                                          \
        uint16 _ACCUM = (ACCUM), _TAINT = (TAINT);                            \
        if (_ACCUM != _TAINT) {                                               \
            if (_ACCUM == MOCHA_TAINT_IDENTITY) {                             \
                (ACCUM) = _TAINT;                                             \
            } else if (_TAINT != MOCHA_TAINT_IDENTITY) {                      \
                if (mocha_MixTaint)                                           \
                    (ACCUM) = (*mocha_MixTaint)(MC, _ACCUM, _TAINT);          \
                else                                                          \
                    (ACCUM) = MOCHA_TAINT_MAX;                                \
            }                                                                 \
        }                                                                     \
    NSPR_END_MACRO

extern MochaTaintMixer mocha_MixTaint;

/*
** Call MOCHA_INIT_DATUM to initialize a datum passed by reference from the
** runtime into an object operation like OBJ_GET_PROPERTY, or as a result param
** in a native method (rval).
**
** Call MOCHA_INIT_FULL_DATUM to initialize an auto storage class temporary
** datum that's passed to MOCHA_SetProperty(), MOCHA_ConvertDatum(), etc.
*/
#define MOCHA_INIT_DATUM(MC,DP,TAG,ARM,VAL)                                   \
    NSPR_BEGIN_MACRO                                                          \
        (DP)->tag = TAG;                                                      \
        (DP)->ARM = VAL;                                                      \
    NSPR_END_MACRO

#define MOCHA_INIT_FULL_DATUM(MC,DP,TAG,FLAGS,TAINT,ARM,VAL)                  \
    NSPR_BEGIN_MACRO                                                          \
        MOCHA_INIT_DATUM(MC,DP,TAG,ARM,VAL);                                  \
        (DP)->flags = MDF_TRACEBITS | ((FLAGS) & MDF_ALLFLAGS);               \
        (DP)->taint = TAINT;                                                  \
    NSPR_END_MACRO

#define MOCHA_DATUM_IS_NULL(D)  ((D).tag == MOCHA_OBJECT && (D).u.obj == 0)

/* XXX should use macros from prdtoa.c, exported via a header... */
#include "prcpucfg.h"

#ifdef IS_LITTLE_ENDIAN
#define _word0(x) (((uint32 *)&(x))[1])
#define _word1(x) (((uint32 *)&(x))[0])
#else
#define _word0(x) (((uint32 *)&(x))[0])
#define _word1(x) (((uint32 *)&(x))[1])
#endif
#define _expmask  0x7ff00000

#define MOCHA_FLOAT_IS_NaN(x) ((_word0(x) & _expmask) == _expmask &&          \
			       (_word1(x) || (_word0(x) & 0x000fffff)))

/*
** Well-known global Mocha data.  Use these instead of making your own if
** your native function returns void or 0.
*/
extern MochaDatum MOCHA_void;
extern MochaDatum MOCHA_null;
extern MochaDatum MOCHA_zero;
extern MochaDatum MOCHA_NaN;
extern MochaDatum MOCHA_false;
extern MochaDatum MOCHA_true;
extern MochaDatum MOCHA_empty;          /* the empty string */

/*
** Hold any reference from *dp to an atom, function, or object.  If *dp is not
** of reference type (i.e., it's a number or boolean), do nothing.
*/
extern void
MOCHA_HoldRef(MochaContext *mc, MochaDatum *dp);

/*
** Drop any reference from *dp to an atom, function, or object.  If *dp is not
** of reference type (i.e., it's a number or boolean), do nothing.
*/
extern void
MOCHA_DropRef(MochaContext *mc, MochaDatum *dp);

/*
** Lower any reference count held by dp, but don't destroy the referenced
** object if it goes to zero.  Used to make weak links that are strengthened
** by the runtime, e.g., the dp value/result parameter of OBJ_GET_PROPERTY.
*/
extern void
MOCHA_WeakenRef(MochaContext *mc, MochaDatum *dp);

/*
** Convert d to the type indicated by tag and return the result in dp.
*/
MochaBoolean
MOCHA_ConvertDatum(MochaContext *mc, MochaDatum d, MochaTag tag,
                   MochaDatum *dp);

/*
** Convert d to a function reference.  Return true on success with *funp
** pointing to the held function, which the caller must release by calling
** MOCHA_DropObject().  Return false on error, implying MOCHA_ReportError()
** has been called.
*/
extern MochaBoolean
MOCHA_DatumToFunction(MochaContext *mc, MochaDatum d, MochaFunction **funp);

/*
** Convert d to an object reference.  Report the error and return false if
** there's no valid conversion.  Return true with *objp pointing to the held
** object on success.  It is up to the user to call MOCHA_DropObject() after
** using the returned object.
*/
extern MochaBoolean
MOCHA_DatumToObject(MochaContext *mc, MochaDatum d, MochaObject **objp);

/*
** Convert d to a number.  Report the error and return false if there's no
** valid conversion.  Return true with the number in *fvalp on success.
*/
extern MochaBoolean
MOCHA_DatumToNumber(MochaContext *mc, MochaDatum d, MochaFloat *fvalp);

/*
** Convert d to a Boolean.  Report the error and return false if there's no
** valid conversion.  Return true with the boolean in *bvalp on success.
*/
extern MochaBoolean
MOCHA_DatumToBoolean(MochaContext *mc, MochaDatum d, MochaBoolean *bvalp);

/*
** Convert d to a string representation.  Report the error and return null
** if out of memory.  Return a pointer to an internalized string structure,
** MochaAtom, which must be released via mocha_DropAtom().
*/
extern MochaBoolean
MOCHA_DatumToString(MochaContext *mc, MochaDatum d, MochaAtom **atomp);

/*
** XXX comment me
*/
extern const char *
MOCHA_TypeOfDatum(MochaContext *mc, MochaDatum d);

/*
** Initialize Mocha by calling MOCHA_NewContext(), which creates a new
** execution context for compiling and running Mocha scripts and functions.
** Its return value is an opaque pointer to the new Mocha context.
**
** When you're all through doing Mocha, you may call MOCHA_DestroyContext().
*/
extern MochaContext *
MOCHA_NewContext(size_t stackSize);

extern void
MOCHA_DestroyContext(MochaContext *mc);

extern MochaContext *
MOCHA_ContextIterator(MochaContext **iterp);

/* XXX comment me */
extern MochaObject *
MOCHA_GetGlobalObject(MochaContext *mc);

/* XXX comment me */
extern MochaBoolean
MOCHA_SetGlobalObject(MochaContext *mc, MochaObject *obj);

/* XXX comment me */
extern MochaObject *
MOCHA_GetStaticLink(MochaContext *mc);

/*
** Wrapper function that calls malloc but reports errors via mc.
*/
extern void *
MOCHA_malloc(MochaContext *mc, size_t nbytes);

extern char *
MOCHA_strdup(MochaContext *mc, const char *s);

extern void
MOCHA_free(MochaContext *mc, void *p);

/*
** Mocha Object structure.
** XXX eliminate nrefs via gc, make data a symbol in scope
*/
struct MochaObject {
    MochaRefCount       nrefs;          /* reference count, must be first */
    MochaClass          *clazz;         /* class pointer */
    void                *data;          /* private data */
    MochaScope          *scope;         /* symbol table and public data */
    MochaObject         *prototype;     /* prototype object; strong link */
    MochaObject         *parent;        /* parent scope's object; weak link */
};

/*
** Mocha object operations.  Mocha API clients may stub these with functions
** prototyped below, or provide their own specific implementations.
**
** The getProperty and setProperty functions allow clients to intercept gets
** and sets, and to modify the nominal property value being fetched or stored
** (the MochaDatum pointed at by dp).  Mocha properties are numbered by slot,
** which the API client defines (see below for more on this).  So for objects
** with fixed properties, the easiest way to implement getProperty/setProperty
** is with 'switch (slot) {...}' statements.
**
** The listProperties function is called just before an object's properties
** are enumerated by a for-in loop.  If an object reflects properties lazily,
** it should provide this method rather than a stub, and the function should
** eagerly add all unreflected properties to obj.
**
** The resolveName entry point should be used by objects to reflect properties
** lazily from their native source into Mocha (e.g., HTML page elements, which
** should only be reflected into Mocha if an embedded script mentions them).
** resolveName takes a name and adds a property or defines a symbol for it in
** the object's scope if name is resolvable.
**
** The dp value/result parameter to get/setProperty and convert need not be set
** by these methods: it is preset by the runtime to hold a nominal value: the
** current property value for get, the value to store for set, and MOCHA_void
** for convert.  Any new object reference returned via dp must be weak (as are
** refs returned by MOCHA_NewObject(), etc.).  The runtime will strengthen dp
** after a successful return from one of these methods.
**
** get/setProperty, listProperties, resolveName, and convert use the same error
** reporting convention as native functions: return MOCHA_FALSE after reporting
** the error with MOCHA_ReportError() or MOCHA_ReportOutOfMemory() on failure,
** return MOCHA_TRUE on success.
*/
struct MochaClass {
    const char      *name;
    MochaPropertyOp getProperty;
    MochaPropertyOp setProperty;
    MochaBoolean    (*listProperties)(MochaContext *mc, MochaObject *obj);
    MochaBoolean    (*resolveName)(MochaContext *mc, MochaObject *obj,
                                   const char *name);
    MochaBoolean    (*convert)(MochaContext *mc, MochaObject *obj,
                               MochaTag tag, MochaDatum *dp);
    void            (*finalize)(MochaContext *mc, MochaObject *obj);
};

/* Helper macros that take MochaObject * and call object operations. */
#define OBJ_GET_PROPERTY(mc, obj, slot, dp) \
        ((*(obj)->clazz->getProperty)(mc, obj, slot, dp))
#define OBJ_SET_PROPERTY(mc, obj, slot, dp) \
        ((*(obj)->clazz->setProperty)(mc, obj, slot, dp))
#define OBJ_LIST_PROPERTIES(mc, obj) \
        ((*(obj)->clazz->listProperties)(mc, obj))
#define OBJ_RESOLVE_NAME(mc, obj, name) \
        ((*(obj)->clazz->resolveName)(mc, obj, name))
#define OBJ_CONVERT(mc, obj, tag, dp) \
        ((*(obj)->clazz->convert)(mc, obj, tag, dp))
#define OBJ_FINALIZE(mc, obj) \
        ((*(obj)->clazz->finalize)(mc, obj))

/*
** Do-nothing stub for getProperty and setProperty, in case the object obj
** doesn't care about setProperty calls, and doesn't need to compute new
** values to satisfy getProperty calls.
*/
extern MochaBoolean
MOCHA_PropertyStub(MochaContext *mc, MochaObject *obj, MochaSlot slot,
                   MochaDatum *dp);

/*
** Do-nothing stub for MochaClass.listProperties.
*/
extern MochaBoolean
MOCHA_ListPropStub(MochaContext *mc, MochaObject *obj);

/*
** Do-nothing stub for MochaClass.resolveName.
*/
extern MochaBoolean
MOCHA_ResolveStub(MochaContext *mc, MochaObject *obj, const char *name);

/*
** Do-nothing stub for MochaClass.convert.
*/
extern MochaBoolean
MOCHA_ConvertStub(MochaContext *mc, MochaObject *obj, MochaTag tag,
                  MochaDatum *dp);

/*
** Do-nothing stub for MochaClass.finalize.
*/
extern void
MOCHA_FinalizeStub(MochaContext *mc, MochaObject *obj);

/*
** Mocha property specifier.
**
** A native object typically allocates property specifiers statically in an
** initialized array, passing the array to MOCHA_DefineObject() in the native
** object module's initialization function.  The array must be terminated with
** a specifier whose name is null.
**
** Example use, for a gauge widget written in C that exposes 3 properties to
** Mocha scripts:
**
**  enum gauge_slot { INITIAL_VALUE, REFRESH_RATE, REFRESH_URL };
**
**  static MochaPropertySpec gauge_props[] = {
**      {"initialValue",        INITIAL_VALUE},
**      {"refreshRate",         REFRESH_RATE},
**      {"refreshURL",          REFRESH_URL},
**      {0}
**  };
*/
struct MochaPropertySpec {
    char            *name;
    int16           slot;
    uint16          flags;
    MochaPropertyOp getter;
    MochaPropertyOp setter;
};

/*
** Mocha native function specifier.  Tells Mocha how to call a hand-written
** C function that may bridge to another language, like Java, or implement a
** low-level OS-specific function.  The call function pointer points to this
** user-supplied native function.  The function may return a MochaDatum via
** its *rval result parameter, but the default result is MOCHA_void.
**
** nargs is the minimum number of arguments to expect.  If fewer arguments
** are passed the rest will be passed as MOCHA_void.
**
** The fun member should not be set by the API client -- it should be left
** statically-initialized by default to 0.  After the first call that passes
** a vector of MochaFunctionSpec structs to MOCHA_DefineFunctions(), each vector
** element's fun pointer will point to a shared MochaFunction to be added to
** subsequent objects' scopes by reference.
*/
struct MochaFunctionSpec {
    char                *name;
    MochaNativeCall     call;
    uint8               nargs;
    uint8               flags;
    uint16              spare;
    MochaFunction       *fun;
};

/*
** Initialize clazz, defining a constructor named clazz->name in obj's scope.
** Construct the prototype object for clazz, giving it data, properties spec'd
** in ps, and methods in fs.  Add static properties and methods in static_ps
** and static_fs to the constructor's scope, so Number.MAX_VALUE and Date.UTC
** can be used.  Any of the final four arguments may be null.
**
** Return a pointer to the prototype, or null on error.
*/
extern MochaObject *
MOCHA_InitClass(MochaContext *mc, MochaObject *obj, MochaClass *clazz,
                void *data, MochaNativeCall constructor, unsigned nargs,
                MochaPropertySpec *ps, MochaFunctionSpec *fs,
                MochaPropertySpec *static_ps, MochaFunctionSpec *static_fs);

/*
** Return true with iff obj->clazz == clazz; return false otherwise, reporting
** the class error using the fun argument, which should be argv[-1].u.fun when
** in a native function.
*/
extern MochaBoolean
MOCHA_InstanceOf(MochaContext *mc, MochaObject *obj, MochaClass *clazz,
                 MochaFunction *fun);

/*
** Mocha native object constructor, which defines a name for the new object.
** Example call, in gauge initialization code:
**
**  obj = MOCHA_DefineNewObject(mc, obj, "gauge", &gauge_class, gauge_data,
**                              0, 0, gauge_props, gauge_methods);
**
** Note that gauge_data is the "void *data" member of the (MochaObject *)
** argument.  It should contain object private data, plus any (pointers to)
** allocation bookkeeping required by alloc and freeMemory.
*/
extern MochaObject *
MOCHA_DefineNewObject(MochaContext *mc, MochaObject *obj, const char *name,
                      MochaClass *clazz, void *data,
                      MochaObject *prototype, unsigned flags,
                      MochaPropertySpec *ps, MochaFunctionSpec *fs);

/*
** Sometimes it is useful to create an anonymous object.  Anonymous objects
** are destroyed automatically on last dereference, unless there are cycles
** from object to object, in which case a direct call to MOCHA_DestroyObject()
** is required somewhere.
*/
extern MochaObject *
MOCHA_NewObject(MochaContext *mc, MochaClass *clazz, void *data,
                MochaObject *prototype, MochaObject *parent,
                MochaPropertySpec *ps, MochaFunctionSpec *fs);

/*
** Bind nobj to the name given by atom in obj's scope, using a Mocha property.
** The flags argument should contain MochaDatum flags such as MDF_READONLY.
*/
extern MochaBoolean
MOCHA_DefineObject(MochaContext *mc, MochaObject *obj, const char *name,
                   MochaObject *nobj, unsigned flags);

/*
** Add the properties specified by the all-zeroes terminated array at ps to
** obj's scope.
*/
extern MochaBoolean
MOCHA_SetProperties(MochaContext *mc, MochaObject *obj, MochaPropertySpec *ps);

/*
** Named objects, those created via MOCHA_DefineObject(), must be destroyed
** with MOCHA_UndefineName(), not MOCHA_DestroyObject().  Reference-counted
** objects should be held with MOCHA_HoldObject() and destroyed on the last
** dereference with MOCHA_DropObject(), not by calling MOCHA_DestroyObject().
**
** The Mocha runtime takes care of holding and dropping objects created with
** zero reference count by a native or getProperty function.  Native code
** should just return the pointer returned by MOCHA_NewObject(), and not use
** MOCHA_HoldObject().
*/
extern void
MOCHA_DestroyObject(MochaContext *mc, MochaObject *obj);

/*
** Add a property with the given name and slot to an object.  Use this to
** create dynamic properties, and then be prepared to handle new slot numbers
** in your getProperty and setProperty object ops.  If name is null, add the
** property indexed by slot.
**
** Returns MOCHA_FALSE if out of memory, MOCHA_TRUE otherwise.
*/
extern MochaBoolean
MOCHA_SetProperty(MochaContext *mc, MochaObject *obj, const char *name,
                  MochaSlot slot, MochaDatum datum);

/*
** Remove a named property from an object.
** XXX need to take MochaDatum for number/boolean/string key
*/
extern void
MOCHA_RemoveProperty(MochaContext *mc, MochaObject *obj, const char *name);

/*
** Get, set (add), and remove property by slot instead of by name.
*/
extern MochaBoolean
MOCHA_GetSlot(MochaContext *mc, MochaObject *obj, MochaSlot slot,
              MochaDatum *dp);

extern MochaBoolean
MOCHA_SetSlot(MochaContext *mc, MochaObject *obj, MochaSlot slot,
              MochaDatum datum);

extern void
MOCHA_RemoveSlot(MochaContext *mc, MochaObject *obj, MochaSlot slot);

/*
** XXX comment me
*/
MochaAtom *
MOCHA_Atomize(MochaContext *mc, const char *name);

/*
** XXX comment me
*/
const char *
MOCHA_GetAtomName(MochaContext *mc, MochaAtom *atom);

/*
** XXX comment me
*/
MochaAtom *
MOCHA_HoldAtom(MochaContext *mc, MochaAtom *atom);

/*
** XXX comment me
*/
MochaAtom *
MOCHA_DropAtom(MochaContext *mc, MochaAtom *atom);

/*
** Create a native function with the given callback and number of arguments.
** Don't associate it with a name in any scope.  It is up to the caller to
** free the returned structure via MOCHA_DestroyFunction() when no references
** remain.
**
** MochaFunction derives from MochaObject, so it's safe to cast up.
*/
extern MochaFunction *
MOCHA_NewFunction(MochaContext *mc, MochaObject *obj, MochaNativeCall call,
                  unsigned nargs);

extern void
MOCHA_DestroyFunction(MochaContext *mc, MochaFunction *fun);

/*
** Define a native function with the given name in obj's scope, and with the
** given C callback and number of arguments.  The call arguments points to a
** function of the following form:
**
**  MochaBoolean
**  call(MochaContext *mc, MochaObject *obj, unsigned argc,
**       MochaDatum *argv, MochaDatum *rval);
**
** The flags argument should contain MochaDatum flags such as MDF_READONLY.
*/
extern MochaFunction *
MOCHA_DefineFunction(MochaContext *mc, MochaObject *obj, const char *name,
                     MochaNativeCall call, unsigned nargs, unsigned flags);

extern MochaBoolean
MOCHA_DefineFunctions(MochaContext *mc, MochaObject *obj,
                      MochaFunctionSpec *fs);

/*
** Increment obj's reference count.  Use this before a MOCHA_DefineObject()
** or MOCHA_UndefineName() call that will decrement obj's reference count, so
** you can detach obj from its name and keep hold of it anonymously.
*/
extern MochaObject *
MOCHA_HoldObject(MochaContext *mc, MochaObject *obj);

/*
** Decrement obj's reference count, destroying it and returning null if the
** count goes to zero.  Return obj otherwise.
**
** If the reference count goes to zero, obj must not be named in any scope.
** You must call MOCHA_UndefineName() to drop a named object reference.
*/
extern MochaObject *
MOCHA_DropObject(MochaContext *mc, MochaObject *obj);

/*
** Lookup a name in obj's scope, returning MOCHA_FALSE only on error, else
** MOCHA_TRUE with *dp initialized to describe name's value, or MOCHA_UNDEF
** if name is not bound in obj.
*/
extern MochaBoolean
MOCHA_LookupName(MochaContext *mc, MochaObject *obj, const char *name,
                 MochaDatum *dp);

/*
** Like MOCHA_LookupName, but resolve undefined names via OBJ_RESOLVE_NAME.
*/
extern MochaBoolean
MOCHA_ResolveName(MochaContext *mc, MochaObject *obj, const char *name,
                  MochaDatum *dp);

/*
** Remove a name from the given object's scope.
*/
extern void
MOCHA_UndefineName(MochaContext *mc, MochaObject *obj, const char *name);

/*
** Remove all names from the given object's scope.
*/
extern void
MOCHA_ClearScope(MochaContext *mc, MochaObject *obj);

/*
** Compile Mocha source in the input buffer starting at base and continuing
** for length bytes, returning MOCHA_TRUE on success with the code member of
** script pointing at the generated bytecode.  Return MOCHA_FALSE on failure.
** On failure, the error-reporting function set by MOCHA_SetErrorReporter()
** (see below) will be called with details about the failure.
**
** NB: the buffer does not need to be a '\0'-terminated string.
*/
extern MochaScript *
MOCHA_CompileBuffer(MochaContext *mc, MochaObject *obj,
                    const char *base, size_t length,
                    const char *filename, unsigned lineno);

#ifdef MOCHAFILE
/*
** Like MOCHA_CompileBuffer, but opens filename readonly and reads it.
*/
extern MochaScript *
MOCHA_CompileFile(MochaContext *mc, MochaObject *obj, const char *filename);
#endif

/*
** Compile the source (base,length) into an object method for the given name
** and number of arguments.  Return MOCHA_TRUE on success, else MOCHA_FALSE.
** On failure, the error-reporting function set by MOCHA_SetErrorReporter()
** (see below) will be called with details about the failure.
*/
extern MochaBoolean
MOCHA_CompileMethod(MochaContext *mc, MochaObject *obj,
                    const char *name, unsigned nargs,
                    const char *base, size_t length,
                    const char *filename, unsigned lineno);

/*
** XXX comment me
*/
MochaBoolean
MOCHA_DecompileScript(MochaContext *mc, MochaScript *script, const char *name,
                      unsigned indent, char **sp);

/*
** XXX comment me
*/
MochaBoolean
MOCHA_DecompileFunctionBody(MochaContext *mc, MochaFunction *fun,
                            unsigned indent, char **sp);

/*
** Execute Mocha script compiled from MOCHA_Compile*().  Return MOCHA_TRUE
** on success with the last stack datum stored via result.  Return MOCHA_FALSE
** on failure, which means an error was reported (see mocha_SetErrorReporter(),
** below, for more on error reporting).
*/
extern MochaBoolean
MOCHA_ExecuteScript(MochaContext *mc, MochaObject *obj, MochaScript *script,
                    MochaDatum *result);

/*
** Destroy the given script, which must have been constructed by an earlier
** successful call to MOCHA_CompileBuffer().
*/
extern void
MOCHA_DestroyScript(MochaContext *mc, MochaScript *script);

/*
** A composite MOCHA_CompileBuffer/MOCHA_ExecuteScript utility function.
*/
extern MochaBoolean
MOCHA_EvaluateBuffer(MochaContext *mc, MochaObject *obj,
                     const char *base, size_t length,
                     const char *filename, unsigned lineno,
                     MochaDatum *result);

/*
** Call the named method (function or function-valued property) in obj with
** the given arguments.  Return true on success with *result set.  On error
** of any kind, return false.
*/
extern MochaBoolean
MOCHA_CallMethod(MochaContext *mc, MochaObject *obj, const char *name,
                 unsigned argc, MochaDatum *argv, MochaDatum *result);

/*
** Arrange for a function to be called on every branch, in order to schedule
** other threads, police infinite loops, etc.  The function pointed to by cb
** has the following prototype:
**
**  MochaBoolean
**  myMochaBranchCallback(MochaContext *mc, MochaScript *script);
**
** If it returns MOCHA_FALSE, the currently running script aborts.  Otherwise
** the script continues to execute.
*/
extern MochaBranchCallback
MOCHA_SetBranchCallback(MochaContext *mc, MochaBranchCallback cb);

/*
** Predicate telling whether the Mocha interpreter is currently running.
*/
extern MochaBoolean
MOCHA_IsRunning(MochaContext *mc);

/*
** Report an exception represented by the sprintf-like conversion of format
** and its arguments.  This exception message string is passed to a pre-set
** MochaErrorReporter function (see immediately below).
*/
extern void
MOCHA_ReportError(MochaContext *mc, const char *format, ...);

/*
** Complain when out of memory.
*/
extern void
MOCHA_ReportOutOfMemory(MochaContext *mc);

/*
** Register a function for reporting diagnostics.  The message string passed
** to the reporter may contain newlines, but will not end in a newline.
**
**  void
**  myErrorReporter(MochaContext *context, const char *message,
**                  MochaErrorReport *report);
**
** The final report argument is either null or a pointer to the following
** structure.  NB: the linebuf member points to a source line without the
** final newline.  If the source line is overlong, only the first buffer's
** worth is passed by reference.
*/
struct MochaErrorReport {
    const char  *filename;      /* source file name, URL, etc., or null */
    unsigned    lineno;         /* source line number */
    const char  *linebuf;       /* offending source line without final '\n' */
    const char  *tokenptr;      /* pointer to error token in linebuf */
};

/*
** Register a per-context error-reporter function.
*/
extern MochaErrorReporter
MOCHA_SetErrorReporter(MochaContext *mc, MochaErrorReporter reporter);

/*
** Data tainting structure and routines.
*/
struct MochaTaintInfo {
    uint16              taint;          /* a context's taint identifier */
    uint16              accum;          /* and its taint accumulator */
    void                *data;          /* private data pointer */
};

extern void
MOCHA_SetTaintCallbacks(MochaTaintCounter hold, MochaTaintCounter drop,
                        MochaTaintMixer mix);

extern MochaTaintInfo *
MOCHA_GetTaintInfo(MochaContext *mc);

extern void
MOCHA_SetTaintInfo(MochaContext *mc, MochaTaintInfo *info);

extern MochaBoolean
MOCHA_SetPropertyTaint(MochaContext *mc, MochaObject *obj, const char *name,
                       uint16 taint);

typedef int (*MochaCharFilter)(void *, char);

extern void
MOCHA_SetCharFilter(MochaContext *mc, MochaCharFilter filter, void *arg);

NSPR_END_EXTERN_C

#endif /* _mochaapi_h_ */
