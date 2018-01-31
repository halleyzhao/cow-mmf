#!/bin/sh
if test -z "$PS1_OLD"; then
    export PS1_OLD=$PS1
fi

export PS1="\[\033[0;31m\]$1\[\033[0m\] $PS1_OLD"
