#pragma once
#include "atto/math.h"

struct Camera {
	struct AMat4f projection;
	struct AMat4f view_projection;
	struct AMat4f view;
	struct AMat3f axes, orientation;
	struct AVec3f pos, dir, up;
	float z_near, z_far;
};

void cameraRecompute(struct Camera *cam);
void cameraProjection(struct Camera *cam, float znear, float zfar, float horizfov, float aspect);
void cameraLookAt(struct Camera *cam, struct AVec3f pos, struct AVec3f at, struct AVec3f up);
void cameraMove(struct Camera *cam, struct AVec3f v);
void cameraRotateYaw(struct Camera *cam, float yaw);
void cameraRotatePitch(struct Camera *cam, float pitch);
