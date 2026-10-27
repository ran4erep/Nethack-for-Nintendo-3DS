#ifndef _3DSI18N_H
#define _3DSI18N_H

#include <stddef.h>

#define NH_LANG_EN 0
#define NH_LANG_RU 1

void _3ds_i18n_init(void);
int _3ds_i18n_get_language(void);
void _3ds_i18n_set_language(int lang);
void _3ds_utf8_to_cp1251(char *dst, const char *src, size_t dst_sz);

const char *nh_tr(const char *str);

/* When Russian is active, return a "<name>.ru" variant name for a data
 * file (help, history, ...) or NULL when the English name should be used.
 * The returned pointer is valid until the next call. */
const char *_3ds_i18n_file_alias(const char *fname);

#ifndef _
#define _(str) nh_tr(str)
#endif

#endif /* _3DSI18N_H */
