#include <math.h>

long intpow(long base, long exp){
	if(exp == 0)
		return 1;

	long mult = base;

	// this is kinda inefficient but works for now I guess

	// i = 1 because value only really changes when the exponent is 2

	for(int i = 1; i < exp; ++i){
		base *= mult;
	}

	return base;


}
