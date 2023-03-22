#include"../external/directx12-wrapper/dx12w/dx12w.hpp"

constexpr std::size_t WINDOW_WIDTH = 960;
constexpr std::size_t WINDOW_HEIGHT = 640;

constexpr DXGI_FORMAT FRAME_BUFFER_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr std::size_t FRAME_BUFFER_NUM = 2;

int main()
{
	auto hwnd = dx12w::create_window(L"practice-bullet", WINDOW_WIDTH, WINDOW_HEIGHT);

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

	while (dx12w::update_window())
	{
		auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();

		commandManager->reset_list(0);

		dx12w::resource_barrior(commandManager->get_list(), frameBufferResource[backBufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandManager->get_list()->ClearRenderTargetView(frameBufferDescriptorHeapRTV.get_CPU_handle(backBufferIndex), grayClearValue.Color, 0, nullptr);
		dx12w::resource_barrior(commandManager->get_list(), frameBufferResource[backBufferIndex], D3D12_RESOURCE_STATE_COMMON);

		commandManager->get_list()->Close();
		commandManager->excute();
		commandManager->signal();

		commandManager->wait(0);

		swapChain->Present(1, 0);

	}

	return 0;
}