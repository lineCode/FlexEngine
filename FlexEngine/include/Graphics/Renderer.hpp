#pragma once

#include <string>
#include <vector>
#include <array>
#include <map>

#pragma warning(push, 0)
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#pragma warning(pop)

#include "GameContext.hpp"

#include "VertexBufferData.hpp"
#include "Transform.hpp"
#include "Physics/PhysicsDebuggingSettings.hpp"
#include "RendererTypes.hpp"

class btIDebugDraw;

namespace flex
{
	class MeshComponent;
	class BitmapFont;

	class Renderer
	{
	public:
		Renderer();
		virtual ~Renderer();

		virtual void Initialize(const GameContext& gameContext) = 0;
		virtual void PostInitialize(const GameContext& gameContext) = 0;
		virtual void Destroy() = 0;

		virtual MaterialID InitializeMaterial(const MaterialCreateInfo* createInfo) = 0;
		virtual RenderID InitializeRenderObject(const RenderObjectCreateInfo* createInfo) = 0;
		virtual void PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID) = 0; // Only call when creating objects after calling PostInitialize()

		virtual void ClearRenderObjects() = 0;
		virtual void ClearMaterials() = 0;

		virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) = 0;
		virtual void SetClearColor(real r, real g, real b) = 0;

		virtual void Update(const GameContext& gameContext) = 0;
		virtual void Draw(const GameContext& gameContext) = 0;
		virtual void DrawImGuiItems(const GameContext& gameContext) = 0;

		virtual void UpdateRenderObjectVertexData(RenderID renderID) = 0;

		virtual void ReloadShaders() = 0;

		virtual void OnWindowSizeChanged(i32 width, i32 height) = 0;

		virtual void OnSceneChanged() = 0;

		/*
		* Fills outInfo with an up-to-date version of the render object's info
		* Returns true if renderID refers to a valid render object
		*/
		virtual bool GetRenderObjectCreateInfo(RenderID renderID, RenderObjectCreateInfo& outInfo) = 0;

		virtual void SetVSyncEnabled(bool enableVSync) = 0;
		virtual bool GetVSyncEnabled() = 0;

		virtual u32 GetRenderObjectCount() const = 0;
		virtual u32 GetRenderObjectCapacity() const = 0;

		virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size, DataType dataType, bool normalized,
			i32 stride, void* pointer) = 0;

		virtual void SetSkyboxMesh(GameObject* skyboxMesh) = 0;
		virtual GameObject* GetSkyboxMesh() = 0;

		virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) = 0;

		virtual Material& GetMaterial(MaterialID matID) = 0;
		virtual Shader& GetShader(ShaderID shaderID)  = 0;

		virtual bool GetMaterialID(const std::string& materialName, MaterialID& materialID) = 0;
		virtual bool GetShaderID(const std::string& shaderName, ShaderID& shaderID) = 0;

		virtual void DestroyRenderObject(RenderID renderID) = 0;

		virtual void NewFrame() = 0;

		virtual void SetReflectionProbeMaterial(MaterialID reflectionProbeMaterialID);

		virtual btIDebugDraw* GetDebugDrawer() = 0;

		virtual void SetFont(BitmapFont* font) = 0;
		virtual void DrawString(const std::string& str, const glm::vec4& color, const glm::vec2& pos, real spacing, const std::vector<real>& letterYOffsets) = 0;

		virtual void SaveSettingsToDisk(bool bSaveOverDefaults = false) = 0;
		virtual void LoadSettingsFromDisk(bool bLoadDefaults = false) = 0;

		void SetPostProcessingEnabled(bool bEnabled);
		bool GetPostProcessingEnabled() const;

		PhysicsDebuggingSettings& GetPhysicsDebuggingSettings();

		bool InitializeDirectionalLight(const DirectionalLight& dirLight);
		PointLightID InitializePointLight(const PointLight& pointLight);

		void ClearDirectionalLight();
		void ClearPointLights();

		DirectionalLight& GetDirectionalLight();
		PointLight& GetPointLight(PointLightID pointLight);
		i32 GetNumPointLights();

		struct PostProcessSettings
		{
			bool bEnabled = true;

			real saturation = 1.0f;
			glm::vec3 brightness;
			glm::vec3 offset;
			bool bEnableFXAA = true;
			bool bEnableFXAADEBUGShowEdges = false;
		};
			
		PostProcessSettings& GetPostProcessSettings();

		static const u32 MAX_TEXTURE_DIM = 65536;
		static const u32 MAX_POINT_LIGHT_COUNT = 4;

		BitmapFont* m_FntUbuntuCondensed = nullptr;
		BitmapFont* m_FntSourceCodePro = nullptr;

	protected:
		/*
		* Draws common data for a game object
		*/
		void DrawImGuiForRenderObjectCommon(GameObject* gameObject);

		void DrawImGuiLights();
		
		std::vector<PointLight> m_PointLights;
		DirectionalLight m_DirectionalLight;

		struct DrawCallInfo
		{
			RenderID cubemapObjectRenderID = InvalidRenderID;
			bool bRenderToCubemap = false;
			bool bDeferred = false;
		};
		
		MaterialID m_ReflectionProbeMaterialID = InvalidMaterialID; // Set by the user via SetReflecionProbeMaterial

		bool m_bVSyncEnabled = true;
		PhysicsDebuggingSettings m_PhysicsDebuggingSettings;

		/* Objects that are created at bootup and stay active until shutdown, regardless of scene */
		std::vector<GameObject*> m_PersistentObjects;
		
		BitmapFont* m_CurrentFont = nullptr;
		std::vector<BitmapFont*> m_Fonts;

		PostProcessSettings m_PostProcessSettings;

		bool m_bShowImGui = true;
		bool m_bPostProcessingEnabled = true;

	private:
		Renderer& operator=(const Renderer&) = delete;
		Renderer(const Renderer&) = delete;

	};
} // namespace flex
