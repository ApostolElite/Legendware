// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#include "animation_system.h"
#include "..\ragebot\aim.h"
#include "resolver.h"

float math::ApproachAngle(float target, float value, float speed) {
	target = (target * 182.04445f) * 0.0054931641f;
	value = (value * 182.04445f) * 0.0054931641f;

	float delta = target - value;

	// Speed is assumed to be positive
	if (speed < 0)
		speed = -speed;

	if (delta < -180.0f)
		delta += 360.0f;
	else if (delta > 180.0f)
		delta -= 360.0f;

	if (delta > speed)
		value += speed;
	else if (delta < -speed)
		value -= speed;
	else
		value = target;

	return value;
}

static auto GetSmoothedVelocity = [](float min_delta, Vector a, Vector b) {
	Vector delta = a - b;
	float delta_length = delta.Length();

	if (delta_length <= min_delta) {
		Vector result;
		if (-min_delta <= delta_length) {
			return a;
		}
		else {
			float iradius = 1.0f / (delta_length + FLT_EPSILON);
			return b - ((delta * iradius) * min_delta);
		}
	}
	else {
		float iradius = 1.0f / (delta_length + FLT_EPSILON);
		return b + ((delta * iradius) * min_delta);
	}
};


void resolver::initialize(player_t* e, adjust_data* record, const float& goal_feet_yaw, const float& pitch)
{
	player = e;
	player_record = record;

	original_goal_feet_yaw = math::normalize_yaw(goal_feet_yaw);
	original_pitch = math::normalize_pitch(pitch);
}

void resolver::reset()
{
	player = nullptr;
	player_record = nullptr;

	side = false;
	fake = false;

	was_first_bruteforce = false;
	was_second_bruteforce = false;

	original_goal_feet_yaw = 0.0f;
	original_pitch = 0.0f;
}

void resolver::resolve_yaw(player_t* e, adjust_data* record, const float& prev_goal_feet_yaw)
{
	if (g_cfg.ragebot.enable_resolver)
	{
		//////////////////////////////////////
		auto AnimState = player->get_animation_state();
		//	auto& resolverInfo = ResolverData::ResolvedData[player->EntIndex()];
			// Rebuild setup velocity to receive flMinBodyYaw and flMaxBodyYaw
		Vector velocity = player->m_vecVelocity();
		float speedFD = velocity.LengthSquared();

		if (speedFD > std::powf(1.2f * 260.0f, 2.f)) {
			Vector velocity_normalized = velocity.Normalized();
			velocity = velocity_normalized * (1.2f * 260.0f);
		}
		float m_flChokedTime = TIME_TO_TICKS(player->m_flSimulationTime() - player->m_flOldSimulationTime());
		float v25 = math::clamp(player->m_flDuckAmount() + AnimState->m_fLandingDuckAdditiveSomething, 0.0f, 1.0f);
		float v26 = AnimState->m_fDuckAmount;
		float v27 = m_flChokedTime * 6.0f;
		float v28;

		// clamp
		if ((v25 - v26) <= v27) {
			if (-v27 <= (v25 - v26))
				v28 = v25;
			else
				v28 = v26 - v27;
		}
		else {
			v28 = v26 + v27;
		}

		float flDuckAmount = math::clamp(v28, 0.0f, 1.0f);

		////////////////////FixEnd////////////////////////////

		Vector animationVelocity = GetSmoothedVelocity(m_flChokedTime * 2000.0f, velocity, player->m_vecVelocity());

		float speed = std::fminf(animationVelocity.Length(), 260.0f);

		auto weapon = player->m_hActiveWeapon().Get();

		auto weapon_info = g_ctx.globals.weapon->get_csweapon_info();

		float flMaxMovementSpeed = 260.0f;
		if (weapon) {
			auto flMaxMovementSpeed = 0.33f * (g_ctx.globals.scoped ? weapon_info->flMaxPlayerSpeedAlt : weapon_info->flMaxPlayerSpeed);
		}

		float flRunningSpeed = speed / (flMaxMovementSpeed * 0.520f);
		float flDuckingSpeed = speed / (flMaxMovementSpeed * 0.340f);

		flRunningSpeed = math::clamp(flRunningSpeed, 0.0f, 1.0f);

		float m_flGroundFraction = *(float*)(AnimState + 0x11C);

		float flYawModifier = (((m_flGroundFraction * -0.3f) - 0.2f) * flRunningSpeed) + 1.0f;

		if (flDuckAmount > 0.0f) {
			float flDuckingSpeed = math::clamp(flDuckingSpeed, 0.0f, 1.0f);
			flYawModifier += (flDuckAmount * flDuckingSpeed) * (0.5f - flYawModifier);
		}

		float m_flMinBodyYaw = std::fabsf(((float)(uintptr_t(AnimState) + 0x330)));
		float m_flMaxBodyYaw = std::fabsf(((float)(uintptr_t(AnimState) + 0x334)));

		float flMinBodyYaw = std::fabsf(m_flMinBodyYaw * flYawModifier);
		float flMaxBodyYaw = std::fabsf(m_flMaxBodyYaw * flYawModifier);

		float flEyeYaw = player->m_angEyeAngles().y;
		float flEyeDiff = std::remainderf(flEyeYaw - resolverInfo.m_flFakeGoalFeetYaw, 360.f);

		if (flEyeDiff <= flMaxBodyYaw) {
			if (flMinBodyYaw > flEyeDiff)
				resolverInfo.m_flFakeGoalFeetYaw = fabs(flMinBodyYaw) + flEyeYaw;
		}
		else {
			resolverInfo.m_flFakeGoalFeetYaw = flEyeYaw - fabs(flMaxBodyYaw);
		}

		resolverInfo.m_flFakeGoalFeetYaw = std::remainderf(resolverInfo.m_flFakeGoalFeetYaw, 360.f);

		if (speed > 0.1f || fabs(velocity.z) > 100.0f) {
			resolverInfo.m_flFakeGoalFeetYaw = math::ApproachAngle(
				flEyeYaw,
				resolverInfo.m_flFakeGoalFeetYaw,
				((AnimState->m_flMovingFraction * 20.0f) + 30.0f)
				* m_flChokedTime);
		}
		else {
			resolverInfo.m_flFakeGoalFeetYaw = math::ApproachAngle(
				player->m_flLowerBodyYawTarget(),
				resolverInfo.m_flFakeGoalFeetYaw,
				m_flChokedTime * 100.0f);
		}
		float Left = flEyeYaw - flMinBodyYaw;
		float Right = flEyeYaw + flMaxBodyYaw;

		player_info_t player_info;

		if (!m_engine()->GetPlayerInfo(player->EntIndex(), &player_info))
			return;

#if RELEASE
		if (player_info.fakeplayer || !g_ctx.local()->is_alive() || player->m_iTeamNum() == g_ctx.local()->m_iTeamNum()) //-V807
#else
		if (!g_ctx.local()->is_alive() || player->m_iTeamNum() == g_ctx.local()->m_iTeamNum())
#endif
		{
			player_record->side = RESOLVER_ORIGINAL;
			return;
		}

		if (g_ctx.globals.missed_shots[player->EntIndex()] >= 3 || g_ctx.globals.missed_shots[player->EntIndex()] && aim::get().last_target[player->EntIndex()].record.type != LBY)
		{
			switch (last_side)
			{
			case RESOLVER_ORIGINAL:
				g_ctx.globals.missed_shots[player->EntIndex()] = 0;
				fake = true;
				break;
			case RESOLVER_ZERO:
				player_record->type = BRUTEFORCE;
				player_record->side = RESOLVER_LOW_FIRST;

				was_first_bruteforce = false;
				was_second_bruteforce = false;

				return;
			case RESOLVER_FIRST:
				player_record->type = BRUTEFORCE;
				player_record->side = was_second_bruteforce ? RESOLVER_ZERO : RESOLVER_SECOND;

				was_first_bruteforce = true;

				return;
			case RESOLVER_SECOND:
				player_record->type = BRUTEFORCE;
				player_record->side = was_first_bruteforce ? RESOLVER_ZERO : RESOLVER_FIRST;

				was_second_bruteforce = true;

				return;
			case RESOLVER_LOW_FIRST:
				player_record->type = BRUTEFORCE;
				player_record->side = RESOLVER_LOW_SECOND;
				return;
			case RESOLVER_LOW_SECOND:
				player_record->type = BRUTEFORCE;
				player_record->side = RESOLVER_FIRST;
				return;
			}
		}

		auto animstate = player->get_animation_state();

		//	if (!animstate)
		//	{
			//	player_record->side = RESOLVER_ORIGINAL;
			//	return;
			//}

			//if (fabs(original_pitch) > 85.0f)
			//	fake = true;
		//	else if (!fake)
			//{
			//	player_record->side = RESOLVER_ORIGINAL;
			//	return;
			//}

		auto delta = math::normalize_yaw(player->m_angEyeAngles().y - original_goal_feet_yaw);
		auto valid_lby = true;

		if (animstate->m_velocity > 0.1f || fabs(animstate->flUpVelocity) > 100.f)
			valid_lby = animstate->m_flTimeSinceStartedMoving < 0.22f;

		if (fabs(delta) > 30.0f && valid_lby)
		{
			if (g_ctx.globals.missed_shots[player->EntIndex()])
				delta = -delta;

			if (delta > 30.0f)
			{
				player_record->type = LBY;
				player_record->side = RESOLVER_FIRST;
			}
			else if (delta < -30.0f)
			{
				player_record->type = LBY;
				player_record->side = RESOLVER_SECOND;
			}
		}
		else
		{
			auto trace = false;

			if (m_globals()->m_curtime - lock_side > 2.0f)
			{
				auto first_visible = util::visible(g_ctx.globals.eye_pos, player->hitbox_position_matrix(HITBOX_HEAD, player_record->matrixes_data.first), player, g_ctx.local());
				auto second_visible = util::visible(g_ctx.globals.eye_pos, player->hitbox_position_matrix(HITBOX_HEAD, player_record->matrixes_data.second), player, g_ctx.local());

				if (first_visible != second_visible)
				{
					trace = true;
					side = second_visible;
					lock_side = m_globals()->m_curtime;
				}
				else
				{
					auto first_position = g_ctx.globals.eye_pos.DistTo(player->hitbox_position_matrix(HITBOX_HEAD, player_record->matrixes_data.first));
					auto second_position = g_ctx.globals.eye_pos.DistTo(player->hitbox_position_matrix(HITBOX_HEAD, player_record->matrixes_data.second));

					if (fabs(first_position - second_position) > 1.0f)
						side = first_position > second_position;
				}
			}
			else
				trace = true;

			if (side)
			{
				player_record->type = trace ? TRACE : DIRECTIONAL;
				player_record->side = RESOLVER_FIRST;
			}
			else
			{
				player_record->type = trace ? TRACE : DIRECTIONAL;
				player_record->side = RESOLVER_SECOND;
			}
		}

		if (g_cfg.player_list.low_delta[e->EntIndex()])
		{
			switch (record->side)
			{
			case RESOLVER_FIRST:
				record->side = RESOLVER_LOW_FIRST;
				break;
			case RESOLVER_SECOND:
				record->side = RESOLVER_LOW_SECOND;
				break;
			case RESOLVER_LOW_FIRST:
				record->side = RESOLVER_FIRST;
				break;
			case RESOLVER_LOW_SECOND:
				record->side = RESOLVER_SECOND;
				break;
			}
		}

		switch (record->side)
		{
		case RESOLVER_ORIGINAL:
			//	animstate->m_flGoalFeetYaw = prev_goal_feet_yaw[e->EntIndex()];
			break;
		case RESOLVER_ZERO:
			animstate->m_flGoalFeetYaw = math::normalize_yaw(e->m_angEyeAngles().y);
			break;
		case RESOLVER_FIRST:
			animstate->m_flGoalFeetYaw = math::normalize_yaw(e->m_angEyeAngles().y + 60.0f);
			break;
		case RESOLVER_SECOND:
			animstate->m_flGoalFeetYaw = math::normalize_yaw(e->m_angEyeAngles().y - 60.0f);
			break;
		case RESOLVER_LOW_FIRST:
			animstate->m_flGoalFeetYaw = math::normalize_yaw(e->m_angEyeAngles().y + 30.0f);
			break;
		case RESOLVER_LOW_SECOND:
			animstate->m_flGoalFeetYaw = math::normalize_yaw(e->m_angEyeAngles().y - 30.0f);
			break;
		}

		//	switch (g_ctx.globals.missed_shots[player->EntIndex()] % 3)
			//{
			//case 0: // brute left side
			//	animstate->m_flGoalFeetYaw = math::normalize_yaw(Left);
			//	break;
			//case 1: // brute fake side
			//	animstate->m_flGoalFeetYaw = math::normalize_yaw(resolverInfo.m_flFakeGoalFeetYaw);
			//	break;
		//	case 2: // brute right side
		//		animstate->m_flGoalFeetYaw = math::normalize_yaw(Right);
		//		break;
		//	default:
			//	break;
			//}
	}
}

float resolver::resolve_pitch()
{
	return original_pitch;
}
