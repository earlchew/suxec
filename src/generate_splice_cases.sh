#!/usr/bin/env bash

generate()
{
    local LEVEL=$1 ; shift
    local WORD=$1 ; shift

    local SLASH=/
    local DOT=.
    local DOTDOT=..
    local ZED=z

    if [ $LEVEL -eq 0 ] ; then

        WORD=${WORD#@/}

        local REALPATH
        REALPATH=$(
            set --
            [ "${WORD#/}" != "$WORD" ] || set -- --relative-to .
            realpath -m "$@" "$WORD"
        )

        local SECOND=${WORD#?*/}
        local FIRST=${WORD%%$SECOND}
        local FIRST=${FIRST%/}

        set -- \"$REALPATH\", \"$FIRST\", \"$SECOND\"
        printf '    { %-16s %-12s %s },\n' "$@"

    else
        LEVEL=$((LEVEL - 1))

        local SYMBOL
        for SYMBOL in $SLASH $DOT $DOTDOT $ZED ; do
            generate $LEVEL "$WORD/$SYMBOL"
        done
    fi
}

main()
{
    local LEVELS=$1 ; shift

    local LEVEL
    for (( LEVEL=1 ; LEVEL <= $LEVELS ; ++LEVEL )) ; do
        generate $LEVEL ""
    done

    for (( LEVEL=1 ; LEVEL <= $LEVELS ; ++LEVEL )) ; do
        generate $LEVEL "@"
    done
}

main "$@"
