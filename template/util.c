#include "util.h"

// This is the function defined in the header
// can be any util functions
float fold(float x, float threshold) {
	while(fabsf(x) > threshold) {
		if (x > threshold) {
			x = 2 * threshold - x;
		} else {
			x = -2 * threshold - x;
		}
	}
	return x;
}
