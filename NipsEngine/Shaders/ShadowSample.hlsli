// Shadow Sampling 함수 모음

// Poisson Disk Distribution에 의한 16개의 샘플 오프셋
static const float2 PoissonDisk[16] =
{
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
        float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760),
        float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
        float2(-0.38440307, 0.95628987), float2(0.20334582, -0.66986957),
        float2(0.11558417, 0.82333505), float2(0.18510671, 0.47451193),
        float2(-0.71677703, 0.13222270), float2(-0.23241232, -0.01105474),
        float2(0.47545300, 0.79539304), float2(0.51189273, -0.24621427),
        float2(0.67251712, 0.42839893), float2(0.70830405, -0.82433983)
};

// PCF 
float SampleShadow(float2 ShadowUV, float CurrentDepth, Texture2D<float> ShadowMap, int2 AtlasSize)
{
    float2 TexelSize = 1.0f / (float2) AtlasSize;
    
    float Shadow = 0.0f;

    // 3x3 주변 텍셀을 샘플링하여 평균 계산
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 Offset = float2(x, y) * TexelSize;
            float2 SampleUV = ShadowUV + Offset;
            
            const int2 ShadowTexel = clamp((int2) floor(SampleUV * (float2) AtlasSize), int2(0, 0), AtlasSize - 1);

            float StoredDepth = ShadowMap.Load(int3(ShadowTexel, 0));
            Shadow += (CurrentDepth <= StoredDepth) ? 1.0f : 0.0f;
        }
    }

    return Shadow / 9.0f;
}

// PCF (Poisson Disk Sampling)
float SampleShadowPoissonDisk(float2 ShadowUV, float CurrentDepth, Texture2D<float> ShadowMap, int2 AtlasSize)
{
    float2 TexelSize = 1.0f / (float2) AtlasSize;
    
    float Shadow = 0.0f;
    float Spread = 3.0f;
    
    // Random Rotation: 각 픽셀마다 다른 패턴이 되도록 샘플링 데이터 회전
    // 화이트 노이즈 생성
    float Angle = frac(sin(dot(ShadowUV, float2(12.9898, 78.233))) * 43758.5453) * 6.283185;
    float2x2 RotationMatrix = float2x2(cos(Angle), -sin(Angle), sin(Angle), cos(Angle));
    
    for (int i = 0; i < 16; ++i)
    {
        float2 Offset = mul(PoissonDisk[i], RotationMatrix) * TexelSize * Spread;
        float2 SampleUV = ShadowUV + Offset;
        
        const int2 ShadowTexel = clamp((int2) floor(SampleUV * (float2) AtlasSize), int2(0, 0), AtlasSize - 1);

        float StoredDepth = ShadowMap.Load(int3(ShadowTexel, 0));
        Shadow += (CurrentDepth <= StoredDepth) ? 1.0f : 0.0f;
    }

    return Shadow / 16.0f;
}

// VSM
// TODO: ESM
float SampleShadowVSM(float2 ShadowUV, float CurrentDepth, Texture2D<float2> ShadowMapVSM, int2 AtlasSize)
{
    float2 TexelSize = 1.0f / (float2) AtlasSize;
    
    float2 Moments = ShadowMapVSM.Load(int3(ShadowUV * AtlasSize, 0)).xy;
    float d = Moments.x; // depth
    float dSq = Moments.y; // depth^2

    if (CurrentDepth <= d)
        return 1.0f;
    
    // VSM 공식: 페비쇼프의 부등식
    float variance = dSq - (d * d); // 분산
    float epsilon = 0.00002f;
    variance = max(variance, epsilon);
    
    float probability = variance / (variance + (CurrentDepth - d) * (CurrentDepth - d));
    float lerpFactor = 0.98f;
    probability = smoothstep(lerpFactor, 1.0f, probability);
    
    float shadow = probability;

    return shadow;
}

// PCSS
float SampleShadowPCSS(float2 ShadowUV, float CurrentDepth, Texture2D<float> ShadowMap, int2 AtlasSize)
{
    float2 TexelSize = 1.0f / (float2) AtlasSize;

    // 1. Blocker Search (5x5)
    float BlockerSum = 0.0f;
    int BlockerCount = 0;
    for (int bx = -2; bx <= 2; ++bx)
    {
        for (int by = -2; by <= 2; ++by)
        {
            float2 Offset = float2(bx, by) * TexelSize;
            float2 SampleUV = ShadowUV + Offset;
            
            const int2 ShadowTexel = clamp((int2) floor(SampleUV * (float2) AtlasSize), int2(0, 0), AtlasSize - 1);
            
            float StoredDepth = ShadowMap.Load(int3(ShadowTexel, 0));
            if (StoredDepth < CurrentDepth)
            {
                BlockerSum += StoredDepth;
                ++BlockerCount;
            }
        }
    }

    // 완전히 빛 안에 있는 경우
    if (BlockerCount == 0)
        return 1.0f;

    // 2. Penumbra Size 계산
    float AvgBlockerDepth = BlockerSum / (float) BlockerCount;
    float PenumbraWidth = (CurrentDepth - AvgBlockerDepth) / AvgBlockerDepth;
    float FilterRadius = max(PenumbraWidth * 1000.0f, 1.0f); // 3.0f: LightSize 근사값

    // 3. PCF (Poisson Disk Sampling)  
    float Angle = frac(sin(dot(ShadowUV, float2(12.9898, 78.233))) * 43758.5453) * 6.283185;
    float2x2 RotationMatrix = float2x2(cos(Angle), -sin(Angle), sin(Angle), cos(Angle));

    float Shadow = 0.0f;
    for (int i = 0; i < 16; ++i)
    {
        float2 Offset = mul(PoissonDisk[i], RotationMatrix) * TexelSize * FilterRadius;
        float2 SampleUV = ShadowUV + Offset;
        
        const int2 ShadowTexel = clamp((int2) floor(SampleUV * (float2) AtlasSize), int2(0, 0), AtlasSize - 1);

        float StoredDepth = ShadowMap.Load(int3(ShadowTexel, 0));
        Shadow += (CurrentDepth <= StoredDepth) ? 1.0f : 0.0f;
    }
    return Shadow / 16.0f;
}
