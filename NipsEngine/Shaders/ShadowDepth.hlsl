cbuffer ShadowConstants : register(b8)
{
    row_major float4x4 LightViewProj; // Light 시점의 VP 행렬
    row_major float4x4 Model;         // Object World 행렬
};

struct VSOutput
{
    float4 PosH : SV_Position;
    float4 PosW : TEXCOORD0; // w 나누기 전 클립 공간 좌표
};

VSOutput VS(float3 pos : POSITION)
{
    VSOutput output;
    float4 worldPos = mul(float4(pos, 1.0f), Model);
    output.PosH = mul(worldPos, LightViewProj);
    output.PosW = output.PosH; 
    return output;
}

float2 PS(VSOutput Input) : SV_Target0
{
    // PS에서 나누기
    float d = Input.PosW.z / Input.PosW.w;
    d = saturate(d);
    return float2(d, d * d);
}