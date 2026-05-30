#!/bin/sh

if [ ! -d bin/module ]
then
    mkdir bin/module
else
    unlink bin/flz_chat
    unlink bin/module/libflz_chat.so
fi

cp flz_server/bin/flz bin/flz_chat
cp lib/libflz_chat.so bin/module/
