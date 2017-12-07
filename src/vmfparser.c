#include "vmfparser.h"
#include "libc.h"

ParseResult parserParse(ParserState *state, StringView string) {
	const char *c = string.str;
	const char * const end = string.str + string.length;

#define CHECK_END (end == c || *c == '\0')

	int nest = 0;
	for (;;) {
		while(!CHECK_END && isspace(*c)) ++c;
		if (CHECK_END)
			return nest == 0 ? ParseResult_Success : ParseResult_Error;

		ParserCallbackResult cb_result = Parser_Continue;
		StringView str = { .str = c, .length = 0 };
		int quote = 0;
		switch(*c) {
			case '{':
				++nest;
				cb_result = state->callbacks.curlyOpen(state, str);
				++c;
				break;
			case '}':
				if (nest < 1)
					return ParseResult_Error;
				--nest;
				cb_result = state->callbacks.curlyClose(state, str);
				++c;
				break;
			case '/':
				if (*++c == '/') {
					while(!CHECK_END && *c != '\n') ++c;
				} else
					return ParseResult_Error;
				break;
			case '\"':
				str.str = ++c;
				quote = 1;

				// fall through
			default:
				if (quote) {
					for (;; ++c) {
						if (CHECK_END)
							return ParseResult_Error;
						if (*c == '\"')
							break;
					}
				} else {
					while (!CHECK_END && isgraph(*c)) ++c;
				}

				str.length = c - str.str;
				cb_result = state->callbacks.string(state, str);
				++c;
		} /* switch(*c) */

		switch (cb_result) {
		case Parser_Continue:
			break;
		case Parser_Exit:
			return ParseResult_Success;
		default:
			return ParseResult_Error;
		}
	} /* forever */
}

ParserCallbackResult parserError(ParserState *state, StringView s) {
	(void)state; (void)s;
	return Parser_Error;
}

ParserCallbackResult parserIgnore(ParserState *state, StringView s) {
	(void)state; (void)s;
	return Parser_Continue;
}
