#pragma once

#include <stddef.h>

void *allocate(size_t size);

void deallocate(void *ptr);
void deallocate_sized(void *ptr, size_t size);
