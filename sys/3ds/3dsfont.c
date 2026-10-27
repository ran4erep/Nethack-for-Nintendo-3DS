#include "3dsfont.h"

extern PrintConsole topConsole;
extern PrintConsole bottomConsole;

static ConsoleFont _3ds_console_font_cp1251 = {
    (u8 *) _3ds_font_cp1251,
    0,
    256
};

void
_3ds_font_init(void)
{
    consoleSetFont(&topConsole, &_3ds_console_font_cp1251);
    consoleSetFont(&bottomConsole, &_3ds_console_font_cp1251);
}
