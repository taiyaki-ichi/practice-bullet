
struct CameraData
{
	matrix view;
	matrix proj;
};


cbuffer CameraDataConstantBuffer : register(b0)
{
	CameraData cameraData;
}