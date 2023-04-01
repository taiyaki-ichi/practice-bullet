#include"../external/directx12-wrapper/dx12w/dx12w.hpp"
#include"../external/imgui/imgui.h"
#include"../external/imgui/imgui_impl_dx12.h"
#include"../external/imgui/imgui_impl_win32.h"
#include<DirectXMath.h>
#include<fstream>
#include"obj_loader.hpp"


using namespace DirectX;

constexpr std::size_t WINDOW_WIDTH = 960;
constexpr std::size_t WINDOW_HEIGHT = 640;

constexpr DXGI_FORMAT FRAME_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr std::size_t FRAME_BUFFER_NUM = 2;

constexpr DXGI_FORMAT DEPTH_BUFFER_FORMAT = DXGI_FORMAT_D32_FLOAT;


struct CameraData
{
	XMMATRIX viewProj{};
};



extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// Imgui用の処理
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	if (msg == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int main()
{
	auto hwnd = dx12w::create_window(L"practice-bullet", WINDOW_WIDTH, WINDOW_HEIGHT, wnd_proc);

	auto device = dx12w::create_device();

	auto commandManager = std::make_unique<dx12w::command_manager<1>>(dx12w::create_command_manager<1>(device.get()));

	auto swapChain = dx12w::create_swap_chain(commandManager->get_queue(), hwnd, FRAME_BUFFER_FORMAT, FRAME_BUFFER_NUM);

	D3D12_CLEAR_VALUE grayClearValue{
		.Format = FRAME_BUFFER_FORMAT,
		.Color = {0.5f,0.5f,0.5f,1.f},
	};

	D3D12_CLEAR_VALUE depthClearValue{
		.Format = DEPTH_BUFFER_FORMAT,
		.DepthStencil{.Depth = 1.f}
	};


	auto frameBufferResource = dx12w::get_frame_buffer_resource<FRAME_BUFFER_NUM>(swapChain.get());

	auto depthBuffer = dx12w::create_commited_texture_resource(device.get(), DEPTH_BUFFER_FORMAT, WINDOW_WIDTH, WINDOW_HEIGHT,
		2, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, &depthClearValue);

	auto cameraDataResource = dx12w::create_commited_upload_buffer_resource(device.get(), dx12w::alignment<UINT64>(sizeof(CameraData), 256));


	// 頂点シェーダ
	auto vertexShader = []() {
		std::ifstream shaderFile{ L"shader/VertexShader.cso",std::ios::binary };
		return dx12w::load_blob(shaderFile);
	}();

	// ピクセルシェーダ
	auto indexShader = []() {
		std::ifstream shaderFile{ L"shader/PixelShader.cso",std::ios::binary };
		return dx12w::load_blob(shaderFile);
	}();

	std::ifstream sphereFile{ "data/sphere.obj" };
	auto sphereVertexData = load_obj(sphereFile);

	// 球の頂点データ
	auto sphereVertexResource = [&device, &sphereVertexData]() {
		auto result = dx12w::create_commited_upload_buffer_resource(device.get(), sizeof(decltype(sphereVertexData)::value_type) * sphereVertexData.size());

		float* tmp = nullptr;
		result.first->Map(0, nullptr, reinterpret_cast<void**>(&tmp));
		std::copy(&sphereVertexData[0][0], &sphereVertexData[0][0] + sphereVertexData.size() * 6, tmp);
		result.first->Unmap(0, nullptr);

		return result;
	}();

	// pmxxの頂点バッファのビュー
	D3D12_VERTEX_BUFFER_VIEW sphereVertexBufferView{
		.BufferLocation = sphereVertexResource.first->GetGPUVirtualAddress(),
		.SizeInBytes = static_cast<UINT>(sizeof(decltype(sphereVertexData)::value_type) * sphereVertexData.size()),
		.StrideInBytes = static_cast<UINT>(sizeof(decltype(sphereVertexData)::value_type)),
	};

	//
	// デスクリプタヒープ
	//

	// フレームバッファのビューを作成する用のデスクリプタヒープ
	dx12w::descriptor_heap frameBufferDescriptorHeapRTV{};
	{
		frameBufferDescriptorHeapRTV.initialize(device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FRAME_BUFFER_NUM);

		for (std::size_t i = 0; i < FRAME_BUFFER_NUM; i++)
			dx12w::create_texture2D_RTV(device.get(), frameBufferDescriptorHeapRTV.get_CPU_handle(i), frameBufferResource[i].first.get(), FRAME_BUFFER_FORMAT, 0, 0);
	}

	// フレームバッファに描画する際に使用する
	dx12w::descriptor_heap frameBufferDescriptorHeapCBVSRVUAV{};
	{
		frameBufferDescriptorHeapCBVSRVUAV.initialize(device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

		// カメラのデータ
		dx12w::create_CBV(device.get(), frameBufferDescriptorHeapCBVSRVUAV.get_CPU_handle(0), cameraDataResource.first.get(), dx12w::alignment<UINT64>(sizeof(CameraData), 256));
	}

	auto frameBufferDescriptorHeapDSV = dx12w::create_descriptor_heap(device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
	dx12w::create_texture2D_DSV(device.get(), frameBufferDescriptorHeapDSV.get_CPU_handle(0), depthBuffer.first.get(), DEPTH_BUFFER_FORMAT, 0);

	// imgui用のディスクリプタヒープ
	auto imguiDescriptorHeap = dx12w::create_descriptor_heap(device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);


	//
	// ルートシグネチャ
	//

	// pmxのフレームバッファに描画する際のルートシグネチャ
	auto rootSignature = dx12w::create_root_signature(device.get(), { {{/*カメラデータ*/D3D12_DESCRIPTOR_RANGE_TYPE_CBV}} }, {});


	//
	// グラフィックスパイプライン 
	//

	// pmxをフレームバッファに描画する際のグラフィクスパイプライン
	auto pmx_graphics_pipeline_state = dx12w::create_graphics_pipeline(device.get(), rootSignature.get(),
		{ { "POSITION",DXGI_FORMAT_R32G32B32_FLOAT },{ "NORMAL",DXGI_FORMAT_R32G32B32_FLOAT } },
		{ FRAME_BUFFER_FORMAT }, { {vertexShader.data(),vertexShader.size()},{indexShader.data(),indexShader.size()} },
		true, true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

	//
	// Imguiの設定
	//

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device.get(), 1,
		DXGI_FORMAT_R8G8B8A8_UNORM, imguiDescriptorHeap.get(),
		imguiDescriptorHeap.get_CPU_handle(),
		imguiDescriptorHeap.get_GPU_handle());


	//
	// その他設定
	//

	D3D12_VIEWPORT viewport{
		.TopLeftX = 0.f,
		.TopLeftY = 0.f,
		.Width = static_cast<float>(WINDOW_WIDTH),
		.Height = static_cast<float>(WINDOW_HEIGHT),
		.MinDepth = 0.f,
		.MaxDepth = 1.f,
	};

	D3D12_RECT scissorRect{
		.left = 0,
		.top = 0,
		.right = static_cast<LONG>(WINDOW_WIDTH),
		.bottom = static_cast<LONG>(WINDOW_HEIGHT),
	};

	XMFLOAT3 eye{ 0.f,0.f,-10.f };
	XMFLOAT3 target{ 0.f,0.f,0.f };
	XMFLOAT3 up{ 0,1,0 };
	float asspect = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
	float viewAngle = XM_PIDIV2;
	float cameraNearZ = 0.01f;
	float cameraFarZ = 1000.f;


	//
	// メインループ
	//

	while (dx12w::update_window())
	{

		//
		// ImGUIの準備
		//

		// Start the Dear ImGui frame
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		bool hoge = true;
		ImGui::ShowDemoWindow(&hoge);

		ImGui::Begin("Hello, world!");
		ImGui::Text("This is some useful text.");
		ImGui::End();

		// Rendering
		ImGui::Render();


		//
		// 定数の更新
		//

		{
			CameraData* tmp = nullptr;
			cameraDataResource.first->Map(0, nullptr, reinterpret_cast<void**>(&tmp));

			auto const view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up));;
			auto const proj = XMMatrixPerspectiveFovLH(viewAngle, asspect, cameraNearZ, cameraFarZ);
			tmp->viewProj = view * proj;
		}

		//
		// フレームバッファへの描画の準備
		//

		auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();

		commandManager->reset_list(0);

		dx12w::resource_barrior(commandManager->get_list(), frameBufferResource[backBufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET);
		dx12w::resource_barrior(commandManager->get_list(), depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		commandManager->get_list()->ClearRenderTargetView(frameBufferDescriptorHeapRTV.get_CPU_handle(backBufferIndex), grayClearValue.Color, 0, nullptr);
		commandManager->get_list()->ClearDepthStencilView(frameBufferDescriptorHeapDSV.get_CPU_handle(), D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

		auto rtvCpuHandle = frameBufferDescriptorHeapRTV.get_CPU_handle(backBufferIndex);
		auto dsvCpuHandle = frameBufferDescriptorHeapDSV.get_CPU_handle(0);
		commandManager->get_list()->OMSetRenderTargets(1, &rtvCpuHandle, false, &dsvCpuHandle);

		commandManager->get_list()->RSSetViewports(1, &viewport);
		commandManager->get_list()->RSSetScissorRects(1, &scissorRect);


		//
		// オブジェクトを描画
		//

		commandManager->get_list()->SetGraphicsRootSignature(rootSignature.get());
		commandManager->get_list()->SetPipelineState(pmx_graphics_pipeline_state.get());
		commandManager->get_list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		{
			auto tmp = frameBufferDescriptorHeapCBVSRVUAV.get();
			commandManager->get_list()->SetDescriptorHeaps(1, &tmp);
		}
		commandManager->get_list()->SetGraphicsRootDescriptorTable(0, frameBufferDescriptorHeapCBVSRVUAV.get_GPU_handle(0));

		commandManager->get_list()->IASetVertexBuffers(0, 1, &sphereVertexBufferView);
		commandManager->get_list()->DrawInstanced(sphereVertexData.size(), 1, 0, 0);

		//
		// Imguiの描画
		//
		auto imguiDescriptorHeapPtr = imguiDescriptorHeap.get();
		commandManager->get_list()->SetDescriptorHeaps(1, &imguiDescriptorHeapPtr);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandManager->get_list());


		dx12w::resource_barrior(commandManager->get_list(), frameBufferResource[backBufferIndex], D3D12_RESOURCE_STATE_COMMON);
		dx12w::resource_barrior(commandManager->get_list(), depthBuffer, D3D12_RESOURCE_STATE_COMMON);


		commandManager->get_list()->Close();
		commandManager->excute();
		commandManager->signal();

		commandManager->wait(0);

		swapChain->Present(1, 0);
	}


	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	return 0;
}