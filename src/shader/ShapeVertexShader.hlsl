#include"ShapeHeader.hlsli"

VSOutput main(float4 pos : POSITION, float4 normal : NORMAL, uint instanceId : SV_InstanceId)
{
	VSOutput output;
	output.position = mul(cameraData.viewProj, mul(boxData[instanceId].transform, pos));
	output.normal = normal;
	output.instanceId = instanceId;

	return output;
}