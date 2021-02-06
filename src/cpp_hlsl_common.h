struct VERTEX {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT4 tangent;
    XMFLOAT2 uv;
};

struct DEBUG_VERTEX {
    XMFLOAT3 position;
    U32 color;
};

struct DRAW_COMMAND {
    U32 index_offset;
    U32 vertex_offset;
    U32 renderable_id;
};

struct GLOBALS {
    XMFLOAT4X4 world_to_clip;
};

struct RENDERABLE_CONSTANTS {
    XMFLOAT4X4 object_to_world;
};
