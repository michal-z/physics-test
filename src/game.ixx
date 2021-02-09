module;
#include "pch.h"
export module game;
import graphics;
import physics;
import library;
namespace game {

using namespace DirectX;
using namespace DirectX::PackedVector;
#include "cpp_hlsl_common.h"

struct MESH {
    U32 index_offset;
    U32 vertex_offset;
    U32 num_indices;
};

struct RENDERABLE {
    MESH mesh;
    XMFLOAT3 position;
};

struct GAME {
    graphics::GRAPHICS graphics;
    physics::PHYSICS physics;
    library::FRAME_STATS frame_stats;
    library::IMGUI_CONTEXT gui;
    VECTOR<MESH> meshes;
    VECTOR<RENDERABLE> renderables;
    graphics::PIPELINE_HANDLE mesh_pso;
    graphics::PIPELINE_HANDLE physics_debug_pso;
    graphics::RESOURCE_HANDLE vertex_buffer;
    graphics::RESOURCE_HANDLE index_buffer;
    graphics::RESOURCE_HANDLE renderable_const_buffer;
    graphics::RESOURCE_HANDLE srgb_texture;
    graphics::RESOURCE_HANDLE depth_texture;
    D3D12_CPU_DESCRIPTOR_HANDLE vertex_buffer_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE index_buffer_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE renderable_const_buffer_srv;
    D3D12_CPU_DESCRIPTOR_HANDLE srgb_texture_rtv;
    D3D12_CPU_DESCRIPTOR_HANDLE depth_texture_dsv;
    struct {
        btTriangleIndexVertexArray* mesh_interface;
        btBvhTriangleMeshShape* shape;
        btCollisionObject* body;
        graphics::RESOURCE_HANDLE ao_texture;
        D3D12_CPU_DESCRIPTOR_HANDLE ao_texture_srv;
    } level;
    struct {
        btSphereShape* shape;
        btCollisionObject* body;
    } player;
    struct {
        ID2D1_SOLID_COLOR_BRUSH* brush;
        IDWRITE_TEXT_FORMAT* text_format;
    } hud;
    struct {
        XMFLOAT3 position;
        F32 pitch;
        F32 yaw;
    } camera;
    struct {
        S32 cursor_prev_x;
        S32 cursor_prev_y;
    } mouse;
};

void Add_Mesh(
    const CHAR* filename,
    VECTOR<MESH>* meshes,
    VECTOR<VERTEX>* all_vertices,
    VECTOR<U32>* all_indices,
    btIndexedMesh* physics_mesh
) {
    assert(filename && meshes && all_vertices && all_indices);

    VECTOR<XMFLOAT3> positions;
    VECTOR<XMFLOAT3> normals;
    VECTOR<XMFLOAT4> tangents;
    VECTOR<XMFLOAT2> uvs;
    VECTOR<U32> indices;
    library::Load_Mesh(filename, &indices, &positions, &normals, &uvs, &tangents);
    assert(!normals.empty() && !uvs.empty() && !tangents.empty());

    if (physics_mesh) {
        U8* index_base = (U8*)btAlignedAlloc(indices.size() * sizeof U32, 16);
        U8* vertex_base = (U8*)btAlignedAlloc(positions.size() * sizeof XMFLOAT3, 16);
        memcpy(index_base, indices.data(), indices.size() * sizeof U32);
        memcpy(vertex_base, positions.data(), positions.size() * sizeof XMFLOAT3);

        physics_mesh->m_numTriangles = (S32)(indices.size() / 3);
        physics_mesh->m_triangleIndexBase = index_base;
        physics_mesh->m_triangleIndexStride = 3 * sizeof U32;
        physics_mesh->m_numVertices = (S32)positions.size();
        physics_mesh->m_vertexBase = vertex_base;
        physics_mesh->m_vertexStride = sizeof XMFLOAT3;
    }

    VECTOR<VERTEX> vertices(positions.size());
    for (U32 i = 0; i < vertices.size(); ++i) {
        vertices[i] = { positions[i], normals[i], tangents[i], uvs[i] };
    }
    meshes->push_back({
        .index_offset = (U32)all_indices->size(),
        .vertex_offset = (U32)all_vertices->size(),
        .num_indices = (U32)indices.size(),
    });
    all_vertices->insert(all_vertices->end(), vertices.begin(), vertices.end());
    all_indices->insert(all_indices->end(), indices.begin(), indices.end());
}

void Create_And_Upload_Texture(
    const WCHAR* filename,
    graphics::GRAPHICS* gr,
    library::MIPMAP_GENERATOR* mipgen,
    graphics::RESOURCE_HANDLE* out_texture,
    D3D12_CPU_DESCRIPTOR_HANDLE* out_texture_srv
) {
    assert(filename && gr && mipgen && out_texture && out_texture_srv);
    const auto [tex, srv] = graphics::Create_Texture_From_File(gr, filename);
    library::Generate_Mipmaps(mipgen, gr, tex);
    graphics::Add_Transition_Barrier(gr, tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    *out_texture = tex;
    *out_texture_srv = srv;
}

template<typename T> void Upload_To_Gpu(
    graphics::GRAPHICS* gr,
    graphics::RESOURCE_HANDLE resource,
    const VECTOR<T>* data,
    D3D12_RESOURCE_STATES state
) {
    assert(gr && data);
    const auto [span, buffer, buffer_offset] = graphics::Allocate_Upload_Buffer_Region<T>(
        gr,
        (U32)data->size()
    );
    memcpy(span.data(), data->data(), span.size_bytes());
    gr->cmdlist->CopyBufferRegion(
        graphics::Get_Resource(gr, resource),
        0,
        buffer,
        buffer_offset,
        span.size_bytes()
    );
    graphics::Add_Transition_Barrier(gr, resource, state);
}

bool Init_Game(GAME* game) {
    assert(game);

    const HWND window = library::Create_Window("game", 1920, 1080, /* init_imgui */ true);
    if (!graphics::Init_Graphics(&game->graphics, window)) {
        return false;
    }
    graphics::GRAPHICS* gr = &game->graphics;

    if (!physics::Init_Physics(&game->physics)) {
        return false;
    }

    {
        const VECTOR<U8> vs = library::Load_File("data/shaders/mesh_vs_ps.vs.cso");
        const VECTOR<U8> ps = library::Load_File("data/shaders/mesh_vs_ps.ps.cso");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {
            .VS = { vs.data(), vs.size() },
            .PS = { ps.data(), ps.size() },
            .BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
            .SampleMask = UINT32_MAX,
            .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
            .DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
            .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .NumRenderTargets = 1,
            .RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
            .DSVFormat = DXGI_FORMAT_D32_FLOAT,
            .SampleDesc = { .Count = 1, .Quality = 0 },
        };
        game->mesh_pso = graphics::Create_Graphics_Shader_Pipeline(gr, &desc);
    }
    {
        const VECTOR<U8> vs = library::Load_File("data/shaders/physics_debug_vs_ps.vs.cso");
        const VECTOR<U8> ps = library::Load_File("data/shaders/physics_debug_vs_ps.ps.cso");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {
            .VS = { vs.data(), vs.size() },
            .PS = { ps.data(), ps.size() },
            .BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
            .SampleMask = UINT32_MAX,
            .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
            .DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
            .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
            .NumRenderTargets = 1,
            .RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
            .DSVFormat = DXGI_FORMAT_D32_FLOAT,
            .SampleDesc = { .Count = 1, .Quality = 0 },
        };
        game->physics_debug_pso = graphics::Create_Graphics_Shader_Pipeline(gr, &desc);
    }

    {
        btTransform ground_transform;
        ground_transform.setIdentity();
        ground_transform.setOrigin(btVector3(0.5f, 0.5f, -0.5f));

        game->player.shape = new btSphereShape(0.5f);
        game->player.body = new btCollisionObject();
        game->player.body->setCollisionShape(game->player.shape);
        game->player.body->setWorldTransform(ground_transform);
        game->physics.world->addCollisionObject(game->player.body);
    }

    VHR(gr->d2d.context->CreateSolidColorBrush({ 0.0f }, &game->hud.brush));
    game->hud.brush->SetColor({ .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f });

    VHR(gr->d2d.factory_dwrite->CreateTextFormat(
        L"Verdana",
        NULL,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        32.0f,
        L"en-us",
        &game->hud.text_format
    ));
    VHR(game->hud.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING));
    VHR(game->hud.text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR));

    VECTOR<VERTEX> all_vertices;
    VECTOR<U32> all_indices;
    {
        btIndexedMesh physics_mesh = {};
        Add_Mesh("data/level1_collision.gltf", &game->meshes, &all_vertices, &all_indices, &physics_mesh);

        game->level.mesh_interface = new btTriangleIndexVertexArray();
        game->level.mesh_interface->addIndexedMesh(physics_mesh);
        game->level.shape = new btBvhTriangleMeshShape(game->level.mesh_interface, true);
        game->level.body = new btCollisionObject();
        game->level.body->setCollisionShape(game->level.shape);
        game->physics.world->addCollisionObject(game->level.body);

        const CHAR* mesh_paths[] = {
            "data/cube.gltf",
        };
        for (U32 i = 0; i < eastl::size(mesh_paths); ++i) {
            Add_Mesh(mesh_paths[i], &game->meshes, &all_vertices, &all_indices, NULL);
        }
    }
    game->renderables.push_back({ .mesh = game->meshes[0], .position = { 0.0f, 0.0f, 0.0f } });

    // Create one global vertex buffer for all static geometry.
    game->vertex_buffer = graphics::Create_Committed_Resource(
        gr,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_HEAP_FLAG_NONE,
        Get_Const_Ptr(CD3DX12_RESOURCE_DESC::Buffer(all_vertices.size() * sizeof VERTEX)),
        D3D12_RESOURCE_STATE_COPY_DEST,
        NULL
    );
    game->vertex_buffer_srv = graphics::Allocate_Cpu_Descriptors(
        gr,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        1
    );
    gr->device->CreateShaderResourceView(
        graphics::Get_Resource(gr, game->vertex_buffer),
        Get_Const_Ptr<D3D12_SHADER_RESOURCE_VIEW_DESC>({
            .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer = {
                .FirstElement = 0,
                .NumElements = (U32)all_vertices.size(),
                .StructureByteStride = sizeof VERTEX,
            }
        }),
        game->vertex_buffer_srv
    );
    // Create one global index buffer for all static geometry.
    game->index_buffer = graphics::Create_Committed_Resource(
        gr,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_HEAP_FLAG_NONE,
        Get_Const_Ptr(CD3DX12_RESOURCE_DESC::Buffer(all_indices.size() * sizeof U32)),
        D3D12_RESOURCE_STATE_COPY_DEST,
        NULL
    );
    game->index_buffer_srv = graphics::Allocate_Cpu_Descriptors(
        gr,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        1
    );
    gr->device->CreateShaderResourceView(
        graphics::Get_Resource(gr, game->index_buffer),
        Get_Const_Ptr<D3D12_SHADER_RESOURCE_VIEW_DESC>({
            .Format = DXGI_FORMAT_R32_UINT,
            .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer = { .NumElements = (U32)all_indices.size() },
        }),
        game->index_buffer_srv
    );
    // Create structured buffer containing constants for each renderable object.
    game->renderable_const_buffer = graphics::Create_Committed_Resource(
        gr,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_HEAP_FLAG_NONE,
        Get_Const_Ptr(
            CD3DX12_RESOURCE_DESC::Buffer(game->renderables.size() * sizeof RENDERABLE_CONSTANTS)
        ),
        D3D12_RESOURCE_STATE_COPY_DEST,
        NULL
    );
    game->renderable_const_buffer_srv = graphics::Allocate_Cpu_Descriptors(
        gr,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        1
    );
    gr->device->CreateShaderResourceView(
        graphics::Get_Resource(gr, game->renderable_const_buffer),
        Get_Const_Ptr<D3D12_SHADER_RESOURCE_VIEW_DESC>({
            .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer = {
                .FirstElement = 0,
                .NumElements = (U32)game->renderables.size(),
                .StructureByteStride = sizeof RENDERABLE_CONSTANTS,
            }
        }),
        game->renderable_const_buffer_srv
    );

    // Create srgb color texture.
    {
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            gr->viewport_width,
            gr->viewport_height
        );
        desc.MipLevels = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        game->srgb_texture = graphics::Create_Committed_Resource(
            gr,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            Get_Const_Ptr(CD3DX12_CLEAR_VALUE(desc.Format, XMVECTORF32{ 0.0f, 0.0f, 0.0f, 1.0f }))
        );
        game->srgb_texture_rtv = graphics::Allocate_Cpu_Descriptors(
            gr,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            1
        );
        gr->device->CreateRenderTargetView(
            graphics::Get_Resource(gr, game->srgb_texture),
            NULL,
            game->srgb_texture_rtv
        );
    }
    // Create depth texture.
    {
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT,
            gr->viewport_width,
            gr->viewport_height
        );
        desc.MipLevels = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        game->depth_texture = graphics::Create_Committed_Resource(
            gr,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            Get_Const_Ptr(CD3DX12_CLEAR_VALUE(desc.Format, 1.0f, 0))
        );
        game->depth_texture_dsv = graphics::Allocate_Cpu_Descriptors(
            gr,
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            1
        );
        gr->device->CreateDepthStencilView(
            graphics::Get_Resource(gr, game->depth_texture),
            NULL,
            game->depth_texture_dsv
        );
    }

    library::MIPMAP_GENERATOR mipgen_rgba8 = {};
    library::Init_Mipmap_Generator(&mipgen_rgba8, gr, DXGI_FORMAT_R8G8B8A8_UNORM);

    graphics::Begin_Frame(gr);

    library::Init_Gui_Context(&game->gui, gr, 1);

    Create_And_Upload_Texture(
        L"data/level1_collision_ao.png",
        gr,
        &mipgen_rgba8,
        &game->level.ao_texture,
        &game->level.ao_texture_srv
    );

    // Upload vertices.
    Upload_To_Gpu(gr, game->vertex_buffer, &all_vertices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Upload indices.
    Upload_To_Gpu(gr, game->index_buffer, &all_indices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Flush commands and wait for GPU to complete them.
    graphics::Flush_Gpu_Commands(gr);
    graphics::Finish_Gpu_Commands(gr);

    library::Deinit_Mipmap_Generator(&mipgen_rgba8, gr);

    library::Init_Frame_Stats(&game->frame_stats);

    game->camera = {
        .position = XMFLOAT3(0.0f, 8.0f, -13.2f),
        .pitch = XM_PI * 0.25f,
        .yaw = 0.0f,
    };
    game->mouse = { .cursor_prev_x = 0, .cursor_prev_y = 0 };
    return true;
}

void Deinit_Game(GAME* game) {
    assert(game);
    graphics::GRAPHICS* gr = &game->graphics;
    graphics::Finish_Gpu_Commands(gr);

    game->physics.world->removeCollisionObject(game->level.body);
    game->physics.world->removeCollisionObject(game->player.body);

    btAlignedFree((void*)game->level.mesh_interface->getIndexedMeshArray()[0].m_vertexBase);
    btAlignedFree((void*)game->level.mesh_interface->getIndexedMeshArray()[0].m_triangleIndexBase);
    delete game->level.mesh_interface;
    delete game->level.shape;
    delete game->level.body;
    delete game->player.shape;
    delete game->player.body;

    physics::Deinit_Physics(&game->physics);
    library::Deinit_Gui_Context(&game->gui, gr);
    ImGui::DestroyContext();
    MZ_SAFE_RELEASE(game->hud.brush);
    MZ_SAFE_RELEASE(game->hud.text_format);
    graphics::Release_Resource(gr, game->vertex_buffer);
    graphics::Release_Resource(gr, game->index_buffer);
    graphics::Release_Resource(gr, game->renderable_const_buffer);
    graphics::Release_Resource(gr, game->srgb_texture);
    graphics::Release_Resource(gr, game->depth_texture);
    graphics::Release_Resource(gr, game->level.ao_texture);
    graphics::Release_Pipeline(gr, game->mesh_pso);
    graphics::Release_Pipeline(gr, game->physics_debug_pso);
    graphics::Deinit_Graphics(gr);
}

void Update_Game(GAME* game) {
    assert(game);

    graphics::GRAPHICS* gr = &game->graphics;

    library::Update_Frame_Stats(&game->frame_stats);
    library::Update_Gui(game->frame_stats.delta_time);

    // Handle camera rotation with mouse.
    {
        POINT pos = {};
        GetCursorPos(&pos);
        const F32 delta_x = (F32)pos.x - game->mouse.cursor_prev_x;
        const F32 delta_y = (F32)pos.y - game->mouse.cursor_prev_y;
        game->mouse.cursor_prev_x = pos.x;
        game->mouse.cursor_prev_y = pos.y;

        if (GetAsyncKeyState(VK_RBUTTON) < 0) {
            game->camera.pitch += 0.0025f * delta_y;
            game->camera.yaw += 0.0025f * delta_x;
            game->camera.pitch = XMMin(game->camera.pitch, 0.48f * XM_PI);
            game->camera.pitch = XMMax(game->camera.pitch, -0.48f * XM_PI);
            game->camera.yaw = XMScalarModAngle(game->camera.yaw);
        }
    }
    // Handle camera movement with 'WASD' keys.
    const XMMATRIX camera_view_to_clip = XMMatrixPerspectiveFovLH(
        XM_PI / 3.0f,
        (F32)gr->viewport_width / gr->viewport_height,
        0.1f,
        100.0f
    );
    XMMATRIX camera_world_to_view = {};
    {
        const F32 speed = 5.0f;
        const F32 delta_time = game->frame_stats.delta_time;
        const XMMATRIX transform = XMMatrixRotationX(game->camera.pitch) *
            XMMatrixRotationY(game->camera.yaw);
        XMVECTOR forward = XMVector3Normalize(
            XMVector3Transform(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), transform)
        );
        const XMVECTOR right = speed * delta_time * XMVector3Normalize(
            XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), forward)
        );
        forward = speed * delta_time * forward;

        if (GetAsyncKeyState('W') < 0) {
            const XMVECTOR newpos = XMLoadFloat3(&game->camera.position) + forward;
            XMStoreFloat3(&game->camera.position, newpos);
        } else if (GetAsyncKeyState('S') < 0) {
            const XMVECTOR newpos = XMLoadFloat3(&game->camera.position) - forward;
            XMStoreFloat3(&game->camera.position, newpos);
        }
        if (GetAsyncKeyState('D') < 0) {
            const XMVECTOR newpos = XMLoadFloat3(&game->camera.position) + right;
            XMStoreFloat3(&game->camera.position, newpos);
        } else if (GetAsyncKeyState('A') < 0) {
            const XMVECTOR newpos = XMLoadFloat3(&game->camera.position) - right;
            XMStoreFloat3(&game->camera.position, newpos);
        }
        camera_world_to_view = XMMatrixLookToLH(
            XMLoadFloat3(&game->camera.position),
            forward,
            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
        );
    }

    graphics::Begin_Frame(gr);

    graphics::Add_Transition_Barrier(gr, game->srgb_texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
    graphics::Flush_Resource_Barriers(gr);

    gr->cmdlist->OMSetRenderTargets(1, &game->srgb_texture_rtv, TRUE, &game->depth_texture_dsv);
    gr->cmdlist->ClearDepthStencilView(game->depth_texture_dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
    gr->cmdlist->ClearRenderTargetView(
        game->srgb_texture_rtv,
        XMVECTORF32{ 0.0f, 0.0f, 0.0f, 1.0f },
        0,
        NULL
    );

    D3D12_GPU_VIRTUAL_ADDRESS glob_buffer_addr = {};

    // Upload 'GLOBALS' data.
    {
        const auto [cpu_span, gpu_addr] = graphics::Allocate_Upload_Memory(gr, sizeof GLOBALS);
        glob_buffer_addr = gpu_addr;

        const XMMATRIX world_to_clip = camera_world_to_view * camera_view_to_clip;
        GLOBALS* globals = (GLOBALS*)cpu_span.data();
        XMStoreFloat4x4(&globals->world_to_clip, XMMatrixTranspose(world_to_clip));
    }
    // Upload 'RENDERABLE_CONSTANTS' data.
    {
        graphics::Add_Transition_Barrier(
            gr,
            game->renderable_const_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST
        );
        graphics::Flush_Resource_Barriers(gr);
        const auto [span, buffer, buffer_offset] =
            graphics::Allocate_Upload_Buffer_Region<RENDERABLE_CONSTANTS>(
                gr,
                (U32)game->renderables.size()
            );
        for (U32 i = 0; i < game->renderables.size(); ++i) {
            const XMVECTOR pos = XMLoadFloat3(&game->renderables[i].position);
            XMStoreFloat4x4(
                &span[i].object_to_world,
                XMMatrixTranspose(XMMatrixTranslationFromVector(pos))
            );
        }
        gr->cmdlist->CopyBufferRegion(
            graphics::Get_Resource(gr, game->renderable_const_buffer),
            0,
            buffer,
            buffer_offset,
            span.size_bytes()
        );
        graphics::Add_Transition_Barrier(
            gr,
            game->renderable_const_buffer,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        graphics::Flush_Resource_Barriers(gr);
    }

    const D3D12_GPU_DESCRIPTOR_HANDLE buffer_table_base = graphics::Copy_Descriptors_To_Gpu_Heap(
        gr,
        1,
        game->vertex_buffer_srv
    );
    graphics::Copy_Descriptors_To_Gpu_Heap(gr, 1, game->index_buffer_srv);
    graphics::Copy_Descriptors_To_Gpu_Heap(gr, 1, game->renderable_const_buffer_srv);

    graphics::Set_Pipeline_State(gr, game->mesh_pso);
    gr->cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gr->cmdlist->SetGraphicsRootConstantBufferView(1, glob_buffer_addr);
    gr->cmdlist->SetGraphicsRootDescriptorTable(2, buffer_table_base);
    gr->cmdlist->SetGraphicsRootDescriptorTable(
        3,
        graphics::Copy_Descriptors_To_Gpu_Heap(gr, 1, game->level.ao_texture_srv)
    );
    for (U32 i = 0; i < game->renderables.size(); ++i) {
        const RENDERABLE* renderable = &game->renderables[i];
        gr->cmdlist->SetGraphicsRoot32BitConstants(
            0,
            3,
            Get_Const_Ptr<XMUINT3>({
                renderable->mesh.index_offset,
                renderable->mesh.vertex_offset,
                i, // renderable_id
            }),
            0
        );
        gr->cmdlist->DrawInstanced(renderable->mesh.num_indices, 1, 0, 0);
    }

    physics::PHYSICS* px = &game->physics;
    px->world->debugDrawWorld();
    if (!px->debug->lines.empty()) {
        const auto [cpu_span, gpu_addr] = graphics::Allocate_Upload_Memory(
            gr,
            (U32)(px->debug->lines.size() * sizeof DEBUG_VERTEX)
        );
        memcpy(cpu_span.data(), px->debug->lines.data(), cpu_span.size_bytes());

        graphics::Set_Pipeline_State(gr, game->physics_debug_pso);
        gr->cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        gr->cmdlist->SetGraphicsRootConstantBufferView(0, glob_buffer_addr);
        gr->cmdlist->SetGraphicsRootShaderResourceView(1, gpu_addr);
        gr->cmdlist->DrawInstanced((U32)px->debug->lines.size(), 1, 0, 0);
        px->debug->lines.clear();
    }

    library::Draw_Gui(&game->gui, gr);

    const auto [back_buffer, back_buffer_rtv] = graphics::Get_Back_Buffer(gr);

    graphics::Add_Transition_Barrier(gr, back_buffer, D3D12_RESOURCE_STATE_COPY_DEST);
    graphics::Add_Transition_Barrier(gr, game->srgb_texture, D3D12_RESOURCE_STATE_COPY_SOURCE);
    graphics::Flush_Resource_Barriers(gr);
    gr->cmdlist->CopyResource(
        graphics::Get_Resource(gr, back_buffer),
        graphics::Get_Resource(gr, game->srgb_texture)
    );
    graphics::Add_Transition_Barrier(gr, back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
    graphics::Flush_Resource_Barriers(gr);

    graphics::Begin_Draw_2D(gr);
    {
        WCHAR text[128];
        const S32 len = swprintf(
            text,
            eastl::size(text),
            L"FPS: %.1f\nFrame time: %.3f ms",
            game->frame_stats.fps,
            game->frame_stats.average_cpu_time
        );
        assert(len > 0);

        gr->d2d.context->DrawText(
            text,
            len,
            game->hud.text_format,
            D2D1_RECT_F{
                .left = 4.0f,
                .top = 4.0f,
                .right = (F32)gr->viewport_width,
                .bottom = (F32)gr->viewport_height,
            },
            game->hud.brush
        );
    }
    graphics::End_Draw_2D(gr);

    graphics::End_Frame(gr);
}

export bool Run() {
    SetProcessDPIAware();

    GAME game = {};
    if (!Init_Game(&game)) {
        return false;
    }

    for (;;) {
        MSG message = {};
        if (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
            DispatchMessage(&message);
            if (message.message == WM_QUIT) {
                break;
            }
        } else {
            Update_Game(&game);
        }
    }

    Deinit_Game(&game);
    return true;
}

} // namespace game
