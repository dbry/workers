## WORKERS

Lightweight Workers Thread Manager in C for Windows and POSIX with Prime Number Generating Demo

Copyright (c) 2025 David Bryant.

All Rights Reserved.

Distributed under the [BSD Software License](https://github.com/dbry/workers/blob/master/LICENSE).

## What is this?

This is a lightweight workers thread manager (aka thread pool) written in C. I had added multithreading
to several of my applications and got tired of doing the same thing over and over, so I decided to
abstract away the thread manager portion and create a new module.

I also created a demo application that utilizes the manager to calculate and count the prime numbers
below a given value (up to a quadrillion, or 10<sup>15</sup>).

## What are the key features?

* Works natively with either Windows threads or POSIX threads (pthreads)
* Synchronization functionality to optionally serialize job completetion
* Simple to integrate (single .h and .c files) with intuitive API

## What is it not?

This is generally intended for situations where roughly the same work is split among multiple processor
cores for performance reasons (like large mathematical calculations, simulations, or audio/video
processing). It is **not** intended for splitting various unrelated or dissimilar tasks into various
threads, but it **may** be suitable for that. I simply haven't thought too much about that application. 

Also, note that there is no separate work queue implemented here (when a job is submitted we block until
a worker thread is available). This could be easily implemented outside the manager with another thread,
although I've never found a need for this.

## What's up with the demo application?

To demonstrate the functionality and efficiency of the worker thread manager, I created a simple command-line
application to directly calculate π(N), which is the number of prime numbers less than the given value. This
application also demonstrates the synchronization feature, and shows an alternative using the builtin GCC
atomic intrinsics (I'm sure equivalents exists for Windows).

The command-line arguments are just the value N and, optionally, the number or worker threads to create
(from 0 to 100).

## File descriptions

| File        | Description                                                                     |
|-------------|---------------------------------------------------------------------------------|
| workers.h   | C header file for the worker thread manager                                     |
| workers.c   | C source file for the worker thread manager, including the API documentation    |
| primes.c    | C source for the the prime number generator                                     |

