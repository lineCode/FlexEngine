#include "stdafx.hpp"
#if COMPILE_OPEN_GL

#include "Graphics/GL/GLRenderer.hpp"

#include <array>
#include <algorithm>
#include <string>
#include <utility>
#include <functional>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "ImGui/imgui_impl_glfw_gl3.h"

#include "BulletDynamics\Dynamics\btDiscreteDynamicsWorld.h"

#include "FreeCamera.hpp"
#include "Graphics/GL/GLHelpers.hpp"
#include "Graphics/GL/GLPhysicsDebugDraw.hpp"
#include "Logger.hpp"
#include "Window/Window.hpp"
#include "Window/GLFWWindowWrapper.hpp"
#include "VertexAttribute.hpp"
#include "GameContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scenes/BaseScene.hpp"
#include "Scene/MeshPrefab.hpp"
#include "Helpers.hpp"
#include "Physics/PhysicsWorld.hpp"


namespace flex
{
	namespace gl
	{
		GLRenderer::GLRenderer(GameContext& gameContext)
		{
			gameContext.renderer = this;

			CheckGLErrorMessages();

			LoadShaders();

			CheckGLErrorMessages();

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			CheckGLErrorMessages();

			glFrontFace(GL_CCW);
			CheckGLErrorMessages();

			// Prevent seams from appearing on lower mip map levels of cubemaps
			glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

			m_BRDFTextureSize = { 512, 512 };
			m_BRDFTextureHandle = {};
			m_BRDFTextureHandle.internalFormat = GL_RG16F;
			m_BRDFTextureHandle.format = GL_RG;
			m_BRDFTextureHandle.type = GL_FLOAT;

			m_OffscreenTextureHandle = {};
			m_OffscreenTextureHandle.internalFormat = GL_RGBA16F;
			m_OffscreenTextureHandle.format = GL_RGBA;
			m_OffscreenTextureHandle.type = GL_FLOAT;

			m_LoadingTextureHandle = {};
			m_LoadingTextureHandle.internalFormat = GL_RGBA;
			m_LoadingTextureHandle.format = GL_RGBA;
			m_LoadingTextureHandle.type = GL_FLOAT;


			m_gBuffer_PositionMetallicHandle = {};
			m_gBuffer_PositionMetallicHandle.internalFormat = GL_RGBA16F;
			m_gBuffer_PositionMetallicHandle.format = GL_RGBA;
			m_gBuffer_PositionMetallicHandle.type = GL_FLOAT;

			m_gBuffer_NormalRoughnessHandle = {};
			m_gBuffer_NormalRoughnessHandle.internalFormat = GL_RGBA16F;
			m_gBuffer_NormalRoughnessHandle.format = GL_RGBA;
			m_gBuffer_NormalRoughnessHandle.type = GL_FLOAT;

			m_gBuffer_DiffuseAOHandle = {};
			m_gBuffer_DiffuseAOHandle.internalFormat = GL_RGBA;
			m_gBuffer_DiffuseAOHandle.format = GL_RGBA;
			m_gBuffer_DiffuseAOHandle.type = GL_FLOAT;


			// Capture framebuffer (TODO: Merge with offscreen frame buffer?)
			{
				glGenFramebuffers(1, &m_CaptureFBO);
				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				CheckGLErrorMessages();

				glGenRenderbuffers(1, &m_CaptureRBO);
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512); // TODO: Remove 512
				CheckGLErrorMessages();
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_CaptureRBO);
				CheckGLErrorMessages();

				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				{
					Logger::LogError("Capture frame buffer is incomplete!");
				}

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glBindRenderbuffer(GL_RENDERBUFFER, 0);
				CheckGLErrorMessages();
			}

			// Offscreen framebuffer
			{
				glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

				glGenFramebuffers(1, &m_OffscreenFBO);
				glBindFramebuffer(GL_FRAMEBUFFER, m_OffscreenFBO);
				CheckGLErrorMessages();

				glGenRenderbuffers(1, &m_OffscreenRBO);
				glBindRenderbuffer(GL_RENDERBUFFER, m_OffscreenRBO);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, frameBufferSize.x, frameBufferSize.y);
				CheckGLErrorMessages();
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_OffscreenRBO);
				CheckGLErrorMessages();

				GenerateFrameBufferTexture(&m_OffscreenTextureHandle.id,
					0,
					m_OffscreenTextureHandle.internalFormat,
					m_OffscreenTextureHandle.format,
					m_OffscreenTextureHandle.type,
					frameBufferSize);

				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				{
					Logger::LogError("Offscreen frame buffer is incomplete!");
				}

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glBindRenderbuffer(GL_RENDERBUFFER, 0);
				CheckGLErrorMessages();
			}
			
			const real captureProjectionNearPlane = 0.1f;
			const real captureProjectionFarPlane = 1000.0f;
			m_CaptureProjection = glm::perspective(glm::radians(90.0f), 1.0f, captureProjectionNearPlane, captureProjectionFarPlane);
			m_CaptureViews =
			{
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
				glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
			};

			// Sprite quad
			GenerateGLTexture(m_LoadingTextureHandle.id, RESOURCE_LOCATION + "textures/loading_1.png", false, true);

			MaterialCreateInfo spriteMatCreateInfo = {};
			spriteMatCreateInfo.name = "Sprite material";
			spriteMatCreateInfo.shaderName = "sprite";
			m_SpriteMatID = InitializeMaterial(gameContext, &spriteMatCreateInfo);


			MaterialCreateInfo postProcessMatCreateInfo = {};
			postProcessMatCreateInfo.name = "Post process material";
			postProcessMatCreateInfo.shaderName = "post_process";
			m_PostProcessMatID = InitializeMaterial(gameContext, &postProcessMatCreateInfo);

			VertexBufferData::CreateInfo spriteQuadVertexBufferDataCreateInfo = {};
			spriteQuadVertexBufferDataCreateInfo.positions_2D = {
				glm::vec2(-1.0f,  1.0f),
				glm::vec2(-1.0f, -1.0f),
				glm::vec2(1.0f,  1.0f),

				glm::vec2(1.0f,  1.0f),
				glm::vec2(-1.0f, -1.0f),
				glm::vec2(1.0f, -1.0f),
			};

			spriteQuadVertexBufferDataCreateInfo.texCoords_UV = {
				glm::vec2(0.0f, 0.0f),
				glm::vec2(0.0f, 1.0f),
				glm::vec2(1.0f, 0.0f),

				glm::vec2(1.0f, 0.0f),
				glm::vec2(0.0f, 1.0f),
				glm::vec2(1.0f, 1.0f),
			};

			spriteQuadVertexBufferDataCreateInfo.colors_R32G32B32A32 = {
				glm::vec4(1.0f),
				glm::vec4(1.0f),
				glm::vec4(1.0f),

				glm::vec4(1.0f),
				glm::vec4(1.0f),
				glm::vec4(1.0f),
			};

			spriteQuadVertexBufferDataCreateInfo.attributes = (u32)VertexAttribute::POSITION_2D | (u32)VertexAttribute::UV | (u32)VertexAttribute::COLOR_R32G32B32A32_SFLOAT;
			
			m_SpriteQuadVertexBufferData = {};
			m_SpriteQuadVertexBufferData.Initialize(&spriteQuadVertexBufferDataCreateInfo);

			m_SpriteQuadTransform.SetAsIdentity();

			RenderObjectCreateInfo spriteQuadCreateInfo = {};
			spriteQuadCreateInfo.name = "Sprite quad";
			spriteQuadCreateInfo.vertexBufferData = &m_SpriteQuadVertexBufferData;
			spriteQuadCreateInfo.materialID = m_SpriteMatID;
			spriteQuadCreateInfo.depthWriteEnable = false;
			spriteQuadCreateInfo.transform = &m_SpriteQuadTransform;
			spriteQuadCreateInfo.enableCulling = false;
			m_SpriteQuadRenderID = InitializeRenderObject(gameContext, &spriteQuadCreateInfo);
			GetRenderObject(m_SpriteQuadRenderID)->visible = false;

			m_SpriteQuadVertexBufferData.DescribeShaderVariables(this, m_SpriteQuadRenderID);

			DrawSpriteQuad(gameContext, m_LoadingTextureHandle.id, m_SpriteMatID);
			SwapBuffers(gameContext);

			if (m_BRDFTextureHandle.id == 0)
			{
				Logger::LogInfo("Generating BRDF LUT");
				GenerateGLTexture_Empty(m_BRDFTextureHandle.id, m_BRDFTextureSize, false, m_BRDFTextureHandle.internalFormat, m_BRDFTextureHandle.format, m_BRDFTextureHandle.type);
				GenerateBRDFLUT(gameContext, m_BRDFTextureHandle.id, m_BRDFTextureSize);
			}

			ImGui::CreateContext();
			CheckGLErrorMessages();


			// G-buffer objects
			glGenFramebuffers(1, &m_gBufferHandle);
			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);

			const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

			GenerateFrameBufferTexture(&m_gBuffer_PositionMetallicHandle.id,
				0,
				m_gBuffer_PositionMetallicHandle.internalFormat,
				m_gBuffer_PositionMetallicHandle.format,
				m_gBuffer_PositionMetallicHandle.type,
				frameBufferSize);

			GenerateFrameBufferTexture(&m_gBuffer_NormalRoughnessHandle.id,
				1,
				m_gBuffer_NormalRoughnessHandle.internalFormat,
				m_gBuffer_NormalRoughnessHandle.format,
				m_gBuffer_NormalRoughnessHandle.type,
				frameBufferSize);

			GenerateFrameBufferTexture(&m_gBuffer_DiffuseAOHandle.id,
				2,
				m_gBuffer_DiffuseAOHandle.internalFormat,
				m_gBuffer_DiffuseAOHandle.format,
				m_gBuffer_DiffuseAOHandle.type,
				frameBufferSize);

			// Create and attach depth buffer
			glGenRenderbuffers(1, &m_gBufferDepthHandle);
			glBindRenderbuffer(GL_RENDERBUFFER, m_gBufferDepthHandle);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, frameBufferSize.x, frameBufferSize.y);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gBufferDepthHandle);

			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				Logger::LogError("Framebuffer not complete!");
			}
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			//GenerateGBuffer(gameContext);

			if (m_BRDFTextureHandle.id == 0)
			{
				GenerateGLTexture_Empty(m_BRDFTextureHandle.id, m_BRDFTextureSize, false, m_BRDFTextureHandle.internalFormat, m_BRDFTextureHandle.format, m_BRDFTextureHandle.type);
				GenerateBRDFLUT(gameContext, m_BRDFTextureHandle.id, m_BRDFTextureSize);
			}

			CheckGLErrorMessages();
		}

		void GLRenderer::DrawSpriteQuad(const GameContext& gameContext, u32 textureHandle, MaterialID materialID, bool flipVertically)
		{
			GLRenderObject* spriteRenderObject = GetRenderObject(m_SpriteQuadRenderID);
			if (!spriteRenderObject) return;

			spriteRenderObject->materialID = materialID;

			GLMaterial* spriteMaterial = &m_Materials[spriteRenderObject->materialID];
			GLShader* spriteShader = &m_Shaders[spriteMaterial->material.shaderID];

			glUseProgram(spriteShader->program);
			CheckGLErrorMessages();

			real verticalScale = flipVertically ? -1.0f : 1.0f;

			if (spriteShader->shader.constantBufferUniforms.HasUniform("verticalScale"))
			{
				glUniform1f(spriteMaterial->uniformIDs.verticalScale, verticalScale);
				CheckGLErrorMessages();
			}
			
			glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();
			glViewport(0, 0, (GLsizei)frameBufferSize.x, (GLsizei)frameBufferSize.y);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			CheckGLErrorMessages();

			glBindVertexArray(spriteRenderObject->VAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, spriteRenderObject->VBO);
			CheckGLErrorMessages();

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, textureHandle);
			CheckGLErrorMessages();

			glDepthMask(GL_TRUE);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			CheckGLErrorMessages();

			if (spriteRenderObject->enableCulling) glEnable(GL_CULL_FACE);
			else glDisable(GL_CULL_FACE);

			glCullFace(spriteRenderObject->cullFace);
			CheckGLErrorMessages();

			glDepthFunc(spriteRenderObject->depthTestReadFunc);
			CheckGLErrorMessages();

			glDepthMask(spriteRenderObject->depthWriteEnable);
			CheckGLErrorMessages();

			glDrawArrays(spriteRenderObject->topology, 0, (GLsizei)spriteRenderObject->vertexBufferData->VertexCount);
			CheckGLErrorMessages();
		}

		GLRenderer::~GLRenderer()
		{
			CheckGLErrorMessages();
			
			ImGui_ImplGlfwGL3_Shutdown();
			ImGui::DestroyContext();

			CheckGLErrorMessages();

			if (m_1x1_NDC_QuadVertexBufferData.pDataStart)
			{
				m_1x1_NDC_QuadVertexBufferData.Destroy();
			}

			if (m_SkyBoxMesh)
			{
				Destroy(m_SkyBoxMesh->GetRenderID());
				SafeDelete(m_SkyBoxMesh);
			}

			for (size_t i = 0; i < m_RenderObjects.size(); ++i)
			{
				Destroy(i);
				CheckGLErrorMessages();
			}
			m_RenderObjects.clear();
			CheckGLErrorMessages();

			SafeDelete(m_PhysicsDebugDrawer);

			m_gBufferQuadVertexBufferData.Destroy();
			m_SpriteQuadVertexBufferData.Destroy();

			glfwTerminate();
		}

		MaterialID GLRenderer::InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			CheckGLErrorMessages();

			MaterialID matID = GetNextAvailableMaterialID();
			m_Materials.insert(std::pair<MaterialID, GLMaterial>(matID, {}));
			GLMaterial& mat = m_Materials.at(matID);
			mat.material = {};
			mat.material.name = createInfo->name;

			const MaterialID materialID = m_Materials.size() - 1;

			if (!GetShaderID(createInfo->shaderName, mat.material.shaderID))
			{
				if (createInfo->shaderName.empty())
				{
					Logger::LogError("Material's shader name not set! MaterialCreateInfo::shaderName must be filled in");
				}
				else
				{
					Logger::LogError("Material's shader not set! (material: " + createInfo->name + ", shader: " + createInfo->shaderName + ")");
				}
			}
			
			GLShader& shader = m_Shaders[mat.material.shaderID];

			glUseProgram(shader.program);
			CheckGLErrorMessages();

			// TODO: Is this really needed? (do things dynamically instead?)
			UniformInfo uniformInfo[] = {
				{ "model", 							&mat.uniformIDs.model },
				{ "modelInvTranspose", 				&mat.uniformIDs.modelInvTranspose },
				{ "modelViewProjection",			&mat.uniformIDs.modelViewProjection },
				{ "colorMultiplier", 				&mat.uniformIDs.colorMultiplier },
				{ "view", 							&mat.uniformIDs.view },
				{ "viewInv", 						&mat.uniformIDs.viewInv },
				{ "viewProjection", 				&mat.uniformIDs.viewProjection },
				{ "projection", 					&mat.uniformIDs.projection },
				{ "camPos", 						&mat.uniformIDs.camPos },
				{ "enableDiffuseSampler", 			&mat.uniformIDs.enableDiffuseTexture },
				{ "enableNormalSampler", 			&mat.uniformIDs.enableNormalTexture },
				{ "enableCubemapSampler", 			&mat.uniformIDs.enableCubemapTexture },
				{ "enableAlbedoSampler", 			&mat.uniformIDs.enableAlbedoSampler },
				{ "constAlbedo", 					&mat.uniformIDs.constAlbedo },
				{ "enableMetallicSampler", 			&mat.uniformIDs.enableMetallicSampler },
				{ "constMetallic", 					&mat.uniformIDs.constMetallic },
				{ "enableRoughnessSampler", 		&mat.uniformIDs.enableRoughnessSampler },
				{ "constRoughness", 				&mat.uniformIDs.constRoughness },
				{ "enableAOSampler",				&mat.uniformIDs.enableAOSampler },
				{ "constAO",						&mat.uniformIDs.constAO },
				{ "hdrEquirectangularSampler",		&mat.uniformIDs.hdrEquirectangularSampler },
				{ "enableIrradianceSampler",		&mat.uniformIDs.enableIrradianceSampler },
				{ "verticalScale",					&mat.uniformIDs.verticalScale },
			};

			const u32 uniformCount = sizeof(uniformInfo) / sizeof(uniformInfo[0]);

			for (size_t i = 0; i < uniformCount; ++i)
			{
				if (shader.shader.dynamicBufferUniforms.HasUniform(uniformInfo[i].name) ||
					shader.shader.constantBufferUniforms.HasUniform(uniformInfo[i].name))
				{
					*uniformInfo[i].id = glGetUniformLocation(shader.program, uniformInfo[i].name);
					if (*uniformInfo[i].id == -1)
					{
						Logger::LogWarning(std::string(uniformInfo[i].name) + " was not found for material " + createInfo->name + " (shader " + createInfo->shaderName + ")");
					}
				}
			}

			CheckGLErrorMessages();

			mat.material.diffuseTexturePath = createInfo->diffuseTexturePath;
			mat.material.generateDiffuseSampler = createInfo->generateDiffuseSampler;
			mat.material.enableDiffuseSampler = createInfo->enableDiffuseSampler;

			mat.material.normalTexturePath = createInfo->normalTexturePath;
			mat.material.generateNormalSampler = createInfo->generateNormalSampler;
			mat.material.enableNormalSampler = createInfo->enableNormalSampler;

			mat.material.frameBuffers = createInfo->frameBuffers;

			mat.material.enableCubemapSampler = createInfo->enableCubemapSampler;
			mat.material.generateCubemapSampler = createInfo->generateCubemapSampler || createInfo->generateHDRCubemapSampler;
			mat.material.cubemapSamplerSize = createInfo->generatedCubemapSize;
			mat.material.cubeMapFilePaths = createInfo->cubeMapFilePaths;

			assert(mat.material.cubemapSamplerSize.x <= MAX_TEXTURE_DIM);
			assert(mat.material.cubemapSamplerSize.y <= MAX_TEXTURE_DIM);

			mat.material.constAlbedo = glm::vec4(createInfo->constAlbedo, 0);
			mat.material.generateAlbedoSampler = createInfo->generateAlbedoSampler;
			mat.material.albedoTexturePath = createInfo->albedoTexturePath;
			mat.material.enableAlbedoSampler = createInfo->enableAlbedoSampler;

			mat.material.constMetallic = createInfo->constMetallic;
			mat.material.generateMetallicSampler = createInfo->generateMetallicSampler;
			mat.material.metallicTexturePath = createInfo->metallicTexturePath;
			mat.material.enableMetallicSampler = createInfo->enableMetallicSampler;

			mat.material.constRoughness = createInfo->constRoughness;
			mat.material.generateRoughnessSampler = createInfo->generateRoughnessSampler;
			mat.material.roughnessTexturePath = createInfo->roughnessTexturePath;
			mat.material.enableRoughnessSampler = createInfo->enableRoughnessSampler;

			mat.material.constAO = createInfo->constAO;
			mat.material.generateAOSampler = createInfo->generateAOSampler;
			mat.material.aoTexturePath = createInfo->aoTexturePath;
			mat.material.enableAOSampler = createInfo->enableAOSampler;

			mat.material.enableHDREquirectangularSampler = createInfo->enableHDREquirectangularSampler;
			mat.material.generateHDREquirectangularSampler = createInfo->generateHDREquirectangularSampler;
			mat.material.hdrEquirectangularTexturePath = createInfo->hdrEquirectangularTexturePath;

			mat.material.generateHDRCubemapSampler = createInfo->generateHDRCubemapSampler;

			mat.material.enableIrradianceSampler = createInfo->enableIrradianceSampler;
			mat.material.generateIrradianceSampler = createInfo->generateIrradianceSampler;
			mat.material.irradianceSamplerSize = createInfo->generatedIrradianceCubemapSize;

			mat.material.environmentMapPath = createInfo->environmentMapPath;

			mat.material.generateReflectionProbeMaps = createInfo->generateReflectionProbeMaps;

			mat.material.colorMultiplier = createInfo->colorMultiplier;

			if (shader.shader.needIrradianceSampler)
			{
				if (createInfo->irradianceSamplerMatID >= m_Materials.size())
				{
					Logger::LogError("material being initialized in GLRenderer::InitializeMaterial attempting to use invalid irradianceSamplerMatID: " + std::to_string(createInfo->irradianceSamplerMatID));
					mat.irradianceSamplerID = InvalidID;
				}
				else
				{
					mat.irradianceSamplerID = m_Materials[createInfo->irradianceSamplerMatID].irradianceSamplerID;
				}
			}
			if (shader.shader.needBRDFLUT)
			{
				if (m_BRDFTextureHandle.id == 0)
				{
					GenerateGLTexture_Empty(m_BRDFTextureHandle.id, m_BRDFTextureSize, false, m_BRDFTextureHandle.internalFormat, m_BRDFTextureHandle.format, m_BRDFTextureHandle.type);
					GenerateBRDFLUT(gameContext, m_BRDFTextureHandle.id, m_BRDFTextureSize);
				}
				mat.brdfLUTSamplerID = m_BRDFTextureHandle.id;
			}
			if (shader.shader.needPrefilteredMap)
			{
				if (createInfo->prefilterMapSamplerMatID >= m_Materials.size())
				{
					Logger::LogError("material being initialized in GLRenderer::InitializeMaterial attempting to use invalid prefilterMapSamplerMatID: " + std::to_string(createInfo->prefilterMapSamplerMatID));
					mat.prefilteredMapSamplerID = InvalidID;
				}
				else
				{
					mat.prefilteredMapSamplerID = m_Materials[createInfo->prefilterMapSamplerMatID].prefilteredMapSamplerID;
				}
			}

			mat.material.enablePrefilteredMap = createInfo->enablePrefilteredMap;
			mat.material.generatePrefilteredMap = createInfo->generatePrefilteredMap;
			mat.material.prefilteredMapSize = createInfo->generatedPrefilteredCubemapSize;

			mat.material.enableBRDFLUT = createInfo->enableBRDFLUT;

			struct SamplerCreateInfo
			{
				bool needed;
				bool create;
				u32* id;
				std::string filepath;
				std::string textureName;
				bool flipVertically;
				std::function<bool(u32&, const std::string&, bool, bool)> createFunction;
			};

			// Samplers that need to be loaded from file
			SamplerCreateInfo samplerCreateInfos[] =
			{
				{ shader.shader.needAlbedoSampler, mat.material.generateAlbedoSampler, &mat.albedoSamplerID, 
				createInfo->albedoTexturePath, "albedoSampler", false, GenerateGLTexture },
				{ shader.shader.needMetallicSampler, mat.material.generateMetallicSampler, &mat.metallicSamplerID, 
				createInfo->metallicTexturePath, "metallicSampler", false,GenerateGLTexture },
				{ shader.shader.needRoughnessSampler, mat.material.generateRoughnessSampler, &mat.roughnessSamplerID, 
				createInfo->roughnessTexturePath, "roughnessSampler" ,false, GenerateGLTexture },
				{ shader.shader.needAOSampler, mat.material.generateAOSampler, &mat.aoSamplerID, 
				createInfo->aoTexturePath, "aoSampler", false,GenerateGLTexture },
				{ shader.shader.needDiffuseSampler, mat.material.generateDiffuseSampler, &mat.diffuseSamplerID, 
				createInfo->diffuseTexturePath, "diffuseSampler", false,GenerateGLTexture },
				{ shader.shader.needNormalSampler, mat.material.generateNormalSampler, &mat.normalSamplerID, 
				createInfo->normalTexturePath, "normalSampler",false, GenerateGLTexture },
				{ shader.shader.needHDREquirectangularSampler, mat.material.generateHDREquirectangularSampler, &mat.hdrTextureID, 
				createInfo->hdrEquirectangularTexturePath, "hdrEquirectangularSampler", true, GenerateHDRGLTexture },
			};

			i32 binding = 0;
			for (SamplerCreateInfo& samplerCreateInfo : samplerCreateInfos)
			{
				if (samplerCreateInfo.needed)
				{
					if (samplerCreateInfo.create)
					{
						if (samplerCreateInfo.filepath.empty())
						{
							Logger::LogError("Empty file path specified in SamplerCreateInfo for texture " + samplerCreateInfo.textureName + " in material " + mat.material.name);
						}
						else
						{
							if (!GetLoadedTexture(samplerCreateInfo.filepath, *samplerCreateInfo.id))
							{
								// Texture hasn't been loaded yet, load it now
								samplerCreateInfo.createFunction(*samplerCreateInfo.id, samplerCreateInfo.filepath, samplerCreateInfo.flipVertically, false);
								m_LoadedTextures.insert({ samplerCreateInfo.filepath, *samplerCreateInfo.id });
							}

							i32 uniformLocation = glGetUniformLocation(shader.program, samplerCreateInfo.textureName.c_str());
							CheckGLErrorMessages();
							if (uniformLocation == -1)
							{
								Logger::LogWarning(samplerCreateInfo.textureName + " was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
							}
							else
							{
								glUniform1i(uniformLocation, binding);
								CheckGLErrorMessages();
							}
						}
					}
					// Always increment the binding, even when not binding anything
					++binding;
				}
			}

			for (auto& frameBufferPair : createInfo->frameBuffers)
			{
				const char* frameBufferName = frameBufferPair.first.c_str();
				i32 positionLocation = glGetUniformLocation(shader.program, frameBufferName);
				CheckGLErrorMessages();
				if (positionLocation == -1)
				{
					Logger::LogWarning(frameBufferPair.first + " was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(positionLocation, binding);
					CheckGLErrorMessages();
				}
				++binding;
			}


			if (createInfo->generateCubemapSampler)
			{
				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.cubemapSamplerID;
				cubemapCreateInfo.HDR = false;
				cubemapCreateInfo.generateMipmaps = false;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.filePaths = mat.material.cubeMapFilePaths;

				if (createInfo->cubeMapFilePaths[0].empty())
				{
					cubemapCreateInfo.textureSize = createInfo->generatedCubemapSize;
					GenerateGLCubemap(cubemapCreateInfo);
				}
				else
				{
					// Load from file
					GenerateGLCubemap(cubemapCreateInfo);

					i32 uniformLocation = glGetUniformLocation(shader.program, "cubemapSampler");
					CheckGLErrorMessages();
					if (uniformLocation == -1)
					{
						Logger::LogWarning("cubemapSampler was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
					}
					else
					{
						glUniform1i(uniformLocation, binding);
					}
					CheckGLErrorMessages();
					++binding;
				}
			}

			if (mat.material.generateReflectionProbeMaps)
			{
				mat.cubemapSamplerGBuffersIDs = {
					{ 0, "positionMetallicFrameBufferSampler", GL_RGBA16F, GL_RGBA },
					{ 0, "normalRoughnessFrameBufferSampler", GL_RGBA16F, GL_RGBA },
					{ 0, "albedoAOFrameBufferSampler", GL_RGBA, GL_RGBA },
				};

				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.cubemapSamplerID;
				cubemapCreateInfo.textureGBufferIDs = &mat.cubemapSamplerGBuffersIDs;
				cubemapCreateInfo.depthTextureID = &mat.cubemapDepthSamplerID;
				cubemapCreateInfo.HDR = true;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.generateMipmaps = false;
				cubemapCreateInfo.textureSize = createInfo->generatedCubemapSize;
				cubemapCreateInfo.generateDepthBuffers = createInfo->generateCubemapDepthBuffers;
			
				GenerateGLCubemap(cubemapCreateInfo);
			}
			else if (createInfo->generateHDRCubemapSampler)
			{
				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.cubemapSamplerID;
				cubemapCreateInfo.textureGBufferIDs = &mat.cubemapSamplerGBuffersIDs;
				cubemapCreateInfo.depthTextureID = &mat.cubemapDepthSamplerID;
				cubemapCreateInfo.HDR = true;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.generateMipmaps = false;
				cubemapCreateInfo.textureSize = createInfo->generatedCubemapSize;
				cubemapCreateInfo.generateDepthBuffers = createInfo->generateCubemapDepthBuffers;

				GenerateGLCubemap(cubemapCreateInfo);
			}

			if (shader.shader.needCubemapSampler)
			{
				// TODO: Save location for binding later?
				i32 uniformLocation = glGetUniformLocation(shader.program, "cubemapSampler");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("cubemapSampler was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (shader.shader.needBRDFLUT)
			{
				i32 uniformLocation = glGetUniformLocation(shader.program, "brdfLUT");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("brdfLUT was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (mat.material.generateIrradianceSampler)
			{
				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.irradianceSamplerID;
				cubemapCreateInfo.HDR = true;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.generateMipmaps = false;
				cubemapCreateInfo.textureSize = createInfo->generatedIrradianceCubemapSize;

				GenerateGLCubemap(cubemapCreateInfo);
			}

			if (shader.shader.needIrradianceSampler)
			{
				i32 uniformLocation = glGetUniformLocation(shader.program, "irradianceSampler");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("irradianceSampler was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			if (mat.material.generatePrefilteredMap)
			{
				GLCubemapCreateInfo cubemapCreateInfo = {};
				cubemapCreateInfo.program = shader.program;
				cubemapCreateInfo.textureID = &mat.prefilteredMapSamplerID;
				cubemapCreateInfo.HDR = true;
				cubemapCreateInfo.enableTrilinearFiltering = createInfo->enableCubemapTrilinearFiltering;
				cubemapCreateInfo.generateMipmaps = true;
				cubemapCreateInfo.textureSize = createInfo->generatedPrefilteredCubemapSize;

				GenerateGLCubemap(cubemapCreateInfo);
			}

			if (shader.shader.needPrefilteredMap)
			{
				i32 uniformLocation = glGetUniformLocation(shader.program, "prefilterMap");
				CheckGLErrorMessages();
				if (uniformLocation == -1)
				{
					Logger::LogWarning("prefilterMap was not found in material " + mat.material.name + " (shader " + shader.shader.name + ")");
				}
				else
				{
					glUniform1i(uniformLocation, binding);
				}
				CheckGLErrorMessages();
				++binding;
			}

			glUseProgram(0);

			return materialID;
		}

		u32 GLRenderer::InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo)
		{
			UNREFERENCED_PARAMETER(gameContext);

			const RenderID renderID = GetNextAvailableRenderID();

			GLRenderObject* renderObject = new GLRenderObject();
			renderObject->renderID = renderID;
			InsertNewRenderObject(renderObject);
			renderObject->materialID = createInfo->materialID;
			renderObject->cullFace = CullFaceToGLMode(createInfo->cullFace);
			renderObject->enableCulling = createInfo->enableCulling ? GL_TRUE : GL_FALSE;
			renderObject->depthTestReadFunc = DepthTestFuncToGlenum(createInfo->depthTestReadFunc);
			renderObject->depthWriteEnable = BoolToGLBoolean(createInfo->depthWriteEnable);

			assert(createInfo->transform);
			
			renderObject->materialName = m_Materials[renderObject->materialID].material.name;
			renderObject->name = createInfo->name;
			renderObject->transform = createInfo->transform;
			renderObject->visibleInSceneExplorer = createInfo->visibleInSceneExplorer;

			if (m_Materials.empty()) Logger::LogError("Render object is being created before any materials have been created!");
			if (renderObject->materialID >= m_Materials.size())
			{
				Logger::LogError("Uninitialized material with MaterialID " + std::to_string(renderObject->materialID));
				return renderID;
			}

			GLMaterial& material = m_Materials[renderObject->materialID];
			GLShader& shader = m_Shaders[material.material.shaderID];

			glUseProgram(shader.program);
			CheckGLErrorMessages();

			if (createInfo->vertexBufferData)
			{
				glGenVertexArrays(1, &renderObject->VAO);
				glBindVertexArray(renderObject->VAO);
				CheckGLErrorMessages();

				glGenBuffers(1, &renderObject->VBO);
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)createInfo->vertexBufferData->BufferSize, createInfo->vertexBufferData->pDataStart, GL_STATIC_DRAW);
				CheckGLErrorMessages();
			}

			renderObject->vertexBufferData = createInfo->vertexBufferData;

			if (createInfo->indices != nullptr)
			{
				renderObject->indices = createInfo->indices;
				renderObject->indexed = true;

				glGenBuffers(1, &renderObject->IBO);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderObject->IBO);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(createInfo->indices[0]) * createInfo->indices->size()), createInfo->indices->data(), GL_STATIC_DRAW);
			}

			glBindVertexArray(0);
			glUseProgram(0);

			return renderID;
		}

		void GLRenderer::PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			GLMaterial& material = m_Materials[renderObject->materialID];

			if (material.material.generateReflectionProbeMaps)
			{
				Logger::LogInfo("Capturing reflection probe");
				CaptureSceneToCubemap(gameContext, renderID);
				GenerateIrradianceSamplerFromCubemap(gameContext, renderObject->materialID);
				GeneratePrefilteredMapFromCubemap(gameContext, renderObject->materialID);
				Logger::LogInfo("Done");

				// Capture again to use just generated irradiance + prefilter sampler (TODO: Remove soon)
				Logger::LogInfo("Capturing reflection probe");
				CaptureSceneToCubemap(gameContext, renderID);
				GenerateIrradianceSamplerFromCubemap(gameContext, renderObject->materialID);
				GeneratePrefilteredMapFromCubemap(gameContext, renderObject->materialID);
				Logger::LogInfo("Done");

				// Display captured cubemap as skybox
				//m_Materials[m_RenderObjects[cubemapID]->materialID].cubemapSamplerID =
				//	m_Materials[m_RenderObjects[renderID]->materialID].cubemapSamplerID;
			}
			else if (material.material.generateIrradianceSampler)
			{
				GenerateCubemapFromHDREquirectangular(gameContext, renderObject->materialID, material.material.environmentMapPath);
				GenerateIrradianceSamplerFromCubemap(gameContext, renderObject->materialID);
				GeneratePrefilteredMapFromCubemap(gameContext, renderObject->materialID);
			}
		}

		void GLRenderer::GenerateCubemapFromHDREquirectangular(const GameContext& gameContext, MaterialID cubemapMaterialID, const std::string& environmentMapPath)
		{
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			MaterialCreateInfo equirectangularToCubeMatCreateInfo = {};
			equirectangularToCubeMatCreateInfo.name = "Equirectangular to Cube";
			equirectangularToCubeMatCreateInfo.shaderName = "equirectangular_to_cube";
			equirectangularToCubeMatCreateInfo.enableHDREquirectangularSampler = true;
			equirectangularToCubeMatCreateInfo.generateHDREquirectangularSampler = true;
			// TODO: Make cyclable at runtime
			equirectangularToCubeMatCreateInfo.hdrEquirectangularTexturePath = environmentMapPath;
			MaterialID equirectangularToCubeMatID = InitializeMaterial(gameContext, &equirectangularToCubeMatCreateInfo);

			GLShader* equirectangularToCubemapShader = &m_Shaders[m_Materials[equirectangularToCubeMatID].material.shaderID];
			GLMaterial* equirectangularToCubemapMaterial = &m_Materials[equirectangularToCubeMatID];

			if (!m_SkyBoxMesh)
			{
				GenerateSkybox(gameContext);
			}

			GLRenderObject* skyboxRenderObject = GetRenderObject(m_SkyBoxMesh->GetRenderID());
			GLMaterial* skyboxGLMaterial = &m_Materials[skyboxRenderObject->materialID];

			glUseProgram(equirectangularToCubemapShader->program);
			CheckGLErrorMessages();

			// TODO: Store what location this texture is at (might not be 0)
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_Materials[equirectangularToCubeMatID].hdrTextureID);
			CheckGLErrorMessages();

			// Update object's uniforms under this shader's program
			glUniformMatrix4fv(equirectangularToCubemapMaterial->uniformIDs.model, 1, false, &m_SkyBoxMesh->GetTransform().GetModelMatrix()[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(equirectangularToCubemapMaterial->uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
			CheckGLErrorMessages();

			glm::uvec2 cubemapSize = skyboxGLMaterial->material.cubemapSamplerSize;

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			CheckGLErrorMessages();
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			glViewport(0, 0, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			glBindVertexArray(skyboxRenderObject->VAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, skyboxRenderObject->VBO);
			CheckGLErrorMessages();

			glCullFace(skyboxRenderObject->cullFace);
			CheckGLErrorMessages();

			if (skyboxRenderObject->enableCulling) glEnable(GL_CULL_FACE);
			else glDisable(GL_CULL_FACE);

			glDepthFunc(skyboxRenderObject->depthTestReadFunc);
			CheckGLErrorMessages();

			for (u32 i = 0; i < 6; ++i)
			{
				glUniformMatrix4fv(equirectangularToCubemapMaterial->uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);
				CheckGLErrorMessages();

				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[cubemapMaterialID].cubemapSamplerID, 0);
				CheckGLErrorMessages();

				glDepthMask(GL_TRUE);

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				CheckGLErrorMessages();

				glDepthMask(skyboxRenderObject->depthWriteEnable);
				CheckGLErrorMessages();

				glDrawArrays(skyboxRenderObject->topology, 0, (GLsizei)skyboxRenderObject->vertexBufferData->VertexCount);
				CheckGLErrorMessages();
			}

			// Generate mip maps for generated cubemap
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[cubemapMaterialID].cubemapSamplerID);
			glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

			glUseProgram(0);
			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
		}

		void GLRenderer::GeneratePrefilteredMapFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID)
		{
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			MaterialCreateInfo prefilterMaterialCreateInfo = {};
			prefilterMaterialCreateInfo.name = "Prefilter";
			prefilterMaterialCreateInfo.shaderName = "prefilter";
			MaterialID prefilterMatID = InitializeMaterial(gameContext, &prefilterMaterialCreateInfo);
			GLMaterial& prefilterMat = m_Materials[prefilterMatID];
			GLShader& prefilterShader = m_Shaders[prefilterMat.material.shaderID];

			if (!m_SkyBoxMesh)
			{
				GenerateSkybox(gameContext);
			}

			GLRenderObject* skybox = GetRenderObject(m_SkyBoxMesh->GetRenderID());

			glUseProgram(prefilterShader.program);
			CheckGLErrorMessages();

			glUniformMatrix4fv(prefilterMat.uniformIDs.model, 1, false, &m_SkyBoxMesh->GetTransform().GetModelMatrix()[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(prefilterMat.uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
			CheckGLErrorMessages();

			glActiveTexture(GL_TEXTURE0); // TODO: Remove constant
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[cubemapMaterialID].cubemapSamplerID);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();

			glBindVertexArray(skybox->VAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, skybox->VBO);
			CheckGLErrorMessages();

			if (skybox->enableCulling) glEnable(GL_CULL_FACE);
			else glDisable(GL_CULL_FACE);

			glCullFace(skybox->cullFace);
			CheckGLErrorMessages();

			glDepthFunc(skybox->depthTestReadFunc);
			CheckGLErrorMessages();

			glDepthMask(skybox->depthWriteEnable);
			CheckGLErrorMessages();

			u32 maxMipLevels = 5;
			for (u32 mip = 0; mip < maxMipLevels; ++mip)
			{
				u32 mipWidth = (u32)(m_Materials[cubemapMaterialID].material.prefilteredMapSize.x * pow(0.5f, mip));
				u32 mipHeight = (u32)(m_Materials[cubemapMaterialID].material.prefilteredMapSize.y * pow(0.5f, mip));
				assert(mipWidth <= Renderer::MAX_TEXTURE_DIM);
				assert(mipHeight <= Renderer::MAX_TEXTURE_DIM);

				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
				CheckGLErrorMessages();

				glViewport(0, 0, mipWidth, mipHeight);
				CheckGLErrorMessages();

				real roughness = (real)mip / (real(maxMipLevels - 1));
				i32 roughnessUniformLocation = glGetUniformLocation(prefilterShader.program, "roughness");
				glUniform1f(roughnessUniformLocation, roughness);
				CheckGLErrorMessages();
				for (u32 i = 0; i < 6; ++i)
				{
					glUniformMatrix4fv(prefilterMat.uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);
					CheckGLErrorMessages();

					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[cubemapMaterialID].prefilteredMapSamplerID, mip);
					CheckGLErrorMessages();
					
					glDrawArrays(skybox->topology, 0, (GLsizei)skybox->vertexBufferData->VertexCount);
					CheckGLErrorMessages();
				}
			}

			// TODO: Make this a togglable bool param for the shader (or roughness param)
			// Visualize prefiltered map as skybox:
			//m_Materials[renderObject->materialID].cubemapSamplerID = m_Materials[renderObject->materialID].prefilteredMapSamplerID;

			glUseProgram(0);
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
		}

		void GLRenderer::GenerateBRDFLUT(const GameContext& gameContext, u32 brdfLUTTextureID, glm::uvec2 BRDFLUTSize)
		{
			GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			MaterialCreateInfo brdfMaterialCreateInfo = {};
			brdfMaterialCreateInfo.name = "BRDF";
			brdfMaterialCreateInfo.shaderName = "brdf";
			MaterialID brdfMatID = InitializeMaterial(gameContext, &brdfMaterialCreateInfo);

			if (m_1x1_NDC_Quad == nullptr)
			{
				VertexBufferData::CreateInfo quadVertexBufferDataCreateInfo = {};
				quadVertexBufferDataCreateInfo.positions_3D = {
					{ -1.0f,  1.0f, 0.0f },
					{ -1.0f, -1.0f, 0.0f },
					{ 1.0f,  1.0f, 0.0f },
					{ 1.0f, -1.0f, 0.0f },
				};
				quadVertexBufferDataCreateInfo.texCoords_UV = {
					{ 0.0f, 1.0f },
					{ 0.0f, 0.0f },
					{ 1.0f, 1.0f },
					{ 1.0f, 0.0f },
				};
				quadVertexBufferDataCreateInfo.attributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::UV;

				m_1x1_NDC_QuadVertexBufferData = {};
				m_1x1_NDC_QuadVertexBufferData.Initialize(&quadVertexBufferDataCreateInfo);

				m_1x1_NDC_QuadTransform = Transform::Identity();

				RenderObjectCreateInfo quadCreateInfo = {};
				quadCreateInfo.name = "1x1 Quad";
				quadCreateInfo.materialID = brdfMatID;
				quadCreateInfo.vertexBufferData = &m_1x1_NDC_QuadVertexBufferData;
				quadCreateInfo.transform = &m_1x1_NDC_QuadTransform;
				quadCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS;
				quadCreateInfo.depthWriteEnable = false;

				RenderID quadRenderID = InitializeRenderObject(gameContext, &quadCreateInfo);
				m_1x1_NDC_Quad = GetRenderObject(quadRenderID);

				if (!m_1x1_NDC_Quad)
				{
					Logger::LogError("Failed to create 1x1 NDC quad!");
				}
				else
				{
					SetTopologyMode(quadRenderID, TopologyMode::TRIANGLE_STRIP);
					m_1x1_NDC_Quad->visible = false; // Don't render this normally, we'll draw it manually
					m_1x1_NDC_QuadVertexBufferData.DescribeShaderVariables(gameContext.renderer, quadRenderID);
				}
			}

			glUseProgram(m_Shaders[m_Materials[brdfMatID].material.shaderID].program);
			CheckGLErrorMessages();

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			CheckGLErrorMessages();

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTextureID, 0);
			CheckGLErrorMessages();

			glBindVertexArray(m_1x1_NDC_Quad->VAO);
			CheckGLErrorMessages();
			glBindBuffer(GL_ARRAY_BUFFER, m_1x1_NDC_Quad->VBO);
			CheckGLErrorMessages();

			glViewport(0, 0, BRDFLUTSize.x, BRDFLUTSize.y);
			CheckGLErrorMessages();

			if (m_1x1_NDC_Quad->enableCulling) glEnable(GL_CULL_FACE);
			else glDisable(GL_CULL_FACE);

			glCullFace(m_1x1_NDC_Quad->cullFace);
			CheckGLErrorMessages();

			glDepthFunc(m_1x1_NDC_Quad->depthTestReadFunc);
			CheckGLErrorMessages();

			glDepthMask(m_1x1_NDC_Quad->depthWriteEnable);
			CheckGLErrorMessages();

			// Render quad
			glDrawArrays(m_1x1_NDC_Quad->topology, 0, (GLsizei)m_1x1_NDC_Quad->vertexBufferData->VertexCount);
			CheckGLErrorMessages();
			
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glUseProgram(last_program);
			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
		}

		void GLRenderer::GenerateIrradianceSamplerFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID)
		{
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			// Irradiance sampler generation
			MaterialCreateInfo irrandianceMatCreateInfo = {};
			irrandianceMatCreateInfo.name = "Irradiance";
			irrandianceMatCreateInfo.shaderName = "irradiance";
			irrandianceMatCreateInfo.enableCubemapSampler = true;
			MaterialID irrandianceMatID = InitializeMaterial(gameContext, &irrandianceMatCreateInfo);
			GLMaterial& irradianceMat = m_Materials[irrandianceMatID];
			GLShader& shader = m_Shaders[irradianceMat.material.shaderID];

			if (!m_SkyBoxMesh)
			{
				GenerateSkybox(gameContext);
			}

			GLRenderObject* skybox = GetRenderObject(m_SkyBoxMesh->GetRenderID());

			glUseProgram(shader.program);
			CheckGLErrorMessages();

			glUniformMatrix4fv(irradianceMat.uniformIDs.model, 1, false, &m_SkyBoxMesh->GetTransform().GetModelMatrix()[0][0]);
			CheckGLErrorMessages();

			glUniformMatrix4fv(irradianceMat.uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
			CheckGLErrorMessages();

			glActiveTexture(GL_TEXTURE0); // TODO: Remove constant
			glBindTexture(GL_TEXTURE_CUBE_MAP, m_Materials[cubemapMaterialID].cubemapSamplerID);
			CheckGLErrorMessages();

			glm::uvec2 cubemapSize = m_Materials[cubemapMaterialID].material.irradianceSamplerSize;

			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			glViewport(0, 0, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			if (skybox->enableCulling) glEnable(GL_CULL_FACE);
			else glDisable(GL_CULL_FACE);

			glCullFace(skybox->cullFace);
			CheckGLErrorMessages();

			glDepthFunc(skybox->depthTestReadFunc);
			CheckGLErrorMessages();

			glDepthMask(skybox->depthWriteEnable);
			CheckGLErrorMessages();

			for (u32 i = 0; i < 6; ++i)
			{
				glBindVertexArray(skybox->VAO);
				CheckGLErrorMessages();
				glBindBuffer(GL_ARRAY_BUFFER, skybox->VBO);
				CheckGLErrorMessages();

				glUniformMatrix4fv(irradianceMat.uniformIDs.view, 1, false, &m_CaptureViews[i][0][0]);
				CheckGLErrorMessages();

				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_Materials[cubemapMaterialID].irradianceSamplerID, 0);
				CheckGLErrorMessages();

				// Should be drawing cube here, not object (relfection probe's sphere is being drawn
				glDrawArrays(skybox->topology, 0, (GLsizei)skybox->vertexBufferData->VertexCount);
				CheckGLErrorMessages();
			}

			glUseProgram(0);
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
		}

		void GLRenderer::CaptureSceneToCubemap(const GameContext& gameContext, RenderID cubemapRenderID)
		{
			GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);

			BatchRenderObjects(gameContext);

			DrawCallInfo drawCallInfo = {};
			drawCallInfo.renderToCubemap = true;
			drawCallInfo.cubemapObjectRenderID = cubemapRenderID;

			// Clear cubemap faces
			GLRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
			GLMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

			glm::uvec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;

			// Must be enabled to clear depth buffer
			glDepthMask(GL_TRUE);
			CheckGLErrorMessages();
			glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
			CheckGLErrorMessages();
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
			CheckGLErrorMessages();

			glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);
			CheckGLErrorMessages();

			for (size_t face = 0; face < 6; ++face)
			{
				// Clear all gbuffers
				if (!cubemapMaterial->cubemapSamplerGBuffersIDs.empty())
				{
					for (size_t i = 0; i < cubemapMaterial->cubemapSamplerGBuffersIDs.size(); ++i)
					{
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerGBuffersIDs[i].id, 0);
						CheckGLErrorMessages();

						glDepthMask(GL_TRUE);

						glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
						CheckGLErrorMessages();
					}
				}

				// Clear base cubemap framebuffer + depth buffer
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerID, 0);
				CheckGLErrorMessages();
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapDepthSamplerID, 0);
				CheckGLErrorMessages();

				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				CheckGLErrorMessages();
			}

			drawCallInfo.deferred = true;
			DrawDeferredObjects(gameContext, drawCallInfo);
			drawCallInfo.deferred = false;
			DrawGBufferQuad(gameContext, drawCallInfo);
			DrawForwardObjects(gameContext, drawCallInfo);
			
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SwapBuffers(const GameContext& gameContext)
		{
			glfwSwapBuffers(static_cast<GLWindowWrapper*>(gameContext.window)->GetWindow());
		}

		bool GLRenderer::GetShaderID(const std::string& shaderName, ShaderID& shaderID)
		{
			// TODO: Store shaders using sorted data structure?
			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				if (m_Shaders[i].shader.name.compare(shaderName) == 0)
				{
					shaderID = i;
					return true;
				}
			}

			return false;
		}

		bool GLRenderer::GetMaterialID(const std::string& materialName, MaterialID& materialID)
		{
			// TODO: Store shaders using sorted data structure?
			for (size_t i = 0; i < m_Materials.size(); ++i)
			{
				if (m_Materials[i].material.name.compare(materialName) == 0)
				{
					materialID = i;
					return true;
				}
			}

			return false;
		}

		DirectionalLightID GLRenderer::InitializeDirectionalLight(const DirectionalLight& dirLight)
		{
			m_DirectionalLight = dirLight;
			return 0;
		}

		PointLightID GLRenderer::InitializePointLight(const PointLight& pointLight)
		{
			m_PointLights.push_back(pointLight);
			return m_PointLights.size() - 1;
		}

		Renderer::DirectionalLight& GLRenderer::GetDirectionalLight(DirectionalLightID dirLightID)
		{
			// TODO: Add support for multiple directional lights
			UNREFERENCED_PARAMETER(dirLightID);
			return m_DirectionalLight;
		}

		Renderer::PointLight& GLRenderer::GetPointLight(PointLightID pointLight)
		{
			return m_PointLights[pointLight];
		}

		void GLRenderer::SetTopologyMode(RenderID renderID, TopologyMode topology)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			GLenum glMode = TopologyModeToGLMode(topology);

			if (glMode == GL_INVALID_ENUM)
			{
				Logger::LogError("Unhandled TopologyMode passed to GLRenderer::SetTopologyMode: " + std::to_string((i32)topology));
				renderObject->topology = GL_TRIANGLES;
			}
			else
			{
				renderObject->topology = glMode;
			}
		}

		void GLRenderer::SetClearColor(real r, real g, real b)
		{
			glClearColor(r, g, b, 1.0f);
			CheckGLErrorMessages();
		}

		void GLRenderer::PostInitialize(const GameContext& gameContext)
		{
			GLFWWindowWrapper* castedWindow = dynamic_cast<GLFWWindowWrapper*>(gameContext.window);
			if (castedWindow == nullptr)
			{
				Logger::LogError("GLRenderer::PostInitialize expects gameContext.window to be of type GLFWWindowWrapper!");
				return;
			}

			ImGui_ImplGlfwGL3_Init(castedWindow->GetWindow());
			CheckGLErrorMessages();


			m_PhysicsDebugDrawer = new GLPhysicsDebugDraw(gameContext);
			btDiscreteDynamicsWorld* world = gameContext.sceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			world->setDebugDrawer(m_PhysicsDebugDrawer);

			Logger::LogInfo("Ready!\n");
		}

		void GLRenderer::GenerateFrameBufferTexture(u32* handle, i32 index, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size)
		{
			glGenTextures(1, handle);
			glBindTexture(GL_TEXTURE_2D, *handle);
			CheckGLErrorMessages();
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, type, NULL);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			CheckGLErrorMessages();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			CheckGLErrorMessages();
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, *handle, 0);
			CheckGLErrorMessages();
			glBindTexture(GL_TEXTURE_2D, 0);
			CheckGLErrorMessages();
		}

		void GLRenderer::ResizeFrameBufferTexture(u32 handle, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size)
		{
			glBindTexture(GL_TEXTURE_2D, handle);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.x, size.y, 0, format, type, NULL);
			CheckGLErrorMessages();
		}

		void GLRenderer::ResizeRenderBuffer(u32 handle, const glm::vec2i& size)
		{
			glBindRenderbuffer(GL_RENDERBUFFER, handle);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, size.x, size.y);
		}

		void GLRenderer::Update(const GameContext& gameContext)
		{
			if (gameContext.inputManager->GetKeyDown(InputManager::KeyCode::KEY_U))
			{
				for (auto iter = m_RenderObjects.begin(); iter != m_RenderObjects.end(); ++iter)
				{
					GLRenderObject* renderObject = iter->second;
					if (renderObject && m_Materials[renderObject->materialID].material.generateReflectionProbeMaps)
					{
						Logger::LogInfo("Capturing reflection probe");
						CaptureSceneToCubemap(gameContext, renderObject->renderID);
						GenerateIrradianceSamplerFromCubemap(gameContext, renderObject->materialID);
						GeneratePrefilteredMapFromCubemap(gameContext, renderObject->materialID);
						Logger::LogInfo("Done");
					}
				}
			}
		}

		void GLRenderer::Draw(const GameContext& gameContext)
		{
			CheckGLErrorMessages();

			DrawCallInfo drawCallInfo = {};

			// TODO: Don't sort render objects frame! Only when things are added/removed
			BatchRenderObjects(gameContext);
			DrawDeferredObjects(gameContext, drawCallInfo);
			DrawGBufferQuad(gameContext, drawCallInfo);
			DrawForwardObjects(gameContext, drawCallInfo);
			DrawOffscreenTexture(gameContext);

			if (m_DrawPhysicsDebugObjects)
			{
				PhysicsDebugRender(gameContext);
			}

			ImGuiRender();

			SwapBuffers(gameContext);
		}

		void GLRenderer::BatchRenderObjects(const GameContext& gameContext)
		{
			/*
			TODO: Don't create two nested vectors every call, just sort things by deferred/forward, then by material ID
			

			Eg. deferred | matID
			yes		 0
			yes		 2
			no		 1
			no		 3
			no		 5
			*/
			m_DeferredRenderObjectBatches.clear();
			m_ForwardRenderObjectBatches.clear();
			
			// Sort render objects i32o deferred + forward buckets
			for (size_t i = 0; i < m_Materials.size(); ++i)
			{
				GLShader* shader = &m_Shaders[m_Materials[i].material.shaderID];

				UpdateMaterialUniforms(gameContext, i);

				if (shader->shader.deferred)
				{
					m_DeferredRenderObjectBatches.push_back({});
					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						GLRenderObject* renderObject = GetRenderObject(j);
						if (renderObject && renderObject->visible && renderObject->materialID == i)
						{
							m_DeferredRenderObjectBatches.back().push_back(renderObject);
						}
					}
				}
				else
				{
					m_ForwardRenderObjectBatches.push_back({});
					for (size_t j = 0; j < m_RenderObjects.size(); ++j)
					{
						GLRenderObject* renderObject = GetRenderObject(j);
						if (renderObject && renderObject->visible && renderObject->materialID == i)
						{
							m_ForwardRenderObjectBatches.back().push_back(renderObject);
						}
					}
				}
			}
		}

		void GLRenderer::DrawDeferredObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, m_gBufferDepthHandle);
			CheckGLErrorMessages();

			if (drawCallInfo.renderToCubemap)
			{
				// TODO: Bind depth buffer to cubemap's depth buffer (needs to generated?)

				GLRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
				GLMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

				glm::uvec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;

				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				CheckGLErrorMessages();
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				CheckGLErrorMessages();
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
				CheckGLErrorMessages();

				glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);
				CheckGLErrorMessages();
			}

			{
				// TODO: Make more dynamic (based on framebuffer count)
				constexpr i32 numBuffers = 3;
				u32 attachments[numBuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
				glDrawBuffers(numBuffers, attachments);
				CheckGLErrorMessages();
			}

			glDepthMask(GL_TRUE);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			CheckGLErrorMessages();

			for (size_t i = 0; i < m_DeferredRenderObjectBatches.size(); ++i)
			{
				if (!m_DeferredRenderObjectBatches[i].empty())
				{
					DrawRenderObjectBatch(gameContext, m_DeferredRenderObjectBatches[i], drawCallInfo);
				}
			}

			glUseProgram(0);
			glBindVertexArray(0);
			CheckGLErrorMessages();

			{
				constexpr i32 numBuffers = 1;
				u32 attachments[numBuffers] = { GL_COLOR_ATTACHMENT0 };
				glDrawBuffers(numBuffers, attachments);
				CheckGLErrorMessages();
			}

			// Copy depth from gbuffer to default render target
			if (drawCallInfo.renderToCubemap)
			{
				// No blit is needed, right? We already drew to the cubemap depth?
			}
			else
			{
				const glm::vec2i frameBufferSize = gameContext.window->GetFrameBufferSize();

				glBindFramebuffer(GL_READ_FRAMEBUFFER, m_gBufferHandle);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_OffscreenRBO);
				glBlitFramebuffer(0, 0, frameBufferSize.x, frameBufferSize.y, 0, 0, frameBufferSize.x, frameBufferSize.y, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				CheckGLErrorMessages();
			}
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		}

		void GLRenderer::DrawGBufferQuad(const GameContext& gameContext, const DrawCallInfo& drawCallInfo)
		{
			if (!m_gBufferQuadVertexBufferData.pDataStart)
			{
				// Generate GBuffer if not already generated
				GenerateGBuffer(gameContext);
			}

			if (drawCallInfo.renderToCubemap)
			{
				if (!m_SkyBoxMesh)
				{
					GenerateSkybox(gameContext);
				}

				GLRenderObject* skybox = GetRenderObject(m_SkyBoxMesh->GetRenderID());

				GLRenderObject* cubemapObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
				GLMaterial* cubemapMaterial = &m_Materials[cubemapObject->materialID];
				GLShader* cubemapShader = &m_Shaders[cubemapMaterial->material.shaderID];

				CheckGLErrorMessages();
				glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
				CheckGLErrorMessages();
				glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
				CheckGLErrorMessages();
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapMaterial->material.cubemapSamplerSize.x, cubemapMaterial->material.cubemapSamplerSize.y);
				CheckGLErrorMessages();

				glUseProgram(cubemapShader->program);

				glBindVertexArray(skybox->VAO);
				CheckGLErrorMessages();
				glBindBuffer(GL_ARRAY_BUFFER, skybox->VBO);
				CheckGLErrorMessages();

				UpdateMaterialUniforms(gameContext, cubemapObject->materialID);
				UpdatePerObjectUniforms(cubemapObject->materialID, skybox->transform->GetModelMatrix(), gameContext);

				u32 bindingOffset = BindDeferredFrameBufferTextures(cubemapMaterial);
				BindTextures(&cubemapShader->shader, cubemapMaterial, bindingOffset);

				if (skybox->enableCulling) glEnable(GL_CULL_FACE);
				else glDisable(GL_CULL_FACE);

				glCullFace(skybox->cullFace);
				CheckGLErrorMessages();

				glDepthFunc(GL_ALWAYS);
				CheckGLErrorMessages();

				glDepthMask(GL_FALSE);
				CheckGLErrorMessages();
				
				glUniformMatrix4fv(cubemapMaterial->uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
				CheckGLErrorMessages();

				glBindRenderbuffer(GL_RENDERBUFFER, 0);

				for (i32 face = 0; face < 6; ++face)
				{
					glUniformMatrix4fv(cubemapMaterial->uniformIDs.view, 1, false, &m_CaptureViews[face][0][0]);
					CheckGLErrorMessages();

					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerID, 0);
					CheckGLErrorMessages();

					// Draw cube (TODO: Use "cube" object to be less confusing)
					glDrawArrays(skybox->topology, 0, (GLsizei)skybox->vertexBufferData->VertexCount);
					CheckGLErrorMessages();
				}

				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				CheckGLErrorMessages();
			}
			else
			{
				glBindFramebuffer(GL_FRAMEBUFFER, m_OffscreenFBO);
				CheckGLErrorMessages();
				glBindRenderbuffer(GL_RENDERBUFFER, m_OffscreenRBO);
				CheckGLErrorMessages();

				glDepthMask(GL_TRUE);

				glClear(GL_COLOR_BUFFER_BIT); // Don't clear depth - we just copied it over from the GBuffer
				CheckGLErrorMessages();

				GLRenderObject* gBufferQuad = GetRenderObject(m_GBufferQuadRenderID);

				GLMaterial* material = &m_Materials[gBufferQuad->materialID];
				GLShader* glShader = &m_Shaders[material->material.shaderID];
				Shader* shader = &glShader->shader;
				glUseProgram(glShader->program);

				glBindVertexArray(gBufferQuad->VAO);
				glBindBuffer(GL_ARRAY_BUFFER, gBufferQuad->VBO);
				CheckGLErrorMessages();

				UpdateMaterialUniforms(gameContext, gBufferQuad->materialID);
				UpdatePerObjectUniforms(gBufferQuad->renderID, gameContext);

				u32 bindingOffset = BindFrameBufferTextures(material);
				BindTextures(shader, material, bindingOffset);

				if (gBufferQuad->enableCulling) glEnable(GL_CULL_FACE);
				else glDisable(GL_CULL_FACE);

				glCullFace(gBufferQuad->cullFace);
				CheckGLErrorMessages();

				glDepthFunc(gBufferQuad->depthTestReadFunc);
				CheckGLErrorMessages();

				glDepthMask(gBufferQuad->depthWriteEnable);
				CheckGLErrorMessages();

				glDrawArrays(gBufferQuad->topology, 0, (GLsizei)gBufferQuad->vertexBufferData->VertexCount);
				CheckGLErrorMessages();
			}
		}

		void GLRenderer::DrawForwardObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, m_OffscreenFBO);
			CheckGLErrorMessages();
			glBindRenderbuffer(GL_RENDERBUFFER, m_OffscreenRBO);
			CheckGLErrorMessages();

			for (size_t i = 0; i < m_ForwardRenderObjectBatches.size(); ++i)
			{
				if (!m_ForwardRenderObjectBatches[i].empty())
				{
					DrawRenderObjectBatch(gameContext, m_ForwardRenderObjectBatches[i], drawCallInfo);
				}
			}
		}

		void GLRenderer::DrawOffscreenTexture(const GameContext& gameContext)
		{
			DrawSpriteQuad(gameContext, m_OffscreenTextureHandle.id, m_PostProcessMatID, true);
		}

		void GLRenderer::DrawRenderObjectBatch(const GameContext& gameContext, const std::vector<GLRenderObject*>& batchedRenderObjects, const DrawCallInfo& drawCallInfo)
		{
			assert(!batchedRenderObjects.empty());

			GLMaterial* material = &m_Materials[batchedRenderObjects[0]->materialID];
			GLShader* glShader = &m_Shaders[material->material.shaderID];
			Shader* shader = &glShader->shader;
			glUseProgram(glShader->program);
			CheckGLErrorMessages();

			for (size_t i = 0; i < batchedRenderObjects.size(); ++i)
			{
				GLRenderObject* renderObject = batchedRenderObjects[i];
				if (!renderObject->visible)
				{
					continue;
				}

				glBindVertexArray(renderObject->VAO);
				CheckGLErrorMessages();
				glBindBuffer(GL_ARRAY_BUFFER, renderObject->VBO);
				CheckGLErrorMessages();

				if (renderObject->enableCulling)
				{
					glEnable(GL_CULL_FACE);

					glCullFace(renderObject->cullFace);
					CheckGLErrorMessages();
				}
				else
				{
					glDisable(GL_CULL_FACE);
				}

				glDepthFunc(renderObject->depthTestReadFunc);
				CheckGLErrorMessages();

				glDepthMask(renderObject->depthWriteEnable);
				CheckGLErrorMessages();

				UpdatePerObjectUniforms(renderObject->renderID, gameContext);

				BindTextures(shader, material);

				// TODO: OPTIMIZATION: Create DrawRenderObjectBatchToCubemap rather than check bool
				if (drawCallInfo.renderToCubemap)
				{
					GLRenderObject* cubemapRenderObject = GetRenderObject(drawCallInfo.cubemapObjectRenderID);
					GLMaterial* cubemapMaterial = &m_Materials[cubemapRenderObject->materialID];

					glm::uvec2 cubemapSize = cubemapMaterial->material.cubemapSamplerSize;
					
					glBindFramebuffer(GL_FRAMEBUFFER, m_CaptureFBO);
					CheckGLErrorMessages();
					glBindRenderbuffer(GL_RENDERBUFFER, m_CaptureRBO);
					CheckGLErrorMessages();
					glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize.x, cubemapSize.y);
					CheckGLErrorMessages();
					
					glViewport(0, 0, (GLsizei)cubemapSize.x, (GLsizei)cubemapSize.y);
					CheckGLErrorMessages();

					// Use capture projection matrix
					glUniformMatrix4fv(material->uniformIDs.projection, 1, false, &m_CaptureProjection[0][0]);
					CheckGLErrorMessages();
					
					// TODO: Test if this is actually correct
					glm::vec3 cubemapTranslation = -cubemapRenderObject->transform->GetGlobalPosition();
					for (size_t face = 0; face < 6; ++face)
					{
						glm::mat4 view = glm::translate(m_CaptureViews[face], cubemapTranslation);

						// This doesn't work because it flips the winding order of things (I think), maybe just account for that?
						// Flip vertically to match cubemap, cubemap shouldn't even be captured here eventually?
						//glm::mat4 view = glm::translate(glm::scale(m_CaptureViews[face], glm::vec3(1.0f, -1.0f, 1.0f)), cubemapTranslation);
						
						glUniformMatrix4fv(material->uniformIDs.view, 1, false, &view[0][0]);
						CheckGLErrorMessages();

						if (drawCallInfo.deferred)
						{
							constexpr i32 numBuffers = 3;
							u32 attachments[numBuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
							glDrawBuffers(numBuffers, attachments);

							for (size_t j = 0; j < cubemapMaterial->cubemapSamplerGBuffersIDs.size(); ++j)
							{
								glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + j, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerGBuffersIDs[j].id, 0);
								CheckGLErrorMessages();
							}
						}
						else
						{
							glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapSamplerID, 0);
							CheckGLErrorMessages();
						}

						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapMaterial->cubemapDepthSamplerID, 0);
						CheckGLErrorMessages();

						// TODO: Move to translucent pass?
						if (shader->translucent)
						{
							glEnable(GL_BLEND);
							glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						}
						else
						{
							glDisable(GL_BLEND);
						}

						if (renderObject->indexed)
						{
							glDrawElements(renderObject->topology, (GLsizei)renderObject->indices->size(), GL_UNSIGNED_INT, (void*)renderObject->indices->data());
							CheckGLErrorMessages();
						}
						else
						{
							glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
							CheckGLErrorMessages();
						}
					}
				}
				else
				{
					// renderToCubemap is false, just render normally

					// TODO: Move to translucent pass?
					if (shader->translucent)
					{
						glEnable(GL_BLEND);
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					}
					else
					{
						glDisable(GL_BLEND);
					}

					if (renderObject->indexed)
					{
						glDrawElements(renderObject->topology, (GLsizei)renderObject->indices->size(), GL_UNSIGNED_INT, (void*)renderObject->indices->data());
						CheckGLErrorMessages();
					}
					else
					{
						glDrawArrays(renderObject->topology, 0, (GLsizei)renderObject->vertexBufferData->VertexCount);
						CheckGLErrorMessages();
					}
				}

			}
		}

		u32 GLRenderer::BindTextures(Shader* shader, GLMaterial* glMaterial, u32 startingBinding)
		{
			Material* material = &glMaterial->material;

			struct Tex
			{
				bool needed;
				bool enabled;
				u32 textureID;
				GLenum target;
			};

			std::vector<Tex> textures;
			textures.push_back({ shader->needAlbedoSampler, material->enableAlbedoSampler, glMaterial->albedoSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needMetallicSampler, material->enableMetallicSampler, glMaterial->metallicSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needRoughnessSampler, material->enableRoughnessSampler, glMaterial->roughnessSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needAOSampler, material->enableAOSampler, glMaterial->aoSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needDiffuseSampler, material->enableDiffuseSampler, glMaterial->diffuseSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needNormalSampler, material->enableNormalSampler, glMaterial->normalSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needBRDFLUT, material->enableBRDFLUT, glMaterial->brdfLUTSamplerID, GL_TEXTURE_2D });
			textures.push_back({ shader->needIrradianceSampler, material->enableIrradianceSampler, glMaterial->irradianceSamplerID, GL_TEXTURE_CUBE_MAP });
			textures.push_back({ shader->needPrefilteredMap, material->enablePrefilteredMap, glMaterial->prefilteredMapSamplerID, GL_TEXTURE_CUBE_MAP });
			textures.push_back({ shader->needCubemapSampler, material->enableCubemapSampler, glMaterial->cubemapSamplerID, GL_TEXTURE_CUBE_MAP });

			u32 binding = startingBinding;
			for (Tex& tex : textures)
			{
				if (tex.needed)
				{
					if (tex.enabled)
					{
						GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)binding);
						glActiveTexture(activeTexture);
						glBindTexture(tex.target, (GLuint)tex.textureID);
						CheckGLErrorMessages();
					}
					++binding;
				}
			}

			return binding;
		}

		u32 GLRenderer::BindFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding)
		{
			Material* material = &glMaterial->material;

			struct Tex
			{
				bool needed;
				bool enabled;
				u32 textureID;
			};

			if (material->frameBuffers.empty())
			{
				Logger::LogWarning("Attempted to bind frame buffers on material that doesn't contain any framebuffers!");
				return startingBinding;
			}

			u32 binding = startingBinding;
			for (auto& frameBuffer : material->frameBuffers)
			{
				GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)binding);
				glActiveTexture(activeTexture);
				glBindTexture(GL_TEXTURE_2D, *((GLuint*)frameBuffer.second));
				CheckGLErrorMessages();
				++binding;
			}

			return binding;
		}

		u32 GLRenderer::BindDeferredFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding)
		{
			struct Tex
			{
				bool needed;
				bool enabled;
				u32 textureID;
			};

			if (glMaterial->cubemapSamplerGBuffersIDs.empty())
			{
				Logger::LogWarning("Attempted to bind gbuffer samplers on material doesn't contain any gbuffer samplers!");
				return startingBinding;
			}

			u32 binding = startingBinding;
			for (auto& cubemapGBuffer : glMaterial->cubemapSamplerGBuffersIDs)
			{
				GLenum activeTexture = (GLenum)(GL_TEXTURE0 + (GLuint)binding);
				glActiveTexture(activeTexture);
				glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapGBuffer.id);
				CheckGLErrorMessages();
				++binding;
			}

			return binding;
		}

		bool GLRenderer::GetLoadedTexture(const std::string& filePath, u32& handle)
		{
			auto location = m_LoadedTextures.find(filePath);
			if (location == m_LoadedTextures.end())
			{
				return false;
			}
			else
			{
				handle = location->second;
				return true;
			}
		}

		void GLRenderer::ReloadShaders(GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);

			UnloadShaders();
			LoadShaders();

			CheckGLErrorMessages();
		}

		void GLRenderer::UnloadShaders()
		{
			const size_t shaderCount = m_Shaders.size();
			for (size_t i = 0; i < shaderCount; ++i)
			{
				glDeleteProgram(m_Shaders[i].program);
				CheckGLErrorMessages();
			}
			m_Shaders.clear();
		}

		void GLRenderer::LoadShaders()
		{
			m_Shaders = {
				{ "deferred_simple", RESOURCE_LOCATION + "shaders/GLSL/deferred_simple.vert", RESOURCE_LOCATION + "shaders/GLSL/deferred_simple.frag" },
				{ "deferred_combine", RESOURCE_LOCATION + "shaders/GLSL/deferred_combine.vert", RESOURCE_LOCATION + "shaders/GLSL/deferred_combine.frag" },
				{ "deferred_combine_cubemap", RESOURCE_LOCATION + "shaders/GLSL/deferred_combine_cubemap.vert", RESOURCE_LOCATION + "shaders/GLSL/deferred_combine_cubemap.frag" },
				{ "color", RESOURCE_LOCATION + "shaders/GLSL/color.vert", RESOURCE_LOCATION + "shaders/GLSL/color.frag" },
				{ "pbr", RESOURCE_LOCATION + "shaders/GLSL/pbr.vert", RESOURCE_LOCATION + "shaders/GLSL/pbr.frag" },
				{ "skybox", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/skybox.frag" },
				{ "equirectangular_to_cube", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/equirectangular_to_cube.frag" },
				{ "irradiance", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/irradiance.frag" },
				{ "prefilter", RESOURCE_LOCATION + "shaders/GLSL/skybox.vert", RESOURCE_LOCATION + "shaders/GLSL/prefilter.frag" },
				{ "brdf", RESOURCE_LOCATION + "shaders/GLSL/brdf.vert", RESOURCE_LOCATION + "shaders/GLSL/brdf.frag" },
				{ "background", RESOURCE_LOCATION + "shaders/GLSL/background.vert", RESOURCE_LOCATION + "shaders/GLSL/background.frag" },
				{ "sprite", RESOURCE_LOCATION + "shaders/GLSL/sprite.vert", RESOURCE_LOCATION + "shaders/GLSL/sprite.frag" },
				{ "post_process", RESOURCE_LOCATION + "shaders/GLSL/post_process.vert", RESOURCE_LOCATION + "shaders/GLSL/post_process.frag" },
			};

			ShaderID shaderID = 0;

			// TOOD: Determine this info automatically when parsing shader code

			// Deferred Simple
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.needDiffuseSampler = true;
			m_Shaders[shaderID].shader.needNormalSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("modelInvTranspose");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableDiffuseSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			++shaderID;

			// Deferred combine
			m_Shaders[shaderID].shader.deferred = false; // Sounds strange but this isn't deferred
			// m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.depthWriteEnable = false; // Disable depth writing
			m_Shaders[shaderID].shader.needBRDFLUT = true;
			m_Shaders[shaderID].shader.needIrradianceSampler = true;
			m_Shaders[shaderID].shader.needPrefilteredMap = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("camPos");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("pointLights");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("dirLight");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("irradianceSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("prefilterMap");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("brdfLUT");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("positionMetallicFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("normalRoughnessFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("albedoAOFrameBufferSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableIrradianceSampler");
			++shaderID;

			// Deferred combine cubemap
			m_Shaders[shaderID].shader.deferred = false; // Sounds strange but this isn't deferred
			// m_Shaders[shaderID].shader.subpass = 0;
			m_Shaders[shaderID].shader.depthWriteEnable = false; // Disable depth writing
			m_Shaders[shaderID].shader.needBRDFLUT = true;
			m_Shaders[shaderID].shader.needIrradianceSampler = true;
			m_Shaders[shaderID].shader.needPrefilteredMap = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("camPos");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("pointLights");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("dirLight");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("irradianceSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("prefilterMap");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("brdfLUT");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("positionMetallicFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("normalRoughnessFrameBufferSampler");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("albedoAOFrameBufferSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableIrradianceSampler");
			++shaderID;

			// Color
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.translucent = true;
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("colorMultiplier");
			++shaderID;

			// PBR
			m_Shaders[shaderID].shader.deferred = true;
			m_Shaders[shaderID].shader.needAlbedoSampler = true;
			m_Shaders[shaderID].shader.needMetallicSampler = true;
			m_Shaders[shaderID].shader.needRoughnessSampler = true;
			m_Shaders[shaderID].shader.needAOSampler = true;
			m_Shaders[shaderID].shader.needNormalSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constAlbedo");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableAlbedoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("albedoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constMetallic");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableMetallicSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("metallicSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constRoughness");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableRoughnessSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("roughnessSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableAOSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("constAO");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("aoSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableNormalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("normalSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Skybox
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("enableCubemapSampler");
			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("cubemapSampler");
			++shaderID;

			// Equirectangular to Cube
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needHDREquirectangularSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("hdrEquirectangularSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Irradiance
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Prefilter
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// BRDF
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Background
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.needCubemapSampler = true;

			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("view");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("projection");
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("cubemapSampler");

			m_Shaders[shaderID].shader.dynamicBufferUniforms.AddUniform("model");
			++shaderID;

			// Sprite
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms = {};

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			// Post processing
			m_Shaders[shaderID].shader.deferred = false;
			m_Shaders[shaderID].shader.constantBufferUniforms.AddUniform("verticalScale");

			m_Shaders[shaderID].shader.dynamicBufferUniforms = {};
			++shaderID;

			for (size_t i = 0; i < m_Shaders.size(); ++i)
			{
				m_Shaders[i].program = glCreateProgram();
				CheckGLErrorMessages();

				if (!LoadGLShaders(m_Shaders[i].program, m_Shaders[i]))
				{
					Logger::LogError("Couldn't load shaders " + m_Shaders[i].shader.vertexShaderFilePath + " and " + m_Shaders[i].shader.fragmentShaderFilePath + "!");
				}

				LinkProgram(m_Shaders[i].program);
			}

			CheckGLErrorMessages();
		}

		void GLRenderer::UpdateMaterialUniforms(const GameContext& gameContext, MaterialID materialID)
		{
			GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);

			GLMaterial* material = &m_Materials[materialID];
			GLShader* shader = &m_Shaders[material->material.shaderID];
			
			glUseProgram(shader->program);

			glm::mat4 proj = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 viewInv = glm::inverse(view);
			glm::mat4 viewProj = proj * view;
			glm::vec4 camPos = glm::vec4(gameContext.camera->GetPosition(), 0.0f);


			if (shader->shader.constantBufferUniforms.HasUniform("view"))
			{
				glUniformMatrix4fv(material->uniformIDs.view, 1, false, &view[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("viewInv"))
			{
				glUniformMatrix4fv(material->uniformIDs.viewInv, 1, false, &viewInv[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("projection"))
			{
				glUniformMatrix4fv(material->uniformIDs.projection, 1, false, &proj[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("viewProjection"))
			{
				glUniformMatrix4fv(material->uniformIDs.viewProjection, 1, false, &viewProj[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("camPos"))
			{
				glUniform4f(material->uniformIDs.camPos,
					camPos.x,
					camPos.y,
					camPos.z,
					camPos.w);
				CheckGLErrorMessages();
			}

			if (shader->shader.constantBufferUniforms.HasUniform("dirLight"))
			{
				if (m_DirectionalLight.enabled)
				{
					SetUInt(material->material.shaderID, "dirLight.enabled", 1);
					CheckGLErrorMessages();
					SetVec4f(material->material.shaderID, "dirLight.direction", m_DirectionalLight.direction);
					CheckGLErrorMessages();
					SetVec4f(material->material.shaderID, "dirLight.color", m_DirectionalLight.color);
					CheckGLErrorMessages();
				}
				else
				{
					SetUInt(material->material.shaderID, "dirLight.enabled", 0);
					CheckGLErrorMessages();
				}
			}

			if (shader->shader.constantBufferUniforms.HasUniform("pointLights"))
			{
				for (size_t i = 0; i < m_PointLights.size(); ++i)
				{
					const std::string numberStr = std::to_string(i);

					if (m_PointLights[i].enabled)
					{
						SetUInt(material->material.shaderID, "pointLights[" + numberStr + "].enabled", 1);
						CheckGLErrorMessages();

						SetVec4f(material->material.shaderID, "pointLights[" + numberStr + "].position", m_PointLights[i].position);
						CheckGLErrorMessages();

						SetVec4f(material->material.shaderID, "pointLights[" + numberStr + "].color", m_PointLights[i].color);
						CheckGLErrorMessages();
					}
					else
					{
						SetUInt(material->material.shaderID, "pointLights[" + numberStr + "].enabled", 0);
						CheckGLErrorMessages();
					}
				}
			}

			glUseProgram(last_program);
			CheckGLErrorMessages();
		}

		void GLRenderer::UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			glm::mat4 model = renderObject->transform->GetModelMatrix();
			UpdatePerObjectUniforms(renderObject->materialID, model, gameContext);
		}

		void GLRenderer::UpdatePerObjectUniforms(MaterialID materialID, const glm::mat4& model, const GameContext& gameContext)
		{
			// TODO: OPTIMIZATION: Investigate performance impact of caching each uniform and preventing updates to data that hasn't changed

			GLMaterial* material = &m_Materials[materialID];
			GLShader* shader = &m_Shaders[material->material.shaderID];

			glm::mat4 modelInv = glm::inverse(model);
			glm::mat4 proj = gameContext.camera->GetProjection();
			glm::mat4 view = gameContext.camera->GetView();
			glm::mat4 MVP = proj * view * model;
			glm::vec4 colorMultiplier = material->material.colorMultiplier;

			// TODO: Use set functions here (SetFloat, SetMatrix, ...)
			if (shader->shader.dynamicBufferUniforms.HasUniform("model"))
			{
				glUniformMatrix4fv(material->uniformIDs.model, 1, false, &model[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("modelInvTranspose"))
			{
				// OpenGL will transpose for us if we set the third param to true
				glUniformMatrix4fv(material->uniformIDs.modelInvTranspose, 1, true, &modelInv[0][0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("colorMultiplier"))
			{
				// OpenGL will transpose for us if we set the third param to true
				glUniform4fv(material->uniformIDs.colorMultiplier, 1, &colorMultiplier[0]);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableDiffuseSampler"))
			{
				glUniform1i(material->uniformIDs.enableDiffuseTexture, material->material.enableDiffuseSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableNormalSampler"))
			{
				glUniform1i(material->uniformIDs.enableNormalTexture, material->material.enableNormalSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableCubemapSampler"))
			{
				glUniform1i(material->uniformIDs.enableCubemapTexture, material->material.enableCubemapSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableAlbedoSampler"))
			{
				glUniform1ui(material->uniformIDs.enableAlbedoSampler, material->material.enableAlbedoSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constAlbedo"))
			{
				glUniform4f(material->uniformIDs.constAlbedo, material->material.constAlbedo.x, material->material.constAlbedo.y, material->material.constAlbedo.z, 0);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableMetallicSampler"))
			{
				glUniform1ui(material->uniformIDs.enableMetallicSampler, material->material.enableMetallicSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constMetallic"))
			{
				glUniform1f(material->uniformIDs.constMetallic, material->material.constMetallic);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableRoughnessSampler"))
			{
				glUniform1ui(material->uniformIDs.enableRoughnessSampler, material->material.enableRoughnessSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constRoughness"))
			{
				glUniform1f(material->uniformIDs.constRoughness, material->material.constRoughness);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableAOSampler"))
			{
				glUniform1ui(material->uniformIDs.enableAOSampler, material->material.enableAOSampler);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("constAO"))
			{
				glUniform1f(material->uniformIDs.constAO, material->material.constAO);
				CheckGLErrorMessages();
			}

			if (shader->shader.dynamicBufferUniforms.HasUniform("enableIrradianceSampler"))
			{
				glUniform1i(material->uniformIDs.enableIrradianceSampler, material->material.enableIrradianceSampler);
				CheckGLErrorMessages();
			}
		}

		void GLRenderer::OnWindowSize(i32 width, i32 height)
		{
			if (width == 0 || height == 0) return;

			glViewport(0, 0, width, height);
			CheckGLErrorMessages();

			const glm::vec2i newFrameBufferSize(width, height);

			glBindFramebuffer(GL_FRAMEBUFFER, m_OffscreenFBO);
			CheckGLErrorMessages();
			ResizeFrameBufferTexture(m_OffscreenTextureHandle.id,
				m_OffscreenTextureHandle.internalFormat,
				m_OffscreenTextureHandle.format,
				m_OffscreenTextureHandle.type,
				newFrameBufferSize);

			ResizeRenderBuffer(m_OffscreenRBO, newFrameBufferSize);


			glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferHandle);
			CheckGLErrorMessages();
			ResizeFrameBufferTexture(m_gBuffer_PositionMetallicHandle.id,
				m_gBuffer_PositionMetallicHandle.internalFormat,
				m_gBuffer_PositionMetallicHandle.format,
				m_gBuffer_PositionMetallicHandle.type,
				newFrameBufferSize);

			ResizeFrameBufferTexture(m_gBuffer_NormalRoughnessHandle.id,
				m_gBuffer_NormalRoughnessHandle.internalFormat,
				m_gBuffer_NormalRoughnessHandle.format,
				m_gBuffer_NormalRoughnessHandle.type,
				newFrameBufferSize);

			ResizeFrameBufferTexture(m_gBuffer_DiffuseAOHandle.id,
				m_gBuffer_DiffuseAOHandle.internalFormat,
				m_gBuffer_DiffuseAOHandle.format,
				m_gBuffer_DiffuseAOHandle.type,
				newFrameBufferSize);

			ResizeRenderBuffer(m_gBufferDepthHandle, newFrameBufferSize);
		}

		void GLRenderer::SetRenderObjectVisible(RenderID renderID, bool visible)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject) renderObject->visible = visible;
		}

		void GLRenderer::SetVSyncEnabled(bool enableVSync)
		{
			m_VSyncEnabled = enableVSync;
			glfwSwapInterval(enableVSync ? 1 : 0);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetFloat(ShaderID shaderID, const std::string& valName, real val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName.c_str());
			if (location == -1) Logger::LogWarning("Float " + valName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform1f(location, val);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetUInt(ShaderID shaderID, const std::string& valName, u32 val)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, valName.c_str());
			if (location == -1) Logger::LogWarning("Unsigned i32 " + valName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform1ui(location, val);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1) Logger::LogWarning("Vec2f " + vecName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform2f(location, vec[0], vec[1]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1) Logger::LogWarning("Vec3f " + vecName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform3f(location, vec[0], vec[1], vec[2]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, vecName.c_str());
			if (location == -1) Logger::LogWarning("Vec4f " + vecName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniform4f(location, vec[0], vec[1], vec[2], vec[3]);
			CheckGLErrorMessages();
		}

		void GLRenderer::SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat)
		{
			GLint location = glGetUniformLocation(m_Shaders[shaderID].program, matName.c_str());
			if (location == -1) Logger::LogWarning("Mat4f " + matName + " couldn't be found!");
			CheckGLErrorMessages();

			glUniformMatrix4fv(location, 1, false, &mat[0][0]);
			CheckGLErrorMessages();
		}

		void GLRenderer::GenerateSkybox(const GameContext& gameContext)
		{
			if (!m_SkyBoxMesh)
			{
				m_SkyBoxMesh = new MeshPrefab(m_SkyBoxMaterialID, "Skybox Mesh");
				m_SkyBoxMesh->LoadPrefabShape(gameContext, MeshPrefab::PrefabShape::SKYBOX);
				m_SkyBoxMesh->Initialize(gameContext);
				m_SkyBoxMesh->PostInitialize(gameContext);
			}
		}

		void GLRenderer::GenerateGBuffer(const GameContext& gameContext)
		{
			if (m_gBufferQuadVertexBufferData.pDataStart)
			{
				return; // GBuffer quad has already been generated
			}

			MaterialCreateInfo gBufferMaterialCreateInfo = {};
			gBufferMaterialCreateInfo.name = "GBuffer material";
			gBufferMaterialCreateInfo.shaderName = "deferred_combine";
			gBufferMaterialCreateInfo.enableIrradianceSampler = true;
			gBufferMaterialCreateInfo.irradianceSamplerMatID = m_ReflectionProbeMaterialID;
			gBufferMaterialCreateInfo.enablePrefilteredMap = true;
			gBufferMaterialCreateInfo.prefilterMapSamplerMatID = m_ReflectionProbeMaterialID;
			gBufferMaterialCreateInfo.enableBRDFLUT = true;
			gBufferMaterialCreateInfo.frameBuffers = {
				{ "positionMetallicFrameBufferSampler",  &m_gBuffer_PositionMetallicHandle.id },
				{ "normalRoughnessFrameBufferSampler",  &m_gBuffer_NormalRoughnessHandle.id },
				{ "albedoAOFrameBufferSampler",  &m_gBuffer_DiffuseAOHandle.id },
			};

			MaterialID gBufferMatID = InitializeMaterial(gameContext, &gBufferMaterialCreateInfo);

			VertexBufferData::CreateInfo gBufferQuadVertexBufferDataCreateInfo = {};

			gBufferQuadVertexBufferDataCreateInfo.positions_3D = {
				glm::vec3(-1.0f,  1.0f, 0.0f),
				glm::vec3(-1.0f, -1.0f, 0.0f),
				glm::vec3(1.0f,  1.0f, 0.0f),

				glm::vec3(1.0f, -1.0f, 0.0f),
				glm::vec3(1.0f,  1.0f, 0.0f),
				glm::vec3(-1.0f, -1.0f, 0.0f),
			};

			gBufferQuadVertexBufferDataCreateInfo.texCoords_UV = {
				glm::vec2(0.0f, 1.0f),
				glm::vec2(0.0f, 0.0f),
				glm::vec2(1.0f, 1.0f),

				glm::vec2(1.0f, 0.0f),
				glm::vec2(1.0f, 1.0f),
				glm::vec2(0.0f, 0.0f),
			};

			gBufferQuadVertexBufferDataCreateInfo.attributes = (u32)VertexAttribute::POSITION | (u32)VertexAttribute::UV;

			m_gBufferQuadVertexBufferData.Initialize(&gBufferQuadVertexBufferDataCreateInfo);

			m_gBufferQuadTransform = Transform::Identity();

			RenderObjectCreateInfo gBufferQuadCreateInfo = {};
			gBufferQuadCreateInfo.name = "G Buffer Quad";
			gBufferQuadCreateInfo.materialID = gBufferMatID;
			gBufferQuadCreateInfo.transform = &m_gBufferQuadTransform;
			gBufferQuadCreateInfo.vertexBufferData = &m_gBufferQuadVertexBufferData;
			gBufferQuadCreateInfo.depthTestReadFunc = DepthTestFunc::ALWAYS; // Ignore previous depth values
			gBufferQuadCreateInfo.depthWriteEnable = false; // Don't write GBuffer quad to depth buffer

			m_GBufferQuadRenderID = InitializeRenderObject(gameContext, &gBufferQuadCreateInfo);

			m_gBufferQuadVertexBufferData.DescribeShaderVariables(this, m_GBufferQuadRenderID);

			GLRenderObject* gBufferRenderObject = GetRenderObject(m_GBufferQuadRenderID);
			gBufferRenderObject->visible = false; // Don't render the g buffer normally, we'll handle it separately
		}

		u32 GLRenderer::GetRenderObjectCount() const
		{
			// TODO: Replace function with m_RenderObjects.size()? (only if no nullptr objects exist)
			u32 count = 0;

			for (auto renderObject : m_RenderObjects)
			{
				if (renderObject.second) ++count;
			}

			return count;
		}

		u32 GLRenderer::GetRenderObjectCapacity() const
		{
			return m_RenderObjects.size();
		}

		void GLRenderer::DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size,
			Renderer::Type renderType, bool normalized, i32 stride, void* pointer)
		{
			GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);

			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			GLMaterial* material = &m_Materials[renderObject->materialID];
			u32 program = m_Shaders[material->material.shaderID].program;


			glUseProgram(program);

			glBindVertexArray(renderObject->VAO);
			CheckGLErrorMessages();

			GLint location = glGetAttribLocation(program, variableName.c_str());
			if (location == -1)
			{
				//Logger::LogWarning("Invalid shader variable name: " + variableName);
				glBindVertexArray(0);
				return;
			}
			glEnableVertexAttribArray((GLuint)location);

			GLenum glRenderType = TypeToGLType(renderType);
			glVertexAttribPointer((GLuint)location, size, glRenderType, (GLboolean)normalized, stride, pointer);
			CheckGLErrorMessages();

			glBindVertexArray(0);

			glUseProgram(last_program);
		}

		void GLRenderer::SetSkyboxMaterial(MaterialID skyboxMaterialID)
		{
			assert(skyboxMaterialID >= 0 && skyboxMaterialID < m_Materials.size());

			m_SkyBoxMaterialID = skyboxMaterialID;

			for (u32 i = 0; i < m_RenderObjects.size(); ++i)
			{
				GLRenderObject* renderObject = GetRenderObject(i);
				if (renderObject && m_Shaders[m_Materials[renderObject->materialID].material.shaderID].shader.needPrefilteredMap)
				{
					GLMaterial* mat = &m_Materials[renderObject->materialID];
					mat->irradianceSamplerID = m_Materials[m_SkyBoxMaterialID].irradianceSamplerID;
					mat->prefilteredMapSamplerID = m_Materials[m_SkyBoxMaterialID].prefilteredMapSamplerID;
				}
			}
		}

		void GLRenderer::SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (renderObject)
			{
				renderObject->materialID = materialID;
			}
			else
			{
				Logger::LogError("SetRenderObjectMaterialID couldn't find render object with ID " + std::to_string(renderID));
			}
		}

		Renderer::Material& GLRenderer::GetMaterial(MaterialID matID)
		{
			return m_Materials[matID].material;
		}

		Renderer::Shader& GLRenderer::GetShader(ShaderID shaderID)
		{
			return m_Shaders[shaderID].shader;
		}

		void GLRenderer::Destroy(RenderID renderID)
		{
			GLRenderObject* renderObject = GetRenderObject(renderID);
			if (!renderObject) return;

			m_RenderObjects[renderObject->renderID] = nullptr;

			glDeleteBuffers(1, &renderObject->VBO);
			if (renderObject->indexed)
			{
				glDeleteBuffers(1, &renderObject->IBO);
			}

			SafeDelete(renderObject);
		}

		void GLRenderer::ImGuiNewFrame()
		{
			ImGui_ImplGlfwGL3_NewFrame();
		}

		void GLRenderer::ImGuiRender()
		{
			ImGui::Render();
			ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
		}

		void GLRenderer::PhysicsDebugRender(const GameContext& gameContext)
		{
			btDiscreteDynamicsWorld* physicsWorld = gameContext.sceneManager->CurrentScene()->GetPhysicsWorld()->GetWorld();
			physicsWorld->debugDrawWorld();

			//m_PhysicsDebugDrawer->Draw();
		}

		void GLRenderer::DrawImGuiItems(const GameContext& gameContext)
		{
			UNREFERENCED_PARAMETER(gameContext);

			if (ImGui::CollapsingHeader("Scene info"))
			{
				const u32 objectCount = GetRenderObjectCount();
				const u32 objectCapacity = GetRenderObjectCapacity();
				const std::string objectCountStr("Render object count/capacity: " + std::to_string(objectCount) + "/" + std::to_string(objectCapacity));
				ImGui::Text(objectCountStr.c_str());

				if (ImGui::TreeNode("Render Objects"))
				{
					for (size_t i = 0; i < m_RenderObjects.size(); ++i)
					{
						GLRenderObject* renderObject = GetRenderObject(i);

						if (renderObject && renderObject->visibleInSceneExplorer)
						{
							const std::string objectName(renderObject->name + "##" + std::to_string(i));

							const std::string objectID("##" + objectName + "-visble");
							ImGui::Checkbox(objectID.c_str(), &renderObject->visible);
							ImGui::SameLine();
							if (ImGui::TreeNode(objectName.c_str()))
							{
								Transform* transform = renderObject->transform;
								if (transform)
								{
									static int transformSpace = 0;

									static const char* localStr = "local";
									static const char* globalStr = "global";

									ImGui::RadioButton(localStr, &transformSpace, 0); ImGui::SameLine();
									ImGui::RadioButton(globalStr, &transformSpace, 1);

									const bool local = (transformSpace == 0);


									glm::vec3 translation = local ? transform->GetLocalPosition() : transform->GetGlobalPosition();
									glm::vec3 rotation = glm::eulerAngles(local ? transform->GetLocalRotation() : transform->GetGlobalRotation());
									glm::vec3 scale = local ? transform->GetLocalScale() : transform->GetGlobalScale();

									bool valueChanged = false;
									
									valueChanged = ImGui::DragFloat3("Translation", &translation[0], 0.1f) || valueChanged;
									valueChanged = ImGui::DragFloat3("Rotation", &rotation[0], 0.01f) || valueChanged;
									valueChanged = ImGui::DragFloat3("Scale", &scale[0], 0.01f) || valueChanged;

									if (valueChanged)
									{
										if (local)
										{
											transform->SetLocalPosition(translation);
											transform->SetLocalRotation(glm::quat(rotation));
											transform->SetLocalScale(scale);
										}
										else
										{
											transform->SetGlobalPosition(translation);
											transform->SetGlobalRotation(glm::quat(rotation));
											transform->SetGlobalScale(scale);
										}
									}
								}
								else
								{
									ImGui::Text("Transform not set");
								}

								GLMaterial* material = &m_Materials[m_RenderObjects[i]->materialID];
								if (material->uniformIDs.enableIrradianceSampler)
								{
									ImGui::Checkbox("Enable Irradiance Sampler", &material->material.enableIrradianceSampler);
								}

								ImGui::TreePop();
							}
						}
					}

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Lights"))
				{
					ImGui::AlignFirstTextHeightToWidgets();

					ImGuiColorEditFlags colorEditFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_RGB | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_HDR;

					bool dirLightEnabled = m_DirectionalLight.enabled == 1;
					ImGui::Checkbox("##dir-light-enabled", &dirLightEnabled);
					m_DirectionalLight.enabled = dirLightEnabled ? 1 : 0;
					ImGui::SameLine();
					if (ImGui::TreeNode("Directional Light"))
					{
						ImGui::DragFloat3("Rotation", &m_DirectionalLight.direction.x, 0.01f);

						CopyableColorEdit4("Color ", m_DirectionalLight.color, "c##diffuse", "p##color", colorEditFlags);

						ImGui::TreePop();
					}

					for (size_t i = 0; i < m_PointLights.size(); ++i)
					{
						const std::string iStr = std::to_string(i);
						const std::string objectName("Point Light##" + iStr);

						bool PointLightEnabled = m_PointLights[i].enabled == 1;
						ImGui::Checkbox(std::string("##enabled" + iStr).c_str(), &PointLightEnabled);
						m_PointLights[i].enabled = PointLightEnabled ? 1 : 0;
						ImGui::SameLine();
						if (ImGui::TreeNode(objectName.c_str()))
						{
							ImGui::DragFloat3("Translation", &m_PointLights[i].position.x, 0.1f);

							CopyableColorEdit4("Color ", m_PointLights[i].color, "c##diffuse", "p##color", colorEditFlags);

							ImGui::TreePop();
						}
					}

					ImGui::TreePop();
				}
			}
		}

		GLRenderObject* GLRenderer::GetRenderObject(RenderID renderID)
		{
#if _DEBUG
			auto result = m_RenderObjects.find(renderID);
			if (result != m_RenderObjects.end())
			{
				return result->second;
			}

			Logger::LogError("Couldn't find GLRenderObject with renderID " + std::to_string(renderID));
			return nullptr;
#else
			return m_RenderObjects[renderID];
#endif
		}

		void GLRenderer::InsertNewRenderObject(GLRenderObject* renderObject)
		{
			if (renderObject->renderID < m_RenderObjects.size())
			{
				assert(m_RenderObjects[renderObject->renderID] == nullptr);
				m_RenderObjects[renderObject->renderID] = renderObject;
			}
			else
			{
				m_RenderObjects.insert({ renderObject->renderID, renderObject });
			}
		}

		MaterialID GLRenderer::GetNextAvailableMaterialID()
		{
			return m_Materials.size();
		}

		RenderID GLRenderer::GetNextAvailableRenderID() const
		{
			return m_RenderObjects.size();
		}


		void SetClipboardText(void* userData, const char* text)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			glfwWindow->SetClipboardText(text);
		}

		const char* GetClipboardText(void* userData)
		{
			GLFWWindowWrapper* glfwWindow = static_cast<GLFWWindowWrapper*>(userData);
			return glfwWindow->GetClipboardText();
		}

	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL
