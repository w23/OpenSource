#pragma once

#include "common.h"

typedef enum {
	Parser_Continue,
	Parser_Exit,
	Parser_Error
} ParserCallbackResult;

struct ParserState;
typedef struct ParserState ParserState;

typedef ParserCallbackResult (*ParserCallback)(ParserState *state, StringView s);

struct ParserState {
	void *user_data;
	struct {
		ParserCallback curlyOpen;
		ParserCallback curlyClose;
		ParserCallback string;
	} callbacks;
};

typedef enum {
	ParseResult_Success,
	ParseResult_Error
} ParseResult;

ParseResult parserParse(ParserState *state, StringView string);

// Utility callback function for specifying semantically invalid tokens
ParserCallbackResult parserError(ParserState *state, StringView s);
ParserCallbackResult parserIgnore(ParserState *state, StringView s);
