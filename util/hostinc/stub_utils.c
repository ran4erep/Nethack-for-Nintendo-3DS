/* Small host-side stubs for building NetHack utilities under the 3DS
 * configuration (include/3dsconf.h).  Only used by host-tool build rules
 * in sys/3ds/Makefile.3ds; never linked into the 3DS game. */

#include <string.h>
#include <strings.h>

int
strncmpi(const char *s1, const char *s2, int n)
{
    if (n < 0)
        return strcasecmp(s1, s2);
    return strncasecmp(s1, s2, (size_t) n);
}
