/* overlay-merge 74e0148492f3 e7abb8777762 76f386a383ce */
void ObjBean_FollowPath(ObjBean* this, PlayState* play) {
    Path* path;
    Vec3f acell;
    Vec3f pathPointsFloat;
    f32 speed;
    Vec3f* nextPathPoint;
    Vec3f* currentPoint;
    Vec3f* sp4C;
    Vec3f sp40;
    Vec3f sp34;
    f32 sp30;
    f32 mag;

    Math_StepToF(&this->dyna.actor.speedXZ, sBeanSpeeds[this->unk_1F6].velocity, sBeanSpeeds[this->unk_1F6].accel);
    path = ObjBean_Path(this, play);
    nextPathPoint = &((Vec3f*)SEGMENTED_TO_VIRTUAL(path->points))[this->nextPointIndex];

    pathPointsFloat = *nextPathPoint; // SOH [Unbound]

    Math_Vec3f_Diff(&pathPointsFloat, &this->pathPoints, &acell);
    mag = Math3D_Vec3fMagnitude(&acell);
    speed = CLAMP_MIN(this->dyna.actor.speedXZ, 0.5f);
    if (speed > mag) {
        currentPoint = &((Vec3f*)SEGMENTED_TO_VIRTUAL(path->points))[this->currentPointIndex];

        Math_Vec3f_Copy(&this->pathPoints, &pathPointsFloat);
        this->currentPointIndex = this->nextPointIndex;

        if (this->pathCount <= this->currentPointIndex) {
            this->nextPointIndex = 0;
        } else {
            this->nextPointIndex++;
        }
        sp4C = &((Vec3f*)SEGMENTED_TO_VIRTUAL(path->points))[this->nextPointIndex];
        // SOH [Unbound] path points are f32; the Vec3s reader would decode their bytes as shorts
        Math_Vec3f_Diff(nextPathPoint, currentPoint, &sp40);
        Math_Vec3f_Diff(sp4C, nextPathPoint, &sp34);
        if (Math3D_CosOut(&sp40, &sp34, &sp30)) {
            this->dyna.actor.speedXZ = 0.0f;
        } else {
            this->dyna.actor.speedXZ *= (sp30 + 1.0f) * 0.5f;
        }
    } else {
        Math_Vec3f_Scale(&acell, this->dyna.actor.speedXZ / mag);
        this->pathPoints.x += acell.x;
        this->pathPoints.y += acell.y;
        this->pathPoints.z += acell.z;
    }
}