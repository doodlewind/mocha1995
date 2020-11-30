#ifndef prclist_h___
#define prclist_h___

#include "prmacros.h"
#include "prtypes.h"

/*
** Circular linked list
*/
struct PRCListStr {
    PRCList	*next;
    PRCList	*prev;
};

/*
** Insert element "_e" into the list, before "_l".
*/
#define PR_INSERT_BEFORE(_e,_l)	 \
    NSPR_BEGIN_MACRO		 \
	(_e)->next = (_l);	 \
	(_e)->prev = (_l)->prev; \
	(_l)->prev->next = (_e); \
	(_l)->prev = (_e);	 \
    NSPR_END_MACRO

/*
** Insert element "_e" into the list, after "_l".
*/
#define PR_INSERT_AFTER(_e,_l)	 \
    NSPR_BEGIN_MACRO		 \
	(_e)->next = (_l)->next; \
	(_e)->prev = (_l);	 \
	(_l)->next->prev = (_e); \
	(_l)->next = (_e);	 \
    NSPR_END_MACRO

/*
** Append an element "_e" to the end of the list "_l"
*/
#define PR_APPEND_LINK(_e,_l) PR_INSERT_BEFORE(_e,_l)

/*
** Insert an element "_e" at the head of the list "_l"
*/
#define PR_INSERT_LINK(_e,_l) PR_INSERT_AFTER(_e,_l)

/* Return the head/tail of the list */
#define PR_LIST_HEAD(_l) (_l)->next
#define PR_LIST_TAIL(_l) (_l)->prev

/*
** Remove the element "_e" from it's circular list.
*/
#define PR_REMOVE_LINK(_e)	       \
    NSPR_BEGIN_MACRO		       \
	(_e)->prev->next = (_e)->next; \
	(_e)->next->prev = (_e)->prev; \
    NSPR_END_MACRO

/*
** Remove the element "_e" from it's circular list. Also initializes the
** linkage.
*/
#define PR_REMOVE_AND_INIT_LINK(_e)    \
    NSPR_BEGIN_MACRO		       \
	(_e)->prev->next = (_e)->next; \
	(_e)->next->prev = (_e)->prev; \
	(_e)->next = (_e);	       \
	(_e)->prev = (_e);	       \
    NSPR_END_MACRO

/*
** Return non-zero if the given circular list "_l" is empty, zero if the
** circular list is not empty
*/
#define PR_CLIST_IS_EMPTY(_l) \
    ((_l)->next == (_l))

/*
** Initialize a circular list
*/
#define PR_INIT_CLIST(_l)  \
    NSPR_BEGIN_MACRO	   \
	(_l)->next = (_l); \
	(_l)->prev = (_l); \
    NSPR_END_MACRO

#define PR_INIT_STATIC_CLIST(_l) \
    {(_l), (_l)}

#endif /* prclist_h___ */
