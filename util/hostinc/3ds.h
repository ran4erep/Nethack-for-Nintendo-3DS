/* Stub <3ds.h> for building NetHack host-side utilities that include
 * hack.h while include/3dsconf.h defines NINTENDO_3DS.  The utilities
 * never call any libctru function, they only need the header to exist
 * so that the compiler does not fail on the include.  This directory is
 * only placed on the include path by the host-tool build rules in
 * sys/3ds/Makefile.3ds (and must come *before* $(CTRULIB)/include). */
#ifndef NETHACK_3DS_STUB_H
#define NETHACK_3DS_STUB_H
#endif /* NETHACK_3DS_STUB_H */
