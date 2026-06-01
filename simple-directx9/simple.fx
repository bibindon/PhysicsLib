float4x4 g_matWorldViewProj;
float4 g_lightNormal = { 0.3f, 1.0f, 0.5f, 0.0f };
float4 g_meshColor = { 1.0f, 1.0f, 1.0f, 1.0f };
float g_useTexture = 1.0f;

texture texture1;
sampler textureSampler = sampler_state {
    Texture = (texture1);
    MipFilter = LINEAR;
    MinFilter = LINEAR;
    MagFilter = LINEAR;
};

void VertexShader1(in  float4 inPosition  : POSITION,
                   in  float4 inNormal    : NORMAL0,
                   in  float4 inTexCood   : TEXCOORD0,

                   out float4 outPosition : POSITION,
                   out float4 outDiffuse  : COLOR0,
                   out float4 outTexCood  : TEXCOORD0)
{
    outPosition = mul(inPosition, g_matWorldViewProj);

    float lightIntensity = dot(inNormal, g_lightNormal);
    outDiffuse.rgb = max(0, lightIntensity);
    outDiffuse.rgb += 0.3f;
    outDiffuse.a = 1.0f;

    outTexCood = inTexCood;
}

void VertexShaderInstanced(in  float4 inPosition    : POSITION,
                           in  float4 inNormal      : NORMAL0,
                           in  float4 inTexCood     : TEXCOORD0,
                           in  float4 inMatrixRow0  : TEXCOORD1,
                           in  float4 inMatrixRow1  : TEXCOORD2,
                           in  float4 inMatrixRow2  : TEXCOORD3,
                           in  float4 inMatrixRow3  : TEXCOORD4,

                           out float4 outPosition   : POSITION,
                           out float4 outDiffuse    : COLOR0,
                           out float4 outTexCood    : TEXCOORD0)
{
    outPosition.x = dot(inPosition, float4(inMatrixRow0.x, inMatrixRow1.x, inMatrixRow2.x, inMatrixRow3.x));
    outPosition.y = dot(inPosition, float4(inMatrixRow0.y, inMatrixRow1.y, inMatrixRow2.y, inMatrixRow3.y));
    outPosition.z = dot(inPosition, float4(inMatrixRow0.z, inMatrixRow1.z, inMatrixRow2.z, inMatrixRow3.z));
    outPosition.w = dot(inPosition, float4(inMatrixRow0.w, inMatrixRow1.w, inMatrixRow2.w, inMatrixRow3.w));

    float lightIntensity = dot(inNormal, g_lightNormal);
    outDiffuse.rgb = max(0, lightIntensity);
    outDiffuse.rgb += 0.3f;
    outDiffuse.a = 1.0f;

    outTexCood = inTexCood;
}

void PixelShader1(in float4 inScreenColor : COLOR0,
                  in float2 inTexCood     : TEXCOORD0,

                  out float4 outColor     : COLOR)
{
    float4 workColor = g_meshColor;

    if (g_useTexture > 0.5f)
    {
        workColor = tex2D(textureSampler, inTexCood);
    }

    outColor = inScreenColor * workColor;
}

technique Technique1
{
   pass Pass1
   {
      VertexShader = compile vs_2_0 VertexShader1();
      PixelShader = compile ps_2_0 PixelShader1();
   }
}

technique TechniqueInstanced
{
   pass Pass1
   {
      VertexShader = compile vs_3_0 VertexShaderInstanced();
      PixelShader = compile ps_2_0 PixelShader1();
   }
}
