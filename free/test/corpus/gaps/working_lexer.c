/* POSITIVE TEST: A non-trivial lexer program that compiles successfully.
 * This exercises many features at once: enums, structs, switch, pointer
 * arithmetic, character classification, multiple functions with single
 * int params, do-while, const char *, array indexing.
 *
 * Use this as a regression test to ensure existing functionality isn't
 * broken when fixing the gaps.
 *
 * EXPECTED: compile success
 * STATUS: PASSES
 */

enum token_kind {
    TK_EOF = 0,
    TK_INT,
    TK_IDENT,
    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_LPAREN,
    TK_RPAREN,
    TK_SEMI,
    TK_EQ
};

struct token {
    enum token_kind kind;
    int int_val;
    char ident[64];
};

int is_alpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

int is_digit(int c) {
    return c >= '0' && c <= '9';
}

int is_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

struct lexer {
    const char *src;
    int pos;
};

int lex_peek(struct lexer *l) {
    return l->src[l->pos];
}

int lex_next(struct lexer *l) {
    int c;
    c = l->src[l->pos];
    if (c != '\0') l->pos = l->pos + 1;
    return c;
}

void lex_skip_ws(struct lexer *l) {
    while (is_space(lex_peek(l))) {
        lex_next(l);
    }
}

struct token lex_token(struct lexer *l) {
    struct token tok;
    int c;
    int i;

    lex_skip_ws(l);
    c = lex_peek(l);

    if (c == '\0') {
        tok.kind = TK_EOF;
        return tok;
    }

    if (is_digit(c)) {
        tok.kind = TK_INT;
        tok.int_val = 0;
        while (is_digit(lex_peek(l))) {
            tok.int_val = tok.int_val * 10 + (lex_next(l) - '0');
        }
        return tok;
    }

    if (is_alpha(c)) {
        tok.kind = TK_IDENT;
        i = 0;
        while (is_alpha(lex_peek(l)) || is_digit(lex_peek(l))) {
            if (i < 63) {
                tok.ident[i] = (char)lex_next(l);
                i = i + 1;
            }
        }
        tok.ident[i] = '\0';
        return tok;
    }

    lex_next(l);
    tok.kind = TK_EOF;
    switch (c) {
        case '+': tok.kind = TK_PLUS; break;
        case '-': tok.kind = TK_MINUS; break;
        case '*': tok.kind = TK_STAR; break;
        case '/': tok.kind = TK_SLASH; break;
        case '(': tok.kind = TK_LPAREN; break;
        case ')': tok.kind = TK_RPAREN; break;
        case ';': tok.kind = TK_SEMI; break;
        case '=': tok.kind = TK_EQ; break;
    }
    return tok;
}

int main(void) {
    struct lexer l;
    struct token t;
    int count;

    l.src = "x = 10 + 20 * 3;";
    l.pos = 0;
    count = 0;

    do {
        t = lex_token(&l);
        count = count + 1;
    } while (t.kind != TK_EOF);

    return count;
}
