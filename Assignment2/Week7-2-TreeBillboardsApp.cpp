//***************************************************************************************
// TreeBillboardsApp.cpp 
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTested,
	AlphaTestedTreeSprites,
	Count
};

class TreeBillboardsApp : public D3DApp
{
public:
    TreeBillboardsApp(HINSTANCE hInstance);
    TreeBillboardsApp(const TreeBillboardsApp& rhs) = delete;
    TreeBillboardsApp& operator=(const TreeBillboardsApp& rhs) = delete;
    ~TreeBillboardsApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	//void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt); 

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayouts();

    void BuildLandGeometry();
    void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildGrassWallGeometry();
	void BuildWedgeGeometry();
	void BuildSphereGeometry();
	void BuildCylinderGeometry();
	void BuildConeGeometry();
	void BuildPyramidGeometry();
	void BuildPrismGeometry();


	void BuildTreeSpritesGeometry();
	void BuildDiamondGeometry();

    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

    float GetHillsHeight(float x, float z)const;
    XMFLOAT3 GetHillsNormal(float x, float z)const;

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mStdInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

    RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

    PassConstants mMainPassCB;

	Camera mCamera;
    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        TreeBillboardsApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

TreeBillboardsApp::TreeBillboardsApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

TreeBillboardsApp::~TreeBillboardsApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TreeBillboardsApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mWaves = std::make_unique<Waves>(305, 150, 1.0f, 0.03f, 4.0f, 0.2f);
	mCamera.SetPosition(-0.0f, 40.0f, -100.0f);


	LoadTextures();
    BuildRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayouts();

    BuildLandGeometry();
    BuildWavesGeometry();
	BuildBoxGeometry();
	BuildWedgeGeometry();
	BuildSphereGeometry();
	BuildCylinderGeometry();
	BuildConeGeometry();
	BuildPyramidGeometry();
	BuildDiamondGeometry();
	BuildPrismGeometry();

	BuildGrassWallGeometry();


	BuildTreeSpritesGeometry();


	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}
 
void TreeBillboardsApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void TreeBillboardsApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	//UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
    UpdateWaves(gt);
}

void TreeBillboardsApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mMainPassCB.FogColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TreeBillboardsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void TreeBillboardsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void TreeBillboardsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
		mCamera.Pitch(dy);
		mCamera.RotateY(dx);

        // Restrict the angle mPhi.
    }


    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void TreeBillboardsApp::OnKeyboardInput(const GameTimer& gt)
{
	//step3: we handle keyboard input to move the camera:

	const float dt = gt.DeltaTime();

	//GetAsyncKeyState returns a short (2 bytes)
	if (GetAsyncKeyState('W') & 0x8000) //most significant bit (MSB) is 1 when key is pressed (1000 000 000 000)
		mCamera.Walk(20.0f * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-20.0f * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-20.0f * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(20.0f * dt);

	mCamera.UpdateViewMatrix();
}
 
void TreeBillboardsApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if(tu >= 1.0f)
		tu -= 1.0f;

	if(tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void TreeBillboardsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void TreeBillboardsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void TreeBillboardsApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();

	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.97f, 0.98f, 0.06f, 1.0f };

	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 1.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[0].Position = { 24.0f, 33.0f, -40.5f };


	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 1.0f, 1.0f, 1.0f };
	mMainPassCB.Lights[1].Position = { 24.0f, 33.0f, 40.5f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void TreeBillboardsApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for(int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);
		
		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + (v.Pos.x) / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void TreeBillboardsApp::LoadTextures()
{
	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"../../Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto grasswallTex = std::make_unique<Texture>();
	grasswallTex->Name = "grasswallTex";
	grasswallTex->Filename = L"../../Textures/grasswall.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grasswallTex->Filename.c_str(),
		grasswallTex->Resource, grasswallTex->UploadHeap));


	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"../../Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));


	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"../../Textures/bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto WedgeTex = std::make_unique<Texture>();
	WedgeTex->Name = "bricks2Tex";
	WedgeTex->Filename = L"../../Textures/bricks2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), WedgeTex->Filename.c_str(),
		WedgeTex->Resource, WedgeTex->UploadHeap));

	auto cylinderTex = std::make_unique<Texture>();
	cylinderTex->Name = "bricks3Tex";
	cylinderTex->Filename = L"../../Textures/bricks3.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), cylinderTex->Filename.c_str(),
		cylinderTex->Resource, cylinderTex->UploadHeap));

	auto sphereTex = std::make_unique<Texture>();
	sphereTex->Name = "iceTex";
	sphereTex->Filename = L"../../Textures/ice.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), sphereTex->Filename.c_str(),
		sphereTex->Resource, sphereTex->UploadHeap));

	auto coneTex = std::make_unique<Texture>();
	coneTex->Name = "tileTex";
	coneTex->Filename = L"../../Textures/tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), coneTex->Filename.c_str(),
		coneTex->Resource, coneTex->UploadHeap));

	auto pyramidTex = std::make_unique<Texture>();
	pyramidTex->Name = "sandTex";
	pyramidTex->Filename = L"../../Textures/sand.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), pyramidTex->Filename.c_str(),
		pyramidTex->Resource, pyramidTex->UploadHeap));

	auto PrismTex = std::make_unique<Texture>();
	PrismTex->Name = "checkboardTex";
	PrismTex->Filename = L"../../Textures/checkboard.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), PrismTex->Filename.c_str(),
		PrismTex->Resource, PrismTex->UploadHeap));

	auto DiamondTex = std::make_unique<Texture>();
	DiamondTex->Name = "shinyTex";
	DiamondTex->Filename = L"../../Textures/shiny.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), DiamondTex->Filename.c_str(),
		DiamondTex->Resource, DiamondTex->UploadHeap));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"../../Textures/treeArray.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));

	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[grasswallTex->Name] = std::move(grasswallTex);

	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[bricksTex->Name] = std::move(bricksTex);
	mTextures[WedgeTex->Name] = std::move(WedgeTex);
	mTextures[cylinderTex->Name] = std::move(cylinderTex);
	mTextures[sphereTex->Name] = std::move(sphereTex);
	mTextures[coneTex->Name] = std::move(coneTex);
	mTextures[pyramidTex->Name] = std::move(pyramidTex);
	mTextures[PrismTex->Name] = std::move(PrismTex);
	mTextures[DiamondTex->Name] = std::move(DiamondTex);


	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
}

void TreeBillboardsApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0);
    slotRootParameter[2].InitAsConstantBufferView(1);
    slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void TreeBillboardsApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 12;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = mTextures["grassTex"]->Resource;
	auto grasswallTex = mTextures["grasswallTex"]->Resource;

	auto waterTex = mTextures["waterTex"]->Resource;
	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto bricks2Tex = mTextures["bricks2Tex"]->Resource;
	auto bricks3Tex = mTextures["bricks3Tex"]->Resource;

	auto iceTex = mTextures["iceTex"]->Resource;
	auto tileTex = mTextures["tileTex"]->Resource;
	auto sandTex = mTextures["sandTex"]->Resource;
	auto checkboardTex = mTextures["checkboardTex"]->Resource;
	auto shinyTex = mTextures["shinyTex"]->Resource;

	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);
	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = grasswallTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(grasswallTex.Get(), &srvDesc, hDescriptor);
	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = bricksTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = bricks2Tex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(bricks2Tex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = bricks3Tex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(bricks3Tex.Get(), &srvDesc, hDescriptor);

	//	next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = iceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

	//	next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = tileTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

	//	next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = sandTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(sandTex.Get(), &srvDesc, hDescriptor);

	//	next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = checkboardTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, hDescriptor);

	//	next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = shinyTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(shinyTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);

}

void TreeBillboardsApp::BuildShadersAndInputLayouts()
{
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_1");
	
	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mStdInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void TreeBillboardsApp::BuildLandGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(200.0f, 160.0f, 50, 50);

    //
    // Extract the vertex elements we are interested and apply the height function to
    // each vertex.  In addition, color the vertices based on their height so we have
    // sandy looking beaches, grassy low hills, and snow mountain peaks.
    //
	int j = 0;
	int k = 0;
    std::vector<Vertex> vertices(grid.Vertices.size());
    for(size_t i = 0; i < grid.Vertices.size(); ++i)
    {

        auto& p = grid.Vertices[i].Position;
		vertices[i].Pos.x = -p.z;
		vertices[i].Pos.z = p.x - 55.f;


		if (i > grid.Vertices.size()/16*3 +31  && i < grid.Vertices.size() / 16 *13 -28)
		{

			vertices[i].Pos.y = 0;
		}
		else

			vertices[i].Pos.y= -8;

        vertices[i].Normal = GetHillsNormal(p.x, p.z);
		vertices[i].TexC = grid.Vertices[i].TexC;
    }

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    std::vector<std::uint16_t> indices = grid.GetIndices16();
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildWavesGeometry()
{
    std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

    // Iterate over each quad.
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for(int i = 0; i < m - 1; ++i)
    {
        for(int j = 0; j < n - 1; ++j)
        {
            indices[k] = i*n + j;
            indices[k + 1] = i*n + j + 1;
            indices[k + 2] = (i + 1)*n + j;

            indices[k + 3] = (i + 1)*n + j;
            indices[k + 4] = i*n + j + 1;
            indices[k + 5] = (i + 1)*n + j + 1;

            k += 6; // next quad
        }
    }

	UINT vbByteSize = mWaves->VertexCount()*sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size()*sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildBoxGeometry()
{

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);

	std::vector<Vertex> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		auto& p = box.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["box"] = submesh;

	mGeometries["boxGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildGrassWallGeometry()
{

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.5f, 1.0f, 3);

	std::vector<Vertex> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		auto& p = box.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "grasswallGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grasswall"] = submesh;

	mGeometries["grasswallGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildWedgeGeometry()
{

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f, 3);

	std::vector<Vertex> vertices(wedge.Vertices.size());
	for (size_t i = 0; i < wedge.Vertices.size(); ++i)
	{
		auto& p = wedge.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = wedge.Vertices[i].Normal;
		vertices[i].TexC = wedge.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = wedge.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "wedgeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["wedge"] = submesh;

	mGeometries["wedgeGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildSphereGeometry()
{

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(1.0f, 20, 20);

	std::vector<Vertex> vertices(sphere.Vertices.size());
	for (size_t i = 0; i < sphere.Vertices.size(); ++i)
	{
		auto& p = sphere.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = sphere.Vertices[i].Normal;
		vertices[i].TexC = sphere.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = sphere.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "sphereGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["sphere"] = submesh;

	mGeometries["sphereGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildCylinderGeometry()
{

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(1.5f, 1.5f, 6.0f, 20, 20);

	std::vector<Vertex> vertices(cylinder.Vertices.size());
	for (size_t i = 0; i < cylinder.Vertices.size(); ++i)
	{
		auto& p = cylinder.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = cylinder.Vertices[i].Normal;
		vertices[i].TexC = cylinder.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = cylinder.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "cylinderGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["cylinder"] = submesh;

	mGeometries["cylinderGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildConeGeometry()
{

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData cone = geoGen.CreateCone(2.0f, 0.0f, 6.0f, 20, 20);

	std::vector<Vertex> vertices(cone.Vertices.size());
	for (size_t i = 0; i < cone.Vertices.size(); ++i)
	{
		auto& p = cone.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = cone.Vertices[i].Normal;
		vertices[i].TexC = cone.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = cone.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "coneGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["cone"] = submesh;

	mGeometries["coneGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildPyramidGeometry()
{

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData Pyramid = geoGen.CreatePyramid(1.0f, 0.0f, 1.0f, 4, 20);

	std::vector<Vertex> vertices(Pyramid.Vertices.size());
	for (size_t i = 0; i < Pyramid.Vertices.size(); ++i)
	{
		auto& p = Pyramid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = Pyramid.Vertices[i].Normal;
		vertices[i].TexC = Pyramid.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = Pyramid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "pyramidGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["pyramid"] = submesh;

	mGeometries["pyramidGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildPrismGeometry()
{

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData Prism = geoGen.CreateCylinder(1.0f, 1.0f, 1.0f, 3, 20);

	std::vector<Vertex> vertices(Prism.Vertices.size());
	for (size_t i = 0; i < Prism.Vertices.size(); ++i)
	{
		auto& p = Prism.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = Prism.Vertices[i].Normal;
		vertices[i].TexC = Prism.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = Prism.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "prismGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["prism"] = submesh;

	mGeometries["prismGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildTreeSpritesGeometry()
{
	//step5
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};
	static const int treeCount = 24;
	std::array<TreeSpriteVertex, treeCount> vertices;
	for(UINT i = 0; i < treeCount; i++)
	{
		if(i<3)
			vertices[i].Pos = XMFLOAT3(15.0f, 8.0f, -10.0f - i * 10.0f);
		else if (i >= 3 && i < 6)
		{
			vertices[i].Pos = XMFLOAT3(-15.0f, 8.0f, -10.0f - (i-3) * 10.0f);

		}
		else if (i >= 6 && i < 13)
		{
			if (i >= 11)
			{
				vertices[i].Pos = XMFLOAT3(-45.0f, 8.0f, -140.0f + ((i - 7) * 20));
			}
			else{
				vertices[i].Pos = XMFLOAT3(-45.0f, 8.0f, -140.0f + ((i - 6) * 20));

			}
		}
		else if (i >= 13 && i < 20)
		{
			vertices[i].Pos = XMFLOAT3(45.0f, 8.0f, -140.0f + ((i-13) * 20));
		}

		else if (i >= 20 && i <= 21)
		{
			vertices[i].Pos = XMFLOAT3(35.0f - ((i - 20) * 20), 8.0f, -150.0f );
		}

		else if (i >= 22 && i <= 23)
		{
			vertices[i].Pos = XMFLOAT3(-35.0f + ((i - 22) * 20), 8.0f, -150.0f);
		}

		vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
	}

	std::array<std::uint16_t, treeCount> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7, 
		8, 9, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildDiamondGeometry()
{

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData Diamond = geoGen.CreateDiamond(2.0f, 1.0f, 2.0f, 1.0f, 20,  20);

	std::vector<Vertex> vertices(Diamond.Vertices.size());
	for (size_t i = 0; i < Diamond.Vertices.size(); ++i)
	{
		auto& p = Diamond.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = Diamond.Vertices[i].Normal;
		vertices[i].TexC = Diamond.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = Diamond.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "diamondGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["diamond"] = submesh;

	mGeometries["diamondGeo"] = std::move(geo);
}

void TreeBillboardsApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mStdInputLayout.data(), (UINT)mStdInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;

	//there is abug with F2 key that is supposed to turn on the multisampling!
//Set4xMsaaState(true);
	//m4xMsaaState = true;

	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	//
	// PSO for transparent objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	//transparentPsoDesc.BlendState.AlphaToCoverageEnable = true;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	//
	// PSO for alpha tested objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));

	//
	// PSO for tree sprites
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	treeSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	//step1
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}

void TreeBillboardsApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void TreeBillboardsApp::BuildMaterials()
{
	UINT MatCBIndex = 0;
	UINT DiffuseSrvHeapIndex = 0;

	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = MatCBIndex++;
	grass->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;
	
	auto grasswall = std::make_unique<Material>();
	grasswall->Name = "grass";
	grasswall->MatCBIndex = MatCBIndex++;
	grasswall->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	grasswall->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grasswall->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grasswall->Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = MatCBIndex++;
	water->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto bricks = std::make_unique<Material>();
	bricks->Name = "bricks";
	bricks->MatCBIndex = MatCBIndex++;
	bricks->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks->Roughness = 0.25f;

	auto wedge = std::make_unique<Material>();
	wedge->Name = "bricks2";
	wedge->MatCBIndex = MatCBIndex++;
	wedge->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	wedge->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wedge->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	wedge->Roughness = 0.25f;

	auto cylinder = std::make_unique<Material>();
	cylinder->Name = "bricks3";
	cylinder->MatCBIndex = MatCBIndex++;
	cylinder->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	cylinder->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	cylinder->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	cylinder->Roughness = 0.25f;

	auto sphere = std::make_unique<Material>();
	sphere->Name = "ice";
	sphere->MatCBIndex = MatCBIndex++;
	sphere->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	sphere->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	sphere->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sphere->Roughness = 0.0f;


	auto cone = std::make_unique<Material>();
	cone->Name = "tile";
	cone->MatCBIndex = MatCBIndex++;
	cone->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	cone->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	cone->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	cone->Roughness = 0.25f;


	auto pyramid = std::make_unique<Material>();
	pyramid->Name = "sand";
	pyramid->MatCBIndex = MatCBIndex++;
	pyramid->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	pyramid->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	pyramid->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	pyramid->Roughness = 0.25f;

	auto prism = std::make_unique<Material>();
	prism->Name = "checkboard";
	prism->MatCBIndex = MatCBIndex++;
	prism->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	prism->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	prism->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	prism->Roughness = 0.25f;

	auto diamond = std::make_unique<Material>();
	diamond->Name = "shiny";
	diamond->MatCBIndex = MatCBIndex++;
	diamond->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	diamond->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	diamond->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	diamond->Roughness = 0.0f;

	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = MatCBIndex++;
	treeSprites->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex++;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;



	mMaterials["grass"] = std::move(grass);
	mMaterials["grass2"] = std::move(grasswall);

	mMaterials["water"] = std::move(water);
	mMaterials["bricks"] = std::move(bricks);
	mMaterials["bricks2"] = std::move(wedge);
	mMaterials["bricks3"] = std::move(cylinder);
	mMaterials["ice"] = std::move(sphere);
	mMaterials["tile"] = std::move(cone);
	mMaterials["sand"] = std::move(pyramid);
	mMaterials["checkboard"] = std::move(prism);
	mMaterials["shiny"] = std::move(diamond);
	mMaterials["treeSprites"] = std::move(treeSprites);

}

void TreeBillboardsApp::BuildRenderItems()
{
	UINT objCBIndex = 0;

	//build unique Items
    auto wavesRitem = std::make_unique<RenderItem>();
    wavesRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&wavesRitem->TexTransform,XMMatrixScaling(5.0f, 5.0f, 1.0f));
	wavesRitem->ObjCBIndex = objCBIndex++;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    mWavesRitem = wavesRitem.get();

	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f) );
	gridRitem->ObjCBIndex = objCBIndex++;
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());


	auto CenterRoom = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&CenterRoom->World, XMMatrixTranslation(0.0f , 0.5f, 0.5f) * XMMatrixScaling(35.0f, 30.0f,35.0f));
	CenterRoom->ObjCBIndex = objCBIndex++;
	CenterRoom->Mat = mMaterials["bricks"].get();
	CenterRoom->Geo = mGeometries["boxGeo"].get();
	CenterRoom->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	CenterRoom->IndexCount = CenterRoom->Geo->DrawArgs["box"].IndexCount;
	CenterRoom->StartIndexLocation = CenterRoom->Geo->DrawArgs["box"].StartIndexLocation;
	CenterRoom->BaseVertexLocation = CenterRoom->Geo->DrawArgs["box"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(CenterRoom.get());


    mAllRitems.push_back(std::move(wavesRitem));
    mAllRitems.push_back(std::move(gridRitem));
	mAllRitems.push_back(std::move(CenterRoom));




	//walls
	for (int k = 0; k < 2; k++)
	{
		auto SideWall = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&SideWall->World, XMMatrixTranslation(-6.0f +12*k, 0.5f, 0.0f) * XMMatrixScaling(4.0f, 12.0f, 84.0f));
		SideWall->ObjCBIndex = objCBIndex++;
		SideWall->Mat = mMaterials["bricks"].get();
		SideWall->Geo = mGeometries["boxGeo"].get();
		SideWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		SideWall->IndexCount = SideWall->Geo->DrawArgs["box"].IndexCount;
		SideWall->StartIndexLocation = SideWall->Geo->DrawArgs["box"].StartIndexLocation;
		SideWall->BaseVertexLocation = SideWall->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(SideWall.get());
		mAllRitems.push_back(std::move(SideWall));
	}




	auto BackWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&BackWall->World, XMMatrixTranslation(0.0f, 0.5f, 10.0f) * XMMatrixScaling(45.0f, 12.0f, 4.0f));
	BackWall->ObjCBIndex = objCBIndex++;
	BackWall->Mat = mMaterials["bricks"].get();
	BackWall->Geo = mGeometries["boxGeo"].get();
	BackWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	BackWall->IndexCount = BackWall->Geo->DrawArgs["box"].IndexCount;
	BackWall->StartIndexLocation = BackWall->Geo->DrawArgs["box"].StartIndexLocation;
	BackWall->BaseVertexLocation = BackWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(BackWall.get());
	mAllRitems.push_back(std::move(BackWall));

	auto FrontLeftWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&FrontLeftWall->World, XMMatrixTranslation(-1.0f, 0.5f, -10.0f) * XMMatrixScaling(15.0f, 12.0f, 4.0f));
	FrontLeftWall->ObjCBIndex = objCBIndex++;
	FrontLeftWall->Mat = mMaterials["bricks"].get();
	FrontLeftWall->Geo = mGeometries["boxGeo"].get();
	FrontLeftWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	FrontLeftWall->IndexCount = FrontLeftWall->Geo->DrawArgs["box"].IndexCount;
	FrontLeftWall->StartIndexLocation = FrontLeftWall->Geo->DrawArgs["box"].StartIndexLocation;
	FrontLeftWall->BaseVertexLocation = FrontLeftWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(FrontLeftWall.get());
	mAllRitems.push_back(std::move(FrontLeftWall));

	auto FrontRightWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&FrontRightWall->World, XMMatrixTranslation(1.00f, 0.5f, -10.0f) * XMMatrixScaling(15.0f, 12.0f, 4.0f));
	FrontRightWall->ObjCBIndex = objCBIndex++;
	FrontRightWall->Mat = mMaterials["bricks"].get();
	FrontRightWall->Geo = mGeometries["boxGeo"].get();
	FrontRightWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	FrontRightWall->IndexCount = FrontRightWall->Geo->DrawArgs["box"].IndexCount;
	FrontRightWall->StartIndexLocation = FrontRightWall->Geo->DrawArgs["box"].StartIndexLocation;
	FrontRightWall->BaseVertexLocation = FrontRightWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(FrontRightWall.get());
	mAllRitems.push_back(std::move(FrontRightWall));

	//Side WallWedges
	for(int j =1; j >-2; j=j-2)
	for (int i = 0; i < 10; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&borders->World, XMMatrixTranslation(-12.5f*j, 3.5f, -10.0f+i*2) * XMMatrixScaling(2.0f, 4.0f, 4.0f));
		XMStoreFloat4x4(&dersbor->World, XMMatrixTranslation(12.5f*j, 3.5f, -9.0f + i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f) * XMMatrixRotationY(3.1416));

		borders->ObjCBIndex = objCBIndex++;
		borders->Mat = mMaterials["bricks2"].get();
		borders->Geo = mGeometries["wedgeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(borders.get());
		mAllRitems.push_back(std::move(borders));

		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Mat = mMaterials["bricks2"].get();
		dersbor->Geo = mGeometries["wedgeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(dersbor.get());
		mAllRitems.push_back(std::move(dersbor));
	}

	//BackWAll Wedges

	for (int i = 0; i < 6; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&borders->World, XMMatrixTranslation(20.5f, 3.5f, -6.0f + i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f) * XMMatrixRotationY(-3.1416 / 2));
		XMStoreFloat4x4(&dersbor->World, XMMatrixTranslation(-20.5f, 3.5f, -5.0f + i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f) * XMMatrixRotationY(3.1416/2));

		borders->ObjCBIndex = objCBIndex++;
		borders->Mat = mMaterials["bricks2"].get();
		borders->Geo = mGeometries["wedgeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(borders.get());
		mAllRitems.push_back(std::move(borders));

		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Mat = mMaterials["bricks2"].get();
		dersbor->Geo = mGeometries["wedgeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(dersbor.get());
		mAllRitems.push_back(std::move(dersbor));
	}
	//FrontWall Wedges
	for (int i = 0; i < 2; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&borders->World, XMMatrixTranslation(-20.5f, 3.5f, -6.0f + i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f) * XMMatrixRotationY(-3.1416 / 2));
		XMStoreFloat4x4(&dersbor->World, XMMatrixTranslation(20.5f, 3.5f, 3.0f + i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f) * XMMatrixRotationY(3.1416 / 2));

		borders->ObjCBIndex = objCBIndex++;
		borders->Mat = mMaterials["bricks2"].get();
		borders->Geo = mGeometries["wedgeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(borders.get());
		mAllRitems.push_back(std::move(borders));

		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Mat = mMaterials["bricks2"].get();
		dersbor->Geo = mGeometries["wedgeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(dersbor.get());
		mAllRitems.push_back(std::move(dersbor));
	}
	for (int i = 0; i < 2; i++)
	{
		auto borders = std::make_unique<RenderItem>();
		auto dersbor = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&borders->World, XMMatrixTranslation(-20.5f, 3.5f, 3.0f + i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f) * XMMatrixRotationY(-3.1416 / 2));
		XMStoreFloat4x4(&dersbor->World, XMMatrixTranslation(20.5f, 3.5f, -6.0f + i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f) * XMMatrixRotationY(3.1416 / 2));

		borders->ObjCBIndex = objCBIndex++;
		borders->Mat = mMaterials["bricks2"].get();
		borders->Geo = mGeometries["wedgeGeo"].get();
		borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
		borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
		borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(borders.get());
		mAllRitems.push_back(std::move(borders));

		dersbor->ObjCBIndex = objCBIndex++;
		dersbor->Mat = mMaterials["bricks2"].get();
		dersbor->Geo = mGeometries["wedgeGeo"].get();
		dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
		dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
		dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(dersbor.get());
		mAllRitems.push_back(std::move(dersbor));
	}

	//Corners

	for (int j = 0; j < 2; j++)
	{
		for (int i = 0; i < 2; i++)
		{
			auto Cylinder = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&Cylinder->World, XMMatrixTranslation(8.00f - 16 * j, 3.0f, -13.5f + 27 * i)* XMMatrixScaling(3.0f, 3.0f, 3.0f));
			Cylinder->ObjCBIndex = objCBIndex++;
			Cylinder->Mat = mMaterials["bricks3"].get();
			Cylinder->Geo = mGeometries["cylinderGeo"].get();
			Cylinder->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			Cylinder->IndexCount = Cylinder->Geo->DrawArgs["cylinder"].IndexCount;
			Cylinder->StartIndexLocation = Cylinder->Geo->DrawArgs["cylinder"].StartIndexLocation;
			Cylinder->BaseVertexLocation = Cylinder->Geo->DrawArgs["cylinder"].BaseVertexLocation;
			mRitemLayer[(int)RenderLayer::Opaque].push_back(Cylinder.get());
			mAllRitems.push_back(std::move(Cylinder));

			auto Cone = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&Cone->World, XMMatrixTranslation(8.00f - 16 * j, 12.0f, -13.5f + 27 * i) * XMMatrixScaling(3.0f, 2.0f, 3.0f));
			Cone->ObjCBIndex = objCBIndex++;
			Cone->Mat = mMaterials["tile"].get();
			Cone->Geo = mGeometries["coneGeo"].get();
			Cone->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			Cone->IndexCount = Cone->Geo->DrawArgs["cone"].IndexCount;
			Cone->StartIndexLocation = Cone->Geo->DrawArgs["cone"].StartIndexLocation;
			Cone->BaseVertexLocation = Cone->Geo->DrawArgs["cone"].BaseVertexLocation;
			mRitemLayer[(int)RenderLayer::Opaque].push_back(Cone.get());
			mAllRitems.push_back(std::move(Cone));

			auto SphereRitem = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&SphereRitem->World, XMMatrixTranslation(8.00f - 16 * j, 11.0f, -13.5f + 27 * i)* XMMatrixScaling(3.0f , 3.0f, 3.0f));
			SphereRitem->ObjCBIndex = objCBIndex++;
			SphereRitem->Mat = mMaterials["ice"].get();
			SphereRitem->Geo = mGeometries["sphereGeo"].get();
			SphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			SphereRitem->IndexCount = SphereRitem->Geo->DrawArgs["sphere"].IndexCount;
			SphereRitem->StartIndexLocation = SphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
			SphereRitem->BaseVertexLocation = SphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

			mRitemLayer[(int)RenderLayer::Transparent].push_back(SphereRitem.get());
			mAllRitems.push_back(std::move(SphereRitem));
		}

	}

	//center Wedges

	for (int j = 0; j <2; j ++)
		for (int i = 0; i < 4; i++)
		{
			auto borders = std::make_unique<RenderItem>();
			auto dersbor = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&borders->World, XMMatrixTranslation(0.5f +16.5f*j, 8.0f, -3.5f + i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f)* XMMatrixRotationY(-3.1416/ 2));
			XMStoreFloat4x4(&dersbor->World, XMMatrixTranslation(-0.5f-16.5f*j, 8.0f, 2.5f - i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f) * XMMatrixRotationY(3.1416/2));

			borders->ObjCBIndex = objCBIndex++;
			borders->Mat = mMaterials["bricks2"].get();
			borders->Geo = mGeometries["wedgeGeo"].get();
			borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
			borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
			borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;

			mRitemLayer[(int)RenderLayer::Opaque].push_back(borders.get());
			mAllRitems.push_back(std::move(borders));

			dersbor->ObjCBIndex = objCBIndex++;
			dersbor->Mat = mMaterials["bricks2"].get();
			dersbor->Geo = mGeometries["wedgeGeo"].get();
			dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
			dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
			dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

			mRitemLayer[(int)RenderLayer::Opaque].push_back(dersbor.get());
			mAllRitems.push_back(std::move(dersbor));
		}

	for (int j = 0; j < 2; j++)
		for (int i = 0; i < 4; i++)
		{
			auto borders = std::make_unique<RenderItem>();
			auto dersbor = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&borders->World, XMMatrixTranslation(-8.5f + 16.5f * j, 8.0f, 0.5f + i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f));
			XMStoreFloat4x4(&dersbor->World, XMMatrixTranslation(8.5f - 16.5f * j, 8.0f, -1.5f - i * 2) * XMMatrixScaling(2.0f, 4.0f, 4.0f) * XMMatrixRotationY(3.1416 ));

			borders->ObjCBIndex = objCBIndex++;
			borders->Mat = mMaterials["bricks2"].get();
			borders->Geo = mGeometries["wedgeGeo"].get();
			borders->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			borders->IndexCount = borders->Geo->DrawArgs["wedge"].IndexCount;
			borders->StartIndexLocation = borders->Geo->DrawArgs["wedge"].StartIndexLocation;
			borders->BaseVertexLocation = borders->Geo->DrawArgs["wedge"].BaseVertexLocation;

			mRitemLayer[(int)RenderLayer::Opaque].push_back(borders.get());
			mAllRitems.push_back(std::move(borders));

			dersbor->ObjCBIndex = objCBIndex++;
			dersbor->Mat = mMaterials["bricks2"].get();
			dersbor->Geo = mGeometries["wedgeGeo"].get();
			dersbor->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			dersbor->IndexCount = dersbor->Geo->DrawArgs["wedge"].IndexCount;
			dersbor->StartIndexLocation = dersbor->Geo->DrawArgs["wedge"].StartIndexLocation;
			dersbor->BaseVertexLocation = dersbor->Geo->DrawArgs["wedge"].BaseVertexLocation;

			mRitemLayer[(int)RenderLayer::Opaque].push_back(dersbor.get());
			mAllRitems.push_back(std::move(dersbor));
		}

	auto CenterPyramid = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&CenterPyramid->World, XMMatrixTranslation(0.0f, 2.5f, 1.2f) * XMMatrixScaling(15.0f, 15.0f, 15.0f));
	CenterPyramid->ObjCBIndex = objCBIndex++;
	CenterPyramid->Mat = mMaterials["sand"].get();
	CenterPyramid->Geo = mGeometries["pyramidGeo"].get();
	CenterPyramid->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	CenterPyramid->IndexCount = CenterPyramid->Geo->DrawArgs["pyramid"].IndexCount;
	CenterPyramid->StartIndexLocation = CenterPyramid->Geo->DrawArgs["pyramid"].StartIndexLocation;
	CenterPyramid->BaseVertexLocation = CenterPyramid->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(CenterPyramid.get());
	mAllRitems.push_back(std::move(CenterPyramid));

	//Gate
	for (int i = 0; i < 2; i++)
	{
		float Additional;
		float Additional2;

		if (i == 1)
		{
			Additional = -10.0f;
			Additional2 = -25.0f;
		}
		else {
			Additional = 0.0f;
			Additional2 = 0.0f;
		}

		auto GatePrism = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&GatePrism->World, XMMatrixTranslation(1.9f, 0.5f , -4.0f +Additional)* XMMatrixScaling(5.0f, 20.0f, 10.0f) );
		GatePrism->ObjCBIndex = objCBIndex++;
		GatePrism->Mat = mMaterials["checkboard"].get();
		GatePrism->Geo = mGeometries["prismGeo"].get();
		GatePrism->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		GatePrism->IndexCount = GatePrism->Geo->DrawArgs["prism"].IndexCount;
		GatePrism->StartIndexLocation = GatePrism->Geo->DrawArgs["prism"].StartIndexLocation;
		GatePrism->BaseVertexLocation = GatePrism->Geo->DrawArgs["prism"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(GatePrism.get());
		mAllRitems.push_back(std::move(GatePrism));

		auto GatePrism2 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&GatePrism2->World, XMMatrixTranslation(1.9f, 0.5f, 4.0f - Additional)* XMMatrixScaling(5.0f, 20.0f, 10.0f)* XMMatrixRotationY(3.1416));
		GatePrism2->ObjCBIndex = objCBIndex++;
		GatePrism2->Mat = mMaterials["checkboard"].get();
		GatePrism2->Geo = mGeometries["prismGeo"].get();
		GatePrism2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		GatePrism2->IndexCount = GatePrism2->Geo->DrawArgs["prism"].IndexCount;
		GatePrism2->StartIndexLocation = GatePrism2->Geo->DrawArgs["prism"].StartIndexLocation;
		GatePrism2->BaseVertexLocation = GatePrism2->Geo->DrawArgs["prism"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(GatePrism2.get());
		mAllRitems.push_back(std::move(GatePrism2));

		auto GatePrism3 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&GatePrism3->World, XMMatrixTranslation(4.5, 0.0f, -4.0f + Additional)* XMMatrixScaling(5.0f, 30.0f, 10.0f)* XMMatrixRotationZ(3.1416 / 2));
		GatePrism3->ObjCBIndex = objCBIndex++;
		GatePrism3->Mat = mMaterials["checkboard"].get();
		GatePrism3->Geo = mGeometries["prismGeo"].get();
		GatePrism3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		GatePrism3->IndexCount = GatePrism3->Geo->DrawArgs["prism"].IndexCount;
		GatePrism3->StartIndexLocation = GatePrism3->Geo->DrawArgs["prism"].StartIndexLocation;
		GatePrism3->BaseVertexLocation = GatePrism3->Geo->DrawArgs["prism"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(GatePrism3.get());
		mAllRitems.push_back(std::move(GatePrism3));

		auto middleStairs = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&middleStairs->World, XMMatrixTranslation(0.0f, 0.5f, -10.0f + Additional2)* XMMatrixScaling(15.0f, 2.0f, 4.0f));
		middleStairs->ObjCBIndex = objCBIndex++;
		middleStairs->Mat = mMaterials["bricks"].get();
		middleStairs->Geo = mGeometries["boxGeo"].get();
		middleStairs->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		middleStairs->IndexCount = middleStairs->Geo->DrawArgs["box"].IndexCount;
		middleStairs->StartIndexLocation = middleStairs->Geo->DrawArgs["box"].StartIndexLocation;
		middleStairs->BaseVertexLocation = middleStairs->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(middleStairs.get());
		mAllRitems.push_back(std::move(middleStairs));

		auto frontStairs = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&frontStairs->World, XMMatrixTranslation(0.0f, 0.5f, -11.0f + Additional2)* XMMatrixScaling(15.0f, 2.0f, 4.0f));
		frontStairs->ObjCBIndex = objCBIndex++;
		frontStairs->Mat = mMaterials["bricks2"].get();
		frontStairs->Geo = mGeometries["wedgeGeo"].get();
		frontStairs->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		frontStairs->IndexCount = frontStairs->Geo->DrawArgs["wedge"].IndexCount;
		frontStairs->StartIndexLocation = frontStairs->Geo->DrawArgs["wedge"].StartIndexLocation;
		frontStairs->BaseVertexLocation = frontStairs->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(frontStairs.get());
		mAllRitems.push_back(std::move(frontStairs));

		auto BackStairs = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&BackStairs->World, XMMatrixTranslation(0.0f, 0.5f, 9.0f - Additional2)* XMMatrixScaling(15.0f, 2.0f, 4.0f)* XMMatrixRotationY(3.1416));
		BackStairs->ObjCBIndex = objCBIndex++;
		BackStairs->Mat = mMaterials["bricks2"].get();
		BackStairs->Geo = mGeometries["wedgeGeo"].get();
		BackStairs->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		BackStairs->IndexCount = BackStairs->Geo->DrawArgs["wedge"].IndexCount;
		BackStairs->StartIndexLocation = BackStairs->Geo->DrawArgs["wedge"].StartIndexLocation;
		BackStairs->BaseVertexLocation = BackStairs->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(BackStairs.get());
		mAllRitems.push_back(std::move(BackStairs));

	}

	//maze
	auto Leftgrasswall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Leftgrasswall->World, XMMatrixTranslation(7.5f, 0.75f, 0.9f ) * XMMatrixScaling(5.0f, 12.0f, 100.0f) * XMMatrixRotationY(3.1416));
	Leftgrasswall->ObjCBIndex = objCBIndex++;
	Leftgrasswall->Mat = mMaterials["grass2"].get();
	Leftgrasswall->Geo = mGeometries["grasswallGeo"].get();
	Leftgrasswall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Leftgrasswall->IndexCount = Leftgrasswall->Geo->DrawArgs["grasswall"].IndexCount;
	Leftgrasswall->StartIndexLocation = Leftgrasswall->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Leftgrasswall->BaseVertexLocation = Leftgrasswall->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Leftgrasswall.get());
	mAllRitems.push_back(std::move(Leftgrasswall));

	auto Rigthgrasswall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Rigthgrasswall->World, XMMatrixTranslation(-7.5f, 0.75f, 0.9f)* XMMatrixScaling(5.0f, 12.0f, 100.0f)* XMMatrixRotationY(3.1416));
	Rigthgrasswall->ObjCBIndex = objCBIndex++;
	Rigthgrasswall->Mat = mMaterials["grass2"].get();
	Rigthgrasswall->Geo = mGeometries["grasswallGeo"].get();
	Rigthgrasswall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Rigthgrasswall->IndexCount = Rigthgrasswall->Geo->DrawArgs["grasswall"].IndexCount;
	Rigthgrasswall->StartIndexLocation = Rigthgrasswall->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Rigthgrasswall->BaseVertexLocation = Rigthgrasswall->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Rigthgrasswall.get());
	mAllRitems.push_back(std::move(Rigthgrasswall));



	auto LeftFrontgrasswall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&LeftFrontgrasswall->World, XMMatrixTranslation(1.1f, 0.75f, -27.5f)* XMMatrixScaling(22.5f, 12.0f, 5.0f));
	LeftFrontgrasswall->ObjCBIndex = objCBIndex++;
	LeftFrontgrasswall->Mat = mMaterials["grass2"].get();
	LeftFrontgrasswall->Geo = mGeometries["grasswallGeo"].get();
	LeftFrontgrasswall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	LeftFrontgrasswall->IndexCount = LeftFrontgrasswall->Geo->DrawArgs["grasswall"].IndexCount;
	LeftFrontgrasswall->StartIndexLocation = LeftFrontgrasswall->Geo->DrawArgs["grasswall"].StartIndexLocation;
	LeftFrontgrasswall->BaseVertexLocation = LeftFrontgrasswall->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(LeftFrontgrasswall.get());
	mAllRitems.push_back(std::move(LeftFrontgrasswall));

	auto RightFrontgrasswall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&RightFrontgrasswall->World, XMMatrixTranslation(-1.1f, 0.75f, -27.5f)* XMMatrixScaling(22.5f, 12.0f, 5.0f));
	RightFrontgrasswall->ObjCBIndex = objCBIndex++;
	RightFrontgrasswall->Mat = mMaterials["grass2"].get();
	RightFrontgrasswall->Geo = mGeometries["grasswallGeo"].get();
	RightFrontgrasswall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	RightFrontgrasswall->IndexCount = RightFrontgrasswall->Geo->DrawArgs["grasswall"].IndexCount;
	RightFrontgrasswall->StartIndexLocation = RightFrontgrasswall->Geo->DrawArgs["grasswall"].StartIndexLocation;
	RightFrontgrasswall->BaseVertexLocation = RightFrontgrasswall->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(RightFrontgrasswall.get());
	mAllRitems.push_back(std::move(RightFrontgrasswall));

	auto LeftBackgrasswall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&LeftBackgrasswall->World, XMMatrixTranslation(4.15f, 0.75f, -8.5f)* XMMatrixScaling(7.5f, 12.0f, 5.0f));
	LeftBackgrasswall->ObjCBIndex = objCBIndex++;
	LeftBackgrasswall->Mat = mMaterials["grass2"].get();
	LeftBackgrasswall->Geo = mGeometries["grasswallGeo"].get();
	LeftBackgrasswall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	LeftBackgrasswall->IndexCount = LeftBackgrasswall->Geo->DrawArgs["grasswall"].IndexCount;
	LeftBackgrasswall->StartIndexLocation = LeftBackgrasswall->Geo->DrawArgs["grasswall"].StartIndexLocation;
	LeftBackgrasswall->BaseVertexLocation = LeftBackgrasswall->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(LeftBackgrasswall.get());
	mAllRitems.push_back(std::move(LeftBackgrasswall));

	auto RidghtBackgrasswall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&RidghtBackgrasswall->World, XMMatrixTranslation(-4.15f, 0.75f, -8.5f)* XMMatrixScaling(7.5f, 12.0f, 5.0f));
	RidghtBackgrasswall->ObjCBIndex = objCBIndex++;
	RidghtBackgrasswall->Mat = mMaterials["grass2"].get();
	RidghtBackgrasswall->Geo = mGeometries["grasswallGeo"].get();
	RidghtBackgrasswall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	RidghtBackgrasswall->IndexCount = RidghtBackgrasswall->Geo->DrawArgs["grasswall"].IndexCount;
	RidghtBackgrasswall->StartIndexLocation = RidghtBackgrasswall->Geo->DrawArgs["grasswall"].StartIndexLocation;
	RidghtBackgrasswall->BaseVertexLocation = RidghtBackgrasswall->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(RidghtBackgrasswall.get());
	mAllRitems.push_back(std::move(RidghtBackgrasswall));



	//Laberinth
	auto MazeWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&MazeWall->World, XMMatrixTranslation(0.275f, 0.75f, -24.0f)* XMMatrixScaling(45.0f, 10.0f, 5.0f));
	MazeWall->ObjCBIndex = objCBIndex++;
	MazeWall->Mat = mMaterials["grass2"].get();
	MazeWall->Geo = mGeometries["grasswallGeo"].get();
	MazeWall->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	MazeWall->IndexCount = MazeWall->Geo->DrawArgs["grasswall"].IndexCount;
	MazeWall->StartIndexLocation = MazeWall->Geo->DrawArgs["grasswall"].StartIndexLocation;
	MazeWall->BaseVertexLocation = MazeWall->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(MazeWall.get());
	mAllRitems.push_back(std::move(MazeWall));

	auto Maze2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Maze2->World, XMMatrixTranslation(-0.0f, 0.75f, -11.5f)* XMMatrixScaling(55.0f, 10.0f, 5.0f));
	Maze2->ObjCBIndex = objCBIndex++;
	Maze2->Mat = mMaterials["grass2"].get();
	Maze2->Geo = mGeometries["grasswallGeo"].get();
	Maze2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Maze2->IndexCount = Maze2->Geo->DrawArgs["grasswall"].IndexCount;
	Maze2->StartIndexLocation = Maze2->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Maze2->BaseVertexLocation = Maze2->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Maze2.get());
	mAllRitems.push_back(std::move(Maze2));

	auto Maze2_1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Maze2_1->World, XMMatrixTranslation(5.0f, 0.75f, -5.0f)* XMMatrixScaling(5.0f, 10.0f, 10.0f));
	Maze2_1->ObjCBIndex = objCBIndex++;
	Maze2_1->Mat = mMaterials["grass2"].get();
	Maze2_1->Geo = mGeometries["grasswallGeo"].get();
	Maze2_1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Maze2_1->IndexCount = Maze2_1->Geo->DrawArgs["grasswall"].IndexCount;
	Maze2_1->StartIndexLocation = Maze2_1->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Maze2_1->BaseVertexLocation = Maze2_1->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Maze2_1.get());
	mAllRitems.push_back(std::move(Maze2_1));

	auto Maze2_2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Maze2_2->World, XMMatrixTranslation(-4.5f, 0.75f, -8.0f)* XMMatrixScaling(5.0f, 10.0f, 10.0f));
	Maze2_2->ObjCBIndex = objCBIndex++;
	Maze2_2->Mat = mMaterials["grass2"].get();
	Maze2_2->Geo = mGeometries["grasswallGeo"].get();
	Maze2_2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Maze2_2->IndexCount = Maze2_2->Geo->DrawArgs["grasswall"].IndexCount;
	Maze2_2->StartIndexLocation = Maze2_2->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Maze2_2->BaseVertexLocation = Maze2_2->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Maze2_2.get());
	mAllRitems.push_back(std::move(Maze2_2));

	auto Maze2_5 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Maze2_5->World, XMMatrixTranslation(-0.2f, 0.8f, -14.5f)* XMMatrixScaling(50.0f, 10.0f, 5.0f));
	Maze2_5->ObjCBIndex = objCBIndex++;
	Maze2_5->Mat = mMaterials["grass2"].get();
	Maze2_5->Geo = mGeometries["grasswallGeo"].get();
	Maze2_5->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Maze2_5->IndexCount = Maze2_5->Geo->DrawArgs["grasswall"].IndexCount;
	Maze2_5->StartIndexLocation = Maze2_5->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Maze2_5->BaseVertexLocation = Maze2_5->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Maze2_5.get());
	mAllRitems.push_back(std::move(Maze2_5));

	auto Maze3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Maze3->World, XMMatrixTranslation(-0.0f, 0.75f, -17.5f)* XMMatrixScaling(50.0f, 10.0f, 5.0f));
	Maze3->ObjCBIndex = objCBIndex++;
	Maze3->Mat = mMaterials["grass2"].get();
	Maze3->Geo = mGeometries["grasswallGeo"].get();
	Maze3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Maze3->IndexCount = Maze3->Geo->DrawArgs["grasswall"].IndexCount;
	Maze3->StartIndexLocation = Maze3->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Maze3->BaseVertexLocation = Maze3->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Maze3.get());
	mAllRitems.push_back(std::move(Maze3));

	auto Maze4 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Maze4->World, XMMatrixTranslation(-4.5f, 0.75f, -3.25f)* XMMatrixScaling(5.0f, 10.0f, 32.5f));
	Maze4->ObjCBIndex = objCBIndex++;
	Maze4->Mat = mMaterials["grass2"].get();
	Maze4->Geo = mGeometries["grasswallGeo"].get();
	Maze4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Maze4->IndexCount = Maze4->Geo->DrawArgs["grasswall"].IndexCount;
	Maze4->StartIndexLocation = Maze4->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Maze4->BaseVertexLocation = Maze4->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Maze4.get());
	mAllRitems.push_back(std::move(Maze4));

	auto Maze5 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Maze5->World, XMMatrixTranslation(4.5f, 0.75f, -6.0f)* XMMatrixScaling(5.0f, 10.0f, 16.25f));
	Maze5->ObjCBIndex = objCBIndex++;
	Maze5->Mat = mMaterials["grass2"].get();
	Maze5->Geo = mGeometries["grasswallGeo"].get();
	Maze5->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Maze5->IndexCount = Maze5->Geo->DrawArgs["grasswall"].IndexCount;
	Maze5->StartIndexLocation = Maze5->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Maze5->BaseVertexLocation = Maze5->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Maze5.get());
	mAllRitems.push_back(std::move(Maze5));

	auto Maze6 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Maze6->World, XMMatrixTranslation(0.3f, 0.75f, -20.5f)* XMMatrixScaling(25.0f, 10.0f, 5.0f));
	Maze6->ObjCBIndex = objCBIndex++;
	Maze6->Mat = mMaterials["grass2"].get();
	Maze6->Geo = mGeometries["grasswallGeo"].get();
	Maze6->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Maze6->IndexCount = Maze6->Geo->DrawArgs["grasswall"].IndexCount;
	Maze6->StartIndexLocation = Maze6->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Maze6->BaseVertexLocation = Maze6->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Maze6.get());
	mAllRitems.push_back(std::move(Maze6));

	auto Maze7 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Maze7->World, XMMatrixTranslation(3.0f, 0.75f, -14.5f)* XMMatrixScaling(10.0f, 10.0f, 5.0f));
	Maze7->ObjCBIndex = objCBIndex++;
	Maze7->Mat = mMaterials["grass2"].get();
	Maze7->Geo = mGeometries["grasswallGeo"].get();
	Maze7->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Maze7->IndexCount = Maze7->Geo->DrawArgs["grasswall"].IndexCount;
	Maze7->StartIndexLocation = Maze7->Geo->DrawArgs["grasswall"].StartIndexLocation;
	Maze7->BaseVertexLocation = Maze7->Geo->DrawArgs["grasswall"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Maze7.get());
	mAllRitems.push_back(std::move(Maze7));

	// diamond
	auto Diamond = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&Diamond->World, XMMatrixTranslation(0.0, 7.0f, 0.5f)* XMMatrixScaling(5.0f, 5.0f, 5.0f));
	Diamond->ObjCBIndex = objCBIndex++;
	Diamond->Mat = mMaterials["shiny"].get();
	Diamond->Geo = mGeometries["diamondGeo"].get();
	Diamond->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Diamond->IndexCount = Diamond->Geo->DrawArgs["diamond"].IndexCount;
	Diamond->StartIndexLocation = Diamond->Geo->DrawArgs["diamond"].StartIndexLocation;
	Diamond->BaseVertexLocation = Diamond->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(Diamond.get());
	mAllRitems.push_back(std::move(Diamond));



	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = objCBIndex++;
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	//step2
	treeSpritesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());

	mAllRitems.push_back(std::move(treeSpritesRitem));




}

void TreeBillboardsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		//step3
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TreeBillboardsApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}

float TreeBillboardsApp::GetHillsHeight(float x, float z)const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 TreeBillboardsApp::GetHillsNormal(float x, float z)const
{
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
        1.0f,
        -0.3f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}
