#ifndef KISS_FFT_LOG_H
#define KISS_FFT_LOG_H
#include <stdio.h>
#define KISS_FFT_ERROR(msg) fprintf(stderr, "KISS_FFT_ERROR: %s\n", msg)
#define KISS_FFT_WARNING(msg) fprintf(stderr, "KISS_FFT_WARNING: %s\n", msg)
#endif
