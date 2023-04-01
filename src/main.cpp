#include"../external/directx12-wrapper/dx12w/dx12w.hpp"
#include"../external/imgui/imgui.h"
#include"../external/imgui/imgui_impl_dx12.h"
#include"../external/imgui/imgui_impl_win32.h"

constexpr std::size_t WINDOW_WIDTH = 960;
constexpr std::size_t WINDOW_HEIGHT = 640;

constexpr DXGI_FORMAT FRAME_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr std::size_t FRAME_BUFFER_NUM = 2;

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

	auto frameBufferResource = dx12w::get_frame_buffer_resource<FRAME_BUFFER_NUM>(swapChain.get());


	// フレームバッファのビューを作成する用のデスクリプタヒープ
	dx12w::descriptor_heap frameBufferDescriptorHeapRTV{};
	{
		frameBufferDescriptorHeapRTV.initialize(device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FRAME_BUFFER_NUM);

		for (std::size_t i = 0; i < FRAME_BUFFER_NUM; i++)
			dx12w::create_texture2D_RTV(device.get(), frameBufferDescriptorHeapRTV.get_CPU_handle(i), frameBufferResource[i].first.get(), FRAME_BUFFER_FORMAT, 0, 0);
	}

	// imgui用のディスクリプタヒープ
	auto imguiDescriptorHeap = dx12w::create_descriptor_heap(device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);


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
		//
		//

		auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();

		commandManager->reset_list(0);

		dx12w::resource_barrior(commandManager->get_list(), frameBufferResource[backBufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandManager->get_list()->ClearRenderTargetView(frameBufferDescriptorHeapRTV.get_CPU_handle(backBufferIndex), grayClearValue.Color, 0, nullptr);

		auto frame_buffer_cpu_handle = frameBufferDescriptorHeapRTV.get_CPU_handle(backBufferIndex);
		commandManager->get_list()->OMSetRenderTargets(1, &frame_buffer_cpu_handle, false, nullptr);

		//
		// Imguiの描画
		//
		auto imguiDescriptorHeapPtr = imguiDescriptorHeap.get();
		commandManager->get_list()->SetDescriptorHeaps(1, &imguiDescriptorHeapPtr);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandManager->get_list());

		dx12w::resource_barrior(commandManager->get_list(), frameBufferResource[backBufferIndex], D3D12_RESOURCE_STATE_COMMON);

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