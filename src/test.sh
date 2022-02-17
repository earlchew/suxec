#!/usr/bin/env bash
# -*- sh-basic-offset:4; indent-tabs-mode:nil -*- vi: set sw=4 et:

[ -z "${0##/*}" ] || exec "$PWD/$0" "$@"

set -eu

say()
{
    printf '%s\n' "$*"
}

suxec()
{
    set -- "${0%/*}/suxec" --debug "$@"
    set -- "--" "$@"
    set -- --error-exitcode=255 "$@"
    set -- --suppressions="${0%/*}/suxec.supp" "$@"

    valgrind "$@"
}

expect()
{
    test "$@" || {
        say "Failed at line ${BASH_LINENO[0]}: $*" >&2
        exit 1
    }
}

test_01()
{
    local RESULT
    RESULT=$(suxec "${0%/*}/test/01/run")
    say "$RESULT" >&2
    expect -n "$RESULT"

    expect x"$(say "$RESULT" | wc -l)" = x4
    expect -z "${RESULT##*HOME=$HOME*}"
    expect -z "${RESULT##*LOGNAME=$USER*}"
    expect -z "${RESULT##*PATH=/usr/bin:/bin*}"
    expect -z "${RESULT##*SHELL=/bin/sh*}"
}

test_02()
{
    local RESULT
    RESULT=$(suxec "${0%/*}/test/02/run")
    say "$RESULT" >&2
    expect -n "$RESULT"

    expect x"$(say "$RESULT" | wc -l)" = x4
    expect -z "${RESULT##*HOME=$HOME*}"
    expect -z "${RESULT##*LOGNAME=$USER*}"
    expect -z "${RESULT##*PATH=/usr/bin:/bin*}"
    expect -z "${RESULT##*SHELL=/bin/sh*}"
}

test_03()
{
    local RESULT
    RESULT=$(suxec "${0%/*}/test/03/run")
    say "$RESULT" >&2
    expect -n "$RESULT"

    expect x"$(pwd -P)" = x"$RESULT"
}

test_04()
{
    suxec "${0%/*}/test/04/run"
    expect $? = 127
}

test_05()
{
    chmod og-rw suxec "${0%/*}/test"
    suxec "${0%/*}/test/05/run"
    expect $? = 0

    chmod o+r suxec "${0%/*}/test"
    suxec "${0%/*}/test/05/run"
    expect $? = 127
    chmod og-rw suxec "${0%/*}/test"
    suxec "${0%/*}/test/05/run"
    expect $? = 0

    chmod o+w suxec "${0%/*}/test"
    suxec "${0%/*}/test/05/run"
    expect $? = 127
    chmod og-rw suxec "${0%/*}/test"
    suxec "${0%/*}/test/05/run"
    expect $? = 0

    chmod g+r suxec "${0%/*}/test"
    suxec "${0%/*}/test/05/run"
    expect $? = 127
    chmod og-rw suxec "${0%/*}/test"
    suxec "${0%/*}/test/05/run"
    expect $? = 0

    chmod g+w suxec "${0%/*}/test"
    suxec "${0%/*}/test/05/run"
    expect $? = 127
    chmod og-rw suxec "${0%/*}/test"
    suxec "${0%/*}/test/05/run"
    expect $? = 0
}

test_06()
{
    mv "${0%/*}/test/@6" "${0%/*}/test/06" || :

    suxec "${0%/*}/test/06/run"
    expect $? = 0

    mv "${0%/*}/test/06" "${0%/*}/test/@6"
    suxec "${0%/*}/test/@6/run"
    expect $? = 127

    mv "${0%/*}/test/@6" "${0%/*}/test/06"
    suxec "${0%/*}/test/06/run"
    expect $? = 0
}

test_07()
{
    mv "${0%/*}/test/.7" "${0%/*}/test/07" || :

    suxec "${0%/*}/test/07/run"
    expect $? = 0

    mv "${0%/*}/test/07" "${0%/*}/test/.7"
    suxec "${0%/*}/test/.7/run"
    expect $? = 127

    mv "${0%/*}/test/.7" "${0%/*}/test/07"
    suxec "${0%/*}/test/07/run"
    expect $? = 0
}

test_08()
{
    local RESULT
    RESULT=$(suxec "${0%/*}/test/08/run")
    say "$RESULT" >&2
    expect -n "$RESULT"

    expect x"$(groups)" = x"$RESULT"
}

run()
{
    local OUTPUT
    OUTPUT=$(
        trap cleanup EXIT
        exec 2>&1 >/dev/null
        set -x
        "$@"
    ) || {
        say "$OUTPUT"
        say "FAILED $*"
        false
    }
    [ -z "${OPT_VERBOSE++}" ] ||
        say "$OUTPUT"
    say "OK     -$- $*"
}

run_tests()
{
    chmod og-rw suxec "${0%/*}/test"

    run test_01
    run test_02
    run test_03
    run test_04
    run test_05
    run test_06
    run test_07
    run test_08
}

main()
{
    run_tests
}

main "$@"
