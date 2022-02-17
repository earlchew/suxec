#ifndef SUXEC_SPLIT_PATH_H
#define SUXEC_SPLIT_PATH_H
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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "finally.h"

/* -------------------------------------------------------------------------- */
/* Split path from for base and directory names
 *
 * Isolate the dirname from the basename in aPath. Return both the dirname
 * and the basename in *aDirName and *aBaseName, respectively, that the
 * caller must free.
 */

static int
split_path(const char *aPath, char **aDirName, char **aBaseName)
{
    int rc = -1;

    char *dirName = 0;
    char *baseName = 0;

    if (!aPath || !*aPath) {
        errno = ENOENT;
        goto Finally;
    }

    do {

        char pathBuf[strlen(aPath) + 1];
        char *endp = stpcpy(pathBuf, aPath);

        while (pathBuf+1 < endp && '/' == endp[-1])
            --endp;
        *endp = 0;

        char *lastSlash = 0;

        while (pathBuf != endp) {
            if ('/' == *--endp) {
                lastSlash = endp;
                break;
            }
        }

        const char *parentBuf;

        if (!lastSlash) {

            parentBuf = ".";

            baseName = strdup(pathBuf);
            if (!baseName)
                goto Finally;

        } else {

            baseName = strdup(lastSlash+1);
            if (!baseName)
                goto Finally;

            while (pathBuf != lastSlash) {
                if ('/' != lastSlash[-1])
                    break;
                --lastSlash;
            }

            if (pathBuf == lastSlash)
                lastSlash[1] = 0;
            else
                lastSlash[0] = 0;

            parentBuf = pathBuf;
        }

        dirName = strdup(parentBuf);
        if (!dirName)
            goto Finally;

    } while (0);

    if (aBaseName)
        *aBaseName = baseName;
    baseName = 0;

    if (aDirName)
        *aDirName = dirName;
    dirName = 0;

    rc = 0;

Finally:

    FINALLY({
        free(baseName);
        free(dirName);
    });

    return rc;
}

/* -------------------------------------------------------------------------- */

#endif /* SUXEC_SPLIT_PATH_H */
