#include "vmfparser.h"
#include "libc.h"

enum TokenType getNextToken(struct TokenContext *tok) {
	enum TokenType type = Token_Skip;
	const char *c = tok->cursor;

#define CHECK_END ((tok->end && tok->end == c) || *c == '\0')

	while (type == Token_Skip) {
		while(!CHECK_END && isspace(*c)) ++c;
		if (CHECK_END) return Token_End;

		tok->str_start = c;
		tok->str_length = 0;
		switch(*c) {
			case '\"':
				tok->str_start = ++c;
				while(!CHECK_END && *c != '\"') ++c;
				type = (*c == '\"') ? Token_String : Token_Error;
				break;
			case '{': type = Token_CurlyOpen; break;
			case '}': type = Token_CurlyClose; break;
			case '/':
				if (*++c == '/') {
					while(!CHECK_END && *c != '\n') ++c;
					type = Token_Skip;
				} else
					type = Token_Error;
				break;
			default:
				while (!CHECK_END && isgraph(*c)) ++c;
				type = (c == tok->str_start) ? Token_Error : Token_String;
		} /* switch(*c) */
	} /* while skip */

	tok->str_length = c - tok->str_start;
	tok->cursor = c + 1;
	return type;
}
