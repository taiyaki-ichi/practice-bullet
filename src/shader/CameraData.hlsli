
struct CameraData
{
	matrix viewProj;
};

cbuffer CameraDataConstantBuffer : register(b0)
{
	CameraData cameraData;
}