#include <stdlib.h>
#include <stdint.h>

// https://www.geeksforgeeks.org/move-zeroes-end-array/
size_t push_zeroes_to_end(uint32_t arr[], size_t n) {
	size_t count = 0;

	for (size_t i = 0; i < n; i++) {
		if (arr[i] != 0) {
			arr[count++] = arr[i];
		}
	}

	size_t ret = count;

	while (count < n) {
		arr[count++] = 0;
	}

	return ret;
}
