struct VSOut
{
    noperspective float4 Position : SV_Position;
    noperspective float2 UV : TEXCOORD;
};

VSOut FullScreenTriangleVS(in uint VertexIdx : SV_VertexID)
{
    VSOut output;
    // TODO: フルスクリーンの三角形のコードを書く。
    output.Position = float4(0, 0, 0, 1);

    return output;
}