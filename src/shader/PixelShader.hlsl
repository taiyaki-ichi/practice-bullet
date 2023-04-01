#include"Header.hlsli"

float4 main(VSOutput input) : SV_TARGET
{
	float3 lightDir = float3(1.f,1.f,-1.f);

	float diffuseBias = saturate(dot(lightDir, input.normal.xyz));
	float3 diffuse = float3(0.8f, 0.8f, 0.8f) * diffuseBias;
	float3 ambient = float3(0.2f, 0.2f, 0.2f);
	return float4(ambient + diffuse, 1.f);
}