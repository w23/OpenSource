#pragma once

struct DXTUnpackContext {
	int width, height;
	const void *packed;
	void *output;
};

void dxt1Unpack(struct DXTUnpackContext ctx);
void dxt5Unpack(struct DXTUnpackContext ctx);
