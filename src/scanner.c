#include <tree_sitter/parser.h>
#include <tree_sitter/alloc.h>
#include <stdbool.h>

enum TokenType {
    WHITESPACE,
    BOL_COMMENT,
    ERROR_SENTINEL,
};

typedef struct {
    bool pending_bol_comment;
} ScannerState;

void *tree_sitter_abap_external_scanner_create() {
    ScannerState *state = (ScannerState *)ts_calloc(1, sizeof(ScannerState));
    state->pending_bol_comment = false;
    return state;
}

void tree_sitter_abap_external_scanner_destroy(void *payload) {
    ts_free(payload);
}

unsigned tree_sitter_abap_external_scanner_serialize(void *payload, char *buffer) {
    ScannerState *state = (ScannerState *)payload;
    buffer[0] = state->pending_bol_comment ? 1 : 0;
    return 1;
}

void tree_sitter_abap_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    ScannerState *state = (ScannerState *)payload;
    state->pending_bol_comment = length > 0 && buffer[0] != 0;
}

bool tree_sitter_abap_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    ScannerState *state = (ScannerState *)payload;
    bool can_ws  = valid_symbols[WHITESPACE];
    bool can_bol = valid_symbols[BOL_COMMENT];

    // If we've already split off leading whitespace for an indented comment,
    // now emit the BOL_COMMENT token.
    if (state->pending_bol_comment && can_bol) {
    comment:
        state->pending_bol_comment = false;
        lexer->result_symbol = BOL_COMMENT;
        // consume the '*' itself
        lexer->advance(lexer, false);
        // consume up to newline
        while (!lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
            lexer->advance(lexer, false);
        }
        return true;
    }

    // We only ever produce WHITESPACE or BOL_COMMENT here
    if (!can_ws && !can_bol) {
        return false;
    }

    // Treat any newline (CR, LF, or CRLF) as whitespace
    if (lexer->lookahead == '\r' || lexer->lookahead == '\n' || lexer->lookahead == '\v' || lexer->lookahead == '\f') {
        lexer->result_symbol = WHITESPACE;
        if (lexer->lookahead == '\r') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '\n') {
                lexer->advance(lexer, false);
            }
        } else {
            lexer->advance(lexer, false);
        }
        return true;
    }

    // Measure current column to detect "start of line"
    int col = lexer->get_column(lexer);
    int cnt = 0;

    // Consume any spaces or tabs
    while (!lexer->eof(lexer) && (lexer->lookahead == ' ' || lexer->lookahead == '\t')) {
        lexer->advance(lexer, false);
        cnt++;
    }

    // If we consumed indentation at column 0 and the next char is '*',
    // split it: return the whitespace now, then schedule the comment.
    if (cnt > 0 && col == 0 && lexer->lookahead == '*' && can_ws && can_bol) {
        state->pending_bol_comment = true;
        lexer->result_symbol = WHITESPACE;
        return true;
    }

    // If we consumed any whitespace (not leading into a comment), emit it
    if (cnt > 0 && can_ws) {
        lexer->result_symbol = WHITESPACE;
        return true;
    }

    // If at start of line with '*' and comments are allowed, consume it
    if (col == 0 && lexer->lookahead == '*' && can_bol) {
        goto comment;
    }

    return false;
}
