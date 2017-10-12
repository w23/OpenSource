#pragma once

enum TokenType {
	Token_Skip,
	Token_String,
	Token_CurlyOpen,
	Token_CurlyClose,
	Token_Error,
	Token_End
};
struct TokenContext {
	const char *cursor;
	const char *end;

	const char *str_start;
	int str_length;
};

enum TokenType getNextToken(struct TokenContext *tok);
