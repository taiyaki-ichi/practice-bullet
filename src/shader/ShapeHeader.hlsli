
struct CameraData
{
	matrix viewProj;
};

cbuffer CameraDataConstantBuffer : register(b0)
{
	CameraData cameraData;
}

struct VSOutput
{
	float4 position : SV_POSITION;
	float4 normal : NORMAL;
	uint instanceId : INSTANCE_ID;
};

#define MAX_BOX_NUM 256

struct ShapeData
{
	matrix transform;
	float3 color;
};

cbuffer BoxDataConstantBuffer : register(b1)
{
	ShapeData boxData[MAX_BOX_NUM];
}