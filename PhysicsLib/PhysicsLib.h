#pragma once

#include <d3dx9.h>
#include <tchar.h>

#include <vector>

namespace PhysicsLib
{
class PhysicsLib
{
public:
    enum class ObjectType
    {
        PassThrough,
        Slide,
        MovingSlide,
    };

    enum class ShapeType
    {
        Point,
        Sphere,
        Cylinder,
    };

    struct Transform
    {
        D3DXVECTOR3 position = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        D3DXVECTOR3 rotation = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        D3DXVECTOR3 scale = D3DXVECTOR3(1.0f, 1.0f, 1.0f);
        D3DXVECTOR3 velocity = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    };

    static void Initialize();
    static void Finalize();
    static void Update(float deltaSeconds = 1.0f / 60.0f);

    static int Load(const TCHAR* modelPath, ObjectType objectType, float friction);

    static void SetTransform(int id,
                             const D3DXVECTOR3& position,
                             const D3DXVECTOR3& rotation = D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                             const D3DXVECTOR3& scale = D3DXVECTOR3(1.0f, 1.0f, 1.0f));

    static void SetVelocity(int id, const D3DXVECTOR3& velocity);
    static Transform GetTransform(int id);

    static bool CheckCollide(const D3DXVECTOR3& currentPosition,
                             const D3DXVECTOR3& moveVector,
                             ShapeType shapeType,
                             D3DXVECTOR3* outPosition,
                             std::vector<int>* outPassThroughIds,
                             std::vector<int>* outSolidIds,
                             float radius = 0.0f,
                             float height = 0.0f);
};
}
