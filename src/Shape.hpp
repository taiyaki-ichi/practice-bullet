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

class ShapeResource
{
	dx12w::resource_and_state vertexResource{};
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	UINT vertexNum{};

	dx12w::resource_and_state shapeDataConstantBuffer{};

	dx12w::descriptor_heap descriptorHeapCBVSRVUAV{};

	UINT shapeNum{};

public:
	ShapeResource(ID3D12Device*, char const* fileName, ID3D12Resource* cameraDataResource);
	virtual ~ShapeResource() = default;
	ShapeResource(ShapeResource&) = delete;
	ShapeResource& operator=(ShapeResource const&) = delete;
	ShapeResource(ShapeResource&&) = default;
	ShapeResource& operator=(ShapeResource&&) = default;

	template<typename Iter>
	void setShapeData(Iter first, Iter last);

	D3D12_VERTEX_BUFFER_VIEW const& getVertexBufferView() noexcept;
	UINT getVertexNum() const noexcept;
	dx12w::descriptor_heap& getDescriptorHeap() noexcept;
	UINT getShapeNum() const noexcept;
};

class ShapePipeline
{
	std::vector<std::uint8_t> vertexShader{};
	std::vector<std::uint8_t> pixelShader{};

	dx12w::release_unique_ptr<ID3D12RootSignature> rootSignature{};
	dx12w::release_unique_ptr<ID3D12PipelineState> graphicsPipeline{};

	UINT boxNum{};

public:
	ShapePipeline(ID3D12Device*, DXGI_FORMAT rendererFormat);
	virtual ~ShapePipeline() = default;
	ShapePipeline(ShapePipeline&) = delete;
	ShapePipeline& operator=(ShapePipeline const&) = delete;
	ShapePipeline(ShapePipeline&&) = default;
	ShapePipeline& operator=(ShapePipeline&&) = default;

	void draw(ID3D12GraphicsCommandList*, ShapeResource& shapeResource);
};


//
// 以下、実装
//


inline ShapeResource::ShapeResource(ID3D12Device* device, char const* fileName, ID3D12Resource* cameraDataResource)
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

	// 定数バッファ
	{
		shapeDataConstantBuffer = dx12w::create_commited_upload_buffer_resource(device, dx12w::alignment<UINT64>(sizeof(ShapeData), 256));
	}

	// ディスクリプタヒープ
	{
		descriptorHeapCBVSRVUAV.initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

		dx12w::create_CBV(device, descriptorHeapCBVSRVUAV.get_CPU_handle(0), cameraDataResource, dx12w::alignment<UINT64>(sizeof(CameraData), 256));
		dx12w::create_CBV(device, descriptorHeapCBVSRVUAV.get_CPU_handle(1), shapeDataConstantBuffer.first.get(), dx12w::alignment<UINT64>(sizeof(ShapeData), 256));
	}
}

template<typename Iter>
inline void ShapeResource::setShapeData(Iter first, Iter last)
{
	ShapeData* shapeDataPtr = nullptr;
	shapeDataConstantBuffer.first->Map(0, nullptr, reinterpret_cast<void**>(&shapeDataPtr));

	std::copy(first, last, shapeDataPtr);
	shapeNum = std::distance(first, last);
}

inline D3D12_VERTEX_BUFFER_VIEW const& ShapeResource::getVertexBufferView() noexcept
{
	return vertexBufferView;
}

inline UINT ShapeResource::getVertexNum() const noexcept
{
	return vertexNum;
}

inline dx12w::descriptor_heap& ShapeResource::getDescriptorHeap() noexcept
{
	return descriptorHeapCBVSRVUAV;
}

inline UINT ShapeResource::getShapeNum() const noexcept
{
	return shapeNum;
}


inline ShapePipeline::ShapePipeline(ID3D12Device* device, DXGI_FORMAT rendererFormat)
{

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

inline void ShapePipeline::draw(ID3D12GraphicsCommandList* list, ShapeResource& shapeResource)
{
	list->SetGraphicsRootSignature(rootSignature.get());
	list->SetPipelineState(graphicsPipeline.get());
	list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	{
		auto tmp = shapeResource.getDescriptorHeap().get();
		list->SetDescriptorHeaps(1, &tmp);
	}
	list->SetGraphicsRootDescriptorTable(0, shapeResource.getDescriptorHeap().get_GPU_handle(0));

	list->IASetVertexBuffers(0, 1, &shapeResource.getVertexBufferView());
	list->DrawInstanced(shapeResource.getVertexNum(), shapeResource.getShapeNum(), 0, 0);
}