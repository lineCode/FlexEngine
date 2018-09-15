#pragma once

namespace flex
{
	class Player;

	class PlayerController final
	{
	public:
		PlayerController();
		~PlayerController();

		void Initialize(Player* player);
		void Update();
		void Destroy();

		void ResetTransformAndVelocities();

	private:
		real m_MoveAcceleration = 140.0f;
		real m_MaxMoveSpeed = 16.0f;
		real m_RotateSpeed = 4.0f;
		real m_RotateFriction = 0.05f;
		// How quickly to turn towards direction of movement
		real m_RotationSnappiness = 80.0f;
		// If the player has a velocity magnitude of this value or lower, their
		// rotation speed will linearly decrease as their velocity approaches 0
		real m_MaxSlowDownRotationSpeedVel = 10.0f;

		enum class Mode
		{
			THIRD_PERSON,
			FIRST_PERSON
		};

		Mode m_Mode = Mode::FIRST_PERSON;

		i32 m_PlayerIndex = -1;

		Player* m_Player = nullptr;

		bool m_bGrounded = false;

	};
} // namespace flex
