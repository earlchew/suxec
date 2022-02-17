#ifndef SUXEC_SPLICE_PATH_H
#define SUXEC_SPLICE_PATH_H
/* -*- c-basic-offset:4; indent-tabs-mode:nil -*- vi: set sw=4 et: */
/*
// Copyright (c) 2022, Earl Chew
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the names of the authors of source code nor the names
//       of the contributors to the source code may be used to endorse or
//       promote products derived from this software without specific
//       prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL EARL CHEW BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "finally.h"

#include "stpcpyv.c.h"

/* -------------------------------------------------------------------------- */
/* Splice path from for base and directory names
 *
 */

static int
splice_path(char **aPath, const char *aDirName, const char *aBaseName)
{
    int rc = -1;

    /* Join the lhs and rhs together, separated by a slash, and
     * always add an additional slash as a sentinel. The sentinel
     * simplifies checking for the end of the path since the terminating
     * nul will only occur after a slash, and there will always be
     * a slash to mark the end of a name.
     */

    size_t lhsLen = aDirName ? strlen(aDirName) : 0;
    size_t rhsLen = aBaseName ? strlen(aBaseName) : 0;

    size_t pathBufLen = lhsLen + sizeof("/") + rhsLen + sizeof("/");

    char *pathBuf = malloc(pathBufLen);
    if (!pathBuf)
        goto Finally;
    if (!lhsLen)
        strcpy(strcpy(pathBuf, aBaseName) + rhsLen, "/");
    else if (!rhsLen)
        strcpy(strcpy(pathBuf, aDirName) + lhsLen, "/");
    else if (!stpcpyv(pathBuf, pathBufLen, aDirName, "/", aBaseName, "/", 0))
        goto Finally;

    DEBUG("Spliced path %s", pathBuf);

    char *rhsp = pathBuf;

    /* If the path is relative, skip the first name to find the
     * first slash. Note that this slash might be the sentinel
     * that terminates the path.
     */

    while ('/' != *rhsp)
        ++rhsp;

    char *lhsp = rhsp;

    while (1) {

        /* Making use of of the fact that there is a final slash
         * as a sentinel, each iteration of the loop starts at
         * a slash.
         *
         * Advance the pointer until it points at the final
         * slash in a sequence.
         */

        while ('/' == rhsp[1])
            ++rhsp;

        /* There are four interesting cases at scanned path:
         *
         *  1. "..../"
         *  2. "...././"
         *  3. "..../../"
         *  4. "..../name/"
         *
         * Case 4 is the most common, so check for that first,
         * combining that with a check for Case 1.
         */

        if ('.' != rhsp[1]) {

            /* Check for Case 1 which detects the terminating sentinel
             * at the end of the path. Take care of the special case
             * where the path contains only slashes.
             */

            if (!rhsp[1]) {
                if (pathBuf == lhsp)
                    *lhsp++ = '/';
                break;
            }

            /* This is Case 4, but check of the special cases where
             * the emitted path contains:
             *
             *  a. "/" - Overwrite with "/name"
             *  b. "." - Overwrite with "name"
             */

            if (pathBuf+1 == lhsp) {
                if ('/' == lhsp[-1]) {
                    --lhsp;
                } else if ('.' == lhsp[-1]) {
                    --lhsp;
                    ++rhsp;
                }
            }

        } else if ('/' == rhsp[2]) {

            /* This is Case 2 which is treated simply by moving
             * the scanner past the dot name.
             */

            rhsp += 2;
            continue;

        } else if ('.' == rhsp[2] && '/' == rhsp[3]) {

            /* This is Case 3 which requires the previous name
             * in the emitted path to be retracted, if possible.
             * There are four special cases for the emitted path:
             *
             *  a. ""     - Scanning absolute path starting with "/../"
             *  b. "."    - Scanning relative path starting with "./"
             *  c. ".."   - Scanning a sequence of "/.."
             *  d. "name" - Backing up to a parent directory
             */

            if (pathBuf == lhsp) {

                /* This is Case a which can be dealt with by dropping
                 * the leading "/../".
                 */

                rhsp += 3;
                continue;

            } else if (pathBuf+1 == lhsp && '.' == lhsp[-1]) {

                /* This is Case b which can be dealt with by
                 * converting the emitted path from "." to "..".
                 */

                *lhsp++ = '.';

                rhsp += 3;
                continue;

            } else if (
                    !(pathBuf+2 == lhsp &&
                        '.' == lhsp[-2] &&
                        '.' == lhsp[-1]) &&
                    !(pathBuf+3 <= lhsp &&
                        '/' == lhsp[-3] &&
                        '.' == lhsp[-2] &&
                        '.' == lhsp[-1])) {

                /* Check that this is not Case c, in which case it
                 * must be Case d.
                 *
                 * Check that there is a parent in the emitted path
                 * before reversing to find the previous sentinel.
                 */

                rhsp += 3;

                if (pathBuf != lhsp &&
                        !(pathBuf+1 == lhsp && '/' == lhsp[-1])) {

                    do {
                        --lhsp;

                        /* Check for the previous slash which marks
                         * the start of the name being reverted, and
                         * retain the slash if it is the start of
                         * an absolute path..
                         */

                        if ('/' == *lhsp) {
                            if (pathBuf == lhsp)
                                ++lhsp;
                            break;
                        }

                        /* Check for the start of the path which
                         * marks the beginning of a relative path,
                         * and replace it with ".".
                         */

                        if (pathBuf == lhsp) {
                            *lhsp++ = '.';
                            break;
                        }

                    } while (pathBuf != lhsp);
                }

                continue;

            }
        }

        /* Append the name to the emitted path, and stop at the
         * next slash which is the invariant for the main loop.
         */

        do
            *lhsp++ = *rhsp++;
        while ('/' != *rhsp);
    }

    *lhsp = 0;

    DEBUG("Normalised path %s", pathBuf);

    *aPath = pathBuf;
    pathBuf = 0;

    rc = 0;

Finally:

    FINALLY({
        free(pathBuf);
    });

    return rc;
}

/* -------------------------------------------------------------------------- */

#endif /* SUXEC_SPLICE_PATH_H */
