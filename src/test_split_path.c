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

#include <assert.h>
#include <stdio.h>

/* -------------------------------------------------------------------------- */
#include "split_path.c.h"

/* -------------------------------------------------------------------------- */
static struct {
    const char *mPath;
    const char *mLhs;
    const char *mRhs;
    int mResult;
} sTestPlan[] = {

    { "/",  "/", "" },
    { "//", "/", "" },

    { "",   "",  "", -1 },
    { ".",  ".", "." },
    { "..", ".", ".." },
    { "z",  ".",  "z" },

    { "/.",  "/", "." },
    { "/.",  "/", "." },
    { "/..", "/", ".." },
    { "/z",  "/", "z" },

    { "/./",  "/", "." },
    { "/./",  "/", "." },
    { "/../", "/", ".." },
    { "/z/",  "/", "z" },

    { "/.//",  "/", "." },
    { "/.//",  "/", "." },
    { "/..//", "/", ".." },
    { "/z//",  "/", "z" },

    { "/./x",  "/.",  "x" },
    { "/./x",  "/.",  "x" },
    { "/../x", "/..", "x" },
    { "/z/x",  "/z",  "x" },

    { "/.//x",  "/.",  "x" },
    { "/.//x",  "/.",  "x" },
    { "/..//x", "/..", "x" },
    { "/z//x",  "/z",  "x" },

    { ".//.//x",  ".//.",  "x" },
    { ".//.//x",  ".//.",  "x" },
    { ".//..//x", ".//..", "x" },
    { ".//z//x",  ".//z",  "x" },

};

/* -------------------------------------------------------------------------- */
int
main(int argc, char **argv)
{
    for (unsigned ix = 0; ix < sizeof(sTestPlan)/sizeof(sTestPlan[0]); ++ix) {

        fprintf(stderr,
            "[%u] %d (%s) <= (%s) + (%s)\n",
            ix,
            sTestPlan[ix].mResult, sTestPlan[ix].mPath,
            sTestPlan[ix].mLhs, sTestPlan[ix].mRhs);

        char *lhs = 0;
        char *rhs = 0;

        int rc = split_path(sTestPlan[ix].mPath, &lhs, &rhs);
        if (rc) {
            assert(sTestPlan[ix].mResult);
        } else {
            fprintf(stderr, "(%s) (%s)\n", lhs, rhs);

            assert(!strcmp(sTestPlan[ix].mLhs, lhs));
            assert(!strcmp(sTestPlan[ix].mRhs, rhs));
        }

        free(lhs);
        free(rhs);
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
