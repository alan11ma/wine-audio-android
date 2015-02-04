# wine-audio-android
Project contains source for a Android exectuable that read pcm stream from a named pipe and forward it to Android OpenSL 1.0.1 api.
Named piped start with a header contains pcm format paramters  (Samples per second, no. of channel, sample bit count). This server will allocate a buffered audio player from OpenSL for the pcm format read from steam header. Player creation can fail if the running platform cannot support requested format. Audio playback stop when pipe souce is closed.

Includes a complement patch that modify Wine-1.4.1 alsa driver, which write pcm samples to named pipe. This allows WINE running inside Android "chroot" send pcm stream directly to OpenSL and cut down audio latency.
