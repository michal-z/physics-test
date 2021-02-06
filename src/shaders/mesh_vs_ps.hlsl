#include "common.hlsli"
#include "../cpp_hlsl_common.h"

#define ROOT_SIGNATURE \
    "RootConstants(b0, num32BitConstants = 3), " \
    "CBV(b1), " \
    "DescriptorTable(SRV(t0, numDescriptors = 3)), " \

ConstantBuffer<DRAW_COMMAND> cbv_draw_cmd : register(b0);
ConstantBuffer<GLOBALS> cbv_glob : register(b1);

StructuredBuffer<VERTEX> srv_vertices : register(t0);
Buffer<U32> srv_indices : register(t1);
StructuredBuffer<RENDERABLE_CONSTANTS> srv_const_renderables : register(t2);

[RootSignature(ROOT_SIGNATURE)]
void Vertex_Shader(
    U32 vertex_id : SV_VertexID,
    out XMFLOAT4 out_position_ndc : SV_Position,
    out XMFLOAT3 out_position : _Position,
    out XMFLOAT3 out_normal : _Normal,
    out XMFLOAT4 out_tangent : _Tangent,
    out XMFLOAT2 out_uv : _Uv
) {
    const U32 vertex_index = srv_indices[vertex_id + cbv_draw_cmd.index_offset] +
        cbv_draw_cmd.vertex_offset;

    const VERTEX v = srv_vertices[vertex_index];

    const XMFLOAT4X4 object_to_world = srv_const_renderables[cbv_draw_cmd.renderable_id].object_to_world;
    const XMFLOAT4X4 object_to_clip = mul(object_to_world, cbv_glob.world_to_clip);

    out_position_ndc = mul(XMFLOAT4(v.position, 1.0f), object_to_clip);
    out_position = mul(v.position, (XMFLOAT3X3)object_to_world);
    out_normal = v.normal;
    out_tangent = v.tangent;
    out_uv = v.uv;
}

[RootSignature(ROOT_SIGNATURE)]
void Pixel_Shader(
    XMFLOAT4 position_ndc : SV_Position,
    XMFLOAT3 position : _Position,
    XMFLOAT3 normal : _Normal,
    XMFLOAT4 tangent : _Tangent,
    XMFLOAT2 uv : _Uv,
    out XMFLOAT4 out_color : SV_Target0
) {
    out_color = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
}
