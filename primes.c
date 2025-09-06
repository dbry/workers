//////////////////////////////////////////////////////////////////////////////
//                            **** PRIMES ****                              //
//                 Calculate π(N) Utilizing Multithreading                  //
//                    Copyright (c) 2025 David Bryant.                      //
//                          All Rights Reserved.                            //
//         Distributed under the BSD Software License (see LICENSE)         //
//////////////////////////////////////////////////////////////////////////////

// primes.c

// This program calculates all the primes less than a given value and counts them.
// The primes are calculated using the sieve of Eratosthenes, with only the odd
// integers stored in the array because, except for 2, even numbers cannot be
// prime. This allows each byte to effectively represent 16 values.
//
// To calculate π(N) for very large values of N where available memory would be
// a limiting factor, we perform the sieve in strips. And to take advantage of
// multicore processors, we process the strips in separate worker threads which
// are managed by a separate library.
//
// Note that there are more advanced and more efficient methods for calculating
// π(N), such as the Meissel-Lehmer method, but these are not implemented here.

#ifdef __GNUC__
#define __USE_MINGW_ANSI_STDIO 1
#include <locale.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "workers.h"

// This is the structure that is used to interface to the slice calculator. The
// worker manager requires everything that needs to be passed in or out of the
// worker thread be stored in a single structure (although of course pointers
// to external data are allowed, with the user ensuring thread safety).

typedef struct {
    const unsigned char *base_primes;   // input: source primes table
    uint64_t slice_start;               // input: start value of slice (multiple of 16)
    int slice_values;                   // input: number of values to consider
    uint64_t *total_primes;             // output: pointer to total primes counter
    uint64_t *last_prime;               // output: pointer to last prime storage
} prime_slice_interface;

static int prime_slice (void *context, void *worker);

// This is the main function. It accepts a max prime value and an optional worker
// thread count on the command-line and performs the calculation. When done it prints
// the number of primes found and the last prime.

int main (int argc, char **argv)
{
    int max_base_prime, num_slices = 0, num_workers = 4;
    uint64_t max_prime;

#ifdef __GNUC__
    setlocale (LC_NUMERIC, "");
#endif

    if (argc < 2) {
        printf ("\nusage: primes <max value> [num workers]\n");
        printf ("note:  max value must be at least 10 and no greater than a quadrillion (\"1e15\")\n");
        printf ("note:  num workers can be from 0 (no threading) to 100 (default is 4)\n\n");
        return 0;
    }

    max_prime = (uint64_t) strtod (argv [1], NULL);

    // based on the size of N, determine strategy (including possibly not using threads at all)

    if (max_prime > 1000000000000000ULL) {
        printf ("\nsorry, this program is limited to a quadrillion!\n\n");
        return 1;
    }
    else if (max_prime > 1000000000000ULL) {
        max_base_prime = (int) ceil (sqrt (max_prime));
        max_base_prime += -max_base_prime & 0xf;
        num_slices = (int) ceil ((double)(max_prime - max_base_prime) / max_base_prime);
    }
    else if (max_prime > 1048576) {
        max_base_prime = 1048576;
        num_slices = (int) ceil ((double)(max_prime - max_base_prime) / max_base_prime);
    }
    else if (max_prime >= 10) {
        max_base_prime = max_prime;
        max_base_prime += -max_base_prime & 0xf;
    }
    else {
        printf ("\nsorry, max value must be at least 10!\n\n");
        return 1;
    }

    if (argc > 2)
        num_workers = atoi (argv [2]);

    if (num_workers < 0 || num_workers > 100) {
        printf ("\nif specified, number of workers must be from 0 to 100!\n\n");
        return 1;
    }

    // first we allocate and calculate the primes for the "base"

    unsigned char *primes = calloc (1, max_base_prime / 16);

    primes [0] |= 1;                                // 1 is not prime

    for (int tprime = 3; tprime * tprime < max_base_prime; tprime += 2)
        if (!(primes [tprime >> 4] & ((tprime & 1) << ((tprime >> 1) & 0x7))))
            for (int cprime = tprime * tprime; cprime < max_base_prime; cprime += tprime * 2)
                primes [cprime >> 4] |= 1 << ((cprime >> 1) & 0x7);

    uint64_t prime_count = 1, last_prime;           // 1 prime already accounted for (2)

    for (int tprime = 3; tprime < max_base_prime && tprime < max_prime; tprime += 2)
        if (!(primes [tprime >> 4] & ((tprime & 1) << ((tprime >> 1) & 0x7)))) {
            last_prime = tprime;
            prime_count++;
        }

    if (num_slices)
#ifdef __GNUC__
        printf ("base primes: there are %'d primes less than %'d; the last is %'d\n",
            (int) prime_count, max_base_prime, (int) last_prime);
#else
        printf ("base primes: there are %d primes less than %d; the last is %d\n",
            (int) prime_count, max_base_prime, (int) last_prime);
#endif
    else
#ifdef __GNUC__
        printf ("there are %'d primes less than %'d; the last is %'d\n",
            (int) prime_count, (int) max_prime, (int) last_prime);
#else
        printf ("there are %d primes less than %d; the last is %d\n",
            (int) prime_count, (int) max_prime, (int) last_prime);
#endif

    // If we need to do additional slices, that's done here. Note that all the slices are
    // the same size as the "base" data, except for possibly the last one,

    if (num_slices) {
        Workers *workers = workersInit (num_workers);
        int progress_percent = -1;

        printf ("processing %d slices using %d threads...\n", num_slices, num_workers);

        for (int slice = 1; slice <= num_slices; ++slice) {
            prime_slice_interface *interface = malloc (sizeof (prime_slice_interface));

            interface->base_primes = primes;
            interface->slice_start = (uint64_t) max_base_prime * slice;
            interface->total_primes = &prime_count;
            interface->last_prime = &last_prime;

            // For the last slice we calculate a possibly truncated size because this is where the
            // "leftover" values are. Also, we can do this on the main thread because we have to
            // wait for everything else to complete anyway afterward.

            if (slice == num_slices) {
                interface->slice_values = max_prime - interface->slice_start;
                workersEnqueueJob (workers, prime_slice, interface, DontUseWorkerThread);
            }
            else {
                interface->slice_values = max_base_prime;
                workersEnqueueJob (workers, prime_slice, interface, WaitForAvailableWorkerThread);
            }

            if (num_slices > 1000) {
                int percent = (slice * 100 + (num_slices / 2)) / num_slices;

                if (percent != progress_percent) {
                    fprintf (stderr, "\rprogress: %d%%%s", progress_percent = percent, percent == 100 ? " (done)\n" : " ");
                    fflush (stderr);
                }
            }
        }

        // wait for all the worker threads run to completion and destroy the worker thread manager

        workersWaitAllJobs (workers);
        workersDeinit (workers);

        // report the results

#ifdef __GNUC__
        printf ("there are %'llu primes less than %'llu; the last is %'llu\n", (unsigned long long) prime_count,
            (unsigned long long) max_prime, (unsigned long long) last_prime);
#else
        printf ("there are %llu primes less than %llu; the last is %llu\n", (unsigned long long) prime_count,
            (unsigned long long) max_prime, (unsigned long long) last_prime);
#endif
    }

    free (primes);
    return 0;
}

// This is the function that calculates the primes in a strip of values, counts
// them and updates a global count. It also updates a variable holding the highest
// prime calculated. Of course, this requires a pre-built table containing the
// primes up to the square root of the highest prime requested. This function is
// written to use just 32-bit math as much possible for performance, but otherwise
// should be able to handle primes up to 2^60, which would require the supplied
// primes to go up to 2^30. The strips always must start on multiples of 16.
// The value count does not need to be a multiple of 16, however we will round
// this up to an even byte in the slice and calculate primes for the whole slice,
// and then ignore the last few when counting them.

static int prime_slice (void *context, void *worker)
{
    prime_slice_interface *cxt = context;
    int prime_count = cxt->slice_values, slice_count = prime_count + (-prime_count & 0xf);
    int tprime_limit = (int) ceil (sqrt (cxt->slice_start + slice_count));
    unsigned char *slice_primes = calloc (1, slice_count / 16);
    uint64_t num_primes = 0, last_prime = 0;

    for (int tprime = 3; tprime < tprime_limit; tprime += 2)
        if (!(cxt->base_primes [tprime >> 4] & (1 << ((tprime >> 1) & 0x7))))
            for (int cprime = ((cxt->slice_start + tprime - 1) / (tprime * 2) * 2 + 1) * tprime - cxt->slice_start; cprime < slice_count; cprime += tprime * 2)
                slice_primes [cprime >> 4] |= 1 << ((cprime >> 1) & 0x7);

    for (int tprime = 1; tprime < prime_count; tprime += 2)
        if (!(slice_primes [tprime >> 4] & (1 << ((tprime >> 1) & 0x7)))) {
            last_prime = cxt->slice_start + tprime;
            num_primes++;
        }

    // The sync here is REQUIRED for correct operation. Without it the "last prime" calculated is often wrong,
    // which makes sense. However, less obvious is that the "total primes" is also often wrong because it's
    // no longer modified atomically. This is known edge case that we don't often see consistently show up in
    // real life, but do primes to a tillion and it will happen many times every run (and always differently).
    // Another way to fix this problem here is to replace the addition and prime store with code utilizing gcc
    // builtins to do the operations atomically, and in most cases that will be faster (but is not always as
    // straightforward as this case).

#if 1
    workerSync (worker);
    *cxt->total_primes += num_primes;
    *cxt->last_prime = last_prime;
#else
    __atomic_add_fetch (cxt->total_primes, num_primes, __ATOMIC_RELAXED);

    uint64_t old_last;
    do
        old_last = __atomic_load_n (cxt->last_prime, __ATOMIC_RELAXED);
    while (last_prime > old_last && !__atomic_compare_exchange_n (cxt->last_prime, &old_last, last_prime, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
#endif

    // free our primes slice storage (because we allocated it) and also free the job context (which we did
    // not allocate, but this is a good place to do it so that the caller does not have to deal with that).

    free (slice_primes);
    free (cxt);
    return 0;
}

