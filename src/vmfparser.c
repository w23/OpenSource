#include "vmfparser.h"
#include "libc.h"

typedef enum {
	VMFTokenType_End,
	VMFTokenType_Error,
	VMFTokenType_String,
	VMFTokenType_Open,
	VMFTokenType_Close
} VMFTokenType;

typedef struct {
	VMFTokenType type;
	StringView string;
} VMFToken;

static VMFToken readNextToken(VMFState *state) {
	const char *c = state->data.str;
	const char * const end = state->data.str + state->data.length;
	const char *error_message = NULL;

#define CHECK_END (end == c || *c == '\0')
#define REPORT_ERROR(msg) \
	do {\
		error_message = msg; \
		token.type = VMFTokenType_Error; \
		goto exit; \
	} while(0)

	VMFToken token;
	token.string.str = NULL;
	token.string.length = 0;
	token.type = VMFTokenType_End;

	for (;;) {
		for (;; ++c) {
			if (CHECK_END) {
				--c;
				goto exit;
			}
			if (!isspace(*c))
				break;
		}

		switch(*c) {
			case '{':
				token.string.str = c;
				token.string.length = 1;
				token.type = VMFTokenType_Open;
				break;
			case '}':
				token.string.str = c;
				token.string.length = 1;
				token.type = VMFTokenType_Close;
				break;
			case '/':
				++c;
				if (CHECK_END || *c != '/')
					REPORT_ERROR("'/' expected");
				while(!CHECK_END && *c != '\n') ++c;
				continue;
			case '\"':
				token.string.str = ++c;
				for (;; ++c) {
					if (CHECK_END)
						REPORT_ERROR("\" is not closed at the end");
					if (*c == '\"')
						break;
				}
				token.string.length = (int)(c - token.string.str);
				token.type = VMFTokenType_String;
				break;

			default:
				token.string.str = c;
				while (!CHECK_END && isgraph(*c)) ++c;
				token.string.length = (int)(c - token.string.str);
				token.type = VMFTokenType_String;
		} /* switch(*c) */

		break;
	} /* forever */

exit:
	if (error_message)
		PRINTF("Parsing error \"%s\" @%d (%.*s)", error_message,
				(int)(c - state->data.str), (int)(end - c < 32 ? end - c : 32), c);
	//else PRINTF("Token %d, (%.*s)", token.type, PRI_SVV(token.string));

	state->data.str = c + 1;
	state->data.length = (int)(end - c - 1);
	return token;
}

VMFResult vmfParse(VMFState *state) {
	VMFKeyValue kv;
	VMFAction action = VMFAction_Continue;

	for (;;) {
		switch (action) {
			case VMFAction_Continue:
				break;
			case VMFAction_Exit:
				return VMFResult_Success;
			case VMFAction_SemanticError:
				return VMFResult_SemanticError;
		}

		kv.key.str = kv.value.str = "";
		kv.key.length = kv.value.length = 0;

		VMFToken token = readNextToken(state);
		switch (token.type) {
			case VMFTokenType_String:
				kv.key = token.string;
				break;
			case VMFTokenType_Open:
				action = state->callback(state, VMFEntryType_SectionOpen, &kv);
				continue;
			case VMFTokenType_Close:
				action = state->callback(state, VMFEntryType_SectionClose, &kv);
				continue;
			case VMFTokenType_End:
				return VMFResult_Success;
			default:
				PRINTF("Unexpected token %d", token.type);
				return VMFResult_SyntaxError;
		}

		token = readNextToken(state);
		switch (token.type) {
			case VMFTokenType_String:
				kv.value = token.string;
				action = state->callback(state, VMFEntryType_KeyValue, &kv);
				continue;
			case VMFTokenType_Open:
				action = state->callback(state, VMFEntryType_SectionOpen, &kv);
				continue;
			default:
				PRINTF("Unexpected token %d", token.type);
				return VMFResult_SyntaxError;
		}
	} // forever
} // vmfParse
