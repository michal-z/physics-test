module;
#include "pch.h"
export module physics;
import library;
namespace physics {

using namespace DirectX;
using namespace DirectX::PackedVector;
#include "cpp_hlsl_common.h"

struct DEBUG_DRAW : public btIDebugDraw {
    VECTOR<DEBUG_VERTEX> lines;
    int debug_mode;

    virtual void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override {
        const XMFLOAT3 p0 = { from.x(), from.y(), from.z() };
        const XMFLOAT3 p1 = { to.x(), to.y(), to.z() };
        const XMCOLOR packed_color(color.x(), color.y(), color.z(), 1.0f);
        lines.push_back({ .position = p0, .color = packed_color });
        lines.push_back({ .position = p1, .color = packed_color });
    }
    virtual void drawLine(
        const btVector3& from,
        const btVector3& to,
        const btVector3& fromColor,
        const btVector3& toColor
    ) override {
        const XMFLOAT3 p0 = { from.x(), from.y(), from.z() };
        const XMFLOAT3 p1 = { to.x(), to.y(), to.z() };
        const XMCOLOR packed_color0(fromColor.x(), fromColor.y(), fromColor.z(), 1.0f);
        const XMCOLOR packed_color1(toColor.x(), toColor.y(), toColor.z(), 1.0f);
        lines.push_back({ .position = p0, .color = packed_color0 });
        lines.push_back({ .position = p1, .color = packed_color1 });
    }
    virtual void drawContactPoint(
        const btVector3&,
        const btVector3&,
        btScalar,
        int,
        const btVector3&
    ) override {
    }
    virtual void reportErrorWarning(const char* warningString) override {
        if (warningString) {
            OutputDebugStringA(warningString);
        }
    }
    virtual void draw3dText(const btVector3&, const char*) override {
    }
    virtual void setDebugMode(int in_debug_mode) override {
        debug_mode = in_debug_mode;
    }
    virtual int getDebugMode() const override {
        return debug_mode;
    }
};

export struct PHYSICS {
    btDefaultCollisionConfiguration* collision_config;
    btCollisionDispatcher* dispatcher;
    btBroadphaseInterface* broadphase;
    btSequentialImpulseConstraintSolver* solver;
    btDiscreteDynamicsWorld* world;
    DEBUG_DRAW* debug;
};

export bool Init_Physics(PHYSICS* px) {
    assert(px);
    px->collision_config = new btDefaultCollisionConfiguration();
    px->dispatcher = new btCollisionDispatcher(px->collision_config);
    px->broadphase = new btDbvtBroadphase();
    px->solver = new btSequentialImpulseConstraintSolver();
    px->world = new btDiscreteDynamicsWorld(
        px->dispatcher,
        px->broadphase,
        px->solver,
        px->collision_config
    );
    px->debug = new DEBUG_DRAW();
    px->debug->setDebugMode(btIDebugDraw::DBG_DrawWireframe | btIDebugDraw::DBG_DrawFrames);
    px->world->setDebugDrawer(px->debug);

    return true;
}

export void Deinit_Physics(PHYSICS* px) {
    assert(px && px->collision_config && px->world);
    delete px->debug;
    delete px->world;
    delete px->solver;
    delete px->broadphase;
    delete px->dispatcher;
    delete px->collision_config;
}

} // namespace physics
