
struct CameraData
{
	matrix viewProj;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float4 normal : NORMAL;
};


cbuffer CameraDataConstantBuffer : register(b0)
{
	CameraData cameraData;
}