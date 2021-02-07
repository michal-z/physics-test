#include "common.hlsli"
#include "../cpp_hlsl_common.h"

#define ROOT_SIGNATURE \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "SRV(t0, visibility = SHADER_VISIBILITY_VERTEX)"

ConstantBuffer<GLOBALS> cbv_glob : register(b0);
StructuredBuffer<DEBUG_VERTEX> srv_vertices : register(t0);

[RootSignature(ROOT_SIGNATURE)]
void Vertex_Shader(
    U32 vertex_id : SV_VertexID,
    out XMFLOAT4 out_position_ndc : SV_Position,
    out XMFLOAT3 out_color : _Color
) {
    const DEBUG_VERTEX v = srv_vertices[vertex_id];

    out_position_ndc = mul(XMFLOAT4(v.position, 1.0f), cbv_glob.world_to_clip);
    out_color = XMFLOAT3(
        (v.color & 0xFF) / 255.0f,
        ((v.color >> 8) & 0xFF) / 255.0f,
        ((v.color >> 16) & 0xFF) / 255.0f
    );
}

[RootSignature(ROOT_SIGNATURE)]
void Pixel_Shader(
    XMFLOAT4 position_ndc : SV_Position,
    XMFLOAT3 color : _Color,
    out XMFLOAT4 out_color : SV_Target0
) {
    out_color = XMFLOAT4(color, 1.0f);
}
