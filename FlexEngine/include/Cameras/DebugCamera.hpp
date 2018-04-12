#pragma once

#include "BaseCamera.hpp"

#include "GameContext.hpp"
#include "InputManager.hpp"

namespace flex
{
	class DebugCamera final : public BaseCamera
	{
	public:
		DebugCamera(GameContext& gameContext, real FOV = glm::radians(45.0f), real zNear = 0.1f, real zFar = 10000.0f);
		~DebugCamera();

		virtual void Update(const GameContext& gameContext) override;

		void SetMoveSpeed(real moveSpeed);
		real GetMoveSpeed() const;
		void SetRotationSpeed(real rotationSpeed);
		real GetRotationSpeed() const;

		void LoadDefaultKeybindings();
		void LoadAzertyKeybindings();

		void SetYaw(real rawRad);
		real GetYaw() const;
		void SetPitch(real pitchRad);
		real GetPitch() const;

	private:
		glm::vec3 m_DragStartPosition;

		InputManager::KeyCode m_MoveForwardKey;
		InputManager::KeyCode m_MoveBackwardKey;
		InputManager::KeyCode m_MoveLeftKey;
		InputManager::KeyCode m_MoveRightKey;
		InputManager::KeyCode m_MoveUpKey;
		InputManager::KeyCode m_MoveDownKey;
		InputManager::KeyCode m_MoveFasterKey;
		InputManager::KeyCode m_MoveSlowerKey;
	};
} // namespace flex
