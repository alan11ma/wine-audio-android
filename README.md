# wine-audio-android
The goal of this project is to enable low latency pcm audio for WINE running inside Debian chroot environment ("Debian noroot") in x86 Android tablet device. Instructions for running WINE inside Android <http://forum.xda-developers.com/android/apps-games/guide-run-starcraft-x86-android-tablet-t2993529>

Project contains source for a Android exectuable that read pcm stream from a named pipe and forward it to Android OpenSL 1.0.1 API. PCM stream begins with a header contains pcm format paramters  (Samples per second, no. of channel, sample bit count). This server will allocate a buffered audio player from OpenSL for the pcm format read from steam header. Audio player creation can fail if the underlining platform cannot support requested format. Audio playback stop when pipe source is closed.

Includes a companion patch that modify Wine-1.4.1 alsa driver, make it write pcm samples to named pipe. This allows WINE running inside Android "chroot" send pcm stream directly to OpenSL and cut down audio latency.

COMPILATION:

PCM Server- add Android NDK location to $PATH and run ./build.sh, result executable is wine-audio.
By default only x86 PIE(position independent executable) is build and can only be execute in device running x86 Android 4.1 and greater.

WINE- In Ubuntun/Debain host create a x86 build environment following pelya's xserver-debian guide:
<https://github.com/pelya/commandergenius/tree/sdl_android/project/jni/application/xserver-debian>

Detail steps:

Prepare chroot environment:

        sudo apt-get install qemu-user-static
        
        sudo qemu-debootstrap --arch=i386 --verbose \
        
                --components=main,universe,restricted,multiverse \
                
                --include=fakeroot,libc-bin,locales-all,build-essential,sudo \
                
                wheezy wheezy-x86 http://ftp.ua.debian.org/debian/


Activate chroot: 

        sudo chroot wheezy-x86

Add following to /etc/apt/sources.list inside chroot, then do sudo apt-get update:
        
        deb http://http.debian.net/debian/ wheezy contrib main non-free
        deb-src http://http.debian.net/debian/ wheezy main contrib
        
        deb http://security.debian.org/ wheezy/updates contrib main non-free
        deb-src http://security.debian.org/ wheezy/updates main contrib
        
        deb http://http.debian.net/debian/ wheezy-updates contrib main non-free
        deb-src http://http.debian.net/debian/ wheezy-updates main contrib
        
        deb http://http.debian.net/debian/ wheezy-backports contrib main non-free
        deb-src http://http.debian.net/debian/ wheezy-backports contrib main

Install development packages:

        sudo apt-get install bison libpixman-1-dev \
        
        libxfont-dev libxkbfile-dev libpciaccess-dev \
        
        xutils-dev xcb-proto python-xcbgen xsltproc \
        
        x11proto-bigreqs-dev x11proto-composite-dev \
        
        x11proto-core-dev x11proto-damage-dev \
        
        x11proto-dmx-dev x11proto-dri2-dev x11proto-fixes-dev \
        
        x11proto-fonts-dev x11proto-gl-dev \
        
        x11proto-input-dev x11proto-kb-dev \
        
        x11proto-print-dev x11proto-randr-dev \
        
        x11proto-record-dev x11proto-render-dev \
        
        x11proto-resource-dev x11proto-scrnsaver-dev \
        
        x11proto-video-dev x11proto-xcmisc-dev \
        
        x11proto-xext-dev x11proto-xf86bigfont-dev \
        
        x11proto-xf86dga-dev x11proto-xf86dri-dev \
        
        x11proto-xf86vidmode-dev x11proto-xinerama-dev \
        
        libxmuu-dev libxt-dev libsm-dev libice-dev libudev-dev \
        
        libxrender-dev libxrandr-dev curl autoconf automake libtool \
        
        pkg-config libjpeg-dev libpng-dev
        
        sudo apt-get install libasound2-dev

Get Wine source:

        sudo apt-get source wine

Apply alsa driver patch and build wine:

        cd wine-1.4.1
        
        patch -p1 < wine-alsa-pipe_sink.patch
        
        ./configure
        
        make

Find patched alsa driver module under dlls/winealsa.drv/winealsa.drv.so

INSTALLATION:

Connect Android tablet to host. transfer both wine-audio and winealsa.drv.so to SDCARD

        adb push winealsa.drv.so /sdcard/
        
        adb push wine-audio /sdcard/

On tablet: Install "Debian noroot" from Google Play:

<https://play.google.com/store/apps/details?id=com.cuntubuntu&hl=en>
Launch Debain and get to XSDL/Xfce desktop. Double click on Enable Audio icon or install pulseaudio manually.

Double click User Terminal icon to launch a terminal and install wine:

        fakeroot apt-get install wine

Copy executable and driver from SDCARD:

        cd /
        cp /sdcard/wine-audio .
        chmod 777 wine-audio  
        cd /usr/lib/i386-linux-gnu/wine
        mv winealsa.drv.so winealsa.drv.so-official
        cp /sdcard/winealsa.drv.so .
        chmod 644 winealsa.drv.so

Add a command to lauch wine-audio in /proot.sh (It may be difficult to edit file on tablet. You can transfer proot.sh to host , edit it with unix text file friendly editor and transfer it back)

Add  "./wine-audio &" to /proot.sh right after these line:

        ...
        
        done
        echo "STORAGE $STORAGE"
        
        ./wine-audio &    <--- new command
        
        ...

Wine-audio server will now start whenever you launch Debian chroot in Android 

In host create a wine-audio.reg file for configure wine audio with following lines:

        REGEDIT4
        
        [HKEY_CURRENT_USER\Software\Wine\DirectSound]
        "HalBuflen"="16384"
        "SndQueueMax"="4"

Transfer wine-audio.reg to tablet and update wine registry with it:

In host:  

        adb push wine-audio.reg /sdcard/
        
In tablet Debian terminal emulator:  

        regedit /sdcard/wine-audio.reg

Exit Debian chroot and launch it again to kickstart audio server. Start a wine application/game that play pcm audio.
If you hear choppy audio, try double value of HalBuflen and/or SndQueueMax, update wine registry with new value and test again.












