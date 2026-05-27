#pragma once

namespace PhysicsLib
{
bool IsDoubleJumpEnabled();
void SetDoubleJumpEnabled(bool enabled);
bool IsInfiniteJumpEnabled();
void SetInfiniteJumpEnabled(bool enabled);
bool IsGravityEnabled();
void SetGravityEnabled(bool enabled);
bool IsInertiaEnabled();
void SetInertiaEnabled(bool enabled);
bool IsContactEnabled();
void SetContactEnabled(bool enabled);

void DestroySettingsDialog();
}
