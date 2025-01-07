struct VSOut
{
    noperspective float4 Position : SV_Position;
    noperspective float2 UV : TEXCOORD;
};

VSOut FullScreenTriangleVS(in uint VertexIdx : SV_VertexID)
{
    VSOut output;
    float2 tex;
    float2 pos;
    if (VertexIdx == 0)
    {
        tex = float2(0.0f, 0.0f);
        pos = float2(-1.0f, 1.0f);
    }
    else if (VertexIdx == 1)
    {
        tex = float2(0.0f, 2.0f);
        pos = float2(-1.0f, -3.0f);
    }
    else if (VertexIdx == 2)
    {
        tex = float2(2.0f, 0.0f);
        pos = float2(3.0f, 1.0f);
    }

    output.Position = float4(pos, 0.0f, 1.0f);
    output.UV = tex;

    return output;
}