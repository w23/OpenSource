#pragma once

#include "common.h"

struct VMFState;
typedef struct VMFState VMFState;

typedef enum {
	VMFEntryType_KeyValue,
	VMFEntryType_SectionOpen,
	VMFEntryType_SectionClose
} VMFEntryType;

typedef enum {
	VMFAction_Continue,
	VMFAction_Exit,
	VMFAction_SemanticError
} VMFAction;

typedef struct {
	StringView key, value;
} VMFKeyValue;

typedef VMFAction (*VMFCallback)(VMFState *state, VMFEntryType entry, const VMFKeyValue *kv);

struct VMFState {
	void *user_data;
	StringView data;
	VMFCallback callback;
};

typedef enum {
	VMFResult_Success,
	VMFResult_SyntaxError,
	VMFResult_SemanticError
} VMFResult;

VMFResult vmfParse(VMFState *state);
