struct VSInput {
    float2 inPosition;
    float3 inColor;
    float2 inTexCoord;
};

struct UniformBuffer {
    float4x4 camModel;
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

Sampler2D watTexture;
Sampler2D camTexture;

struct VSOutput {
    float4 pos : SV_Position;
    bool isCam;
    float3 color;
    float2 fragTexCoord;
};

[shader("vertex")]
VSOutput vertMain(VSInput input, uint vertexID: SV_VertexID) {
    VSOutput output;
    if (vertexID < 4) {
        output.pos = mul(ubo.camModel, float4(input.inPosition, 0.0, 1.0));
        output.isCam = true;
    } else {
        output.pos = float4(input.inPosition, 0.0, 1.0);
        // output.pos =
        //     mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 0.0, 1.0))));
        output.isCam = false;
    }
    output.color = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_Target {
    if (vertIn.isCam) {
        return camTexture.Sample(vertIn.fragTexCoord);
    }
    return watTexture.Sample(vertIn.fragTexCoord);
}
