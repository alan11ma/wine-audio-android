#!/bin/sh

# first add ndk-build path to system $PATH
cd jni
ndk-build

cd ..
cp libs/x86/wine-audio .

