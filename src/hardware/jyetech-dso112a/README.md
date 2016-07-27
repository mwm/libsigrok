# JYETech DSO112A support
This version adds support for the JYETech DSO112A. **This is a work in
progress.** See the known issues section before you even try to
install it.

Build it from sources as normal. If you're installing it with homebrew
on the Mac, you can use
[these instructions](http://jyetech.com/forum/viewtopic.php?f=17&t=858&p=2719&sid=daf0138ec7b7255a48df6c091228b7a7#p2719).

# Known issues

1. It requires test firmware available available from the
   [JYETech forums](http://jyetech.com/forum/viewtopic.php?f=17&t=844&p=2717#p2717).
2. It works fine in sigrok-cli, but Pulseview is getting garbage
   **!?!!**.
3. The horizontal trigger position isn't really configurable, because
   it doesn't match sigrok's idea of how that should work.
4. It seems to miss samples at the end of frames.
