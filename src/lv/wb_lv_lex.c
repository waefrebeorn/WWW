/*
 * wb_lv_lex.c -- the token stream: whitespace/comment skipping, numbers,
 * keywords, strings, punctuation. See wb_lv_internal.h.
 */
#include "wb_lv_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void wb_lv_perr(parser *P, const char *msg) {
    if (!P->err) {
        snprintf(P->L->err, sizeof P->L->err, "%s (line %d)", msg, P->line);
        P->L->has_err = 1;
        P->err = 1;
    }
}

void wb_lv_next_tok(parser *P) {
    const char *s = P->src;
    size_t n = strlen(s);
    /* skip whitespace + comments */
    for (;;) {
        while (P->p < n && (s[P->p] == ' ' || s[P->p] == '\t' || s[P->p] == '\r')) P->p++;
        if (P->p < n && s[P->p] == '\n') { P->line++; P->p++; continue; }
        if (P->p + 1 < n && s[P->p] == '-' && s[P->p + 1] == '-') {
            P->p += 2;
            while (P->p < n && s[P->p] != '\n') P->p++;
            continue;
        }
        break;
    }
    tok t;
    memset(&t, 0, sizeof t);
    t.line = P->line;
    if (P->p >= n) { t.type = T_EOF; P->cur = t; return; }
    char c = s[P->p];

    /* number */
    if (c >= '0' && c <= '9') {
        double v = 0;
        int isfrac = 0;
        double scale = 0.1;
        while (P->p < n) {
            char d = s[P->p];
            if (d >= '0' && d <= '9') {
                if (!isfrac) v = v * 10 + (d - '0');
                else { v += (d - '0') * scale; scale *= 0.1; }
                P->p++;
            } else if (d == '.' && !isfrac) { isfrac = 1; P->p++; }
            else break;
        }
        /* exponent (simple, optional) */
        if (P->p < n && (s[P->p] == 'e' || s[P->p] == 'E')) {
            size_t save = P->p;
            P->p++;
            int sgn = 1, expdig = 0;
            if (P->p < n && (s[P->p] == '+' || s[P->p] == '-')) {
                if (s[P->p] == '-') sgn = -1;
                P->p++;
            }
            double e = 0;
            while (P->p < n && s[P->p] >= '0' && s[P->p] <= '9') {
                e = e * 10 + (s[P->p] - '0');
                expdig = 1;
                P->p++;
            }
            if (expdig) v *= pow(10.0, sgn * e);
            else P->p = save;   /* not actually an exponent */
        }
        P->cur.type = T_NUM;
        P->cur.num = v;
        return;
    }

    /* identifier or keyword */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        size_t st = P->p;
        while (P->p < n && ((s[P->p] >= 'a' && s[P->p] <= 'z') ||
                            (s[P->p] >= 'A' && s[P->p] <= 'Z') ||
                            s[P->p] == '_' ||
                            (s[P->p] >= '0' && s[P->p] <= '9'))) P->p++;
        size_t wl = P->p - st;
        char w[64];
        if (wl >= sizeof w) wl = sizeof w - 1;
        memcpy(w, s + st, wl);
        w[wl] = 0;
        int kw = 1;
        if      (!strcmp(w, "if"))        t.type = T_IF;
        else if (!strcmp(w, "then"))      t.type = T_THEN;
        else if (!strcmp(w, "else"))      t.type = T_ELSE;
        else if (!strcmp(w, "elseif"))    t.type = T_ELSEIF;
        else if (!strcmp(w, "end"))       t.type = T_END;
        else if (!strcmp(w, "while"))     t.type = T_WHILE;
        else if (!strcmp(w, "do"))        t.type = T_DO;
        else if (!strcmp(w, "for"))       t.type = T_FOR;
        else if (!strcmp(w, "function"))  t.type = T_FUNCTION;
        else if (!strcmp(w, "local"))     t.type = T_LOCAL;
        else if (!strcmp(w, "return"))    t.type = T_RETURN;
        else if (!strcmp(w, "in"))        t.type = T_IN;
        else if (!strcmp(w, "true"))      t.type = T_TRUE;
        else if (!strcmp(w, "false"))     t.type = T_FALSE;
        else if (!strcmp(w, "nil"))       t.type = T_NIL;
        else if (!strcmp(w, "not"))       t.type = T_NOT;
        else if (!strcmp(w, "and"))       t.type = T_AND;
        else if (!strcmp(w, "or"))        t.type = T_OR;
        else kw = 0;
        if (!kw) { t.type = T_NAME; t.str = wb_lv_intern(P->L, w, wl); }
        P->cur = t;
        return;
    }

    /* string */
    if (c == '"' || c == '\'') {
        char q = c;
        P->p++;
        char buf[256];
        size_t bl = 0;
        while (P->p < n && s[P->p] != q) {
            if (s[P->p] == '\\' && P->p + 1 < n) { P->p++; buf[bl++] = s[P->p++]; }
            else buf[bl++] = s[P->p++];
        }
        if (P->p < n) P->p++; /* closing quote */
        buf[bl] = 0;
        t.type = T_STR;
        t.str = wb_lv_intern(P->L, buf, bl);
        P->cur = t;
        return;
    }

    /* punctuation */
    P->p++;
    switch (c) {
    case '=': if (s[P->p] == '=') { P->p++; t.type = T_EQ; } else t.type = '='; break;
    case '<': if (s[P->p] == '=') { P->p++; t.type = T_LE; } else t.type = '<'; break;
    case '>': if (s[P->p] == '=') { P->p++; t.type = T_GE; } else t.type = '>'; break;
    case '~': if (s[P->p] == '=') { P->p++; t.type = T_NE; } else t.type = '~'; break;
    case '.': if (s[P->p] == '.') { P->p++; t.type = T_CONCAT; } else t.type = '.'; break;
    default: t.type = WB_LV_TKCHAR(c); break;
    }
    P->cur = t;
}
