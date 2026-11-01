"""Part 9b-v mutations: the UTF-8 helpers (game/text_box.cpp: IsLegalUtf8Range, EncodeUtf8,
HasUtf8Sequences, HasCjkCharacters).

Run with the text build: --build build_text.bat --check check_text.py (TEXT_TEST; the "utf8:"
checks in textbox_test.inc). The deletions are named by the line they delete. The first run caught
58 of 59.

Equivalent (left alive, by design):
  B 132 cmp             an empty range let into the loop: its first sequence is longer than the
                        range, false either way.
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
B = os.path.join(ROOT, r"src/game/text_box.cpp")

MUTATIONS = [
    (B, 'B 132 cmp', '    if (begin >= end)\n', '    if (begin > end)\n'),
    (B, 'B 133 bool', '    if (begin >= end)\n        return false;\n', '    if (begin >= end)\n        return true;\n'),
    (B, 'B 135 num', '        const int length = UTF8_TRAILING[*p] + 1;\n', '        const int length = UTF8_TRAILING[*p] + 2;\n'),
    (B, 'B 135 arith', '        const int length = UTF8_TRAILING[*p] + 1;\n', '        const int length = UTF8_TRAILING[*p] - 1;\n'),
    (B, 'B 136 cmp', '        if (end - p < length)\n', '        if (end - p <= length)\n'),
    (B, 'B 136 arith', '        if (end - p < length)\n', '        if (end + p < length)\n'),
    (B, 'B 137 bool', '        if (end - p < length)\n            return false;\n', '        if (end - p < length)\n            return true;\n'),
    (B, 'B 139 bool', '        if (!IsLegalUtf8(p, length))\n            return false;\n', '        if (!IsLegalUtf8(p, length))\n            return true;\n'),
    (B, 'B 140 del', '            return false;\n        p += length;\n', '            return false;\n\n'),
    (B, 'B 141 cmp', '        if (p >= end)\n', '        if (p > end)\n'),
    (B, 'B 142 bool', '        if (p >= end)\n            return true;\n', '        if (p >= end)\n            return false;\n'),
    (B, 'B 149 cmp', '    if (ch >= 0xD800 && ch <= 0xDFFF)\n', '    if (ch >= 0xD800 && ch < 0xDFFF)\n'),
    (B, 'B 150 num', '        return -3;\n', '        return -4;\n'),
    (B, 'B 152 cmp', '    if (ch < 0x80) {\n', '    if (ch <= 0x80) {\n'),
    (B, 'B 153 del', '    if (ch < 0x80) {\n        length = 1;\n', '    if (ch < 0x80) {\n\n'),
    (B, 'B 153 num', '    if (ch < 0x80) {\n        length = 1;\n', '    if (ch < 0x80) {\n        length = 2;\n'),
    (B, 'B 154 cmp', '    } else if (ch < 0x800) {\n', '    } else if (ch <= 0x800) {\n'),
    (B, 'B 155 del', '    } else if (ch < 0x800) {\n        length = 2;\n', '    } else if (ch < 0x800) {\n\n'),
    (B, 'B 155 num', '    } else if (ch < 0x800) {\n        length = 2;\n', '    } else if (ch < 0x800) {\n        length = 3;\n'),
    (B, 'B 156 cmp', '    } else if (ch < 0x10000) {\n', '    } else if (ch <= 0x10000) {\n'),
    (B, 'B 157 del', '    } else if (ch < 0x10000) {\n        length = 3;\n', '    } else if (ch < 0x10000) {\n\n'),
    (B, 'B 157 num', '    } else if (ch < 0x10000) {\n        length = 3;\n', '    } else if (ch < 0x10000) {\n        length = 4;\n'),
    (B, 'B 158 cmp', '    } else if (ch < 0x200000) {\n', '    } else if (ch <= 0x200000) {\n'),
    (B, 'B 159 del', '    } else if (ch < 0x200000) {\n        length = 4;\n', '    } else if (ch < 0x200000) {\n\n'),
    (B, 'B 159 num', '    } else if (ch < 0x200000) {\n        length = 4;\n', '    } else if (ch < 0x200000) {\n        length = 5;\n'),
    (B, 'B 162 del', '        ch = 0xFFFD;\n', '\n'),
    (B, 'B 163 del', '        ch = 0xFFFD;\n        length = 2;\n', '        ch = 0xFFFD;\n\n'),
    (B, 'B 163 num', '        ch = 0xFFFD;\n        length = 2;\n', '        ch = 0xFFFD;\n        length = 3;\n'),
    (B, 'B 166 cmp', '    if (length >= maxBytes)\n', '    if (length > maxBytes)\n'),
    (B, 'B 167 num', '        return -4;\n', '        return -5;\n'),
    (B, 'B 168 arith', '    char *p = out + length;\n', '    char *p = out - length;\n'),
    (B, 'B 170 num', '    switch (length) {\n    case 4:\n', '    switch (length) {\n    case 5:\n'),
    (B, 'B 171 del', '    case 4:\n        *--p = static_cast<char>((ch & 0x3F) | 0x80);\n', '    case 4:\n\n'),
    (B, 'B 175 del', '    case 3:\n        *--p = static_cast<char>((ch & 0x3F) | 0x80);\n', '    case 3:\n\n'),
    (B, 'B 179 del', '    case 2:\n        *--p = static_cast<char>((ch & 0x3F) | 0x80);\n', '    case 2:\n\n'),
    (B, 'B 183 del', '        *--p = static_cast<char>(UTF8_FIRST_BYTE[length] | (ch & 0xFF));\n', '\n'),
    (B, 'B 193 num', '        int length = 0;\n', '        int length = 1;\n'),
    (B, 'B 194 cmp', '        if (*p < 0x80)\n', '        if (*p <= 0x80)\n'),
    (B, 'B 195 num', '            length = 1;\n', '            length = 2;\n'),
    (B, 'B 196 cmp', '        else if (*p >= 0xC0 && *p < 0xE0)\n', '        else if (*p > 0xC0 && *p < 0xE0)\n'),
    (B, 'B 197 num', '            length = 2;\n', '            length = 3;\n'),
    (B, 'B 198 cmp', '        else if (*p >= 0xE0 && *p < 0xF0)\n', '        else if (*p > 0xE0 && *p < 0xF0)\n'),
    (B, 'B 199 num', '            length = 3;\n', '            length = 4;\n'),
    (B, 'B 200 cmp', '        else if (*p >= 0xF0 && *p < 0xF8)\n', '        else if (*p > 0xF0 && *p < 0xF8)\n'),
    (B, 'B 201 num', '            length = 4;\n', '            length = 5;\n'),
    (B, 'B 202 cmp', '        if (length == 0)\n', '        if (length != 0)\n'),
    (B, 'B 202 num', '        if (length == 0)\n', '        if (length == 1)\n'),
    (B, 'B 203 bool', '        if (length == 0)\n            return false;\n', '        if (length == 0)\n            return true;\n'),
    (B, 'B 204 cmp', '        for (int k = 1; k < length; ++k)\n', '        for (int k = 1; k <= length; ++k)\n'),
    (B, 'B 204 num', '        for (int k = 1; k < length; ++k)\n', '        for (int k = 2; k < length; ++k)\n'),
    (B, 'B 205 cmp', '            if (p[k] < 0x80 || p[k] >= 0xC0)\n', '            if (p[k] < 0x80 || p[k] > 0xC0)\n'),
    (B, 'B 206 bool', '            if (p[k] < 0x80 || p[k] >= 0xC0)\n                return false;\n', '            if (p[k] < 0x80 || p[k] >= 0xC0)\n                return true;\n'),
    (B, 'B 207 cmp', '        if (length > 1)\n', '        if (length >= 1)\n'),
    (B, 'B 207 num', '        if (length > 1)\n', '        if (length > 2)\n'),
    (B, 'B 208 bool', '            multibyte = true;\n', '            multibyte = false;\n'),
    (B, 'B 209 del', '            multibyte = true;\n        p += length;\n', '            multibyte = true;\n\n'),
    (B, 'B 217 cmp', '        if (*text > 0x3000)\n', '        if (*text >= 0x3000)\n'),
    (B, 'B 218 bool', '        if (*text > 0x3000)\n            return true;\n', '        if (*text > 0x3000)\n            return false;\n'),
    (B, 'B 219 bool', '            return true;\n    return false;\n', '            return true;\n    return true;\n'),
]
