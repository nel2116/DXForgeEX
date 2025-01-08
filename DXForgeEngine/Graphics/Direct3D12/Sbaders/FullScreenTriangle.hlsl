struct VSOut
{
    noperspective float4 Position : SV_Position;
    noperspective float2 UV : TEXCOORD;
};

VSOut FullScreenTriangleVS(in uint VertexIdx : SV_VertexID)
{
    VSOut output;
    const float2 tex = float2(uint2(VertexIdx, VertexIdx << 1) & 2);
    output.Position = float4(lerp(float2(-1, 1), float2(1, -1), tex), 0, 1);
    output.UV = tex;

    return output;
}