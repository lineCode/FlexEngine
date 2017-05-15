#pragma once
#if COMPILE_D3D

#include "Renderer.h"

struct GameContext;

class D3DRenderer : public Renderer
{
public:
	D3DRenderer(GameContext& gameContext);
	virtual ~D3DRenderer();

	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices) override;
	virtual glm::uint Initialize(const GameContext& gameContext, std::vector<VertexPosCol>* vertices,
		std::vector<glm::uint>* indices) override;

	virtual void SetClearColor(float r, float g, float b) override;

	virtual void PostInitialize() override;

	virtual void Draw(const GameContext& gameContext, glm::uint renderID) override;

	virtual void OnWindowSize(int width, int height) override;

	virtual void SetVSyncEnabled(bool enableVSync) override;
	virtual void Clear(int flags, const GameContext& gameContext) override;
	virtual void SwapBuffers(const GameContext& gameContext) override;

	virtual void UpdateTransformMatrix(const GameContext& gameContext, glm::uint renderID, const glm::mat4x4& model) override;

	virtual int GetShaderUniformLocation(glm::uint program, const std::string uniformName) override;
	virtual void SetUniform1f(glm::uint location, float val) override;

	virtual void DescribeShaderVariable(glm::uint renderID, glm::uint program, const std::string& variableName, int size,
		Renderer::Type renderType, bool normalized, int stride, void* pointer) override;

	virtual void Destroy(glm::uint renderID) override;

private:
	static glm::uint BufferTargetToD3DTarget(BufferTarget bufferTarget);
	static glm::uint TypeToD3DType(Type type);
	static glm::uint UsageFlagToD3DUsageFlag(UsageFlag usage);
	static glm::uint ModeToD3DMode(Mode mode);

	struct RenderObject
	{
		glm::uint renderID;

		glm::uint VAO;
		glm::uint VBO;
		glm::uint IBO;

		glm::uint vertexBuffer;
		std::vector<VertexPosCol>* vertices = nullptr;

		bool indexed;
		glm::uint indexBuffer;
		std::vector<glm::uint>* indices = nullptr;

		glm::uint MVP;
	};

	RenderObject* GetRenderObject(int renderID);

	// TODO: use sorted data type (map)
	std::vector<RenderObject*> m_RenderObjects;

	bool m_VSyncEnabled;

	D3DRenderer(const D3DRenderer&) = delete;
	D3DRenderer& operator=(const D3DRenderer&) = delete;

	// --------- Tutorial Code ---------
	void CreateDevice();
	void CreateResources(const GameContext& gameContext);

	void OnDeviceLost(const GameContext& gameContext);
	
	void PostProcess();

	void UpdateUniformBuffers(const GameContext& gameContext);


	D3D_FEATURE_LEVEL m_featureLevel;
	Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
	Microsoft::WRL::ComPtr<ID3D11Device1> m_Device1;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_DeviceContext;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_DeviceContext1;

	Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> m_SwapChain1;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthStencilView;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_BackBuffer;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_SceneTex;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SceneSRV;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_SceneRT;

	DirectX::XMVECTORF32 m_ClearColor;

	Microsoft::WRL::ComPtr<ID3D11InputLayout> m_InputLayout;
	std::unique_ptr<DirectX::IEffect> m_Effect;

	glm::mat4 m_World;
	glm::mat4 m_View;
	glm::mat4 m_Projection;

	std::unique_ptr<DirectX::CommonStates> m_States;
	std::unique_ptr<DirectX::SpriteBatch> m_SpriteBatch;
	std::unique_ptr<DirectX::GeometricPrimitive> m_Shape;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_Background;
	RECT m_FullscreenRect;

	bool m_Wireframe = false;
	bool m_EnableMSAA = false;
};

#endif // COMPILE_D3D