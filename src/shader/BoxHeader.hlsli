#include"CameraData.hlsli"

struct VSOutput
{
	float4 position : SV_POSITION;
	float4 normal : NORMAL;
	uint instanceId : INSTANCE_ID;
};

#define MAX_BOX_NUM 256

struct BoxData
{
	matrix transform;
	float3 color;
};

cbuffer BoxDataConstantBuffer : register(b1)
{
	BoxData boxData[MAX_BOX_NUM];
}