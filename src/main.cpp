#include"../external/directx12-wrapper/dx12w/dx12w.hpp"
#include"../external/imgui/imgui.h"
#include"../external/imgui/imgui_impl_dx12.h"
#include"../external/imgui/imgui_impl_win32.h"
#include"../external/bullet3/src/btBulletCollisionCommon.h"
#include"../external/bullet3/src/btBulletDynamicsCommon.h"
#include<DirectXMath.h>
#include<fstream>
#include<chrono>
#include"obj_loader.hpp"
#include"Shape.hpp"
#include"DebugDraw.hpp"

#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

using namespace DirectX;

constexpr std::size_t WINDOW_WIDTH = 960;
constexpr std::size_t WINDOW_HEIGHT = 640;

constexpr DXGI_FORMAT FRAME_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr std::size_t FRAME_BUFFER_NUM = 2;

constexpr DXGI_FORMAT DEPTH_BUFFER_FORMAT = DXGI_FORMAT_D32_FLOAT;


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
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);


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

	auto frameBufferDescriptorHeapDSV = dx12w::create_descriptor_heap(device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
	dx12w::create_texture2D_DSV(device.get(), frameBufferDescriptorHeapDSV.get_CPU_handle(0), depthBuffer.first.get(), DEPTH_BUFFER_FORMAT, 0);

	// imgui用のディスクリプタヒープ
	auto imguiDescriptorHeap = dx12w::create_descriptor_heap(device.get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);


	//
	// Shape
	//

	auto box = std::make_unique<ShapeResource>(device.get(), "data/box.obj", cameraDataResource.first.get());
	auto sphere = std::make_unique<ShapeResource>(device.get(), "data/sphere.obj", cameraDataResource.first.get());
	auto capsule = std::make_unique<ShapeResource>(device.get(), "data/capsule.obj", cameraDataResource.first.get());

	auto shapePipeline = std::make_unique<ShapePipeline>(device.get(), FRAME_BUFFER_FORMAT);

	DebugDraw debugDraw{};
	debugDraw.setDebugMode(btIDebugDraw::DBG_DrawWireframe
		| btIDebugDraw::DBG_DrawContactPoints
		| btIDebugDraw::DBG_DrawConstraints);


	//
	// Bullet
	//

	///collision configuration contains default setup for memory, collision setup. Advanced users can create their own configuration.
	btDefaultCollisionConfiguration* collisionConfiguration = new btDefaultCollisionConfiguration();

	///use the default collision dispatcher. For parallel processing you can use a diffent dispatcher (see Extras/BulletMultiThreaded)
	btCollisionDispatcher* dispatcher = new btCollisionDispatcher(collisionConfiguration);

	///btDbvtBroadphase is a good general purpose broadphase. You can also try out btAxis3Sweep.
	btBroadphaseInterface* overlappingPairCache = new btDbvtBroadphase();

	///the default constraint solver. For parallel processing you can use a different solver (see Extras/BulletMultiThreaded)
	btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver;

	btDiscreteDynamicsWorld* dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, overlappingPairCache, solver, collisionConfiguration);

	dynamicsWorld->setGravity(btVector3(0, -9.8, 0));

	///-----initialization_end-----


	//keep track of the shapes, we release memory at exit.
	//make sure to re-use collision shapes among rigid bodies whenever possible!
	btAlignedObjectArray<btCollisionShape*> collisionShapes;

	///create a few basic rigid bodies

	//the ground is a cube of side 100 at position y = -56.
	//the sphere will hit it at y = -6, with center at -5
	{
		btCollisionShape* groundShape = new btBoxShape(btVector3(btScalar(50.), btScalar(50.), btScalar(50.)));
		collisionShapes.push_back(groundShape);

		btTransform groundTransform;
		groundTransform.setIdentity();
		groundTransform.setOrigin(btVector3(0, -56, 0));

		btScalar mass(0.);
		btVector3 localInertia(0, 0, 0);

		//using motionstate is optional, it provides interpolation capabilities, and only synchronizes 'active' objects
		btDefaultMotionState* myMotionState = new btDefaultMotionState(groundTransform);
		btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, groundShape, localInertia);
		btRigidBody* body = new btRigidBody(rbInfo);


		//add the body to the dynamics world
		dynamicsWorld->addRigidBody(body);
	}

	btRigidBody* fixBox;
	float fixBoxX = -2.f;
	float fixBoxY = 10.f;
	float fixBoxZ = 0.f;
	{
		btCollisionShape* groundShape = new btBoxShape(btVector3(btScalar(1.), btScalar(1.), btScalar(1.)));
		collisionShapes.push_back(groundShape);

		btTransform groundTransform;
		groundTransform.setIdentity();
		groundTransform.setOrigin(btVector3(fixBoxX, fixBoxY, fixBoxZ));

		btScalar mass(0.);
		btVector3 localInertia(0, 0, 0);

		//using motionstate is optional, it provides interpolation capabilities, and only synchronizes 'active' objects
		btDefaultMotionState* myMotionState = new btDefaultMotionState(groundTransform);
		btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, groundShape, localInertia);
		fixBox = new btRigidBody(rbInfo);


		//add the body to the dynamics world
		dynamicsWorld->addRigidBody(fixBox);
	}

	btRigidBody* body1;
	btRigidBody* body2;
	btRigidBody* body3;
	{
		//create a dynamic rigidbody

		//btCollisionShape* colShape = new btBoxShape(btVector3(1,1,1));
		// btCollisionShape* colShape = new btSphereShape(btScalar(1.));
		btCollisionShape* colShape = new btCapsuleShape(btScalar(1.), btScalar(4.));
		collisionShapes.push_back(colShape);

		/// Create Dynamic Objects
		btTransform startTransform;
		startTransform.setIdentity();
		startTransform.setRotation(btQuaternion(0.f, 0.f, 0.5f, 1.f));

		btScalar mass(1.f);
		btVector3 localInertia(0, 0, 0);
		colShape->calculateLocalInertia(mass, localInertia);

		{
			startTransform.setOrigin(btVector3(2, 18, 0));

			//using motionstate is recommended, it provides interpolation capabilities, and only synchronizes 'active' objects
			btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
			btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, colShape, localInertia);
			rbInfo.m_restitution = 1.f;
			body1 = new btRigidBody(rbInfo);

			dynamicsWorld->addRigidBody(body1);
		}

		{
			startTransform.setOrigin(btVector3(2, 10, 0));

			//using motionstate is recommended, it provides interpolation capabilities, and only synchronizes 'active' objects
			btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
			btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, colShape, localInertia);
			rbInfo.m_restitution = 1.f;
			body2 = new btRigidBody(rbInfo);

			dynamicsWorld->addRigidBody(body2);
		}

		{
			startTransform.setOrigin(btVector3(2, 2, 0));

			//using motionstate is recommended, it provides interpolation capabilities, and only synchronizes 'active' objects
			btDefaultMotionState* myMotionState = new btDefaultMotionState(startTransform);
			btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, colShape, localInertia);
			rbInfo.m_restitution = 1.f;
			body3 = new btRigidBody(rbInfo);

			dynamicsWorld->addRigidBody(body3);
		}

		{
			btTransform frameInA, frameInB;
			frameInA = btTransform::getIdentity();
			frameInA.setOrigin(btVector3(btScalar(0.), btScalar(-3), btScalar(0.)));
			frameInB = btTransform::getIdentity();
			frameInB.setOrigin(btVector3(btScalar(0.), btScalar(3), btScalar(0.)));

			btGeneric6DofSpringConstraint* pGen6DOFSpring = new btGeneric6DofSpringConstraint(*body1, *body2, frameInA, frameInB, true);
			pGen6DOFSpring->setLinearUpperLimit(btVector3(0., 1., 0.));
			pGen6DOFSpring->setLinearLowerLimit(btVector3(0., -1., 0.));

			pGen6DOFSpring->setAngularLowerLimit(btVector3(0.f, 0.f, -1.5f));
			pGen6DOFSpring->setAngularUpperLimit(btVector3(0.f, 0.f, 1.5f));

			dynamicsWorld->addConstraint(pGen6DOFSpring, true);
			pGen6DOFSpring->setDbgDrawSize(btScalar(5.f));

			pGen6DOFSpring->enableSpring(0, true);
			pGen6DOFSpring->setStiffness(0, 39.478f);
			pGen6DOFSpring->setDamping(0, 0.5f);
			pGen6DOFSpring->setEquilibriumPoint();
		}

		{
			btTransform frameInA, frameInB;
			frameInA = btTransform::getIdentity();
			frameInA.setOrigin(btVector3(btScalar(0.), btScalar(-1), btScalar(0.)));
			frameInB = btTransform::getIdentity();
			frameInB.setOrigin(btVector3(btScalar(0.), btScalar(1), btScalar(0.)));

			btGeneric6DofSpringConstraint* pGen6DOFSpring = new btGeneric6DofSpringConstraint(*body2, *body3, frameInA, frameInB, true);
			pGen6DOFSpring->setLinearUpperLimit(btVector3(0., 1., 0.));
			pGen6DOFSpring->setLinearLowerLimit(btVector3(0., -1., 0.));

			pGen6DOFSpring->setAngularLowerLimit(btVector3(0.f, 0.f, -1.5f));
			pGen6DOFSpring->setAngularUpperLimit(btVector3(0.f, 0.f, 1.5f));

			dynamicsWorld->addConstraint(pGen6DOFSpring, true);
			pGen6DOFSpring->setDbgDrawSize(btScalar(5.f));

			pGen6DOFSpring->enableSpring(0, true);
			pGen6DOFSpring->setStiffness(0, 39.478f);
			pGen6DOFSpring->enableSpring(1, true);
			pGen6DOFSpring->setStiffness(1, 39.478f);
			pGen6DOFSpring->enableSpring(2, true);
			pGen6DOFSpring->setStiffness(2, 39.478f);
			pGen6DOFSpring->enableSpring(3, true);
			pGen6DOFSpring->setStiffness(3, 39.478f);
			pGen6DOFSpring->enableSpring(4, true);
			pGen6DOFSpring->setStiffness(4, 39.478f);
			pGen6DOFSpring->enableSpring(5, true);
			pGen6DOFSpring->setStiffness(5, 39.478f);

			pGen6DOFSpring->setDamping(0, 0.5f);
			pGen6DOFSpring->setEquilibriumPoint();
		}

		{
			btTransform frameInA, frameInB;
			frameInA = btTransform::getIdentity();
			frameInA.setOrigin(btVector3(btScalar(0.), btScalar(-3), btScalar(0.)));
			frameInB = btTransform::getIdentity();
			frameInB.setOrigin(btVector3(btScalar(0.), btScalar(-3), btScalar(0.)));

			btGeneric6DofSpringConstraint* pGen6DOFSpring = new btGeneric6DofSpringConstraint(*body3, *fixBox, frameInA, frameInB, true);
			pGen6DOFSpring->setLinearUpperLimit(btVector3(0., 1., 0.));
			pGen6DOFSpring->setLinearLowerLimit(btVector3(0., -1., 0.));

			//pGen6DOFSpring->setAngularLowerLimit(btVector3(0.f, 0.f, -1.5f));
			//pGen6DOFSpring->setAngularUpperLimit(btVector3(0.f, 0.f, 1.5f));

			dynamicsWorld->addConstraint(pGen6DOFSpring, true);
			pGen6DOFSpring->setDbgDrawSize(btScalar(5.f));

			pGen6DOFSpring->enableSpring(0, true);
			pGen6DOFSpring->setStiffness(0, 39.478f);
			pGen6DOFSpring->setDamping(0, 0.5f);
			pGen6DOFSpring->setEquilibriumPoint();
		}
	}

	// デバック用のインスタンス割当て
	dynamicsWorld->setDebugDrawer(&debugDraw);


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

	XMFLOAT3 eye{ 0.f,0.f,-20.f };
	XMFLOAT3 target{ 0.f,0.f,0.f };
	XMFLOAT3 up{ 0,1,0 };
	float asspect = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
	float viewAngle = XM_PIDIV2;
	float cameraNearZ = 0.01f;
	float cameraFarZ = 1000.f;

	auto prevTime = std::chrono::system_clock::now();

	std::array<float, 3> power{ 0.f,2.f,0.f };

	//
	// メインループ
	//

	while (dx12w::update_window())
	{
		//
		// 物理エンジンのシュミレーション
		//

		auto deltaTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - prevTime).count();
		if (deltaTime >= 1.f / 60.f * 1000.f)
		{
			dynamicsWorld->stepSimulation(deltaTime, 10);
			prevTime = std::chrono::system_clock::now();
		}

		//
		// 物理エンジンの結果を描画するために準備
		//

		debugDraw.sphereData.clear();
		debugDraw.boxData.clear();
		debugDraw.capsuleData.clear();

		dynamicsWorld->debugDrawWorld();

		sphere->setShapeData(debugDraw.sphereData.begin(), debugDraw.sphereData.end());
		box->setShapeData(debugDraw.boxData.begin(), debugDraw.boxData.end());
		capsule->setShapeData(debugDraw.capsuleData.begin(), debugDraw.capsuleData.end());

		//
		// ImGUIの準備
		//

		// Start the Dear ImGui frame
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		ImGui::InputFloat3("eye", &eye.x);
		ImGui::InputFloat3("target", &target.x);

		ImGui::InputFloat3("impulse power", &power[0]);

		if (ImGui::Button("Impulse!")) {
			body1->activate(true);
			body1->applyCentralImpulse(btVector3(power[0], power[1], power[2]));
		}

		ImGui::SliderFloat("fix box x", &fixBoxX, -10.f, 10.f);
		ImGui::SliderFloat("fix box y", &fixBoxY, -10.f, 10.f);
		ImGui::SliderFloat("fix box z", &fixBoxZ, -10.f, 10.f);

		{
			btTransform groundTransform;
			groundTransform.setIdentity();
			groundTransform.setOrigin(btVector3(fixBoxX, fixBoxY, fixBoxZ));

			body3->activate(true);
			fixBox->setWorldTransform(groundTransform);
		}

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
		// 描画
		//

		shapePipeline->draw(commandManager->get_list(), *box);
		shapePipeline->draw(commandManager->get_list(), *sphere);
		shapePipeline->draw(commandManager->get_list(), *capsule);

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


	_CrtDumpMemoryLeaks();

	return 0;
}