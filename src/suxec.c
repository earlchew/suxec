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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/fsuid.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "config.h"

#ifdef USE_VALGRIND
#include <valgrind/memcheck.h>
#endif

#include "finally.h"

/* -------------------------------------------------------------------------- */
struct uid { uid_t _; };
struct gid { gid_t _; };

static inline int uid_eq(struct uid aLhs, struct uid aRhs)
{ return aLhs._ == aRhs._; }

static inline int gid_eq(struct gid aLhs, struct gid aRhs)
{ return aLhs._ == aRhs._; }

static inline int uid_ne(struct uid aLhs, struct uid aRhs)
{ return ! uid_eq(aLhs, aRhs); }

static inline int gid_ne(struct gid aLhs, struct gid aRhs)
{ return ! gid_eq(aLhs, aRhs); }

/* -------------------------------------------------------------------------- */
struct dirfd {
    int mFd;
    char *mPath;
};

/* -------------------------------------------------------------------------- */
struct symlinkfd {
    int mFd;
    char *mName;
    struct dirfd mDir;
};

/* -------------------------------------------------------------------------- */
struct grouplist {
    gid_t *mList;
    size_t mSize;
};

/* -------------------------------------------------------------------------- */
struct user {
    struct uid mUid;
    struct gid mGid;

    char *mName;
    char *mHome;

    struct grouplist mGroups_, *mGroups;
};

/* -------------------------------------------------------------------------- */
struct app {

    char **mEnv;
    char **mCmd;

    char *mPath;

    struct grouplist mGroups;

    struct user mRequestor;
    struct user mLicensor;

    struct {

        struct symlinkfd mSymLink;

    } mLicensee;

};

/* -------------------------------------------------------------------------- */
static void
die(const char *aFmt, ...) __attribute__((format(printf, 1, 2)));

static void
die(const char *aFmt, ...)
{
    if (aFmt) {
        va_list argp;

        va_start(argp, aFmt);
        if (errno)
            vwarn(aFmt, argp);
        else
            vwarnx(aFmt, argp);
        va_end(argp);
    }

    exit(127);
}

/* -------------------------------------------------------------------------- */
static int sDebug;

static struct option sOptions[] = {
   { "debug", no_argument, 0, 'd' },
};

/* -------------------------------------------------------------------------- */
static void
debug_(unsigned aLineNo, const char *aFmt, ...)
{
    va_list argp;

    va_start(argp, aFmt);

    fprintf(stderr, "%s: ", program_invocation_short_name);
    vfprintf(stderr, aFmt, argp);
    fputc('\n', stderr);

    va_end(argp);
}

#define IFDEBUG(...) \
    if (!sDebug) ; else do __VA_ARGS__ while (0)

#define DEBUG(...) \
    if (!sDebug) ; else do debug_(__LINE__, __VA_ARGS__); while (0)

/* -------------------------------------------------------------------------- */
static void
swap_reuid()
{
    struct uid uid = { getuid() };
    struct gid gid = { getgid() };

    struct uid euid = { geteuid() };
    struct gid egid = { getegid() };

   if (setregid(egid._, gid._))
       die("Unable to swap effective gid %d and gid %d", egid._, gid._);

   if (setreuid(euid._, uid._))
       die("Unable to swap effective uid %d and uid %d", euid._, uid._);
}

/* -------------------------------------------------------------------------- */
static int
chain_execv(char *aPath)
{
    DEBUG("Executing %s", aPath);

    VALGRIND_DO_LEAK_CHECK;

    unsigned long
        definiteLeaks, dubiousLeaks, reachableBytes, suppressedBytes;

    VALGRIND_COUNT_LEAKS(
        definiteLeaks, dubiousLeaks, reachableBytes, suppressedBytes);

    (void) reachableBytes;
    (void) suppressedBytes;

    if (definiteLeaks || dubiousLeaks)
        die("Memory leaks found  - definite %lu dubious %lu",
            definiteLeaks, dubiousLeaks);

    char *args[2] = {
        aPath,
       0
    };

    return execv(aPath, args);
}

/* -------------------------------------------------------------------------- */
static void
impersonate_user(const struct user *aUser, const struct grouplist *aGroups)
{
    /* Use conditional setgroups(2) because initgroups(3) sets
     * the supplementary groups unconditionally and fails if the
     * caller is unprivileged.
     */

    if (aUser->mGroups->mSize != aGroups->mSize ||
            memcmp(
                aUser->mGroups->mList,
                aGroups->mList,
                aUser->mGroups->mSize * sizeof(*aUser->mGroups->mList))) {

        if (setgroups(aUser->mGroups->mSize, aUser->mGroups->mList))
            die("Unable to set supplementary groups for user %s", aUser->mName);
    }

    struct uid ouid = { geteuid() };

    if (setgid(aUser->mGid._))
        die("Unable to set gid %d", aUser->mGid._);

    if (setuid(aUser->mUid._))
        die("Unable to set uid %d", aUser->mUid._);

    struct gid egid = { getegid() };
    if (gid_ne(aUser->mGid, egid))
        die("Mismatched effective gid %d", egid._);

    struct uid euid = { geteuid() };
    if (uid_ne(aUser->mUid, euid))
        die("Mismatched effective uid %d", euid._);

    if (aUser->mUid._ && !setreuid(-1, 0))
        die("Unexpected privilege escalation");

    if (uid_ne(ouid, euid) && !setreuid(-1, ouid._))
        die("Unexpected privilege recovery");

    struct gid fsgid = { setfsgid(-1) };
    if (gid_ne(fsgid, aUser->mGid))
        die("Unexpected fsgid %d", fsgid._);

    struct uid fsuid = { setfsuid(-1) };
    if (uid_ne(fsuid, aUser->mUid))
        die("Unexpected fsuid %d", fsuid._);
}

/* -------------------------------------------------------------------------- */
static void
usage(void)
{
    fprintf(
        stderr,
        "usage: %s [--debug] [--] [NAME=VALUE ...] symlink\n",
        program_invocation_short_name);
    die(0);
}

/* -------------------------------------------------------------------------- */
/* String operations
 *
 * The implementations are separated to allow for a comprehensive unit tests
 * to cover the different cases of dot and dot-dot directories, and
 * their interaction with absolute and relative paths.
 */

#include "split_path.c.h"
#include "splice_path.c.h"
#include "stpcpyv.c.h"

/* -------------------------------------------------------------------------- */
static int
fdclose(int aFd)
{
    if (-1 != aFd)
        close(aFd);

    return -1;
}

/* ************************************************************************** */
static int
create_grouplist_rank_gid_(const void *aLhs, const void *aRhs)
{
    struct gid lhs = (struct gid) { * (const gid_t *) aLhs };
    struct gid rhs = (struct gid) { * (const gid_t *) aRhs };

    return lhs._ == rhs._ ? 0 : lhs._ < rhs._ ? -1 : +1;
}

/* -------------------------------------------------------------------------- */
static struct grouplist *
close_grouplist(struct grouplist *self) __attribute__((unused));

static struct grouplist *
create_grouplist(struct grouplist *self)
{
    int rc = -1;

    gid_t *groupList = 0;
    size_t groupListLen = 0;

    /* Find the supplementary groups that the process already belongs
     * to in order to compare with the target set of supplementary
     * groups.
     *
     * This is useful when running as an unprivileged process, especially
     * during unit test, where the process is already has the correct
     * supplementary groups.
     */

    for (int groupBufLen = 1; !groupList; ) {

        gid_t groupBuf[groupBufLen];

        groupListLen = getgroups(groupBufLen, groupBuf);
        if (-1 == groupListLen) {
            if (EINVAL != errno)
                goto Finally;

            groupBufLen *= 2;

            if (0 >= groupBufLen) {
                errno = ENOMEM;
                goto Finally;
            }

            continue;
        }

        groupBufLen = groupListLen;

        /* Unfortunately the primary gid might not be present in the
         * return list. Search for it, and insert it if it is absent.
         */

        struct gid primaryGid_ = { getgid() }, *primaryGid = &primaryGid_;

        for (size_t gx = 0; gx < groupBufLen; ++gx) {
            if (gid_eq((struct gid) { groupBuf[gx] }, *primaryGid)) {
                primaryGid = 0;
                break;
            }
        }

        if (primaryGid)
            ++groupListLen;

        groupList = malloc(sizeof(*groupList) * groupListLen);
        if (!groupList)
            goto Finally;

        memcpy(groupList, groupBuf, sizeof(*groupBuf) * groupBufLen);

        if (primaryGid)
            groupList[groupBufLen] = primaryGid->_;
    }

    qsort(
        groupList, groupListLen, sizeof(*groupList),
        create_grouplist_rank_gid_);

    self->mSize = groupListLen;

    self->mList = groupList;
    groupList = 0;

    rc = 0;

Finally:

    FINALLY({
        free(groupList);
    });

    return rc ? 0 : self;
}

/* -------------------------------------------------------------------------- */
static struct grouplist *
create_grouplist_user(
    struct grouplist *self, const char *aName, struct gid aGid)
{
    int rc = -1;

    gid_t *groupList = 0;
    size_t groupListLen = 0;

    /* Find the supplementary groups required for the target user.
     * Comapre this list with the supplementary groups bound
     * to the process, and only attempt to configure the groups
     * if required.
     */

    for (int groupBufLen = 1; !groupList; ) {

        gid_t groupBuf[groupBufLen];

        groupListLen = groupBufLen;

        if (-1 == getgrouplist(aName, aGid._, groupBuf, &groupBufLen)) {

            if (groupListLen == groupBufLen)
                goto Finally;

            continue;
        }

        groupListLen = groupBufLen;
        groupList = malloc(sizeof(*groupList) * groupListLen);
        if (!groupList)
            goto Finally;

        memcpy(groupList, groupBuf, sizeof(*groupBuf) * groupListLen);
    }

    qsort(
        groupList, groupListLen, sizeof(*groupList),
        create_grouplist_rank_gid_);

    self->mSize = groupListLen;

    self->mList = groupList;
    groupList = 0;

    rc = 0;

Finally:

    FINALLY({
        free(groupList);
    });

    return rc ? 0 : self;
}

/* -------------------------------------------------------------------------- */
static struct grouplist *
close_grouplist(struct grouplist *self)
{
    if (self) {
        free(self->mList);
    }

    return 0;
}

/* ************************************************************************** */
static struct user *
close_user(struct user *self) __attribute__((unused));

static struct user *
create_user(struct user *self, struct uid aUid, struct gid aGid)
{
    int rc = -1;

    char *name = 0;
    char *home = 0;

    self->mUid = (struct uid) { -1 };
    self->mGid = (struct gid) { -1 };
    self->mName = 0;
    self->mHome = 0;

    self->mGroups = 0;

    struct passwd *pw = getpwuid(aUid._);
    if (!pw)
        goto Finally;

    /* If the caller passes -1 as the gid, then use struct passwd
     * as the source for both uid and gid, otherwise prefer the
     * uid and gid passed in from the caller.
     */

    if (-1 == aGid._) {
        self->mUid = (struct uid) { pw->pw_uid };
        self->mGid = (struct gid) { pw->pw_gid };
    } else {
        self->mUid = aUid;
        self->mGid = aGid;
    }

    name = strdup(pw->pw_name);
    if (!name)
        goto Finally;

    home = strdup(pw->pw_dir);
    if (!home)
        goto Finally;

    self->mName = name;
    name = 0;

    self->mHome = home;
    home = 0;

    rc = 0;

Finally:
    FINALLY({
        free(home);
        free(name);
    });

    return rc ? 0 : self;
}

/* -------------------------------------------------------------------------- */
static int
fetch_user_groups(struct user *self)
{
    int rc = -1;

    if (!self->mGroups) {
        self->mGroups = create_grouplist_user(
            &self->mGroups_, self->mName, self->mGid);
        if (!self->mGroups)
            goto Finally;
    }

    rc = 0;

Finally:

    return rc;
}

/* -------------------------------------------------------------------------- */
static struct user *
close_user(struct user *self)
{
    if (self) {
        free(self->mName);
        free(self->mHome);

        close_grouplist(self->mGroups);
    }

    return 0;
}

/* ************************************************************************** */
static struct dirfd *
close_dirfd(struct dirfd *self);

static struct dirfd *
create_dirfd(struct dirfd *self, const struct dirfd *aAt, const char *aPath)
{
    int rc = -1;

    char *dirPath = 0;
    int dirFd = -1;

    self->mFd = -1;
    self->mPath = 0;

    if (!aPath || !aPath[0]) {
        errno = EINVAL;
        goto Finally;
    }

    /* Open a file descriptor to the directory. If the caller uses
     * dot to name the current working directory, use the current
     * working directory as the name. If the caller specifies
     * an absolute path, then use that as the name, otherwise
     * splice the name together.
     */

    dirFd = openat(
        aAt ? aAt->mFd : AT_FDCWD,
        aPath,
        O_RDONLY | O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (-1 == dirFd)
        goto Finally;

    if ('.' == aPath[0] && !aPath) {

        if (aAt)
            dirPath = strdup(aAt->mPath);
        else
            dirPath = get_current_dir_name();

    } else if ('/' == aPath[0]) {

        dirPath = strdup(aPath);

    } else {

        if (splice_path(&dirPath, aAt ? aAt->mPath : 0, aPath))
            goto Finally;
    }

    if (!dirPath)
        goto Finally;

    self->mFd = dirFd;
    dirFd = -1;

    self->mPath = dirPath;
    dirPath = 0;

    rc = 0;

Finally:

    FINALLY({
        dirFd = fdclose(dirFd);

        free(dirPath);
    });

    return rc ? 0 : self;
}

/* -------------------------------------------------------------------------- */
static struct dirfd *
close_dirfd(struct dirfd *self)
{
    if (self) {
        fdclose(self->mFd);
        free(self->mPath);
    }

    return 0;
}

/* ************************************************************************** */
static struct symlinkfd *
close_symlinkfd(struct symlinkfd *self);

static struct symlinkfd *
create_symlinkfd(
    struct symlinkfd *self, const struct dirfd *aAt, const char *aPath)
{
    int rc = -1;

    struct dirfd dirFd_, *dirFd = 0;
    int symlinkFd = -1;

    char *dirName = 0;
    char *baseName = 0;

    self->mDir.mFd = -1;
    self->mDir.mPath = 0;
    self->mFd = -1;
    self->mName = 0;

    if (split_path(aPath, &dirName, &baseName))
        goto Finally;

    dirFd = create_dirfd(&dirFd_, aAt, dirName);
    if (!dirFd)
        goto Finally;

    symlinkFd = openat(
        dirFd->mFd, baseName, O_RDONLY | O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (-1 == symlinkFd)
        goto Finally;

    self->mFd = symlinkFd;
    symlinkFd = -1;

    self->mName = baseName;
    baseName = 0;

    self->mDir = *dirFd;
    dirFd = 0;

    rc = 0;

Finally:

    FINALLY({
        free(baseName);
        free(dirName);

        symlinkFd = fdclose(symlinkFd);

        dirFd = close_dirfd(dirFd);
    });

    return rc ? 0 : self;
}

/* -------------------------------------------------------------------------- */
static char *
read_symlinkfd(const struct symlinkfd *self)
{
    int rc = -1;

    char *symlink = 0;
    size_t bufLen = 1;

    while (1) {
        char buf[bufLen];

        ssize_t symlinkLen = readlinkat(self->mFd, "", buf, bufLen);
        if (-1 == symlinkLen)
            goto Finally;

        if (symlinkLen < bufLen) {
            buf[symlinkLen] = 0;
            symlink = strdup(buf);
            break;
        }

        bufLen *= 2;
    }

    rc = 0;

Finally:

    return rc ? 0 : symlink;
}


/* -------------------------------------------------------------------------- */
static int
follow_symlinkfd(struct symlinkfd *self)
{
    int rc = -1;

    struct symlinkfd followFd_, *followFd = 0;

    char *symlink = 0;

    struct stat symlinkStat;
    if (fstatat(
            self->mFd, "", &symlinkStat, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW))
        goto Finally;

    if (!S_ISLNK(symlinkStat.st_mode)) {

        errno = 0;
        goto Finally;

    }

    symlink = read_symlinkfd(self);
    if (!symlink)
        goto Finally;

    followFd = create_symlinkfd(&followFd_, &self->mDir, symlink);
    if (!followFd)
        goto Finally;

    DEBUG("Follow %s/%s %s/%s",
        self->mDir.mPath, self->mName,
        followFd->mDir.mPath, followFd->mName);

    close_symlinkfd(self);

    *self = *followFd;
    followFd = 0;

    rc = 0;

Finally:

    FINALLY({
        free(symlink);

        followFd = close_symlinkfd(followFd);
    });

    return rc;
}

/* -------------------------------------------------------------------------- */
static struct symlinkfd *
close_symlinkfd(struct symlinkfd *self)
{
    if (self) {
        fdclose(self->mFd);
        free(self->mName);

        close_dirfd(&self->mDir);
    }

    return 0;
}

/* ************************************************************************** */
static void
license_program(
    struct app *aApp, int argc, char **argv, struct uid aUid, struct gid aGid)
{
    while (1) {
        int opt = getopt_long(argc, argv, "+d", sOptions, 0);
        if (-1 == opt)
            break;

        switch (opt) {
        case '?':
            usage();
            break;

        case 'd':
            sDebug =1;
            break;
        }
    }

    if (optind >= argc)
        usage();

    /* Skip all NAME=VALUE arguments, but take care to detect
     * degenerate cases where NAME is empty.
     */

    char **argp = &argv[optind];

    aApp->mEnv = argp;

    while (*argp && strchr(*argp, '=')) {
        if ('=' == **argp)
            usage();
        ++argp;
    }

    /* Ensure that there is one non-empty argument remaining
     * that specifies the command to execute.
     */

    if (!argp[0] || !argp[0][0] || argp[1])
        usage();

    aApp->mCmd = argp;

    if (!create_grouplist(&aApp->mGroups))
        die("Unable to query supplementary groups");

    IFDEBUG({
        for (size_t gx = 0; gx < aApp->mGroups.mSize; ++gx)
            DEBUG("Supplementary gid %d", aApp->mGroups.mList[gx]);
    });

    /* The requestor is determined from user running the program
     * and is required to also be the licensee.
     */

    if (!create_user(&aApp->mRequestor, aUid, aGid))
        die("Unable to find passwd entry for uid %d gid %d", aUid._, aGid._);

    DEBUG("Requestor %s", aApp->mRequestor.mName);

    /* The licensee is determined from the owner of the symlink. For
     * now, simply keep a reference to the symlink so that it can
     * be interrogated later after dropping privileges.
     */

    if (!create_symlinkfd(&aApp->mLicensee.mSymLink, 0, *aApp->mCmd))
        die("Unable to open %s", *aApp->mCmd);

    struct stat dirStat;

    if (fstat(aApp->mLicensee.mSymLink.mDir.mFd, &dirStat))
        die("Unable to stat directory %s",
            aApp->mLicensee.mSymLink.mDir.mPath);

    /* The licensor is determined from the directory containing
     * the symlink. That directory is presumed to house all the
     * registrations for a particular licensee.
     */

    if (!create_user(
            &aApp->mLicensor,
            (struct uid) { dirStat.st_uid },
            (struct gid) { -1 }))
        die("Unable to find passwd entry for uid %d", dirStat.st_uid);

    DEBUG("Licensor %s", aApp->mLicensor.mName);

    if (fetch_user_groups(&aApp->mLicensor))
        die("Unable to query supplementary groups for user %s",
            aApp->mLicensor.mName);

    IFDEBUG({
        for (size_t gx = 0; gx < aApp->mLicensor.mGroups->mSize; ++gx)
            DEBUG("Licensor gid %d", aApp->mLicensor.mGroups->mList[gx]);
    });

    /* The owner of the symlink determines the licensee, and should match
     * the requestor.
     */

    DEBUG("Command %s", *aApp->mCmd);
    DEBUG("Licensee %s", aApp->mLicensee.mSymLink.mDir.mPath);

    /* Determine the name of the directory holding the registrations
     * for this licensee, and verify the format of the name.
     */

    char *licenseeDir = strrchr(aApp->mLicensee.mSymLink.mDir.mPath, '/');
    if (!licenseeDir || licenseeDir == aApp->mLicensee.mSymLink.mDir.mPath)
        die("Unable to determine licensee directory from %s",
            aApp->mLicensee.mSymLink.mDir.mPath);
    ++licenseeDir;

    if ('.' == *licenseeDir)
        die("Hidden directory at %s", aApp->mLicensee.mSymLink.mDir.mPath);

    if ('@' == *licenseeDir)
        die("Restricted directory at %s", aApp->mLicensee.mSymLink.mDir.mPath);

    /* Interrogate the parent of the licensee registration directory.
     * This directory should be owned by the licensor, and should not
     * allow other users to list its contents. Only the licensor
     * should be allowed to know the names of all the registered
     * licensees, and the names of the submission and staging directories.
     */

    struct stat parentDirStat;

    if (fstatat(aApp->mLicensee.mSymLink.mDir.mFd, "..", &parentDirStat, 0))
        die("Unable to stat directory %s/../",
            aApp->mLicensee.mSymLink.mDir.mPath);

    if (parentDirStat.st_mode & (S_IRGRP|S_IWGRP))
        die("Directory %s/../ has group rw permissions",
            aApp->mLicensee.mSymLink.mDir.mPath);

    if (parentDirStat.st_mode & (S_IROTH|S_IWOTH))
        die("Directory %s/../ has other rw permissions",
            aApp->mLicensee.mSymLink.mDir.mPath);

    if (uid_ne(
            (struct uid) { parentDirStat.st_uid },
            aApp->mLicensor.mUid))
        die("Expected owner user %s for directory %s/../",
            aApp->mLicensor.mName, aApp->mLicensee.mSymLink.mDir.mPath);

    /* Verify that the licensor also owns the file resolved by the
     * symlink. Only the symlink itself is owned by the licensee.
     */

    struct stat symLinkStat;

    if (fstat(aApp->mLicensee.mSymLink.mFd, &symLinkStat))
        die("Unable to stat symlink %s/%s",
            aApp->mLicensee.mSymLink.mDir.mPath,
            aApp->mLicensee.mSymLink.mName);

    if (uid_ne(
            (struct uid) { symLinkStat.st_uid },
            aApp->mRequestor.mUid))
        die("Symlink %s should be owned by user %s",
            *aApp->mCmd, aApp->mRequestor.mName);

    /* Follow the chain of symlinks to find the final symlink
     * that resolves to a regular file. Note that the previous
     * fstat(2) would have failed with ELOOP if the chain of
     * symlinks could not resolve.
     */

    while (1) {
        if (follow_symlinkfd(&aApp->mLicensee.mSymLink)) {
            if (errno)
                die("Unable to follow %s/%s",
                    aApp->mLicensee.mSymLink.mDir.mPath,
                    aApp->mLicensee.mSymLink.mName);
            break;
        }
    }

    /* Now that the symlink has resolved, combine the directory
     * name and the base name to form the path to the resolved
     * file.
     */

    if (splice_path(&aApp->mPath,
            aApp->mLicensee.mSymLink.mDir.mPath,
            aApp->mLicensee.mSymLink.mName))
        die("Unable to create path %s/%s",
            aApp->mLicensee.mSymLink.mDir.mPath,
            aApp->mLicensee.mSymLink.mName);

    /* Verify that the resolved symlink is owned by the licensor
     * previously established by looking at the owner of the directory
     * containing the symlink. Also verify that the owner has permission
     * to execute the target file.
     */

    struct stat linkStat;

    if (stat(aApp->mPath, &linkStat))
        die("Unable to stat %s", aApp->mPath);

    if (uid_ne(
            (struct uid) { linkStat.st_uid },
            aApp->mLicensor.mUid))
        die("Expected owner user %s for file referenced by %s",
            aApp->mLicensor.mName, *aApp->mCmd);

    if (!S_ISREG(linkStat.st_mode) || !(linkStat.st_mode & S_IXUSR))
        die("Expected executable file at %s", *aApp->mCmd);

    /* Add all the specified variables named on the command line to
     * the environment. Named variables override the default
     * LOGNAME, PATH, HOME, and SHELL, variables that would
     * normally be added.
     */

    const char *logNameEnv = "LOGNAME";
    const char *pathEnv = "PATH";
    const char *homeEnv = "HOME";
    const char *shellEnv = "SHELL";

    const char *foundEnv = "";

    for (char **envp = aApp->mEnv; envp != aApp->mCmd; ) {
        DEBUG("Env %s", *envp);

        char *envSep = strchr(*envp, '=');
        if (!envSep)
            die("Unable to parse environment variable %s", *envp);

        *envSep++ = 0;

        if (!strcmp(*envp, logNameEnv))
            logNameEnv = foundEnv;
        else if (!strcmp(*envp, pathEnv))
            pathEnv = foundEnv;
        else if (!strcmp(*envp, homeEnv))
            homeEnv = foundEnv;
        else if (!strcmp(*envp, shellEnv))
            shellEnv = foundEnv;

        if (setenv(*envp, envSep, 1))
            die("Unable to set environment variable %s=%s", *envp, envSep);
    }

    if (logNameEnv != foundEnv) {
        if (setenv(logNameEnv, aApp->mLicensor.mName, 1))
            die("Unable to set environment variable %s=%s",
                logNameEnv, aApp->mLicensor.mName);
        DEBUG("Env %s=%s", logNameEnv, aApp->mLicensor.mName);
    }

    if (homeEnv != foundEnv) {
        if (setenv(homeEnv, aApp->mLicensor.mHome, 1))
            die("Unable to set environment variable %s=%s",
                homeEnv, aApp->mLicensor.mHome);
        DEBUG("Env %s=%s", homeEnv, aApp->mLicensor.mHome);
    }

    if (shellEnv != foundEnv) {
        const char *shellPath = "/bin/sh";
        if (setenv(shellEnv, shellPath, 1))
            die("Unable to set environment variable %s=%s",
                shellEnv, shellPath);
        DEBUG("Env %s=%s", shellEnv, shellPath);
    }

    if (pathEnv != foundEnv) {
        const char *path = "/usr/bin:/bin";
        if (setenv(pathEnv, path, 1))
            die("Unable to set environment variable %s=%s", pathEnv, path);
        DEBUG("Env %s=%s", pathEnv, path);
    }
}

/* ************************************************************************** */
int
main(int argc, char **argv)
{
    /* Remove all entries from the environment to prevent confusion
     * and remove this vector from exploits.
     *
     * Additionally the get_current_dir_name(3) function will
     * return getenv("PWD") if it matches the actual working directory.
     * In the absence of the environment variable, the function always
     * computes the name of the current working directory.
     */

    /* PRIVILEGED */ struct gid privilegedGid = { getegid() };
    /* PRIVILEGED */ struct uid privilegedUid = { geteuid() };
    /* PRIVILEGED */
    /* PRIVILEGED */ if (clearenv())
    /* PRIVILEGED */     die("Unable to clean environment");
    /* PRIVILEGED */
    /* PRIVILEGED */ swap_reuid();

    struct gid swappedGid = { getgid() };
    struct uid swappedUid = { getuid() };

    if (uid_ne(swappedUid, privilegedUid) ||
        gid_ne(swappedGid, privilegedGid)) {

        die("Failure to swap effective uid %d and gid %d",
            privilegedUid._, privilegedGid._);
    }

    struct gid unprivilegedGid = { getegid() };
    struct uid unprivilegedUid = { geteuid() };

    /* The following code runs as the unprivileged requestor.
     * The privileged user is saved, and swapped back in order
     * to run the licensed program as the licensor.
     */

    struct app app;

    license_program(&app, argc, argv, unprivilegedUid, unprivilegedGid);

    /* Run the remainder as the privileged user so that the
     * target program can be launched as the licensor.
     */

    /* PRIVILEGED */ swap_reuid();
    /* PRIVILEGED */
    /* PRIVILEGED */ impersonate_user(&app.mLicensor, &app.mGroups);
    /* PRIVILEGED */
    /* PRIVILEGED */ return chain_execv(app.mPath);
}

/* ************************************************************************** */
