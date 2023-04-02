#pragma once
#include"../external/directx12-wrapper/dx12w/dx12w.hpp"
#include<DirectXMath.h>
#include<fstream>
#include"obj_loader.hpp"
#include"CameraData.hpp"

constexpr std::size_t MAX_BOX_NUM = 256;

struct ShapeData
{
	DirectX::XMMATRIX transform{};
	std::array<float, 3> color{};
};

class Shape
{
	dx12w::resource_and_state vertexResource{};
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	UINT vertexNum{};

	dx12w::resource_and_state boxDataConstantBuffer{};

	std::vector<std::uint8_t> vertexShader{};
	std::vector<std::uint8_t> pixelShader{};

	dx12w::descriptor_heap descriptorHeapCBVSRVUAV{};

	dx12w::release_unique_ptr<ID3D12RootSignature> rootSignature{};
	dx12w::release_unique_ptr<ID3D12PipelineState> graphicsPipeline{};

	UINT boxNum{};

public:
	Shape(ID3D12Device*, char const* fileName, ID3D12Resource* cameraDataResource, DXGI_FORMAT rendererFormat);
	virtual ~Shape() = default;
	Shape(Shape&) = delete;
	Shape& operator=(Shape const&) = delete;
	Shape(Shape&&) = default;
	Shape& operator=(Shape&&) = default;

	template<typename Iter>
	void setBoxData(Iter first, Iter last);
	void draw(ID3D12GraphicsCommandList*);
};


//
// 以下、定義
//

inline Shape::Shape(ID3D12Device* device, char const* fileName, ID3D12Resource* cameraDataResource, DXGI_FORMAT rendererFormat)
{
	// 頂点データ
	{
		std::ifstream file{ fileName };
		auto vertexData = load_obj(file);

		vertexResource = dx12w::create_commited_upload_buffer_resource(device, sizeof(decltype(vertexData)::value_type) * vertexData.size());

		float* tmp = nullptr;
		vertexResource.first->Map(0, nullptr, reinterpret_cast<void**>(&tmp));
		std::copy(&vertexData[0][0], &vertexData[0][0] + vertexData.size() * 6, tmp);
		vertexResource.first->Unmap(0, nullptr);

		vertexBufferView = {
			.BufferLocation = vertexResource.first->GetGPUVirtualAddress(),
			.SizeInBytes = static_cast<UINT>(sizeof(decltype(vertexData)::value_type) * vertexData.size()),
			.StrideInBytes = static_cast<UINT>(sizeof(decltype(vertexData)::value_type)),
		};

		vertexNum = vertexData.size();
	}

	// 頂点シェーダ
	{
		std::ifstream shaderFile{ L"shader/ShapeVertexShader.cso",std::ios::binary };
		vertexShader = dx12w::load_blob(shaderFile);
	}

	// ピクセルシェーダ
	{
		std::ifstream shaderFile{ L"shader/ShapePixelShader.cso",std::ios::binary };
		pixelShader = dx12w::load_blob(shaderFile);
	}

	// 定数バッファ
	{
		boxDataConstantBuffer = dx12w::create_commited_upload_buffer_resource(device, dx12w::alignment<UINT64>(sizeof(ShapeData), 256));
	}

	// ディスクリプタヒープ
	{
		descriptorHeapCBVSRVUAV.initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

		dx12w::create_CBV(device, descriptorHeapCBVSRVUAV.get_CPU_handle(0), cameraDataResource, dx12w::alignment<UINT64>(sizeof(CameraData), 256));
		dx12w::create_CBV(device, descriptorHeapCBVSRVUAV.get_CPU_handle(1), boxDataConstantBuffer.first.get(), dx12w::alignment<UINT64>(sizeof(ShapeData), 256));
	}

	// ルートシグネチャ
	{
		rootSignature = dx12w::create_root_signature(device, { {{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,2}} }, {});
	}

	// グラフィックスパイプライン
	{
		graphicsPipeline = dx12w::create_graphics_pipeline(device, rootSignature.get(),
			{ { "POSITION",DXGI_FORMAT_R32G32B32_FLOAT },{ "NORMAL",DXGI_FORMAT_R32G32B32_FLOAT } },
			{ rendererFormat }, { {vertexShader.data(),vertexShader.size()},{pixelShader.data(),pixelShader.size()} },
			true, true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	}
}

template<typename Iter>
inline void Shape::setBoxData(Iter first, Iter last)
{
	ShapeData* boxDataPtr = nullptr;
	boxDataConstantBuffer.first->Map(0, nullptr, reinterpret_cast<void**>(&boxDataPtr));

	std::copy(first, last, boxDataPtr);
	boxNum = std::distance(first, last);
}

inline void Shape::draw(ID3D12GraphicsCommandList* list)
{
	list->SetGraphicsRootSignature(rootSignature.get());
	list->SetPipelineState(graphicsPipeline.get());
	list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	{
		auto tmp = descriptorHeapCBVSRVUAV.get();
		list->SetDescriptorHeaps(1, &tmp);
	}
	list->SetGraphicsRootDescriptorTable(0, descriptorHeapCBVSRVUAV.get_GPU_handle(0));

	list->IASetVertexBuffers(0, 1, &vertexBufferView);
	list->DrawInstanced(vertexNum, boxNum, 0, 0);
}