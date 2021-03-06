/*
    DeaDBeeF -- the music player
    Copyright (C) 2009-2014 Alexey Yakovenko and other contributors

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.

    3. This notice may not be removed or altered from any source distribution.
*/

// basic syntax:
// function call: $function([arg1[,arg2[,...]]])
// meta fields, with spaces allowed: %field name%
// if_defined block: [text$func()%field%more text]
// plain text: anywhere outside of the above
// escaping: $, %, [, ], \ must be escaped

// bytecode format
// 0: indicates start of special block
//  1: function call
//   func_idx:byte, num_args:byte, arg1_len:byte[,arg2_len:byte[,...]]
//  2: meta field
//   len:byte, data
//  3: if_defined block
//   len:int32, data
//  4: pre-interpreted text
//   len:int32, data
// !0: plain text

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include "streamer.h"
#include "utf8.h"
#include "playlist.h"
#include "playqueue.h"
#include "tf.h"
#include "gettext.h"
#include "plugins.h"
#include "junklib.h"

#define min(x,y) ((x)<(y)?(x):(y))

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

typedef struct {
    const char *i;
    char *o;
    int eol;
} tf_compiler_t;

typedef int (*tf_func_ptr_t)(ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef);

#define TF_MAX_FUNCS 0xff

typedef struct {
    const char *name;
    tf_func_ptr_t func;
} tf_func_def;

static int
tf_eval_int (ddb_tf_context_t *ctx, char *code, int size, char *out, int outlen, int *bool_out, int fail_on_undef);

#define TF_EVAL_CHECK(res, ctx, arg, arg_len, out, outlen, fail_on_undef)\
res = tf_eval_int (ctx, arg, arg_len, out, outlen, &bool_out, fail_on_undef);\
if (res < 0) { *out = 0; return -1; }

// empty track is used when ctx.it is null
static playItem_t empty_track;
// empty playlist is used when ctx.plt is null
static playlist_t empty_playlist;
// empty code is used when "code" argumen is null
static char empty_code[4] = {0};

int
tf_eval (ddb_tf_context_t *ctx, char *code, char *out, int outlen) {
    if (!code) {
        code = empty_code;
    }

    int null_it = 0;
    if (!ctx->it) {
        null_it = 1;
        ctx->it = (ddb_playItem_t *)&empty_track;
    }

    int null_plt = 0;
    if (!ctx->plt) {
        null_plt = 1;
        ctx->plt = (ddb_playlist_t *)&empty_playlist;
    }

    int32_t codelen = *((int32_t *)code);
    code += 4;
    memset (out, 0, outlen);
    int l = 0;

    int bool_out = 0;
    int id = -1;
    if (ctx->flags & DDB_TF_CONTEXT_HAS_ID) {
        id = ctx->id;
    }

    switch (id) {
    case DB_COLUMN_FILENUMBER:
        if (ctx->flags & DDB_TF_CONTEXT_HAS_INDEX) {
            l = snprintf (out, outlen, "%d", ctx->idx+1);
        }
        else if (ctx->plt) {
            int idx = plt_get_item_idx ((playlist_t *)ctx->plt, (playItem_t *)ctx->it, PL_MAIN);
            l = snprintf (out, outlen, "%d", idx+1);
        }
        break;
    case DB_COLUMN_PLAYING:
        l = pl_format_item_queue ((playItem_t *)ctx->it, out, outlen);
        break;
    default:
        // tf_eval_int expects outlen to not include the terminating zero
        l = tf_eval_int (ctx, code, codelen, out, outlen-1, &bool_out, 0);
        break;
    }

#if 0
    // this breaks $crlf()
    for (; *out; out++) {
        if (*out == '\n') {
            *out = ';';
        }
    }
#endif
    if (null_it) {
        ctx->it = NULL;
    }
    if (null_plt) {
        ctx->plt = NULL;
    }
    return l;
}

// $greater(a,b) returns true if a is greater than b, otherwise false
int
tf_func_greater (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 2) {
        return -1;
    }
    char *arg = args;

    int bool_out = 0;

    char a[10];
    int len;
    TF_EVAL_CHECK(len, ctx, arg, arglens[0], a, sizeof (a), fail_on_undef);

    int aa = atoi (a);

    arg += arglens[0];
    char b[10];
    TF_EVAL_CHECK(len, ctx, arg, arglens[1], b, sizeof (b), fail_on_undef);
    int bb = atoi (b);

    return aa > bb;
}

// $strcmp(s1,s2) compares s1 and s2, returns true if equal, otherwise false
int
tf_func_strcmp (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 2) {
        return -1;
    }
    char *arg = args;

    int bool_out = 0;

    char s1[1000];
    int len;
    TF_EVAL_CHECK(len, ctx, arg, arglens[0], s1, sizeof (s1), fail_on_undef);

    arg += arglens[0];
    char s2[1000];
    TF_EVAL_CHECK(len, ctx, arg, arglens[1], s2, sizeof (s2), fail_on_undef);

    int res = strcmp (s1, s2);
    return !res;
}

int
tf_func_abbr (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 1 && argc != 2) {
        return -1;
    }

    char *arg = args;

    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, arg, arglens[0], out, outlen, fail_on_undef);

    if (argc == 2) {
        char num_chars_str[10];
        arg += arglens[0];
        int l;
        TF_EVAL_CHECK(l, ctx, arg, arglens[1], num_chars_str, sizeof (num_chars_str), fail_on_undef);
        int num_chars = atoi (num_chars_str);
        if (len <= num_chars) {
            return len;
        }
    }

    char *p = out;
    char *pout = out;
    const char skipchars[] = "() ,/\\|";
    while (*p) {
        // skip whitespace/paren
        while (*p && strchr (skipchars, *p)) {
            p++;
        }
        if (!*p) {
            break;
        }

        // take the first letter for abbrev
        int is_bracket = *p == '[' || *p == ']';
        int32_t size = 0;
        u8_nextchar(p, &size);
        memmove (pout, p, size);
        pout += size;
        p += size;

        // skip to the end of word
        while (*p && !strchr (skipchars, *p)) {
            if (!is_bracket) {
                p++;
            }
            else {
                size = 0;
                u8_nextchar(p, &size);
                memmove (pout, p, size);
                pout += size;
                p += size;
            }
        }
    }

    *pout = 0;
    return (int)(pout - out);
}

int
tf_func_ansi (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 1) {
        return -1;
    }

    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, args, arglens[0], out, outlen, fail_on_undef);
    return len;
}

int
tf_func_ascii (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 1) {
        return -1;
    }

    int bool_out = 0;

    int len;
    char temp_str[1000];
    TF_EVAL_CHECK(len, ctx, args, arglens[0], temp_str, sizeof (temp_str), fail_on_undef);

    len = junk_iconv (temp_str, len, out, outlen, "utf-8", "ascii");

    return len;
}

int
tf_caps_impl (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef, int do_lowercasing) {
    if (argc != 1) {
        return -1;
    }

    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, args, arglens[0], out, outlen, fail_on_undef);

    char *p = out;
    char *end = p + len;
    const char skipchars[] = "() ,/\\|";
    while (*p) {
        // skip whitespace/paren
        while (*p && strchr (skipchars, *p)) {
            p++;
        }
        if (!*p) {
            break;
        }

        int is_bracket = *p == '[' || *p == ']';

        char temp[5];

        // uppercase the first letter
        int32_t size = 0;
        u8_nextchar (p, &size);
        int32_t uppersize = u8_toupper ((const signed char *)p, size, temp);
        if (uppersize != size) {
            memmove (p+uppersize, p+size, end-(p+size));
            end += uppersize - size;
            *end = 0;
        }
        memcpy (p, temp, uppersize);

        p += uppersize;

        // lowercase to the end of word
        while (*p && !strchr (skipchars, *p)) {
            if (is_bracket) {
                p++;
            }
            else {
                size = 0;
                u8_nextchar ((const char *)p, &size);
                if (do_lowercasing) {
                    int32_t lowersize = u8_tolower ((const signed char *)p, size, temp);
                    if (lowersize != size) {
                        memmove (p+lowersize, p+size, end-(p+size));
                        end += lowersize - size;
                        *end = 0;
                    }
                    memcpy (p, temp, lowersize);
                    p += lowersize;
                }
                else {
                    p += size;
                }
            }
        }
    }

    return (int)(end - out);
}

int
tf_func_caps (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    return tf_caps_impl (ctx, argc, arglens, args, out, outlen, fail_on_undef, 1);
}

int
tf_func_caps2 (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    return tf_caps_impl (ctx, argc, arglens, args, out, outlen, fail_on_undef, 0);
}

int
tf_func_char (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 1) {
        return -1;
    }

    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, args, arglens[0], out, outlen, fail_on_undef);

    int n = atoi (out);
    *out = 0;

    if (outlen < 5) {
        return -1;
    }
    len = u8_wc_toutf8 (out, n);
    out[len] = 0;
    return len;
}

int
tf_func_crc32 (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 1) {
        return -1;
    }

    static const uint32_t tab[256] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
        0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
        0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
        0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
        0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
        0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
        0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
        0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
        0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
        0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
        0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
        0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
        0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
        0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
        0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
        0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
        0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
        0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
        0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
        0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
        0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
        0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
        0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
        0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
        0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
        0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
        0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
        0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
        0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
        0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
        0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
        0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
        0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
        0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
        0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
        0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
        0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
        0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
        0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
        0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
    };

    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, args, arglens[0], out, outlen, fail_on_undef);

    uint32_t crc = 0xffffffff;

    for (int i = 0; i < len; i++) {
        crc = (crc >> 8) ^ tab[(crc ^ (uint8_t)out[i]) & 0xff];
    }

    crc ^= 0xffffffff;

    return snprintf (out, outlen, "%u", crc);
}

int
tf_func_crlf (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 0 || outlen < 2) {
        return -1;
    }
    out[0] = '\n';
    out[1] = 0;
    return 1;
}

// $left(text,n) returns the first n characters of text
int
tf_func_left (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 2) {
        return -1;
    }
    char *arg = args;

    int bool_out = 0;

    // get number of characters
    char num_chars_str[10];
    arg += arglens[0];
    int len;
    TF_EVAL_CHECK(len, ctx, arg, arglens[1], num_chars_str, sizeof (num_chars_str), fail_on_undef);
    int num_chars = atoi (num_chars_str);
    if (num_chars <= 0 || num_chars > outlen) {
        *out = 0;
        return -1;
    }

    // get text
    char text[1000];
    arg = args;
    TF_EVAL_CHECK(len, ctx, arg, arglens[0], text, sizeof (text), fail_on_undef);

    int res = u8_strncpy (out, text, num_chars);
    trace ("left: (%s,%d) -> (%s), res: %d\n", text, num_chars, out, res);
    return res;
}

int
tf_func_directory (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc < 1 || argc > 2) {
        return -1;
    }

    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, args, arglens[0], out, outlen, fail_on_undef);

    int path_len = len;

    int levels = 1;
    if (argc == 2) {
        char temp[20];
        args += arglens[0];
        TF_EVAL_CHECK(len, ctx, args, arglens[1], temp, sizeof (temp), fail_on_undef);
        levels = atoi (temp);
        if (levels < 0) {
            return -1;
        }
    }

    char *end = out + path_len - 1;
    char *start = end;

    while (levels--) {
        // get to the last delimiter
        while (end >= out && *end != '/') {
            end--;
        }

        if (end < out) {
            *out = 0;
            return -1;
        }

        // skip multiple delimiters
        while (end >= out && *end == '/') {
            end--;
        }
        end++;

        if (end < out) {
            *out = 0;
            return -1;
        }

        // find another delimiter
        start = end - 1;
        while (start > out && *start != '/') {
            start--;
        }

        if (*start == '/') {
            start++;
        }

        if (levels) {
            end = start;
            while (end >= out && *end == '/') {
                end--;
            }
        }
    }

    memmove (out, start, end-start);
    out[end-start] = 0;
    return (int)(end-start);
}

int
tf_func_directory_path (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc < 1 || argc > 2) {
        return -1;
    }

    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, args, arglens[0], out, outlen, fail_on_undef);

    char *p = out + len - 1;

    while (p >= out && *p != '/') {
        p--;
    }
    while (p >= out && *p == '/') {
        p--;
    }
    if (p < out) {
        *out = 0;
        return -1;
    }

    p++;
    *p = 0;
    return (int)(p-out);
}

int
tf_func_ext (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc < 1 || argc > 2) {
        return -1;
    }

    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, args, arglens[0], out, outlen, fail_on_undef);
    
    char *e = out + len;
    char *c = e - 1;
    char *p = NULL;

    while (c >= out && *c != '/') {
        if (*c == '.') {
            p = c+1;
        }
        c--;
    }

    if (!p) {
        *out = 0;
        return 0;
    }

    memmove (out, p, e-p+1);
    return (int)(e-p);
}

int
tf_func_filename (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc < 1 || argc > 2) {
        return -1;
    }

    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, args, arglens[0], out, outlen, fail_on_undef);

    char *e = out + len;
    char *p = e - 1;
    while (p >= out && *p != '/') {
        p--;
    }

    p++;

    memmove (out, p, e-p+1);
    return (int)(e-p);
}

int
tf_func_add (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    int outval = 0;
    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        outval += atoi (out);
        arg += arglens[i];
    }
    return snprintf (out, outlen, "%d", outval);
}

int
tf_func_div (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    if (argc < 2) {
        return -1;
    }

    float outval = 0;
    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        if (i == 0) {
            outval = atoi (out);
        }
        else {
            int divider = atoi (out);
            if (divider == 0) {
                out[0] = 0;
                return -1;
            }
            outval /= divider;
        }
        arg += arglens[i];
    }
    int res = snprintf (out, outlen, "%d", (int)round (outval));
    return res;
}

int
tf_func_max (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    if (argc == 0) {
        return -1;
    }

    int nmax = -1;
    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        int n = atoi (out);
        if (n > nmax) {
            nmax = n;
        }
        arg += arglens[i];
    }
    int res = snprintf (out, outlen, "%d", nmax);
    return res;
}

int
tf_func_min (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    if (argc == 0) {
        return -1;
    }

    int nmin = 0x7fffffff;
    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        int n = atoi (out);
        if (n < nmin) {
            nmin = n;
        }
        arg += arglens[i];
    }
    int res = snprintf (out, outlen, "%d", nmin);
    return res;
}

int
tf_func_mod (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    if (argc < 2) {
        return -1;
    }

    int outval = 0;
    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        if (i == 0) {
            outval = atoi (out);
        }
        else {
            int divider = atoi (out);
            if (divider == 0) {
                *out = 0;
                return -1;
            }
            outval %= divider;
        }
        arg += arglens[i];
    }
    int res = snprintf (out, outlen, "%d", outval);
    return res;
}

int
tf_func_mul (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    if (argc < 2) {
        return -1;
    }

    int outval = 0;
    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        if (i == 0) {
            outval = atoi (out);
        }
        else {
            outval *= atoi (out);
        }
        arg += arglens[i];
    }
    int res = snprintf (out, outlen, "%d", outval);
    return res;
}

int
tf_func_muldiv (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    if (argc != 3) {
        return -1;
    }

    int vals[3];
    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        vals[i] = atoi (out);
        arg += arglens[i];
    }

    if (vals[2] == 0) {
        *out = 0;
        return -1;
    }

    int outval = (int)round(vals[0] * vals[1] / (float)vals[2]);

    int res = snprintf (out, outlen, "%d", outval);
    return res;
}

int
tf_func_rand (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 0) {
        return -1;
    }

    int outval = rand ();

    int res = snprintf (out, outlen, "%d", outval);
    return res;
}

int
tf_func_sub (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    if (argc < 2) {
        return -1;
    }

    int outval = 0;
    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        if (i == 0) {
            outval = atoi (out);
        }
        else {
            outval -= atoi (out);
        }
        arg += arglens[i];
    }
    int res = snprintf (out, outlen, "%d", outval);
    return res;
}

int
tf_func_if (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc < 2 || argc > 3) {
        return -1;
    }
    int bool_out = 0;

    char *arg = args;
    int res;
    TF_EVAL_CHECK(res, ctx, arg, arglens[0], out, outlen, fail_on_undef);
    arg += arglens[0];
    if (bool_out) {
        trace ("condition true, eval then block\n");
        TF_EVAL_CHECK(res, ctx, arg, arglens[1], out, outlen, fail_on_undef);
    }
    else if (argc == 3) {
        trace ("condition false, eval else block\n");
        arg += arglens[1];
        TF_EVAL_CHECK(res, ctx, arg, arglens[2], out, outlen, fail_on_undef);
    }

    return res;
}

int
tf_func_if2 (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 2) {
        return -1;
    }
    int bool_out = 0;

    char *arg = args;
    int res;
    TF_EVAL_CHECK(res, ctx, arg, arglens[0], out, outlen, fail_on_undef);
    arg += arglens[0];
    if (bool_out) {
        return res;
    }
    else {
        TF_EVAL_CHECK(res, ctx, arg, arglens[1], out, outlen, fail_on_undef);
    }

    return res;
}

int
tf_func_if3 (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc < 2) {
        return -1;
    }
    int bool_out = 0;

    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int res;
        TF_EVAL_CHECK(res, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        arg += arglens[i];
        if (bool_out || i == argc-1) {
            return res;
        }
    }
    *out = 0;
    return -1;
}

int
tf_func_ifequal (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 4) {
        return -1;
    }

    int bool_out = 0;

    char *arg = args;
    int len;
    TF_EVAL_CHECK(len, ctx, arg, arglens[0], out, outlen, fail_on_undef);

    int arg1 = atoi (out);

    arg += arglens[0];
    TF_EVAL_CHECK(len, ctx, arg, arglens[1], out, outlen, fail_on_undef);

    int arg2 = atoi (out);

    arg += arglens[1];

    int idx = 2;
    if (arg1 != arg2) {
        arg += arglens[2];
        idx = 3;
    }

    TF_EVAL_CHECK(len, ctx, arg, arglens[idx], out, outlen, fail_on_undef);
    return len;
}

int
tf_func_ifgreater (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 4) {
        return -1;
    }

    int bool_out = 0;

    char *arg = args;
    int len;
    TF_EVAL_CHECK(len, ctx, arg, arglens[0], out, outlen, fail_on_undef);

    int arg1 = atoi (out);

    arg += arglens[0];
    TF_EVAL_CHECK(len, ctx, arg, arglens[1], out, outlen, fail_on_undef);

    int arg2 = atoi (out);

    arg += arglens[1];

    int idx = 2;
    if (arg1 <= arg2) {
        arg += arglens[2];
        idx = 3;
    }

    TF_EVAL_CHECK(len, ctx, arg, arglens[idx], out, outlen, fail_on_undef);
    return len;
}

int
tf_func_iflonger (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 4) {
        return -1;
    }

    int bool_out = 0;

    char *arg = args;
    int len;
    TF_EVAL_CHECK(len, ctx, arg, arglens[0], out, outlen, fail_on_undef);
    int l1 = (int)strlen (out);

    arg += arglens[0];
    TF_EVAL_CHECK(len, ctx, arg, arglens[1], out, outlen, fail_on_undef);
    int l2 = (int)strlen (out);

    arg += arglens[1];
    int idx = 2;
    if (l1 <= l2) {
        arg += arglens[2];
        idx = 3;
    }

    TF_EVAL_CHECK(len, ctx, arg, arglens[idx], out, outlen, fail_on_undef);
    return len;
}

int
tf_func_select (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc < 3) {
        return -1;
    }

    char *arg = args;

    int bool_out = 0;

    int res;
    TF_EVAL_CHECK(res, ctx, arg, arglens[0], out, outlen, fail_on_undef);

    int n = atoi (out);
    if (n < 1 || n >= argc) {
        return 0;
    }

    arg += arglens[0];

    for (int i = 1; i < n; i++) {
        arg += arglens[i];
    }
    TF_EVAL_CHECK(res, ctx, arg, arglens[n], out, outlen, fail_on_undef);
    return res;
}

static void
tf_append_out (char **out, int *out_len, const char *in, int in_len) {
    in_len = min (in_len, *out_len);
    in_len = u8_strnbcpy (*out, in, in_len);
    *out_len -= in_len;
    *out += in_len;
}

int
tf_func_meta (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 1) {
        return -1;
    }

    if (!ctx->it) {
        return 0;
    }

    int bool_out = 0;

    char *arg = args;
    int len;
    TF_EVAL_CHECK(len, ctx, arg, arglens[0], out, outlen, fail_on_undef);

    const char *meta = pl_find_meta_raw ((playItem_t *)ctx->it, out);
    if (!meta) {
        return 0;
    }

    return u8_strnbcpy(out, meta, outlen);
}

const char *
tf_get_channels_string_for_track (playItem_t *it) {
    const char *val = pl_find_meta_raw (it, ":CHANNELS");
    if (val) {
        int ch = atoi (val);
        if (ch == 1) {
            val = _("mono");
        }
        else if (ch == 2) {
            val = _("stereo");
        }
    }
    else {
        val = _("stereo");
    }
    return val;
}

int
tf_func_channels (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 0) {
        return -1;
    }

    if (!ctx->it) {
        return 0;
    }

    const char *val = tf_get_channels_string_for_track ((playItem_t *)ctx->it);
    return u8_strnbcpy(out, val, outlen);
}

// Boolean
int
tf_func_and (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        if (!bool_out) {
            return 0;
        }
        arg += arglens[i];
    }
    *out = 0;
    return 1;
}

int
tf_func_or (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;

    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        if (bool_out) {
            return 1;
        }
        arg += arglens[i];
    }
    *out = 0;
    return 0;
}

int
tf_func_not (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    if (argc != 1) {
        return -1;
    }
    int bool_out = 0;

    int len;
    TF_EVAL_CHECK(len, ctx, args, arglens[0], out, outlen, fail_on_undef);
    return !bool_out;
}

int
tf_func_xor (ddb_tf_context_t *ctx, int argc, char *arglens, char *args, char *out, int outlen, int fail_on_undef) {
    int bool_out = 0;
    int result = 0;

    char *arg = args;
    for (int i = 0; i < argc; i++) {
        int len;
        TF_EVAL_CHECK(len, ctx, arg, arglens[i], out, outlen, fail_on_undef);
        if (i == 0) {
            result = bool_out;
        }
        else {
            result ^= bool_out;
        }
        arg += arglens[i];
    }
    *out = 0;
    return result;
}

tf_func_def tf_funcs[TF_MAX_FUNCS] = {
    // Control flow
    { "if", tf_func_if },
    { "if2", tf_func_if2 },
    { "if3", tf_func_if3 },
    { "ifequal", tf_func_ifequal },
    { "ifgreater", tf_func_ifgreater },
    { "iflonger", tf_func_iflonger },
    { "select", tf_func_select },
    // Arithmetic
    { "add", tf_func_add },
    { "div", tf_func_div },
    { "greater", tf_func_greater },
    { "max", tf_func_max },
    { "min", tf_func_min },
    { "mod", tf_func_mod },
    { "mul", tf_func_mul },
    { "muldiv", tf_func_muldiv },
    { "rand", tf_func_rand },
    { "sub", tf_func_sub },
    // Boolean
    { "and", tf_func_and },
    { "or", tf_func_or },
    { "not", tf_func_not },
    { "xor", tf_func_xor },
    // String
    { "abbr", tf_func_abbr },
    { "ansi", tf_func_ansi },
    { "ascii", tf_func_ascii },
    { "caps", tf_func_caps },
    { "caps2", tf_func_caps2 },
    { "char", tf_func_char },
    { "crc32", tf_func_crc32 },
    { "crlf", tf_func_crlf },
    { "cut", tf_func_left },
    { "left", tf_func_left }, // alias of 'cut'
    { "directory", tf_func_directory },
    { "directory_path", tf_func_directory_path },
    { "ext", tf_func_ext },
    { "filename", tf_func_filename },
    { "strcmp", tf_func_strcmp },
    // Track info
    { "meta", tf_func_meta },
    { "channels", tf_func_channels },
    { NULL, NULL }
};

static int
tf_eval_int (ddb_tf_context_t *ctx, char *code, int size, char *out, int outlen, int *bool_out, int fail_on_undef) {
    playItem_t *it = (playItem_t *)ctx->it;
    char *init_out = out;
    *bool_out = 0;
    while (size) {
        if (*code) {
            int len = u8_charcpy (out, code, outlen);
            if (len == 0) {
                break;
            }
            code += len;
            size -= len;
            out += len;
            outlen -= len;
        }
        else {
            code++;
            size--;
            if (*code == 1) {
                code++;
                size--;
                tf_func_ptr_t func = tf_funcs[*code].func;
                code++;
                size--;
                int res = func (ctx, code[0], code+1, code+1+code[0], out, outlen, fail_on_undef);
                if (res == -1) {
                    return -1;
                }
                if (res > 0) {
                    *bool_out = 1;
                    // hack for returning true + empty string result (e.g. tf_func_and)
                    if (*out == 0) {
                        res = 0;
                    }
                }

                out += res;
                outlen -= res;

                int blocksize = 1 + code[0];
                for (int i = 0; i < code[0]; i++) {
                    blocksize += code[1+i];
                }
                code += blocksize;
                size -= blocksize;
            }
            else if (*code == 2) {
                code++;
                size--;
                uint8_t len = *code;
                code++;
                size--;

                char name[len+1];
                memcpy (name, code, len);
                name[len] = 0;

                // special cases
                // most if not all of this stuff is to make tf scripts
                // compatible with fb2k syntax
                pl_lock ();
                const char *val = NULL;
                const char *aa_fields[] = { "album artist", "albumartist", "band", "artist", "composer", "performer", NULL };
                const char *a_fields[] = { "artist", "album artist", "albumartist", "composer", "performer", NULL };
                const char *alb_fields[] = { "album", "venue", NULL };

                // set to 1 if special case handler successfully wrote the output
                int skip_out = 0;

                // temp vars used for strcmp optimizations
                int tmp_a = 0, tmp_b = 0, tmp_c = 0, tmp_d = 0;

                if (!strcmp (name, aa_fields[0])) {
                    for (int i = 0; !val && aa_fields[i]; i++) {
                        val = pl_find_meta_raw (it, aa_fields[i]);
                    }
                }
                else if (!strcmp (name, a_fields[0])) {
                    for (int i = 0; !val && a_fields[i]; i++) {
                        val = pl_find_meta_raw (it, a_fields[i]);
                    }
                }
                else if (!strcmp (name, "album")) {
                    for (int i = 0; !val && alb_fields[i]; i++) {
                        val = pl_find_meta_raw (it, alb_fields[i]);
                    }
                }
                else if (!strcmp (name, "track artist")) {
                    const char *aa = NULL;
                    for (int i = 0; !val && aa_fields[i]; i++) {
                        val = pl_find_meta_raw (it, aa_fields[i]);
                    }
                    aa = val;
                    val = NULL;
                    for (int i = 0; !val && a_fields[i]; i++) {
                        val = pl_find_meta_raw (it, a_fields[i]);
                    }
                    if (val && aa && !strcmp (val, aa)) {
                        val = NULL;
                    }
                }
                else if (!strcmp (name, "tracknumber")) {
                    const char *v = pl_find_meta_raw (it, "track");
                    if (v) {
                        const char *p = v;
                        while (*p) {
                            if (!isdigit (*p)) {
                                break;
                            }
                            p++;
                        }
                        if (!*p) {
                            int len = snprintf (out, outlen, "%02d", atoi(v));
                            out += len;
                            outlen -= len;
                            skip_out = 1;
                        }
                    }
                }
                else if (!strcmp (name, "title")) {
                    val = pl_find_meta_raw (it, "title");
                    if (!val) {
                        const char *v = pl_find_meta_raw (it, ":URI");
                        if (v) {
                            const char *start = strrchr (v, '/');
                            if (start) {
                                start++;
                            }
                            else {
                                start = v;
                            }
                            const char *end = strrchr (start, '.');
                            if (end) {
                                int n = (int)(end-start);
                                n = min ((int)(end-start), outlen);
                                n = u8_strnbcpy (out, start, n);
                                outlen -= n;
                                out += n;
                            }
                        }
                    }
                }
                else if (!strcmp (name, "discnumber")) {
                    val = pl_find_meta_raw (it, "disc");
                }
                else if (!strcmp (name, "totaldiscs")) {
                    val = pl_find_meta_raw (it, "numdiscs");
                }
                else if (!strcmp (name, "track number")) {
                    val = pl_find_meta_raw (it, "track");
                }
                else if (!strcmp (name, "date")) {
                    // NOTE: foobar2000 uses "date" instead of "year"
                    // so for %date% we simply return the content of "year"
                    val = pl_find_meta_raw (it, "year");
                }
                else if (!strcmp (name, "samplerate")) {
                    val = pl_find_meta_raw (it, ":SAMPLERATE");
                }
                else if (!strcmp (name, "bitrate")) {
                    val = pl_find_meta_raw (it, ":BITRATE");
                }
                else if (!strcmp (name, "filesize")) {
                    val = pl_find_meta_raw (it, ":FILE_SIZE");
                }
                else if (!strcmp (name, "filesize_natural")) {
                    const char *v = pl_find_meta_raw (it, ":FILE_SIZE");
                    if (v) {
                        int64_t bs = atoll (v);
                        int len;
                        if (bs >= 1024*1024*1024) {
                            double gb = (double)bs / (double)(1024*1024*1024);
                            len = snprintf (out, outlen, "%.3lf GB", gb);
                        }
                        else if (bs >= 1024*1024) {
                            double mb = (double)bs / (double)(1024*1024);
                            len = snprintf (out, outlen, "%.3lf MB", mb);
                        }
                        else if (bs >= 1024) {
                            double kb = (double)bs / (double)(1024);
                            len = snprintf (out, outlen, "%.3lf KB", kb);
                        }
                        else {
                            len = snprintf (out, outlen, "%lld B", bs);
                        }
                        out += len;
                        outlen -= len;
                        skip_out = 1;
                    }
                }
                else if (!strcmp (name, "channels")) {
                    val = tf_get_channels_string_for_track (it);
                }
                else if (!strcmp (name, "codec")) {
                    val = pl_find_meta_raw (it, ":FILETYPE");
                }
                else if (!strcmp (name, "replaygain_album_gain")) {
                    val = pl_find_meta_raw (it, ":REPLAYGAIN_ALBUMGAIN");
                }
                else if (!strcmp (name, "replaygain_album_peak")) {
                    val = pl_find_meta_raw (it, ":REPLAYGAIN_ALBUMPEAK");
                }
                else if (!strcmp (name, "replaygain_track_gain")) {
                    val = pl_find_meta_raw (it, ":REPLAYGAIN_TRACKGAIN");
                }
                else if (!strcmp (name, "replaygain_track_peak")) {
                    val = pl_find_meta_raw (it, ":REPLAYGAIN_TRACKPEAK");
                }
                else if ((tmp_a = !strcmp (name, "playback_time")) || (tmp_b = !strcmp (name, "playback_time_seconds")) || (tmp_c = !strcmp (name, "playback_time_remaining")) || (tmp_d = !strcmp (name, "playback_time_remaining_seconds"))) {
                    playItem_t *playing = streamer_get_playing_track ();
                    if (it && playing == it) {
                        float t = streamer_get_playpos ();
                        if (tmp_c || tmp_d) {
                            printf ("inverse time %d %d %d %d\n", tmp_a, tmp_b, tmp_c, tmp_d);
                            float dur = pl_get_item_duration (it);
                            t = dur - t;
                        }
                        if (t >= 0) {
                            int len = 0;
                            if (tmp_a || tmp_c) {
                                int hr = t/3600;
                                int mn = (t-hr*3600)/60;
                                int sc = t-hr*3600-mn*60;
                                if (hr) {
                                    len = snprintf (out, outlen, "%2d:%02d:%02d", hr, mn, sc);
                                }
                                else {
                                    len = snprintf (out, outlen, "%2d:%02d", mn, sc);
                                }
                            }
                            else if (tmp_b || tmp_d) {
                                len = snprintf (out, outlen, "%0.2f", t);
                            }
                            out += len;
                            outlen -= len;
                            skip_out = 1;
                            // notify the caller about update interval
                            if (!ctx->update || (ctx->update > 1000)) {
                                ctx->update = 1000;
                            }
                        }
                    }
                    if (playing) {
                        pl_item_unref (playing);
                    }
                }
                else if ((tmp_a = !strcmp (name, "length")) || (tmp_b = !strcmp (name, "length_ex"))) {
                    float t = pl_get_item_duration (it);
                    if (tmp_a) {
                        t = roundf (t);
                    }
                    else if (tmp_b) {
                        t = roundf(t * 1000) / 1000.f;
                    }
                    if (t >= 0) {
                        int hr = t/3600;
                        int mn = (t-hr*3600)/60;
                        int sc = tmp_a ? t-hr*3600-mn*60 : t-hr*3600-mn*60;
                        int ms = tmp_b ? (t-hr*3600-mn*60-sc) * 1000.f : 0;
                        int len = 0;
                        if (tmp_a) {
                            if (hr) {
                                len = snprintf (out, outlen, "%2d:%02d:%02d", hr, mn, sc);
                            }
                            else {
                                len = snprintf (out, outlen, "%2d:%02d", mn, sc);
                            }
                        }
                        else if (tmp_b) {
                            if (hr) {
                                len = snprintf (out, outlen, "%2d:%02d:%02d.%03d", hr, mn, sc, ms);
                            }
                            else {
                                len = snprintf (out, outlen, "%2d:%02d.%03d", mn, sc, ms);
                            }
                        }
                        out += len;
                        outlen -= len;
                        skip_out = 1;
                    }
                }
                else if ((tmp_a = !strcmp (name, "length_seconds") || (tmp_b = !strcmp (name, "length_seconds_fp")))) {
                    float t = pl_get_item_duration (it);
                    if (t >= 0) {
                        int len;
                        if (tmp_a) {
                            len = snprintf (out, outlen, "%d", (int)roundf(t));
                        }
                        else {
                            len = snprintf (out, outlen, "%0.3f", t);
                        }
                        out += len;
                        outlen -= len;
                        skip_out = 1;
                    }
                }
                else if (!strcmp (name, "length_samples")) {
                    int len = snprintf (out, outlen, "%d", ctx->it->endsample - ctx->it->startsample);
                    out += len;
                    outlen -= len;
                    skip_out = 1;
                }
                else if ((tmp_a = !strcmp (name, "isplaying")) || (tmp_b = !strcmp (name, "ispaused"))) {
                    playItem_t *playing = streamer_get_playing_track ();
                    
                    if (playing && 
                            (
                            (tmp_a && plug_get_output ()->state () == OUTPUT_STATE_PLAYING)
                            || (tmp_b && plug_get_output ()->state () == OUTPUT_STATE_PAUSED)
                            )) {
                        *out++ = '1';
                        outlen--;
                        skip_out = 1;
                    }
                    if (playing) {
                        pl_item_unref (playing);
                    }
                }
                else if (!strcmp (name, "filename")) {
                    const char *v = pl_find_meta_raw (it, ":URI");
                    if (v) {
                        const char *start = strrchr (val, '/');
                        if (start) {
                            start++;
                        }
                        else {
                            start = v;
                        }
                        const char *end = strrchr (start, '.');
                        if (end) {
                            tf_append_out(&out, &outlen, start, (int)(end-start));
                            skip_out = 1;
                        }
                    }
                }
                else if (!strcmp (name, "filename_ext")) {
                    const char *v = pl_find_meta_raw (it, ":URI");
                    if (v) {
                        const char *start = strrchr (v, '/');
                        if (start) {
                            tf_append_out (&out, &outlen, start+1, (int)strlen (start+1));
                            skip_out = 1;
                        }
                    }
                }
                else if (!strcmp (name, "directoryname")) {
                    const char *v = pl_find_meta_raw (it, ":URI");
                    if (v) {
                        const char *end = strrchr (v, '/');
                        if (end) {
                            const char *start = end - 1;
                            while (start >= v && *start != '/') {
                                start--;
                            }
                            if (start && start != end) {
                                start++;
                                tf_append_out(&out, &outlen, start, (int)(end-start));
                                skip_out = 1;
                            }
                        }
                    }
                }
                else if (!strcmp (name, "path")) {
                    val = pl_find_meta_raw (it, ":URI");
                }
                // index of track in playlist (zero-padded)
                else if (!strcmp (name, "list_index")) {
                    if (it) {
                        int total_tracks = plt_get_item_count ((playlist_t *)ctx->plt, ctx->iter);
                        int digits = 0;
                        do {
                            total_tracks /= 10;
                            digits++;
                        } while (total_tracks);

                        int idx = 0;
                        if (ctx->flags & DDB_TF_CONTEXT_HAS_INDEX) {
                            idx = ctx->idx + 1;
                        }
                        else {
                            idx = pl_get_idx_of_iter (it, ctx->iter) + 1;
                        }
                        int len = snprintf (out, outlen, "%0*d", digits, idx);
                        out += len;
                        outlen -= len;
                        skip_out = 1;
                    }
                }
                // total number of tracks in playlist
                else if (!strcmp (name, "list_total")) {
                    int total_tracks = -1;
                    if (ctx->plt) {
                        total_tracks = plt_get_item_count ((playlist_t *)ctx->plt, ctx->iter);
                    }
                    else {
                        playlist_t *plt = plt_get_curr ();
                        if (plt) {
                            total_tracks = plt_get_item_count (plt, ctx->iter);
                            plt_unref (plt);
                        }
                    }
                    if (total_tracks >= 0) {
                        int len = snprintf (out, outlen, "%d", total_tracks);
                        out += len;
                        outlen -= len;
                        skip_out = 1;
                    }
                }
                // index of track in queue
                else if (!strcmp (name, "queue_index")) {
                    if (it) {
                        int idx = playqueue_test (it) + 1;
                        if (idx >= 1) {
                            int len = snprintf (out, outlen, "%d", idx);
                            out += len;
                            outlen -= len;
                            skip_out = 1;
                        }
                    }
                }
                // indexes of track in queue
                else if (!strcmp (name, "queue_indexes")) {
                    if (it) {
                        int idx = playqueue_test (it) + 1;
                        if (idx >= 1) {
                            int len = snprintf (out, outlen, "%d", idx);
                            out += len;
                            outlen -= len;
                            int count = playqueue_getcount ();
                            for (int i = idx; i < count; i++) {
                                playItem_t *trk = playqueue_get_item (i);
                                if (trk) {
                                    if (it == trk) {
                                        len = snprintf (out, outlen, ",%d", i + 1);
                                        out += len;
                                        outlen -= len;
                                    }
                                    pl_item_unref (trk);
                                }
                            }
                            skip_out = 1;
                        }
                    }
                }
                // total amount of tracks in queue
                else if (!strcmp (name, "queue_total")) {
                    int count = playqueue_getcount ();
                    if (count >= 0) {
                        int len = snprintf (out, outlen, "%d", count);
                        out += len;
                        outlen -= len;
                        skip_out = 1;
                    }
                }
                else if (!strcmp (name, "_deadbeef_version")) {
                    val = VERSION;
                }
                else {
                    val = pl_find_meta_raw (it, name);
                }

                if (val) {
                    *bool_out = 1;
                }

                // default case
                if (!skip_out && val) {
                    int32_t l = u8_strnbcpy (out, val, outlen);

                    // replace any \n with ; for display
                    char *p = out;
                    while (p < out + l) {
                        if (*p == '\n') {
                            *p = ';';
                        }
                        p++;
                    }

                    out += l;
                    outlen -= l;
                }
                pl_unlock ();
                if (!skip_out && !val && fail_on_undef) {
                    return -1;
                }

                code += len;
                size -= len;
            }
            else if (*code == 3) {
                code++;
                size--;
                int32_t len;
                memcpy (&len, code, 4);
                code += 4;
                size -= 4;

                int bool_out = 0;
                int res = tf_eval_int (ctx, code, len, out, outlen, &bool_out, 1);
                if (res > 0) {
                    out += res;
                    outlen -= res;
                }
                code += len;
                size -= len;
            }
            else if (*code == 4) {
                code++;
                size--;
                int32_t len;
                memcpy (&len, code, 4);
                code += 4;
                size -= 4;
                int32_t l = u8_strnbcpy(out, code, len);
                out += l;
                outlen -= l;
                code += len;
                size -= len;
            }
            else {
                return -1;
            }
        }
    }
    *out = 0;
    return (int)(out-init_out);
}

int
tf_compile_plain (tf_compiler_t *c);

int
tf_compile_func (tf_compiler_t *c) {
    c->i++;

    // function marker
    *(c->o++) = 0;
    *(c->o++) = 1;

    const char *name_start = c->i;

    // find opening (
    while (*(c->i) && *(c->i) != '(') {
        c->i++;
    }

    if (!*(c->i)) {
        return -1;
    }

    int i;
    for (i = 0; tf_funcs[i].name; i++) {
        int l = (int)strlen (tf_funcs[i].name);
        if (c->i - name_start == l && !memcmp (tf_funcs[i].name, name_start, l)) {
            *(c->o++) = i;
            break;
        }
    }
    if (!tf_funcs[i].name) {
        return -1;
    }

    char func_name[c->i - name_start + 1];
    memcpy (func_name, name_start, c->i-name_start);
    func_name[c->i-name_start] = 0;

    c->i++;

    // remember ptr and start reading args
    char *start = c->o;
    *(c->o++) = 0; // num args
    char *argstart = c->o;

    //parse comma separated args until )
    while (*(c->i)) {
        if (*(c->i) == ',' || *(c->i) == ')') {
            // next arg
            int len = (int)(c->o - argstart);

            // special case for empty argument list
            if (len == 0 && *(c->i) == ')' && (*start) == 0) {
                break;
            }

            // expand arg lengths buffer by 1
            memmove (start+(*start)+2, start+(*start)+1, c->o - start - (*start));
            c->o++;
            (*start)++; // num args++
            // store arg length
            start[*start] = len;
            argstart = c->o;

            if (*(c->i) == ')') {
                break;
            }
            c->i++;
        }
        else if (tf_compile_plain (c)) {
            return -1;
        }
    }
    if (*(c->i) != ')') {
        return -1;
    }
    c->i++;

    return 0;
}

int
tf_compile_field (tf_compiler_t *c) {
    c->i++;
    *(c->o++) = 0;
    *(c->o++) = 2;

    const char *fstart = c->i;
    char *plen = c->o;
    c->o += 1;
    while (*(c->i)) {
        if (*(c->i) == '%') {
            break;
        }
        else {
            *(c->o++) = *(c->i++);
        }
    }
    if (*(c->i) != '%') {
        return -1;
    }
    c->i++;

    int32_t len = (int32_t)(c->o - plen - 1);
    if (len > 0xff) {
        return -1;
    }
    *plen = len;

    char field[len+1];
    memcpy (field, fstart, len);
    field[len] = 0;
    return 0;
}

int
tf_compile_ifdef (tf_compiler_t *c) {
    c->i++;
    *(c->o++) = 0;
    *(c->o++) = 3;

    char *plen = c->o;
    c->o += 4;

    char *start = c->o;

    while (*(c->i)) {
        if (*(c->i) == '\\') {
            c->i++;
            if (*(c->i) != 0) {
                *(c->o++) = *(c->i++);
            }
        }
        else if (*(c->i) == ']') {
            break;
        }
        else if (tf_compile_plain (c)) {
            return -1;
        }
    }

    if (*(c->i) != ']') {
        return -1;
    }
    c->i++;

    int32_t len = (int32_t)(c->o - plen - 4);
    memcpy (plen, &len, 4);

    char value[len+1];
    memcpy (value, start, len);
    value[len] = 0;
    return 0;
}

int
tf_compile_plain (tf_compiler_t *c) {
    int eol = c->eol;
    c->eol = 0;
    char i = *(c->i);
    if (i == '$') {
        if (tf_compile_func (c)) {
            return -1;
        }
    }
    else if (i == '[') {
        if (tf_compile_ifdef (c)) {
            return -1;
        }
    }
    else if (i == '%') {
        if (tf_compile_field (c)) {
            return -1;
        }
    }
    // FIXME this is not fb2k spec
    else if (*(c->i) == '\\') {
        c->i++;
        if (*(c->i) != 0) {
            *(c->o++) = *(c->i++);
        }
    }
    else if (eol && i == '/' && c->i[1] == '/') {
        // skip to end of line
        while (c->i[0] && c->i[0] != '\n') {
            c->i++;
        }
        c->eol = 1;
    }
    else if (i == '\'') {
        // copy as plain text to next single-quote
        c->i++;
        while (c->i[0] && c->i[0] != '\'') {
            *(c->o++) = *(c->i++);
        }
        if (c->i[0] == '\'') {
            c->i++;
        }
    }
    else if (i == '\n') {
        c->i++;
        c->eol = 1;
    }
    else {
        *(c->o++) = *(c->i++);
    }
    return 0;
}

char *
tf_compile (const char *script) {
    tf_compiler_t c;
    memset (&c, 0, sizeof (c));

    c.i = script;

    char code[strlen(script) * 3];
    memset (code, 0, sizeof (code));

    c.o = code;

    c.eol = 1;

    while (*(c.i)) {
        if (tf_compile_plain (&c)) {
            return NULL;
        }
    }

    size_t size = c.o - code;
    char *out = malloc (size + 8);
    memcpy (out + 4, code, size);
    memset (out + 4 + size, 0, 4); // FIXME: this is the padding for possible buffer overflow bug fix
    *((int32_t *)out) = (int32_t)(size);
    return out;
}

void
tf_free (char *code) {
    free (code);
}
