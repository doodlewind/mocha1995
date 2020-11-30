#include "prtypes.h"

/*
** Compute the log of the least power of 2 greater than or equal to n
*/
PR_PUBLIC_API(int32) PR_CeilingLog2(uint32 n)
{
    int32 log2 = 0;

    if (n == 0)
	return -1;
    if (n & (n-1))
	log2++;
    if (n >> 16)
	log2 += 16, n >>= 16;
    if (n >> 8)
	log2 += 8, n >>= 8;
    if (n >> 4)
	log2 += 4, n >>= 4;
    if (n >> 2)
	log2 += 2, n >>= 2;
    if (n >> 1)
	log2++;
    return log2;
}
