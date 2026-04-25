cbuffer ShadowConstants : register(b8)
{
    row_major float4x4 LightViewProj; // Light 시점의 VP 행렬
    row_major float4x4 Model;         // Object World 행렬
};

float4 VS(float3 pos : POSITION) : SV_Position
{
    float4 worldPos = mul(float4(pos, 1.0f), Model);
    return mul(worldPos, LightViewProj);
}