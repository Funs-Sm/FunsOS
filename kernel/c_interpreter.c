#include "c_interpreter.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "keyboard.h"
#include "fb_console.h"
#include "vga_text.h"

/* ============================================================
 *  Lexer
 * ============================================================ */

typedef enum {
    TOK_EOF = 0,
    TOK_NUMBER,
    TOK_STRING,
    TOK_IDENT,
    TOK_KEYWORD,
    TOK_CHAR_LIT,

    /* Operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,
    TOK_LSHIFT, TOK_RSHIFT,
    TOK_LT, TOK_GT, TOK_LE, TOK_GE, TOK_EQ, TOK_NE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_ASSIGN,
    TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN, TOK_STAR_ASSIGN, TOK_SLASH_ASSIGN,
    TOK_PERCENT_ASSIGN,
    TOK_AMP_ASSIGN, TOK_PIPE_ASSIGN, TOK_CARET_ASSIGN,
    TOK_LSHIFT_ASSIGN, TOK_RSHIFT_ASSIGN,
    TOK_INC, TOK_DEC,

    /* Punctuation */
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMI, TOK_COMMA, TOK_DOT,
    TOK_QUESTION, TOK_COLON,
    TOK_ARROW
} token_type_t;

typedef struct {
    token_type_t type;
    char value[256];
    int32_t int_val;
} token_t;

typedef struct {
    const char *src;
    uint32_t pos;
    token_t current;
    int line;
} lexer_t;

/* Keywords */
static const char *keywords[] = {
    "int", "char", "float", "double", "void", "long", "short",
    "unsigned", "signed", "uint32_t", "int32_t", "uint8_t", "int8_t",
    "uint16_t", "int16_t", "uint64_t", "int64_t", "size_t",
    "if", "else", "while", "for", "do", "return", "break", "continue",
    "struct", "enum", "typedef", "sizeof", "switch", "case", "default",
    "static", "const", "extern", "NULL",
    0
};

static int is_keyword(const char *s) {
    for (int i = 0; keywords[i]; i++) {
        if (strcmp(s, keywords[i]) == 0) return 1;
    }
    return 0;
}

static void lex_next(lexer_t *lex) {
    /* Skip whitespace */
    while (lex->src[lex->pos] == ' ' || lex->src[lex->pos] == '\t' ||
           lex->src[lex->pos] == '\n' || lex->src[lex->pos] == '\r') {
        if (lex->src[lex->pos] == '\n') lex->line++;
        lex->pos++;
    }

    if (lex->src[lex->pos] == '\0') {
        lex->current.type = TOK_EOF;
        lex->current.value[0] = '\0';
        return;
    }

    char c = lex->src[lex->pos];
    char c2 = lex->src[lex->pos + 1];

    /* Skip single-line comments */
    if (c == '/' && c2 == '/') {
        while (lex->src[lex->pos] && lex->src[lex->pos] != '\n') lex->pos++;
        lex_next(lex);
        return;
    }

    /* Skip multi-line comments */
    if (c == '/' && c2 == '*') {
        lex->pos += 2;
        while (lex->src[lex->pos] && !(lex->src[lex->pos] == '*' && lex->src[lex->pos + 1] == '/')) {
            if (lex->src[lex->pos] == '\n') lex->line++;
            lex->pos++;
        }
        if (lex->src[lex->pos]) lex->pos += 2;
        lex_next(lex);
        return;
    }

    /* Number */
    if ((c >= '0' && c <= '9') || (c == '0' && (c2 == 'x' || c2 == 'X'))) {
        uint32_t idx = 0;
        int is_hex = 0;
        if (c == '0' && (c2 == 'x' || c2 == 'X')) {
            is_hex = 1;
            lex->current.value[idx++] = lex->src[lex->pos++];
            lex->current.value[idx++] = lex->src[lex->pos++];
        }
        while ((lex->src[lex->pos] >= '0' && lex->src[lex->pos] <= '9') ||
               (is_hex && ((lex->src[lex->pos] >= 'a' && lex->src[lex->pos] <= 'f') ||
                           (lex->src[lex->pos] >= 'A' && lex->src[lex->pos] <= 'F')))) {
            if (idx < 255) lex->current.value[idx++] = lex->src[lex->pos];
            lex->pos++;
        }
        lex->current.value[idx] = '\0';
        lex->current.type = TOK_NUMBER;
        if (is_hex) {
            lex->current.int_val = (int32_t)strtol(lex->current.value, 0, 16);
        } else {
            lex->current.int_val = atoi(lex->current.value);
        }
        return;
    }

    /* String literal */
    if (c == '"') {
        lex->pos++;
        uint32_t idx = 0;
        while (lex->src[lex->pos] && lex->src[lex->pos] != '"' && idx < 255) {
            if (lex->src[lex->pos] == '\\') {
                lex->pos++;
                switch (lex->src[lex->pos]) {
                    case 'n': lex->current.value[idx++] = '\n'; break;
                    case 't': lex->current.value[idx++] = '\t'; break;
                    case 'r': lex->current.value[idx++] = '\r'; break;
                    case '\\': lex->current.value[idx++] = '\\'; break;
                    case '"': lex->current.value[idx++] = '"'; break;
                    case '0': lex->current.value[idx++] = '\0'; break;
                    default: lex->current.value[idx++] = lex->src[lex->pos]; break;
                }
            } else {
                lex->current.value[idx++] = lex->src[lex->pos];
            }
            lex->pos++;
        }
        lex->current.value[idx] = '\0';
        if (lex->src[lex->pos] == '"') lex->pos++;
        lex->current.type = TOK_STRING;
        return;
    }

    /* Character literal */
    if (c == '\'') {
        lex->pos++;
        if (lex->src[lex->pos] == '\\') {
            lex->pos++;
            switch (lex->src[lex->pos]) {
                case 'n': lex->current.int_val = '\n'; break;
                case 't': lex->current.int_val = '\t'; break;
                case 'r': lex->current.int_val = '\r'; break;
                case '\\': lex->current.int_val = '\\'; break;
                case '\'': lex->current.int_val = '\''; break;
                case '0': lex->current.int_val = '\0'; break;
                default: lex->current.int_val = lex->src[lex->pos]; break;
            }
        } else {
            lex->current.int_val = lex->src[lex->pos];
        }
        lex->pos++;
        if (lex->src[lex->pos] == '\'') lex->pos++;
        lex->current.type = TOK_CHAR_LIT;
        lex->current.value[0] = (char)lex->current.int_val;
        lex->current.value[1] = '\0';
        return;
    }

    /* Identifier or keyword */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        uint32_t idx = 0;
        while ((lex->src[lex->pos] >= 'a' && lex->src[lex->pos] <= 'z') ||
               (lex->src[lex->pos] >= 'A' && lex->src[lex->pos] <= 'Z') ||
               (lex->src[lex->pos] >= '0' && lex->src[lex->pos] <= '9') ||
               lex->src[lex->pos] == '_') {
            if (idx < 255) lex->current.value[idx++] = lex->src[lex->pos];
            lex->pos++;
        }
        lex->current.value[idx] = '\0';
        lex->current.type = is_keyword(lex->current.value) ? TOK_KEYWORD : TOK_IDENT;
        return;
    }

    /* Operators and punctuation */
    lex->current.value[0] = c;
    lex->current.value[1] = '\0';
    lex->pos++;

    switch (c) {
        case '+':
            if (c2 == '+') { lex->current.type = TOK_INC; lex->pos++; }
            else if (c2 == '=') { lex->current.type = TOK_PLUS_ASSIGN; lex->pos++; }
            else lex->current.type = TOK_PLUS;
            return;
        case '-':
            if (c2 == '-') { lex->current.type = TOK_DEC; lex->pos++; }
            else if (c2 == '=') { lex->current.type = TOK_MINUS_ASSIGN; lex->pos++; }
            else if (c2 == '>') { lex->current.type = TOK_ARROW; lex->pos++; }
            else lex->current.type = TOK_MINUS;
            return;
        case '*':
            if (c2 == '=') { lex->current.type = TOK_STAR_ASSIGN; lex->pos++; }
            else lex->current.type = TOK_STAR;
            return;
        case '/':
            if (c2 == '=') { lex->current.type = TOK_SLASH_ASSIGN; lex->pos++; }
            else lex->current.type = TOK_SLASH;
            return;
        case '%':
            if (c2 == '=') { lex->current.type = TOK_PERCENT_ASSIGN; lex->pos++; }
            else lex->current.type = TOK_PERCENT;
            return;
        case '&':
            if (c2 == '&') { lex->current.type = TOK_AND; lex->pos++; }
            else if (c2 == '=') { lex->current.type = TOK_AMP_ASSIGN; lex->pos++; }
            else lex->current.type = TOK_AMP;
            return;
        case '|':
            if (c2 == '|') { lex->current.type = TOK_OR; lex->pos++; }
            else if (c2 == '=') { lex->current.type = TOK_PIPE_ASSIGN; lex->pos++; }
            else lex->current.type = TOK_PIPE;
            return;
        case '^':
            if (c2 == '=') { lex->current.type = TOK_CARET_ASSIGN; lex->pos++; }
            else lex->current.type = TOK_CARET;
            return;
        case '<':
            if (c2 == '<') {
                if (lex->src[lex->pos] == '=') { lex->current.type = TOK_LSHIFT_ASSIGN; lex->pos++; }
                else { lex->current.type = TOK_LSHIFT; }
                lex->pos++;
            } else if (c2 == '=') { lex->current.type = TOK_LE; lex->pos++; }
            else lex->current.type = TOK_LT;
            return;
        case '>':
            if (c2 == '>') {
                if (lex->src[lex->pos] == '=') { lex->current.type = TOK_RSHIFT_ASSIGN; lex->pos++; }
                else { lex->current.type = TOK_RSHIFT; }
                lex->pos++;
            } else if (c2 == '=') { lex->current.type = TOK_GE; lex->pos++; }
            else lex->current.type = TOK_GT;
            return;
        case '=':
            if (c2 == '=') { lex->current.type = TOK_EQ; lex->pos++; }
            else lex->current.type = TOK_ASSIGN;
            return;
        case '!':
            if (c2 == '=') { lex->current.type = TOK_NE; lex->pos++; }
            else lex->current.type = TOK_NOT;
            return;
        case '~': lex->current.type = TOK_TILDE; return;
        case '(': lex->current.type = TOK_LPAREN; return;
        case ')': lex->current.type = TOK_RPAREN; return;
        case '{': lex->current.type = TOK_LBRACE; return;
        case '}': lex->current.type = TOK_RBRACE; return;
        case '[': lex->current.type = TOK_LBRACKET; return;
        case ']': lex->current.type = TOK_RBRACKET; return;
        case ';': lex->current.type = TOK_SEMI; return;
        case ',': lex->current.type = TOK_COMMA; return;
        case '.': lex->current.type = TOK_DOT; return;
        case '?': lex->current.type = TOK_QUESTION; return;
        case ':': lex->current.type = TOK_COLON; return;
        default:
            lex->current.type = TOK_EOF;
            return;
    }
}

static void lex_init(lexer_t *lex, const char *src) {
    lex->src = src;
    lex->pos = 0;
    lex->line = 1;
    lex_next(lex);
}

/* ============================================================
 *  AST
 * ============================================================ */

typedef enum {
    AST_NUMBER, AST_STRING, AST_CHAR_LIT, AST_IDENT,
    AST_BINARY, AST_UNARY, AST_ASSIGN,
    AST_VAR_DECL, AST_ARRAY_DECL,
    AST_IF, AST_WHILE, AST_FOR, AST_DO_WHILE,
    AST_BLOCK, AST_RETURN, AST_BREAK, AST_CONTINUE,
    AST_FUNC_DECL, AST_FUNC_CALL,
    AST_ARRAY_ACCESS, AST_DEREF, AST_ADDR,
    AST_TERNARY, AST_SIZEOF, AST_CAST,
    AST_INC_DEC, AST_SWITCH, AST_CASE, AST_DEFAULT
} ast_type_t;

typedef struct ast_node {
    ast_type_t type;
    char name[64];
    int32_t int_val;
    char str_val[256];
    struct ast_node *left;
    struct ast_node *right;
    struct ast_node *cond;    /* for ternary, for-loop, if, switch */
    struct ast_node *body;    /* for loops, functions, case body */
    struct ast_node *else_body;
    struct ast_node *next;    /* linked list in block */
    token_type_t op;          /* for binary/unary ops */
    int line;                 /* source line number */
} ast_node_t;

static ast_node_t *ast_new(ast_type_t type) {
    ast_node_t *n = (ast_node_t *)kmalloc(sizeof(ast_node_t));
    if (n) memset(n, 0, sizeof(ast_node_t));
    n->type = type;
    return n;
}

static void ast_free(ast_node_t *node) {
    if (!node) return;
    ast_free(node->left);
    ast_free(node->right);
    ast_free(node->cond);
    ast_free(node->body);
    ast_free(node->else_body);
    ast_free(node->next);
    kfree(node);
}

/* ============================================================
 *  Parser
 * ============================================================ */

typedef struct {
    lexer_t lex;
    int error;
    int error_line;
} parser_t;

static ast_node_t *parse_expr(parser_t *p);
static ast_node_t *parse_stmt(parser_t *p);
static ast_node_t *parse_block(parser_t *p);

static int expect(parser_t *p, token_type_t t) {
    if (p->lex.current.type == t) {
        lex_next(&p->lex);
        return 1;
    }
    p->error = 1;
    p->error_line = p->lex.line;
    printf("Error at line %d: expected token type %d, got '%s'\n",
           p->lex.line, (int)t, p->lex.current.value);
    return 0;
}

static int is_type_token(parser_t *p) {
    if (p->lex.current.type != TOK_KEYWORD) return 0;
    const char *v = p->lex.current.value;
    return (strcmp(v, "int") == 0 || strcmp(v, "char") == 0 ||
            strcmp(v, "float") == 0 || strcmp(v, "double") == 0 ||
            strcmp(v, "void") == 0 || strcmp(v, "long") == 0 ||
            strcmp(v, "short") == 0 || strcmp(v, "unsigned") == 0 ||
            strcmp(v, "signed") == 0 || strcmp(v, "uint32_t") == 0 ||
            strcmp(v, "int32_t") == 0 || strcmp(v, "uint8_t") == 0 ||
            strcmp(v, "int8_t") == 0 || strcmp(v, "uint16_t") == 0 ||
            strcmp(v, "int16_t") == 0 || strcmp(v, "uint64_t") == 0 ||
            strcmp(v, "int64_t") == 0 || strcmp(v, "size_t") == 0);
}

/* Forward declarations for expression parsing */
static ast_node_t *parse_assign_expr(parser_t *p);
static ast_node_t *parse_ternary(parser_t *p);
static ast_node_t *parse_or(parser_t *p);
static ast_node_t *parse_and(parser_t *p);
static ast_node_t *parse_bitor(parser_t *p);
static ast_node_t *parse_bitxor(parser_t *p);
static ast_node_t *parse_bitand(parser_t *p);
static ast_node_t *parse_equality(parser_t *p);
static ast_node_t *parse_relational(parser_t *p);
static ast_node_t *parse_shift(parser_t *p);
static ast_node_t *parse_additive(parser_t *p);
static ast_node_t *parse_multiplicative(parser_t *p);
static ast_node_t *parse_unary(parser_t *p);
static ast_node_t *parse_postfix(parser_t *p);
static ast_node_t *parse_primary(parser_t *p);

static ast_node_t *parse_primary(parser_t *p) {
    if (p->lex.current.type == TOK_NUMBER) {
        ast_node_t *n = ast_new(AST_NUMBER);
        n->int_val = p->lex.current.int_val;
        lex_next(&p->lex);
        return n;
    }

    if (p->lex.current.type == TOK_STRING) {
        ast_node_t *n = ast_new(AST_STRING);
        strncpy(n->str_val, p->lex.current.value, 255);
        n->str_val[255] = '\0';
        lex_next(&p->lex);
        return n;
    }

    if (p->lex.current.type == TOK_CHAR_LIT) {
        ast_node_t *n = ast_new(AST_CHAR_LIT);
        n->int_val = p->lex.current.int_val;
        lex_next(&p->lex);
        return n;
    }

    if (p->lex.current.type == TOK_IDENT) {
        ast_node_t *n = ast_new(AST_IDENT);
        strncpy(n->name, p->lex.current.value, 63);
        n->name[63] = '\0';
        lex_next(&p->lex);
        return n;
    }

    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "NULL") == 0) {
        ast_node_t *n = ast_new(AST_NUMBER);
        n->int_val = 0;
        lex_next(&p->lex);
        return n;
    }

    if (p->lex.current.type == TOK_LPAREN) {
        lex_next(&p->lex);
        ast_node_t *n = parse_expr(p);
        expect(p, TOK_RPAREN);
        return n;
    }

    return 0;
}

static ast_node_t *parse_postfix(parser_t *p) {
    ast_node_t *n = parse_primary(p);
    if (!n) return 0;

    while (1) {
        if (p->lex.current.type == TOK_LPAREN) {
            /* Function call */
            ast_node_t *call = ast_new(AST_FUNC_CALL);
            call->left = n;
            call->body = 0;
            lex_next(&p->lex);

            /* Parse arguments */
            ast_node_t **arg_ptr = &call->body;
            while (p->lex.current.type != TOK_RPAREN && p->lex.current.type != TOK_EOF) {
                ast_node_t *arg = parse_assign_expr(p);
                *arg_ptr = arg;
                arg_ptr = &arg->next;
                if (p->lex.current.type == TOK_COMMA) lex_next(&p->lex);
                else break;
            }
            expect(p, TOK_RPAREN);
            n = call;
        } else if (p->lex.current.type == TOK_LBRACKET) {
            /* Array access */
            ast_node_t *arr = ast_new(AST_ARRAY_ACCESS);
            arr->left = n;
            lex_next(&p->lex);
            arr->right = parse_expr(p);
            expect(p, TOK_RBRACKET);
            n = arr;
        } else if (p->lex.current.type == TOK_INC || p->lex.current.type == TOK_DEC) {
            ast_node_t *inc = ast_new(AST_INC_DEC);
            inc->left = n;
            inc->op = p->lex.current.type;
            lex_next(&p->lex);
            n = inc;
        } else {
            break;
        }
    }
    return n;
}

static ast_node_t *parse_unary(parser_t *p) {
    if (p->lex.current.type == TOK_MINUS) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_UNARY);
        n->op = TOK_MINUS;
        n->left = parse_unary(p);
        return n;
    }
    if (p->lex.current.type == TOK_NOT) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_UNARY);
        n->op = TOK_NOT;
        n->left = parse_unary(p);
        return n;
    }
    if (p->lex.current.type == TOK_TILDE) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_UNARY);
        n->op = TOK_TILDE;
        n->left = parse_unary(p);
        return n;
    }
    if (p->lex.current.type == TOK_STAR) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_DEREF);
        n->left = parse_unary(p);
        return n;
    }
    if (p->lex.current.type == TOK_AMP) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_ADDR);
        n->left = parse_unary(p);
        return n;
    }
    if (p->lex.current.type == TOK_INC) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_INC_DEC);
        n->op = TOK_INC;
        n->left = parse_unary(p);
        return n;
    }
    if (p->lex.current.type == TOK_DEC) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_INC_DEC);
        n->op = TOK_DEC;
        n->left = parse_unary(p);
        return n;
    }
    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "sizeof") == 0) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_SIZEOF);
        if (p->lex.current.type == TOK_LPAREN) {
            lex_next(&p->lex);
            n->left = parse_expr(p);
            expect(p, TOK_RPAREN);
        } else {
            n->left = parse_unary(p);
        }
        return n;
    }
    /* Cast: (type)expr */
    if (p->lex.current.type == TOK_LPAREN) {
        /* Peek ahead to see if this is a cast */
        uint32_t save_pos = p->lex.pos;
        token_t save_tok = p->lex.current;
        lex_next(&p->lex);
        if (is_type_token(p)) {
            /* It's a cast */
            ast_node_t *cast = ast_new(AST_CAST);
            lex_next(&p->lex);
            expect(p, TOK_RPAREN);
            cast->left = parse_unary(p);
            return cast;
        }
        /* Not a cast, restore */
        p->lex.pos = save_pos;
        p->lex.current = save_tok;
    }

    return parse_postfix(p);
}

static ast_node_t *parse_multiplicative(parser_t *p) {
    ast_node_t *left = parse_unary(p);
    while (p->lex.current.type == TOK_STAR || p->lex.current.type == TOK_SLASH ||
           p->lex.current.type == TOK_PERCENT) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = p->lex.current.type;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_unary(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_additive(parser_t *p) {
    ast_node_t *left = parse_multiplicative(p);
    while (p->lex.current.type == TOK_PLUS || p->lex.current.type == TOK_MINUS) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = p->lex.current.type;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_multiplicative(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_shift(parser_t *p) {
    ast_node_t *left = parse_additive(p);
    while (p->lex.current.type == TOK_LSHIFT || p->lex.current.type == TOK_RSHIFT) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = p->lex.current.type;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_additive(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_relational(parser_t *p) {
    ast_node_t *left = parse_shift(p);
    while (p->lex.current.type == TOK_LT || p->lex.current.type == TOK_GT ||
           p->lex.current.type == TOK_LE || p->lex.current.type == TOK_GE) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = p->lex.current.type;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_shift(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_equality(parser_t *p) {
    ast_node_t *left = parse_relational(p);
    while (p->lex.current.type == TOK_EQ || p->lex.current.type == TOK_NE) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = p->lex.current.type;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_relational(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_bitand(parser_t *p) {
    ast_node_t *left = parse_equality(p);
    while (p->lex.current.type == TOK_AMP) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = TOK_AMP;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_equality(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_bitxor(parser_t *p) {
    ast_node_t *left = parse_bitand(p);
    while (p->lex.current.type == TOK_CARET) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = TOK_CARET;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_bitand(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_bitor(parser_t *p) {
    ast_node_t *left = parse_bitxor(p);
    while (p->lex.current.type == TOK_PIPE) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = TOK_PIPE;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_bitxor(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_and(parser_t *p) {
    ast_node_t *left = parse_bitor(p);
    while (p->lex.current.type == TOK_AND) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = TOK_AND;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_bitor(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_or(parser_t *p) {
    ast_node_t *left = parse_and(p);
    while (p->lex.current.type == TOK_OR) {
        ast_node_t *n = ast_new(AST_BINARY);
        n->op = TOK_OR;
        n->left = left;
        lex_next(&p->lex);
        n->right = parse_and(p);
        left = n;
    }
    return left;
}

static ast_node_t *parse_ternary(parser_t *p) {
    ast_node_t *cond = parse_or(p);
    if (p->lex.current.type == TOK_QUESTION) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_TERNARY);
        n->cond = cond;
        n->left = parse_expr(p);
        expect(p, TOK_COLON);
        n->right = parse_ternary(p);
        return n;
    }
    return cond;
}

static ast_node_t *parse_assign_expr(parser_t *p) {
    ast_node_t *left = parse_ternary(p);

    token_type_t assign_types[] = {
        TOK_ASSIGN, TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN,
        TOK_STAR_ASSIGN, TOK_SLASH_ASSIGN, TOK_PERCENT_ASSIGN,
        TOK_AMP_ASSIGN, TOK_PIPE_ASSIGN, TOK_CARET_ASSIGN,
        TOK_LSHIFT_ASSIGN, TOK_RSHIFT_ASSIGN
    };

    for (int i = 0; i < 11; i++) {
        if (p->lex.current.type == assign_types[i]) {
            ast_node_t *n = ast_new(AST_ASSIGN);
            n->op = assign_types[i];
            n->left = left;
            lex_next(&p->lex);
            n->right = parse_assign_expr(p);
            return n;
        }
    }
    return left;
}

static ast_node_t *parse_expr(parser_t *p) {
    return parse_assign_expr(p);
}

static ast_node_t *parse_block(parser_t *p) {
    ast_node_t *block = ast_new(AST_BLOCK);
    ast_node_t **tail = &block->body;

    while (p->lex.current.type != TOK_RBRACE && p->lex.current.type != TOK_EOF) {
        ast_node_t *stmt = parse_stmt(p);
        if (stmt) {
            *tail = stmt;
            tail = &stmt->next;
        } else {
            break;
        }
    }
    return block;
}

static ast_node_t *parse_stmt(parser_t *p) {
    /* Variable declaration */
    if (is_type_token(p)) {
        ast_node_t *decl = ast_new(AST_VAR_DECL);
        /* Skip type keywords (handle unsigned, etc.) */
        while (is_type_token(p)) {
            lex_next(&p->lex);
        }
        /* Variable name */
        if (p->lex.current.type == TOK_IDENT) {
            strncpy(decl->name, p->lex.current.value, 63);
            decl->name[63] = '\0';
            lex_next(&p->lex);
        }
        /* Array declaration */
        if (p->lex.current.type == TOK_LBRACKET) {
            lex_next(&p->lex);
            decl->type = AST_ARRAY_DECL;
            if (p->lex.current.type == TOK_NUMBER) {
                decl->int_val = p->lex.current.int_val;
                lex_next(&p->lex);
            }
            expect(p, TOK_RBRACKET);
        }
        /* Optional initializer */
        if (p->lex.current.type == TOK_ASSIGN) {
            lex_next(&p->lex);
            decl->left = parse_expr(p);
        }
        expect(p, TOK_SEMI);
        return decl;
    }

    /* If statement */
    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "if") == 0) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_IF);
        expect(p, TOK_LPAREN);
        n->cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        n->body = parse_stmt(p);
        if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "else") == 0) {
            lex_next(&p->lex);
            n->else_body = parse_stmt(p);
        }
        return n;
    }

    /* While statement */
    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "while") == 0) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_WHILE);
        expect(p, TOK_LPAREN);
        n->cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        n->body = parse_stmt(p);
        return n;
    }

    /* Do-while */
    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "do") == 0) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_DO_WHILE);
        n->body = parse_stmt(p);
        if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "while") == 0) {
            lex_next(&p->lex);
            expect(p, TOK_LPAREN);
            n->cond = parse_expr(p);
            expect(p, TOK_RPAREN);
        }
        expect(p, TOK_SEMI);
        return n;
    }

    /* For statement */
    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "for") == 0) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_FOR);
        expect(p, TOK_LPAREN);
        /* Init: could be a variable declaration or an expression */
        if (is_type_token(p)) {
            /* Parse as variable declaration */
            ast_node_t *decl = ast_new(AST_VAR_DECL);
            while (is_type_token(p)) {
                lex_next(&p->lex);
            }
            if (p->lex.current.type == TOK_IDENT) {
                strncpy(decl->name, p->lex.current.value, 63);
                decl->name[63] = '\0';
                lex_next(&p->lex);
            }
            if (p->lex.current.type == TOK_ASSIGN) {
                lex_next(&p->lex);
                decl->left = parse_expr(p);
            }
            n->left = decl;
        } else {
            n->left = parse_expr(p);   /* init */
        }
        expect(p, TOK_SEMI);
        n->cond = parse_expr(p);   /* condition */
        expect(p, TOK_SEMI);
        n->right = parse_expr(p);  /* update */
        expect(p, TOK_RPAREN);
        n->body = parse_stmt(p);
        return n;
    }

    /* Return */
    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "return") == 0) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_RETURN);
        if (p->lex.current.type != TOK_SEMI) {
            n->left = parse_expr(p);
        }
        expect(p, TOK_SEMI);
        return n;
    }

    /* Break */
    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "break") == 0) {
        lex_next(&p->lex);
        expect(p, TOK_SEMI);
        return ast_new(AST_BREAK);
    }

    /* Continue */
    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "continue") == 0) {
        lex_next(&p->lex);
        expect(p, TOK_SEMI);
        return ast_new(AST_CONTINUE);
    }

    /* Block */
    if (p->lex.current.type == TOK_LBRACE) {
        lex_next(&p->lex);
        ast_node_t *n = parse_block(p);
        expect(p, TOK_RBRACE);
        return n;
    }

    /* Function definition */
    if (p->lex.current.type == TOK_IDENT) {
        /* Check if this is a function definition: name(...) { ... } */
        uint32_t save_pos = p->lex.pos;
        token_t save_tok = p->lex.current;
        char name[64];
        strncpy(name, p->lex.current.value, 63);
        name[63] = '\0';
        lex_next(&p->lex);

        if (p->lex.current.type == TOK_LPAREN) {
            lex_next(&p->lex);
            /* Parse parameters as "type name" pairs */
            char params[8][64];
            int n_params = 0;
            while (p->lex.current.type != TOK_RPAREN && p->lex.current.type != TOK_EOF) {
                /* Skip type keywords */
                while (is_type_token(p)) {
                    lex_next(&p->lex);
                }
                /* Parameter name */
                if (p->lex.current.type == TOK_IDENT && n_params < 8) {
                    strncpy(params[n_params], p->lex.current.value, 63);
                    params[n_params][63] = '\0';
                    n_params++;
                    lex_next(&p->lex);
                }
                if (p->lex.current.type == TOK_COMMA) {
                    lex_next(&p->lex);
                } else {
                    break;
                }
            }
            if (p->lex.current.type == TOK_RPAREN) lex_next(&p->lex);

            if (p->lex.current.type == TOK_LBRACE) {
                /* It's a function definition */
                ast_node_t *n = ast_new(AST_FUNC_DECL);
                strncpy(n->name, name, 63);
                n->name[63] = '\0';
                n->int_val = n_params;
                /* Store params in a linked list via left pointer */
                ast_node_t **ptail = &n->left;
                for (int i = 0; i < n_params; i++) {
                    ast_node_t *pn = ast_new(AST_IDENT);
                    strncpy(pn->name, params[i], 63);
                    pn->name[63] = '\0';
                    *ptail = pn;
                    ptail = &pn->next;
                }
                lex_next(&p->lex);
                n->body = parse_block(p);
                expect(p, TOK_RBRACE);
                return n;
            }
        }

        /* Not a function definition, restore and parse as expression */
        p->lex.pos = save_pos;
        p->lex.current = save_tok;
    }

    /* Switch statement */
    if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "switch") == 0) {
        lex_next(&p->lex);
        ast_node_t *n = ast_new(AST_SWITCH);
        expect(p, TOK_LPAREN);
        n->cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        expect(p, TOK_LBRACE);
        /* Parse case/default clauses as linked list via body */
        ast_node_t **ctail = &n->body;
        while (p->lex.current.type != TOK_RBRACE && p->lex.current.type != TOK_EOF) {
            if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "case") == 0) {
                lex_next(&p->lex);
                ast_node_t *c = ast_new(AST_CASE);
                c->left = parse_expr(p);
                expect(p, TOK_COLON);
                /* Parse statements until next case/default/} */
                ast_node_t **stail = &c->body;
                while (p->lex.current.type != TOK_RBRACE &&
                       !(p->lex.current.type == TOK_KEYWORD &&
                         (strcmp(p->lex.current.value, "case") == 0 ||
                          strcmp(p->lex.current.value, "default") == 0)) &&
                       p->lex.current.type != TOK_EOF) {
                    ast_node_t *s = parse_stmt(p);
                    if (s) { *stail = s; stail = &s->next; }
                    else break;
                }
                *ctail = c;
                ctail = &c->next;
            } else if (p->lex.current.type == TOK_KEYWORD && strcmp(p->lex.current.value, "default") == 0) {
                lex_next(&p->lex);
                ast_node_t *c = ast_new(AST_DEFAULT);
                expect(p, TOK_COLON);
                /* Parse statements until next case/default/} */
                ast_node_t **stail = &c->body;
                while (p->lex.current.type != TOK_RBRACE &&
                       !(p->lex.current.type == TOK_KEYWORD &&
                         (strcmp(p->lex.current.value, "case") == 0 ||
                          strcmp(p->lex.current.value, "default") == 0)) &&
                       p->lex.current.type != TOK_EOF) {
                    ast_node_t *s = parse_stmt(p);
                    if (s) { *stail = s; stail = &s->next; }
                    else break;
                }
                *ctail = c;
                ctail = &c->next;
            } else {
                /* Skip unexpected tokens */
                lex_next(&p->lex);
            }
        }
        expect(p, TOK_RBRACE);
        return n;
    }

    /* Expression statement */
    ast_node_t *n = parse_expr(p);
    expect(p, TOK_SEMI);
    return n;
}

/* ============================================================
 *  Interpreter
 * ============================================================ */

#define CI_MAX_VARS    128
#define CI_MAX_FUNCS   64
#define CI_MAX_STRINGS 64

typedef enum {
    VAL_INT, VAL_PTR, VAL_STR
} val_type_t;

typedef struct {
    char name[64];
    val_type_t type;
    int32_t int_val;
    void *ptr_val;
    char str_val[256];
    int scope_depth;
} ci_var_t;

typedef struct {
    char name[64];
    ast_node_t *body;
    uint32_t n_params;
    char params[8][64];
} ci_func_t;

typedef struct {
    ci_var_t vars[CI_MAX_VARS];
    uint32_t var_count;
    ci_func_t funcs[CI_MAX_FUNCS];
    uint32_t func_count;
    int32_t return_val;
    int returning;
    int breaking;
    int continuing;
    /* String storage */
    char *strings[CI_MAX_STRINGS];
    uint32_t string_count;
    /* Scope stack */
    int scope_depth;
    /* Error reporting */
    int error_line;
} interp_t;

static interp_t ci_interp;

void cinterp_init(void) {
    memset(&ci_interp, 0, sizeof(interp_t));
}

static ci_var_t *find_var(const char *name) {
    /* Search from end to find most recent (innermost scope) first */
    for (int32_t i = (int32_t)ci_interp.var_count - 1; i >= 0; i--) {
        if (strcmp(ci_interp.vars[i].name, name) == 0) {
            return &ci_interp.vars[i];
        }
    }
    return 0;
}

static ci_var_t *create_var(const char *name, val_type_t type) {
    if (ci_interp.var_count >= CI_MAX_VARS) return 0;
    /* Allow shadowing: always create new entry at current scope depth */
    ci_var_t *v = &ci_interp.vars[ci_interp.var_count++];
    strncpy(v->name, name, 63);
    v->name[63] = '\0';
    v->type = type;
    v->int_val = 0;
    v->ptr_val = 0;
    v->str_val[0] = '\0';
    v->scope_depth = ci_interp.scope_depth;
    return v;
}

static ci_func_t *find_func(const char *name) {
    for (uint32_t i = 0; i < ci_interp.func_count; i++) {
        if (strcmp(ci_interp.funcs[i].name, name) == 0) {
            return &ci_interp.funcs[i];
        }
    }
    return 0;
}

static char *store_string(const char *s) {
    if (ci_interp.string_count >= CI_MAX_STRINGS) return "";
    uint32_t len = 0;
    while (s[len]) len++;
    char *copy = (char *)kmalloc(len + 1);
    if (!copy) return "";
    memcpy(copy, s, len + 1);
    ci_interp.strings[ci_interp.string_count++] = copy;
    return copy;
}

static void scope_push(void) {
    ci_interp.scope_depth++;
}

static void scope_pop(void) {
    if (ci_interp.scope_depth <= 0) return;
    ci_interp.scope_depth--;
    /* Remove all variables at the old scope depth */
    while (ci_interp.var_count > 0 &&
           ci_interp.vars[ci_interp.var_count - 1].scope_depth > ci_interp.scope_depth) {
        ci_interp.var_count--;
    }
}

/* Forward declaration */
static int32_t eval_expr(ast_node_t *node);
static void exec_stmt(ast_node_t *node);

/* Built-in printf implementation */
static void ci_printf(ast_node_t *args) {
    if (!args) return;

    /* First arg must be format string */
    if (args->type != AST_STRING) return;

    const char *fmt = args->str_val;
    ast_node_t *arg = args->next;
    char buf[512];
    uint32_t bi = 0;

    while (*fmt && bi < sizeof(buf) - 1) {
        if (*fmt == '%') {
            fmt++;
            int width = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            switch (*fmt) {
                case 'd':
                case 'i':
                    if (arg) {
                        int32_t val = eval_expr(arg);
                        char num[32];
                        itoa(val, num, 10);
                        uint32_t nl = 0;
                        while (num[nl]) nl++;
                        if ((uint32_t)width > nl && bi + width < sizeof(buf) - 1) {
                            for (uint32_t pad = 0; pad < (uint32_t)width - nl; pad++) {
                                buf[bi++] = ' ';
                            }
                        }
                        for (uint32_t k = 0; k < nl && bi < sizeof(buf) - 1; k++) {
                            buf[bi++] = num[k];
                        }
                        arg = arg->next;
                    }
                    break;
                case 'u':
                    if (arg) {
                        int32_t val = eval_expr(arg);
                        char num[32];
                        utoa((uint32_t)val, num, 10);
                        uint32_t nl = 0;
                        while (num[nl]) nl++;
                        for (uint32_t k = 0; k < nl && bi < sizeof(buf) - 1; k++) {
                            buf[bi++] = num[k];
                        }
                        arg = arg->next;
                    }
                    break;
                case 'x':
                    if (arg) {
                        int32_t val = eval_expr(arg);
                        char num[32];
                        utoa((uint32_t)val, num, 16);
                        uint32_t nl = 0;
                        while (num[nl]) nl++;
                        for (uint32_t k = 0; k < nl && bi < sizeof(buf) - 1; k++) {
                            buf[bi++] = num[k];
                        }
                        arg = arg->next;
                    }
                    break;
                case 's':
                    if (arg) {
                        if (arg->type == AST_STRING) {
                            const char *s = arg->str_val;
                            while (*s && bi < sizeof(buf) - 1) buf[bi++] = *s++;
                        } else {
                            ci_var_t *v = 0;
                            if (arg->type == AST_IDENT) v = find_var(arg->name);
                            if (v && v->type == VAL_STR) {
                                const char *s = v->str_val;
                                while (*s && bi < sizeof(buf) - 1) buf[bi++] = *s++;
                            } else {
                                const char *s = "(null)";
                                while (*s && bi < sizeof(buf) - 1) buf[bi++] = *s++;
                            }
                        }
                        arg = arg->next;
                    }
                    break;
                case 'c':
                    if (arg) {
                        int32_t val = eval_expr(arg);
                        buf[bi++] = (char)val;
                        arg = arg->next;
                    }
                    break;
                case '%':
                    buf[bi++] = '%';
                    break;
                default:
                    buf[bi++] = '%';
                    if (*fmt) buf[bi++] = *fmt;
                    break;
            }
            if (*fmt) fmt++;
        } else {
            buf[bi++] = *fmt++;
        }
    }
    buf[bi] = '\0';
    printf("%s", buf);
}

static int32_t eval_expr(ast_node_t *node) {
    if (!node) return 0;

    switch (node->type) {
        case AST_NUMBER:
        case AST_CHAR_LIT:
            return node->int_val;

        case AST_STRING:
            return (int32_t)store_string(node->str_val);

        case AST_IDENT: {
            ci_var_t *v = find_var(node->name);
            if (v) {
                if (v->type == VAL_STR) return (int32_t)v->str_val;
                return v->int_val;
            }
            return 0;
        }

        case AST_BINARY: {
            int32_t l = eval_expr(node->left);
            /* Short-circuit for && and || */
            if (node->op == TOK_AND) return l ? (eval_expr(node->right) ? 1 : 0) : 0;
            if (node->op == TOK_OR) return l ? 1 : (eval_expr(node->right) ? 1 : 0);

            int32_t r = eval_expr(node->right);
            switch (node->op) {
                case TOK_PLUS:    return l + r;
                case TOK_MINUS:   return l - r;
                case TOK_STAR:    return l * r;
                case TOK_SLASH:   return (r != 0) ? l / r : 0;
                case TOK_PERCENT: return (r != 0) ? l % r : 0;
                case TOK_AMP:     return l & r;
                case TOK_PIPE:    return l | r;
                case TOK_CARET:   return l ^ r;
                case TOK_LSHIFT:  return l << r;
                case TOK_RSHIFT:  return (int32_t)((uint32_t)l >> r);
                case TOK_LT:      return l < r;
                case TOK_GT:      return l > r;
                case TOK_LE:      return l <= r;
                case TOK_GE:      return l >= r;
                case TOK_EQ:      return l == r;
                case TOK_NE:      return l != r;
                default:          return 0;
            }
        }

        case AST_UNARY: {
            int32_t v = eval_expr(node->left);
            switch (node->op) {
                case TOK_MINUS: return -v;
                case TOK_NOT:   return !v;
                case TOK_TILDE: return ~v;
                default:        return v;
            }
        }

        case AST_ASSIGN: {
            int32_t val = eval_expr(node->right);
            if (node->left->type == AST_IDENT) {
                ci_var_t *v = find_var(node->left->name);
                if (!v) v = create_var(node->left->name, VAL_INT);
                if (v) {
                    switch (node->op) {
                        case TOK_ASSIGN:         v->int_val = val; break;
                        case TOK_PLUS_ASSIGN:    v->int_val += val; break;
                        case TOK_MINUS_ASSIGN:   v->int_val -= val; break;
                        case TOK_STAR_ASSIGN:    v->int_val *= val; break;
                        case TOK_SLASH_ASSIGN:   v->int_val = (val != 0) ? v->int_val / val : 0; break;
                        case TOK_PERCENT_ASSIGN: v->int_val = (val != 0) ? v->int_val % val : 0; break;
                        case TOK_AMP_ASSIGN:     v->int_val &= val; break;
                        case TOK_PIPE_ASSIGN:    v->int_val |= val; break;
                        case TOK_CARET_ASSIGN:   v->int_val ^= val; break;
                        case TOK_LSHIFT_ASSIGN:  v->int_val <<= val; break;
                        case TOK_RSHIFT_ASSIGN:  v->int_val = (int32_t)((uint32_t)v->int_val >> val); break;
                        default: break;
                    }
                    return v->int_val;
                }
            } else if (node->left->type == AST_ARRAY_ACCESS) {
                /* Array element assignment */
                int32_t idx = eval_expr(node->left->right);
                if (node->left->left->type == AST_IDENT) {
                    ci_var_t *v = find_var(node->left->left->name);
                    if (v && v->ptr_val) {
                        int32_t *arr = (int32_t *)v->ptr_val;
                        arr[idx] = val;
                        return val;
                    }
                }
            }
            return val;
        }

        case AST_TERNARY: {
            int32_t c = eval_expr(node->cond);
            return c ? eval_expr(node->left) : eval_expr(node->right);
        }

        case AST_FUNC_CALL: {
            const char *fname = 0;
            if (node->left->type == AST_IDENT) {
                fname = node->left->name;
            }
            if (!fname) return 0;

            /* Built-in functions */
            if (strcmp(fname, "printf") == 0) {
                ci_printf(node->body);
                return 0;
            }
            if (strcmp(fname, "malloc") == 0) {
                int32_t size = node->body ? eval_expr(node->body) : 0;
                void *ptr = kmalloc((uint32_t)size);
                return (int32_t)ptr;
            }
            if (strcmp(fname, "free") == 0) {
                int32_t ptr = node->body ? eval_expr(node->body) : 0;
                if (ptr) kfree((void *)ptr);
                return 0;
            }
            if (strcmp(fname, "strlen") == 0) {
                if (node->body) {
                    if (node->body->type == AST_STRING) {
                        return (int32_t)strlen(node->body->str_val);
                    }
                    int32_t v = eval_expr(node->body);
                    if (v) return (int32_t)strlen((const char *)v);
                }
                return 0;
            }
            if (strcmp(fname, "atoi") == 0) {
                if (node->body) {
                    int32_t v = eval_expr(node->body);
                    if (v) return atoi((const char *)v);
                }
                return 0;
            }
            if (strcmp(fname, "abs") == 0) {
                if (node->body) {
                    int32_t v = eval_expr(node->body);
                    return abs(v);
                }
                return 0;
            }

            /* User-defined function */
            ci_func_t *func = find_func(fname);
            if (func && func->body) {
                /* Evaluate all arguments first (in caller's scope) */
                int32_t arg_vals[8];
                ast_node_t *arg = node->body;
                uint32_t n_args = 0;
                while (arg && n_args < func->n_params) {
                    arg_vals[n_args] = eval_expr(arg);
                    n_args++;
                    arg = arg->next;
                }

                /* Save current scope depth and variables count */
                int saved_scope_depth = ci_interp.scope_depth;
                uint32_t saved_var_count = ci_interp.var_count;

                /* Create a new scope for the function */
                ci_interp.scope_depth = 0;
                ci_interp.var_count = 0;

                /* Set up parameters as local variables */
                for (uint32_t i = 0; i < n_args; i++) {
                    ci_var_t *pv = create_var(func->params[i], VAL_INT);
                    if (pv) pv->int_val = arg_vals[i];
                }

                /* Execute function body */
                ci_interp.returning = 0;
                ci_interp.return_val = 0;
                exec_stmt(func->body);
                int32_t result = ci_interp.return_val;
                ci_interp.returning = 0;

                /* Restore scope */
                ci_interp.scope_depth = saved_scope_depth;
                ci_interp.var_count = saved_var_count;

                return result;
            }

            return 0;
        }

        case AST_ARRAY_ACCESS: {
            int32_t idx = eval_expr(node->right);
            if (node->left->type == AST_IDENT) {
                ci_var_t *v = find_var(node->left->name);
                if (v && v->ptr_val) {
                    int32_t *arr = (int32_t *)v->ptr_val;
                    return arr[idx];
                }
            }
            return 0;
        }

        case AST_SIZEOF:
            return 4; /* All types are 4 bytes in this interpreter */

        case AST_CAST:
            return eval_expr(node->left);

        case AST_INC_DEC: {
            if (node->left->type == AST_IDENT) {
                ci_var_t *v = find_var(node->left->name);
                if (!v) v = create_var(node->left->name, VAL_INT);
                if (v) {
                    int32_t old = v->int_val;
                    if (node->op == TOK_INC) v->int_val++;
                    else v->int_val--;
                    return old; /* Post-increment/decrement returns old value */
                }
            }
            return 0;
        }

        case AST_DEREF: {
            int32_t v = eval_expr(node->left);
            if (v) return *(int32_t *)v;
            return 0;
        }

        case AST_ADDR: {
            if (node->left->type == AST_IDENT) {
                ci_var_t *v = find_var(node->left->name);
                if (v) return (int32_t)&v->int_val;
            }
            return 0;
        }

        default:
            return 0;
    }
}

static void exec_stmt(ast_node_t *node) {
    if (!node || ci_interp.returning || ci_interp.breaking || ci_interp.continuing) return;

    switch (node->type) {
        case AST_VAR_DECL: {
            ci_var_t *v = create_var(node->name, VAL_INT);
            if (v && node->left) {
                v->int_val = eval_expr(node->left);
            }
            break;
        }

        case AST_ARRAY_DECL: {
            ci_var_t *v = create_var(node->name, VAL_PTR);
            if (v) {
                uint32_t size = (node->int_val > 0) ? (uint32_t)node->int_val : 1;
                v->ptr_val = kmalloc(size * sizeof(int32_t));
                if (v->ptr_val) {
                    memset(v->ptr_val, 0, size * sizeof(int32_t));
                }
            }
            break;
        }

        case AST_IF:
            scope_push();
            if (eval_expr(node->cond)) {
                exec_stmt(node->body);
            } else if (node->else_body) {
                exec_stmt(node->else_body);
            }
            scope_pop();
            break;

        case AST_WHILE:
            scope_push();
            while (eval_expr(node->cond) && !ci_interp.returning) {
                ci_interp.breaking = 0;
                ci_interp.continuing = 0;
                exec_stmt(node->body);
                if (ci_interp.breaking) {
                    ci_interp.breaking = 0;
                    break;
                }
                if (ci_interp.continuing) {
                    ci_interp.continuing = 0;
                    continue;
                }
            }
            scope_pop();
            break;

        case AST_DO_WHILE:
            scope_push();
            do {
                ci_interp.breaking = 0;
                ci_interp.continuing = 0;
                exec_stmt(node->body);
                if (ci_interp.breaking) {
                    ci_interp.breaking = 0;
                    break;
                }
                if (ci_interp.continuing) {
                    ci_interp.continuing = 0;
                }
            } while (eval_expr(node->cond) && !ci_interp.returning);
            scope_pop();
            break;

        case AST_FOR: {
            scope_push();
            /* Init: could be a var decl or expression */
            if (node->left) {
                if (node->left->type == AST_VAR_DECL) {
                    exec_stmt(node->left);
                } else {
                    eval_expr(node->left);
                }
            }
            while (1) {
                if (node->cond && !eval_expr(node->cond)) break;
                ci_interp.breaking = 0;
                ci_interp.continuing = 0;
                exec_stmt(node->body);
                if (ci_interp.breaking) {
                    ci_interp.breaking = 0;
                    break;
                }
                if (ci_interp.continuing) {
                    ci_interp.continuing = 0;
                }
                if (node->right) eval_expr(node->right);
                if (ci_interp.returning) break;
            }
            scope_pop();
            break;
        }

        case AST_BLOCK: {
            scope_push();
            ast_node_t *stmt = node->body;
            while (stmt && !ci_interp.returning && !ci_interp.breaking && !ci_interp.continuing) {
                exec_stmt(stmt);
                stmt = stmt->next;
            }
            scope_pop();
            break;
        }

        case AST_RETURN:
            if (node->left) {
                ci_interp.return_val = eval_expr(node->left);
            }
            ci_interp.returning = 1;
            break;

        case AST_BREAK:
            ci_interp.breaking = 1;
            break;

        case AST_CONTINUE:
            ci_interp.continuing = 1;
            break;

        case AST_FUNC_DECL: {
            if (ci_interp.func_count < CI_MAX_FUNCS) {
                ci_func_t *f = &ci_interp.funcs[ci_interp.func_count++];
                strncpy(f->name, node->name, 63);
                f->name[63] = '\0';
                f->body = node->body;
                f->n_params = (uint32_t)node->int_val;
                /* Extract parameter names from linked list in left */
                ast_node_t *pn = node->left;
                for (uint32_t i = 0; i < f->n_params && pn; i++) {
                    strncpy(f->params[i], pn->name, 63);
                    f->params[i][63] = '\0';
                    pn = pn->next;
                }
            }
            break;
        }

        case AST_SWITCH: {
            int32_t switch_val = eval_expr(node->cond);
            ast_node_t *clause = node->body;
            int matched = 0;
            int fell_through = 0;
            ast_node_t *default_clause = 0;

            /* First pass: find matching case */
            while (clause && !ci_interp.returning) {
                if (clause->type == AST_CASE) {
                    int32_t case_val = eval_expr(clause->left);
                    if (case_val == switch_val) {
                        matched = 1;
                        fell_through = 1;
                    }
                } else if (clause->type == AST_DEFAULT) {
                    default_clause = clause;
                }
                if (fell_through) break;
                clause = clause->next;
            }

            /* If no case matched, try default */
            if (!matched && default_clause) {
                clause = default_clause;
                fell_through = 1;
            }

            /* Execute from matched clause onwards (fall-through) */
            while (clause && fell_through && !ci_interp.returning) {
                /* Execute statements in this clause */
                ast_node_t *stmt = clause->body;
                while (stmt && !ci_interp.returning && !ci_interp.breaking && !ci_interp.continuing) {
                    exec_stmt(stmt);
                    stmt = stmt->next;
                }
                if (ci_interp.breaking) {
                    ci_interp.breaking = 0;
                    break;
                }
                clause = clause->next;
            }
            break;
        }

        default:
            /* Expression statement */
            eval_expr(node);
            break;
    }
}

int cinterp_exec(const char *code) {
    if (!code || !*code) return 0;

    parser_t parser;
    lex_init(&parser.lex, code);
    parser.error = 0;
    parser.error_line = 0;

    while (parser.lex.current.type != TOK_EOF && !parser.error) {
        ast_node_t *stmt = parse_stmt(&parser);
        if (stmt) {
            exec_stmt(stmt);
            ast_free(stmt);
        } else {
            break;
        }
    }

    if (parser.error && parser.error_line > 0) {
        ci_interp.error_line = parser.error_line;
    }

    return parser.error ? -1 : 0;
}

/* Read a line from keyboard */
static int ci_read_line(char *buf, uint32_t size) {
    uint32_t pos = 0;
    while (1) {
        while (!keyboard_has_data()) {
            asm volatile("hlt");
        }
        keyboard_event_t event;
        if (!keyboard_get_event(&event)) continue;
        if (!(event.flags & KEY_PRESSED)) continue;

        if (event.ascii == '\n') {
            printf("\n");
            break;
        }
        if (event.ascii == '\b') {
            if (pos > 0) {
                pos--;
                printf("\b");
            }
            continue;
        }
        if (pos < size - 1 && event.ascii >= 32 && event.ascii < 127) {
            buf[pos++] = event.ascii;
            putchar(event.ascii);
        }
    }
    buf[pos] = '\0';
    return (int)pos;
}

void cinterp_repl(void) {
    printf("C Interpreter REPL (type 'exit' to quit)\n");
    printf("Supports: int/char vars, if/else, while, for, switch/case, functions, printf\n\n");

    char line[512];
    char code_buf[4096];
    uint32_t code_len = 0;
    int brace_depth = 0;

    while (1) {
        if (brace_depth > 0) {
            printf("  ... ");
        } else {
            printf("c> ");
        }

        int len = ci_read_line(line, sizeof(line));
        if (len == 0) continue;

        /* Check for exit */
        if (brace_depth == 0 && strcmp(line, "exit") == 0) {
            printf("Bye.\n");
            break;
        }

        /* Count braces */
        for (int i = 0; line[i]; i++) {
            if (line[i] == '{') brace_depth++;
            else if (line[i] == '}') brace_depth--;
        }

        /* Append to code buffer */
        if (code_len + (uint32_t)len + 2 < sizeof(code_buf)) {
            memcpy(code_buf + code_len, line, len);
            code_len += len;
            code_buf[code_len++] = '\n';
            code_buf[code_len] = '\0';
        }

        /* Execute when braces are balanced */
        if (brace_depth <= 0) {
            /* If it's a simple expression (no semicolons, no keywords at start), evaluate and print */
            int is_simple_expr = 1;
            int has_semi = 0;
            for (int i = 0; code_buf[i]; i++) {
                if (code_buf[i] == ';') has_semi = 1;
            }

            /* Check if it starts with a type keyword or control flow */
            if (strncmp(code_buf, "int ", 4) == 0 ||
                strncmp(code_buf, "char ", 5) == 0 ||
                strncmp(code_buf, "if ", 3) == 0 ||
                strncmp(code_buf, "while ", 6) == 0 ||
                strncmp(code_buf, "for ", 4) == 0 ||
                strncmp(code_buf, "void ", 5) == 0 ||
                has_semi || brace_depth < 0) {
                is_simple_expr = 0;
            }

            if (is_simple_expr && !has_semi) {
                /* Try as expression - wrap in a printf */
                char eval_buf[4200];
                snprintf(eval_buf, sizeof(eval_buf), "printf(\"%%d\\n\", (%s));", code_buf);
                cinterp_exec(eval_buf);
            } else {
                cinterp_exec(code_buf);
            }

            code_len = 0;
            code_buf[0] = '\0';
            brace_depth = 0;
        }
    }

    /* Clean up stored strings */
    for (uint32_t i = 0; i < ci_interp.string_count; i++) {
        if (ci_interp.strings[i]) kfree(ci_interp.strings[i]);
    }
    ci_interp.string_count = 0;
}

void cinterp_run(void) {
    cinterp_init();
    cinterp_repl();
}
