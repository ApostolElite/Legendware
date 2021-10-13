// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#include "aim.h"
#include "..\misc\misc.h"
#include "..\misc\logs.h"
#include "..\autowall\autowall.h"
#include "..\misc\prediction_system.h"
#include "..\fakewalk\slowwalk.h"
#include "..\lagcompensation\local_animations.h"

void aim::run(CUserCmd* cmd)
{
	backup.clear();
	targets.clear();
	scanned_targets.clear();
	final_target.reset();
	should_stop = false;

	if (!g_cfg.ragebot.enable)
		return;

	automatic_revolver(cmd);
	prepare_targets();

	if (g_ctx.globals.weapon->is_non_aim())
		return;

	if (g_ctx.globals.current_weapon == -1)
		return;

	scan_targets();

	if (!should_stop && g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].autostop_modifiers[AUTOSTOP_PREDICTIVE])
	{
		auto max_speed = 260.0f;
		auto weapon_info = g_ctx.globals.weapon->get_csweapon_info();

		if (weapon_info)
			max_speed = g_ctx.globals.scoped ? weapon_info->flMaxPlayerSpeedAlt : weapon_info->flMaxPlayerSpeed;

		auto ticks_to_stop = math::clamp(engineprediction::get().backup_data.velocity.Length2D() / max_speed * 3.0f, 0.0f, 4.0f);
		auto predicted_eye_pos = g_ctx.globals.eye_pos + engineprediction::get().backup_data.velocity * m_globals()->m_intervalpertick * ticks_to_stop;

		for (auto& target : targets)
		{
			if (!target.last_record->valid())
				continue;

			scan_data last_data;

			target.last_record->adjust_player();
			scan(target.last_record, last_data, predicted_eye_pos, true);

			if (!last_data.valid())
				continue;

			should_stop = true;
			break;
		}
	}

	if (!automatic_stop(cmd))
		return;

	if (scanned_targets.empty())
		return;

	find_best_target();

	if (!final_target.data.valid())
		return;

	fire(cmd);
}

void aim::automatic_revolver(CUserCmd* cmd)
{
	if (!m_engine()->IsActiveApp())
		return;

	if (g_ctx.globals.weapon->m_iItemDefinitionIndex() != WEAPON_REVOLVER)
		return;

	if (cmd->m_buttons & IN_ATTACK)
		return;

	cmd->m_buttons &= ~IN_ATTACK2;

	static auto r8cock_time = 0.0f;
	auto server_time = TICKS_TO_TIME(g_ctx.globals.backup_tickbase);

	if (g_ctx.globals.weapon->can_fire(false))
	{
		if (r8cock_time <= server_time) //-V807
		{
			if (g_ctx.globals.weapon->m_flNextSecondaryAttack() <= server_time)
				r8cock_time = server_time + 0.234375f;
			else
				cmd->m_buttons |= IN_ATTACK2;
		}
		else
			cmd->m_buttons |= IN_ATTACK;
	}
	else
	{
		r8cock_time = server_time + 0.234375f;
		cmd->m_buttons &= ~IN_ATTACK;
	}

	g_ctx.globals.revolver_working = true;
}

void aim::prepare_targets()
{
	for (auto i = 1; i < m_globals()->m_maxclients; i++)
	{
		if (g_cfg.player_list.white_list[i])
			continue;

		auto e = (player_t*)m_entitylist()->GetClientEntity(i);

		if (!e->valid(true, false))
			continue;

		auto records = &player_records[i]; //-V826

		if (records->empty())
			continue;
	
		targets.emplace_back(target(e, get_record(records, false), get_record(records, true)));
	}

	if (targets.size() >= 5)///Для оптимизации => больше фпсов
	{
		auto first = rand() % targets.size();
		auto second = rand() % targets.size();
		auto third = rand() % targets.size();

		for (auto i = 0; i < targets.size(); ++i)
		{
			if (i == first || i == second || i == third)
				continue;

			targets.erase(targets.begin() + i);

			if (i > 0)
				--i;
		}
	}


	for (auto& target : targets)
		backup.emplace_back(adjust_data(target.e));
}

static bool compare_records(const optimized_adjust_data& first, const optimized_adjust_data& second)
{
	auto first_pitch = math::normalize_pitch(first.angles.x);
	auto second_pitch = math::normalize_pitch(second.angles.x);

	if (fabs(first_pitch - second_pitch) > 15.0f)
		return fabs(first_pitch) < fabs(second_pitch);
	else if (first.duck_amount != second.duck_amount) //-V550
		return first.duck_amount < second.duck_amount;
	else if (first.origin != second.origin)
		return first.origin.DistTo(g_ctx.local()->GetAbsOrigin()) < second.origin.DistTo(g_ctx.local()->GetAbsOrigin());

	return first.simulation_time > second.simulation_time;
}

adjust_data* aim::get_record(std::deque <adjust_data>* records, bool history)
{
	if (history)
	{
		std::deque <optimized_adjust_data> optimized_records; //-V826

		for (auto i = 0; i < records->size(); ++i)
		{
			auto record = &records->at(i);
			optimized_adjust_data optimized_record;

			optimized_record.i = i;
			optimized_record.player = record->player;
			optimized_record.simulation_time = record->simulation_time;
			optimized_record.duck_amount = record->duck_amount;
			optimized_record.angles = record->angles;
			optimized_record.origin = record->origin;

			optimized_records.emplace_back(optimized_record);
		}

		if (optimized_records.size() < 1)
			return nullptr;

		std::sort(optimized_records.begin(), optimized_records.end(), compare_records);

		for (auto& optimized_record : optimized_records)
		{
			auto record = &records->at(optimized_record.i);

			if (!record->valid())
				continue;

			return record;
		}
	}
	else
	{
		for (auto i = 0; i < records->size(); ++i)
		{
			auto record = &records->at(i);

			if (!record->valid())
				continue;

			return record;
		}
	}

	return nullptr;
}

int aim::get_minimum_damage(bool visible, int health)
{
	auto minimum_damage = 1;

	if (visible)
	{
		if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].minimum_visible_damage > 100)
			minimum_damage = health + g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].minimum_visible_damage - 100;
		else
			minimum_damage = math::clamp(g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].minimum_visible_damage, 1, health);
	}
	else
	{
		if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].minimum_damage > 100)
			minimum_damage = health + g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].minimum_damage - 100;
		else
			minimum_damage = math::clamp(g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].minimum_damage, 1, health);
	}

	if (key_binds::get().get_key_bind_state(4 + g_ctx.globals.current_weapon))
	{
		if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].minimum_override_damage > 100)
			minimum_damage = health + g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].minimum_override_damage - 100;
		else
			minimum_damage = math::clamp(g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].minimum_override_damage, 1, health);
	}
	return minimum_damage;
}

void aim::scan_targets()
{
	if (targets.empty())
		return;

	for (auto& target : targets)
	{
		if (target.history_record->valid())
		{
			scan_data last_data;

			if (target.last_record->valid())
			{
				target.last_record->adjust_player();
				scan(target.last_record, last_data);
			}

			scan_data history_data;

			target.history_record->adjust_player();
			scan(target.history_record, history_data);

			if (last_data.valid() && last_data.damage > history_data.damage)
			{
				scanned_targets.emplace_back(scanned_target(target.last_record, last_data)); //test
				//scanned_targets.emplace_back(scanned_target(target.history_record, history_data));
			}			 
			if (history_data.valid())
				//scanned_targets.emplace_back(scanned_target(target.last_record, last_data));
				scanned_targets.emplace_back(scanned_target(target.history_record, history_data));
		}
		else
		{
			if (!target.last_record->valid())
				continue;

			scan_data last_data;

			target.last_record->adjust_player();
			scan(target.last_record, last_data);

			if (!last_data.valid())
				continue;

			scanned_targets.emplace_back(scanned_target(target.last_record, last_data));
		}
	}
}

bool aim::automatic_stop(CUserCmd* cmd)
{
	if (!should_stop)
		return true;

	if (!g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].autostop)
		return true;

	if (g_ctx.globals.slowwalking)
		return true;

	if (!(g_ctx.local()->m_fFlags() & FL_ONGROUND && engineprediction::get().backup_data.flags & FL_ONGROUND)) //-V807
		return true;

	if (g_ctx.globals.weapon->is_empty())
		return true;

	if (!g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].autostop_modifiers[AUTOSTOP_BETWEEN_SHOTS] && !g_ctx.globals.weapon->can_fire(false))
		return true;

	auto animlayer = g_ctx.local()->get_animlayers()[1];

	if (animlayer.m_nSequence)
	{
		auto activity = g_ctx.local()->sequence_activity(animlayer.m_nSequence);

		if (activity == ACT_CSGO_RELOAD && animlayer.m_flWeight > 0.0f)
			return true;
	}

	auto weapon_info = g_ctx.globals.weapon->get_csweapon_info();

	if (!weapon_info)
		return true;

	auto max_speed = 0.33f * (g_ctx.globals.scoped ? weapon_info->flMaxPlayerSpeedAlt : weapon_info->flMaxPlayerSpeed);

	if (engineprediction::get().backup_data.velocity.Length2D() < max_speed)
		slowwalk::get().create_move(cmd);
	else
	{
		if (!g_cfg.ragebot.autostop_fixer || force_stop)
		{
			Vector direction;
			Vector real_view;

			math::vector_angles(engineprediction::get().backup_data.velocity, direction);
			m_engine()->GetViewAngles(real_view);

			direction.y = real_view.y - direction.y;

			Vector forward;
			math::angle_vectors(direction, forward);

			static auto cl_forwardspeed = m_cvar()->FindVar(crypt_str("cl_forwardspeed"));
			static auto cl_sidespeed = m_cvar()->FindVar(crypt_str("cl_sidespeed"));

			auto negative_forward_speed = -cl_forwardspeed->GetFloat();
			auto negative_side_speed = -cl_sidespeed->GetFloat();

			auto negative_forward_direction = forward * negative_forward_speed;
			auto negative_side_direction = forward * negative_side_speed;

			cmd->m_forwardmove = negative_forward_direction.x;
			cmd->m_sidemove = negative_side_direction.y;
		}
		else
		{
			Vector direction;
			Vector real_view;

			math::vector_angles(engineprediction::get().backup_data.velocity, direction);
			m_engine()->GetViewAngles(real_view);

			direction.y = real_view.y - direction.y;

			Vector forward;
			math::angle_vectors(direction, forward);

			static auto cl_forwardspeed = m_cvar()->FindVar(crypt_str("cl_forwardspeed"));
			static auto cl_sidespeed = m_cvar()->FindVar(crypt_str("cl_sidespeed"));

			auto negative_forward_speed = -cl_forwardspeed->GetFloat();
			auto negative_side_speed = -cl_sidespeed->GetFloat();

			auto negative_forward_direction = forward * negative_forward_speed;
			auto negative_side_direction = forward * negative_side_speed;

			cmd->m_forwardmove = negative_forward_direction.x;
			cmd->m_sidemove = negative_side_direction.y;

			if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].autostop_modifiers[AUTOSTOP_FORCE_ACCURACY])
				return false;
		}
	}

	return true;
}

static bool compare_points(const scan_point& first, const scan_point& second)
{
	return !first.center && first.hitbox == second.hitbox;
}

void aim::scan(adjust_data* record, scan_data& data, const Vector& shoot_position, bool optimized)
{
	auto weapon = optimized ? g_ctx.local()->m_hActiveWeapon().Get() : g_ctx.globals.weapon;

	if (!weapon)
		return;

	auto weapon_info = weapon->get_csweapon_info();

	if (!weapon_info)
		return;

	auto hitboxes = get_hitboxes(record, optimized);

	if (hitboxes.empty())
		return;

	auto force_safe_points = record->player->m_iHealth() <= weapon_info->iDamage || key_binds::get().get_key_bind_state(3) || g_cfg.player_list.force_safe_points[record->i] || g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].max_misses && g_ctx.globals.missed_shots[record->i] >= g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].max_misses_amount; //-V648
	auto best_damage = 0;

	auto minimum_damage = get_minimum_damage(false, record->player->m_iHealth());
	auto minimum_visible_damage = get_minimum_damage(true, record->player->m_iHealth());
	
	auto get_hitgroup = [](const int& hitbox) //need fixe hitpoints
	{		if (hitbox == HITBOX_HEAD)
			return 0;
		else if (hitbox == HITBOX_PELVIS)
			return 1;
		else if (hitbox == HITBOX_STOMACH)
			return 2;
		else if (hitbox >= HITBOX_LOWER_CHEST && hitbox <= HITBOX_UPPER_CHEST)
			return 3;
		else if (hitbox >= HITBOX_RIGHT_THIGH && hitbox <= HITBOX_LEFT_FOOT)
			return 4;
		else if (hitbox >= HITBOX_RIGHT_HAND && hitbox <= HITBOX_LEFT_FOREARM)
			return 5;
		else if (hitbox == HITBOX_NECK)
			return 6;
		else if (hitbox == HITBOX_CHEST)
			return 7;
	//	else if (hitbox >= HITBOX_RIGHT_THIGH && hitbox <= HITBOX_LEFT_THIGH)//HITBOX_LEFT_FOOT)
	//		return 6;
	//	else if (hitbox >= HITBOX_RIGHT_CALF && hitbox <= HITBOX_LEFT_CALF)
//		return 8;
	//	else if (hitbox >= HITBOX_RIGHT_HAND && hitbox <= HITBOX_LEFT_HAND)//HITBOX_LEFT_FOREARM)
		//	return 9;
	//	else if (hitbox >= HITBOX_RIGHT_FOOT && hitbox <= HITBOX_LEFT_FOOT)
		//	return 10;
	//	else if (hitbox >= HITBOX_RIGHT_UPPER_ARM && hitbox <= HITBOX_LEFT_UPPER_ARM)
	//		return 9;
		//else if (hitbox >= HITBOX_RIGHT_FOREARM && hitbox <= HITBOX_LEFT_FOREARM)
		//	return 12;
	//	return -1;
	};

	std::vector <scan_point> points; //-V826

	for (auto& hitbox : hitboxes)
	{
		if (optimized)
		{
			points.emplace_back(scan_point(record->player->hitbox_position_matrix(hitbox, record->matrixes_data.main), hitbox, true));
			continue;
		}

		auto current_points = get_points(record, hitbox);

		for (auto& point : current_points)
		{
			if (!record->bot)
			{
				auto safe = 0.0f;

				if (record->matrixes_data.zero[0].GetOrigin() == record->matrixes_data.first[0].GetOrigin() || record->matrixes_data.zero[0].GetOrigin() == record->matrixes_data.second[0].GetOrigin() || record->matrixes_data.first[0].GetOrigin() == record->matrixes_data.second[0].GetOrigin())
					safe = 0.0f;
				else if (!hitbox_intersection(record->player, record->matrixes_data.zero, hitbox, shoot_position, point.point, &safe))
					safe = 0.0f;
				else if (!hitbox_intersection(record->player, record->matrixes_data.first, hitbox, shoot_position, point.point, &safe))
					safe = 0.0f;
				else if (!hitbox_intersection(record->player, record->matrixes_data.second, hitbox, shoot_position, point.point, &safe))
					safe = 0.0f;

				point.safe = safe;
			}
			else
				point.safe = 1.0f;

			if (!force_safe_points || point.safe)
				points.emplace_back(point);
		}
	}

	if (!optimized)
	{
		for (auto& point : points)
		{
			if (points.empty())
				return;

			if (point.hitbox == HITBOX_HEAD)
				continue;

			for (auto it = points.begin(); it != points.end(); ++it)
			{
				if (point.point == it->point)
					continue;

				auto first_angle = math::calculate_angle(shoot_position, point.point);
				auto second_angle = math::calculate_angle(shoot_position, it->point);

				auto distance = shoot_position.DistTo(point.point);
				auto fov = math::fast_sin(DEG2RAD(math::get_fov(first_angle, second_angle))) * distance;

//				if (fov < 5.0f)
//				{
//					points.erase(it);
//					break;
//				}
			}
		}
	}

	if (points.empty())
		return;

	if (!optimized)
		std::sort(points.begin(), points.end(), compare_points);

	auto body_hitboxes = true;

	for (auto& point : points)
	{
		if (!optimized && body_hitboxes && (point.hitbox < HITBOX_PELVIS || point.hitbox > HITBOX_UPPER_CHEST))
		{
			body_hitboxes = false;

			if (g_cfg.player_list.force_body_aim[record->i])
				break;

			if (key_binds::get().get_key_bind_state(22))
				break;

			if (best_damage >= record->player->m_iHealth())
				break;

			if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].prefer_body_aim && best_damage >= 1)
				break;
		}

		if (!optimized && (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].prefer_safe_points || force_safe_points) && data.point.safe && data.point.safe < point.safe)
			continue;

		auto fire_data = autowall::get().wall_penetration(shoot_position, point.point, record->player);

		if (!fire_data.valid)
			continue;

		if (fire_data.damage < 1)
			continue;

		if (!fire_data.visible && !g_cfg.ragebot.autowall)
			continue;

		if (!optimized && get_hitgroup(fire_data.hitbox) != get_hitgroup(point.hitbox))
			continue;

		auto current_minimum_damage = fire_data.visible ? minimum_visible_damage : minimum_damage;

		if (fire_data.damage >= current_minimum_damage && fire_data.damage >= best_damage)
		{
			if (!optimized && !should_stop)
			{
				should_stop = true;

				if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].autostop_modifiers[AUTOSTOP_LETHAL] && fire_data.damage < record->player->m_iHealth())
					should_stop = false;
				else if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].autostop_modifiers[AUTOSTOP_VISIBLE] && !fire_data.visible)
					should_stop = false;
				else if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].autostop_modifiers[AUTOSTOP_CENTER] && !point.center)
					should_stop = false;
			}
			
			//if (!optimized && force_safe_points && !point.safe)
				//continue;

			best_damage = fire_data.damage;

			data.point = point;
			data.visible = fire_data.visible;
			data.damage = fire_data.damage;
			data.hitbox = fire_data.hitbox;

			if (optimized)
				return;
		}
	}
}

std::vector <int> aim::get_hitboxes(adjust_data* record, bool optimized)
{
	std::vector <int> hitboxes; //-V827

	if (optimized)
	{
		hitboxes.emplace_back(HITBOX_HEAD);
		hitboxes.emplace_back(HITBOX_CHEST);
		hitboxes.emplace_back(HITBOX_STOMACH);
		hitboxes.emplace_back(HITBOX_PELVIS);

		return hitboxes;
	}

	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitboxes.at(1))
		hitboxes.emplace_back(HITBOX_UPPER_CHEST);

	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitboxes.at(2))
		hitboxes.emplace_back(HITBOX_CHEST);

	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitboxes.at(3))
		hitboxes.emplace_back(HITBOX_LOWER_CHEST);

	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitboxes.at(4))
		hitboxes.emplace_back(HITBOX_STOMACH);

	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitboxes.at(5))
		hitboxes.emplace_back(HITBOX_PELVIS);

	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitboxes.at(0))
		hitboxes.emplace_back(HITBOX_HEAD);

	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitboxes.at(6))
	{
		hitboxes.emplace_back(HITBOX_RIGHT_UPPER_ARM);
		hitboxes.emplace_back(HITBOX_LEFT_UPPER_ARM);
	}

	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitboxes.at(7))
	{
		hitboxes.emplace_back(HITBOX_RIGHT_THIGH);
		hitboxes.emplace_back(HITBOX_LEFT_THIGH);

		hitboxes.emplace_back(HITBOX_RIGHT_CALF);
		hitboxes.emplace_back(HITBOX_LEFT_CALF);
	}

	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitboxes.at(8))
	{
		hitboxes.emplace_back(HITBOX_RIGHT_FOOT);
		hitboxes.emplace_back(HITBOX_LEFT_FOOT);
	}

	return hitboxes;
}

std::vector <scan_point> aim::get_points(adjust_data* record, int hitbox, bool from_aim)// надо добавить хтбоксы тут не все
{
	std::vector <scan_point> points;
	auto model = record->player->GetModel();

	if (!model)
		return points;

	auto hdr = m_modelinfo()->GetStudioModel(model);

	if (!hdr)
		return points;

	auto set = hdr->pHitboxSet(record->player->m_nHitboxSet());

	if (!set)
		return points;

	auto bbox = set->pHitbox(hitbox);

	if (!bbox)
		return points;

	auto center = (bbox->bbmin + bbox->bbmax) * 0.5f;

	if (bbox->radius <= 0.0f)
	{
		auto rotation_matrix = math::angle_matrix(bbox->rotation);

		matrix3x4_t matrix;
		math::concat_transforms(record->matrixes_data.main[bbox->bone], rotation_matrix, matrix);

		auto origin = matrix.GetOrigin();

		if (hitbox == HITBOX_RIGHT_FOOT || hitbox == HITBOX_LEFT_FOOT)
		{
			auto side = (bbox->bbmin.z - center.z) * 0.875f;

			if (hitbox == HITBOX_LEFT_FOOT)
				side = -side;

			points.emplace_back(scan_point(Vector(center.x, center.y, center.z + side), hitbox, true));

			auto min = (bbox->bbmin.x - center.x) * 0.875f;
			auto max = (bbox->bbmax.x - center.x) * 0.875f;

			points.emplace_back(scan_point(Vector(center.x + min, center.y, center.z), hitbox, false));
			points.emplace_back(scan_point(Vector(center.x + max, center.y, center.z), hitbox, false));
		}
	}
	else 
	{
		auto scale = 0.0f;

		if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].static_point_scale)
		{
			if (hitbox == HITBOX_HEAD)
				scale = g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].head_scale;
			else
				scale = g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].body_scale;
		}
		else
		{
			auto transformed_center = center;
			math::vector_transform(transformed_center, record->matrixes_data.main[bbox->bone], transformed_center);

			auto spread = g_ctx.globals.spread + g_ctx.globals.inaccuracy;
			auto distance = transformed_center.DistTo(g_ctx.globals.eye_pos);

			distance /= math::fast_sin(DEG2RAD(90.0f - RAD2DEG(spread)));
			spread = math::fast_sin(spread);

			auto radius = max(bbox->radius - distance * spread, 0.0f);
			scale = math::clamp(radius / bbox->radius, 0.0f, 1.0f);
		}

		if (scale <= 0.0f) //-V648
		{
			math::vector_transform(center, record->matrixes_data.main[bbox->bone], center);
			points.emplace_back(scan_point(center, hitbox, true));

			return points;
		}

		auto final_radius = bbox->radius * scale;

		if (hitbox == HITBOX_HEAD)
		{
			auto pitch_down = math::normalize_pitch(record->angles.x) > 85.0f;
			auto backward = fabs(math::normalize_yaw(record->angles.y - math::calculate_angle(record->player->get_shoot_position(), g_ctx.local()->GetAbsOrigin()).y)) > 120.0f;

			points.emplace_back(scan_point(center, hitbox, !pitch_down || !backward));

			points.emplace_back(scan_point(Vector(bbox->bbmax.x + 0.70710678f * final_radius, bbox->bbmax.y - 0.70710678f * final_radius, bbox->bbmax.z), hitbox, false));
			points.emplace_back(scan_point(Vector(bbox->bbmax.x, bbox->bbmax.y, bbox->bbmax.z + final_radius), hitbox, false));
			points.emplace_back(scan_point(Vector(bbox->bbmax.x, bbox->bbmax.y, bbox->bbmax.z - final_radius), hitbox, false));
			
			points.emplace_back(scan_point(Vector(bbox->bbmax.x, bbox->bbmax.y - final_radius, bbox->bbmax.z), hitbox, false));

			if (pitch_down && backward)
				points.emplace_back(scan_point(Vector(bbox->bbmax.x - final_radius, bbox->bbmax.y, bbox->bbmax.z), hitbox, false));
		}
		else if (hitbox >= HITBOX_PELVIS && hitbox <= HITBOX_UPPER_CHEST)
		{
			points.emplace_back(scan_point(center, hitbox, true));

			points.emplace_back(scan_point(Vector(bbox->bbmax.x, bbox->bbmax.y, bbox->bbmax.z + final_radius), hitbox,  false));
			points.emplace_back(scan_point(Vector(bbox->bbmax.x, bbox->bbmax.y, bbox->bbmax.z - final_radius), hitbox,  false));

			points.emplace_back(scan_point(Vector(center.x, bbox->bbmax.y - final_radius, center.z), hitbox, true));
		}
		else if (hitbox == HITBOX_RIGHT_CALF || hitbox == HITBOX_LEFT_CALF)
		{
			points.emplace_back(scan_point(center, hitbox, true));
			points.emplace_back(scan_point(Vector(bbox->bbmax.x - final_radius, bbox->bbmax.y, bbox->bbmax.z), hitbox, false));
		}
		else if (hitbox == HITBOX_RIGHT_THIGH || hitbox == HITBOX_LEFT_THIGH)
			points.emplace_back(scan_point(center, hitbox, true));
		else if (hitbox == HITBOX_RIGHT_UPPER_ARM || hitbox == HITBOX_LEFT_UPPER_ARM)
		{
			points.emplace_back(scan_point(center, hitbox, true));
			points.emplace_back(scan_point(Vector(bbox->bbmax.x + final_radius, center.y, center.z), hitbox, false));
		}
	}

	for (auto& point : points)
		math::vector_transform(point.point, record->matrixes_data.main[bbox->bone], point.point);

	return points;
}

static bool compare_targets(const scanned_target& first, const scanned_target& second)
{
	if (g_cfg.player_list.high_priority[first.record->i] != g_cfg.player_list.high_priority[second.record->i])
		return g_cfg.player_list.high_priority[first.record->i];

	switch (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].selection_type)
	{
	case 1:
		return first.fov < second.fov;
	case 2:
		return first.distance < second.distance;
	case 3:
		return first.health < second.health;
	case 4:
		return first.data.damage > second.data.damage;
	}

	return false;
}

void aim::find_best_target()
{
	if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].selection_type)
		std::sort(scanned_targets.begin(), scanned_targets.end(), compare_targets);

	for (auto& target : scanned_targets)
	{
		if (target.fov > (float)g_cfg.ragebot.field_of_view)
			continue;

		final_target = target;
		final_target.record->adjust_player();
		break;
	}
}

void aim::fire(CUserCmd* cmd)
{
	if (!g_ctx.globals.weapon->can_fire(true))
		return;

	auto aim_angle = math::calculate_angle(g_ctx.globals.eye_pos, final_target.data.point.point).Clamp();

	if (!g_cfg.ragebot.autoshoot)
		return;

	if (!g_cfg.ragebot.silent_aim)
		m_engine()->SetViewAngles(aim_angle);

	auto final_hitchance = 0;
	auto hitchance_amount = 0;

	if (g_ctx.globals.double_tap_aim && g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].double_tap_hitchance)
		hitchance_amount = g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].double_tap_hitchance_amount;
	else if (!g_ctx.globals.double_tap_aim && g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitchance)
		hitchance_amount = g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitchance_amount;

	if (hitchance_amount)
	{

		if (g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_SSG08 && !(g_ctx.local()->m_fFlags() & FL_ONGROUND) && !(engineprediction::get().backup_data.flags & FL_ONGROUND) && fabs(engineprediction::get().backup_data.velocity.z) < 5.0f && engineprediction::get().backup_data.velocity.Length2D() < 5.0f) //-V807
			final_hitchance = 101;
		else if(hitchance(aim_angle) >= hitchance_amount) //-V807)
			final_hitchance = hitchance(aim_angle);


		force_stop = false;

		if (final_hitchance < hitchance_amount)
		{			
			force_stop = true;
			auto is_zoomable_weapon = g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_SCAR20 || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_G3SG1 || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_SSG08 || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_AWP || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_AUG || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_SG553;

			if (g_cfg.ragebot.autoscope && is_zoomable_weapon && !g_ctx.globals.weapon->m_zoomLevel())
				cmd->m_buttons |= IN_ATTACK2;
			return;
		}
	}
	auto backtrack_ticks = 0;
	auto net_channel_info = m_engine()->GetNetChannelInfo();

	if (net_channel_info) //p back track ticks а гдебэктрэк??
	{
		auto original_tickbase = g_ctx.globals.backup_tickbase;

		if (misc::get().double_tap_enabled && misc::get().double_tap_key)
			original_tickbase = g_ctx.globals.backup_tickbase + g_ctx.globals.weapon->get_max_tickbase_shift();

		static auto sv_maxunlag = m_cvar()->FindVar(crypt_str("sv_maxunlag"));

		auto correct = math::clamp(net_channel_info->GetLatency(FLOW_OUTGOING) + net_channel_info->GetLatency(FLOW_INCOMING) + util::get_interpolation(), 0.0f, sv_maxunlag->GetFloat());
		auto delta_time = correct - (TICKS_TO_TIME(original_tickbase) - final_target.record->simulation_time);

		backtrack_ticks = TIME_TO_TICKS(fabs(delta_time));
	}	

	static auto get_hitbox_name = [](int hitbox, bool shot_info = false) -> std::string
	{
		switch (hitbox)
		{
		case HITBOX_HEAD:
			return shot_info ? crypt_str("Head") : crypt_str("head");
		case HITBOX_LOWER_CHEST:
			return shot_info ? crypt_str("Lower chest") : crypt_str("lower chest");
		case HITBOX_CHEST:
			return shot_info ? crypt_str("Chest") : crypt_str("chest");
		case HITBOX_UPPER_CHEST:
			return shot_info ? crypt_str("Upper chest") : crypt_str("upper chest");
		case HITBOX_STOMACH:
			return shot_info ? crypt_str("Stomach") : crypt_str("stomach");
		case HITBOX_PELVIS:
			return shot_info ? crypt_str("Pelvis") : crypt_str("pelvis");
		case HITBOX_RIGHT_UPPER_ARM:
			return shot_info ? crypt_str("Right upper arm") : crypt_str("right upper arm");
		case HITBOX_RIGHT_FOREARM:
			return shot_info ? crypt_str("Right forearm") : crypt_str("right forearm");
		case HITBOX_RIGHT_HAND:
			return shot_info ? crypt_str("Left arm") : crypt_str("left arm");
		case HITBOX_LEFT_UPPER_ARM:
			return shot_info ? crypt_str("Left upper arm") : crypt_str("left upper arm");
		case HITBOX_LEFT_FOREARM:
			return shot_info ? crypt_str("Left forearm") : crypt_str("left forearm");
		case HITBOX_LEFT_HAND:
			return shot_info ? crypt_str("Right arm") : crypt_str("right arm");
		case HITBOX_RIGHT_THIGH:
			return shot_info ? crypt_str("Right thigh") : crypt_str("right thigh");
		case HITBOX_RIGHT_CALF:
			return shot_info ? crypt_str("Left leg") : crypt_str("left leg");
		case HITBOX_LEFT_THIGH:
			return shot_info ? crypt_str("Left thigh") : crypt_str("left thigh");
		case HITBOX_LEFT_CALF:
			return shot_info ? crypt_str("Right leg") : crypt_str("right leg");
		case HITBOX_RIGHT_FOOT:
			return shot_info ? crypt_str("Left foot") : crypt_str("left foot");
		case HITBOX_LEFT_FOOT:
			return shot_info ? crypt_str("Right foot") : crypt_str("right foot");
		}
	};

	static auto get_resolver_type = [](resolver_type type) -> std::string
	{
		switch (type)
		{
		case ORIGINAL:
			return crypt_str("original ");
		case BRUTEFORCE:
			return crypt_str("bruteforce ");
		case LBY:
			return crypt_str("lby ");
		case TRACE:
			return crypt_str("trace ");
		case DIRECTIONAL:
			return crypt_str("directional ");
		}
	};

	player_info_t player_info;
	m_engine()->GetPlayerInfo(final_target.record->i, &player_info);

#if BETA
	std::stringstream log;
	//int log_final_hitchance = (final_hitchance / 2,56);
	log << crypt_str("Attemp Fire: ") + (std::string)player_info.szName + crypt_str(", ");
	log << crypt_str("hitchance: ") + (final_hitchance == 101  ? crypt_str("MA") : std::to_string(final_hitchance)) + crypt_str(", ");
	log << crypt_str("hitbox: ") + get_hitbox_name(final_target.data.hitbox) + crypt_str(", ");
	log << crypt_str("damage: ") + std::to_string(final_target.data.damage) + crypt_str(", ");
	log << crypt_str("safe: ") + std::to_string((bool)final_target.data.point.safe) + crypt_str(", ");
	log << crypt_str("backtrack: ") + std::to_string(backtrack_ticks) + crypt_str(", ");
	log << crypt_str("resolver type: ") + get_resolver_type(final_target.record->type) + std::to_string(final_target.record->side);

	if (g_cfg.misc.events_to_log[EVENTLOG_HIT])
		eventlogs::get().add(log.str());
#endif
	cmd->m_tickcount = TIME_TO_TICKS(final_target.record->simulation_time + util::get_interpolation());
	cmd->m_viewangles = aim_angle;
	cmd->m_buttons |= IN_ATTACK;
	last_target_index = final_target.record->i;
	last_shoot_position = g_ctx.globals.eye_pos;
	last_target[last_target_index] = Last_target
	{
		*final_target.record, final_target.data, final_target.distance 
	};

	static float fire_delay_time = 0;

	if (!g_ctx.local()->m_iShotsFired() && key_binds::get().get_key_bind_state(0) && fire_delay_time < g_cfg.ragebot.fire_delay_time)
	{
		fire_delay_time += 1;
		return;
	}
	else if (!g_ctx.local()->m_iShotsFired())
		fire_delay_time = 0;


	//if (g_cfg.ragebot.fire_delay && )
	//	return;

	auto shot = &g_ctx.shots.emplace_back();

	shot->last_target = last_target_index;
	shot->side = final_target.record->side;
	shot->fire_tick = m_globals()->m_tickcount;
	shot->shot_info.target_name = player_info.szName;
	shot->shot_info.client_hitbox = get_hitbox_name(final_target.data.hitbox, true);
	shot->shot_info.client_damage = final_target.data.damage;
	shot->shot_info.hitchance = final_hitchance;
	shot->shot_info.backtrack_ticks = backtrack_ticks;
	shot->shot_info.aim_point = final_target.data.point.point;

	g_ctx.globals.aimbot_working = true;
	g_ctx.globals.revolver_working = false;
	g_ctx.globals.last_aimbot_shot = m_globals()->m_tickcount;

}

/*
int aim::hitchance(const Vector& aim_angle, float hit_chance)
{
	auto final_hitchance = 0;
	auto weapon = g_ctx.globals.weapon->get_csweapon_info();

	if (!weapon)
		return false;

	Vector forward, right, up;
	Vector src = g_ctx.globals.eye_pos;

	math::angle_vectors(aim_angle, &forward, &right, &up);
	math::fast_vec_normalize(forward);
	math::fast_vec_normalize(right);
	math::fast_vec_normalize(up);

	int cHits = 0;

	float weap_spread = g_ctx.globals.spread;
	float weap_inaccuracy = g_ctx.globals.inaccuracy;
	auto damage = 0;
	auto inaccuracy = weapon->flInaccuracyStand;
	auto high_accuracy_weapon = g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_AWP || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_SSG08;

	if (g_ctx.globals.inaccuracy - 0.000001f < inaccuracy)
		final_hitchance = 101;
	else 
	{
		static auto setup_spread_values = true;
		
		if (setup_spread_values)
		{
			for (int i = 0; i < 256; i++)
			{
				math::random_seed(i + 1);

				float a = math::random_float(0.f, 1.f);
				float b = math::random_float(0.f, 2.f * DirectX::XM_2PI);
				float c = math::random_float(0.f, 1.f);
				float d = math::random_float(0.f, 2.f * DirectX::XM_2PI);

				float inaccuracy = a * weap_inaccuracy;
				float spread = c * weap_spread;

				if (g_ctx.globals.weapon->m_iItemDefinitionIndex() != WEAPON_REVOLVER)
				{
					a = 1.f - a * a;
					a = 1.f - c * c;
				}

				Vector spreadView((cos(b) * inaccuracy) + (cos(d) * spread), (sin(b) * inaccuracy) + (sin(d) * spread), 0), direction;

				direction.x = forward.x + (spreadView.x * right.x) + (spreadView.y * up.x);
				direction.y = forward.y + (spreadView.x * right.y) + (spreadView.y * up.y);
				direction.z = forward.z + (spreadView.x * right.z) + (spreadView.y * up.z);
				direction.Normalized();

				QAngle viewAnglesSpread;
				math::normalize_angles(viewAnglesSpread);
				Vector viewForward;
				viewForward.NormalizeInPlace();
				viewForward = src + (direction * weapon->flRange);
				auto end = g_ctx.globals.eye_pos + direction * weapon->flRange;

				if (hitbox_intersection(final_target.record->player, final_target.record->matrixes_data.main, final_target.data.hitbox, g_ctx.globals.eye_pos, end))
				{
					++cHits;
				}		

				if (hitbox_intersection(final_target.record->player, final_target.record->matrixes_data.main, final_target.data.hitbox, g_ctx.globals.eye_pos, end))
				{
					auto fire_data = autowall::get().wall_penetration(g_ctx.globals.eye_pos, end, final_target.record->player);
					auto valid_hitbox = true;

					if (final_target.data.hitbox == HITBOX_HEAD && fire_data.hitbox != HITBOX_HEAD)
						valid_hitbox = false;

					if (fire_data.valid && fire_data.damage >= 1 && valid_hitbox)
				    {
					damage += high_accuracy_weapon ? fire_data.damage : 1;
				    }
			    }

				if (static_cast<int>((static_cast<float>(cHits) / 255.f) * 100.f) >= hit_chance)
					return final_hitchance;
			}
		}
	}
	final_hitchance = (int)((float)cHits /* 2.56f *);

	if (high_accuracy_weapon)
		return (float)damage / 48.0f >= get_minimum_damage(final_target.data.visible, final_target.health) ? final_hitchance : 0;

	return ((float)damage / 48.0f >= (float)g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitchance_amount * 0.01f ? final_hitchance : 0 && false);

	if (g_ctx.globals.double_tap_aim)
		return final_hitchance;

}
*/


int aim::hitchance(const Vector& aim_angle)
{
	auto final_hitchance = 0;
	auto weapon_info = g_ctx.globals.weapon->get_csweapon_info();

	if (!weapon_info)
		return final_hitchance;

	if ((g_ctx.globals.eye_pos - final_target.data.point.point).Length() > weapon_info->flRange)
		return final_hitchance;

	auto forward = ZERO;
	auto right = ZERO;
	auto up = ZERO;

	math::angle_vectors(aim_angle, &forward, &right, &up);
	math::fast_vec_normalize(forward);
	math::fast_vec_normalize(right);
	math::fast_vec_normalize(up);

	auto is_special_weapon = g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_AWP || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_G3SG1 || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_SCAR20 || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_SSG08;
	auto inaccuracy = weapon_info->flInaccuracyStand;

	if (g_ctx.local()->m_fFlags() & FL_DUCKING)
	{
		if (is_special_weapon)
			inaccuracy = weapon_info->flInaccuracyCrouchAlt;
		else
			inaccuracy = weapon_info->flInaccuracyCrouch;
	}
	else if (is_special_weapon)
		inaccuracy = weapon_info->flInaccuracyStandAlt;

	if (g_ctx.globals.inaccuracy - 0.000001f < inaccuracy)
		final_hitchance = 101;
	else
	{
		static auto setup_spread_values = true;
		static float spread_values[256][6]{};

		if (setup_spread_values)
		{
			//setup_spread_values = false;

			for (auto i = 0; i < 256; ++i)
			{
				math::random_seed(i + 1);

				auto a = math::random_float(0.0f, 1.0f);
				auto b = math::random_float(0.0f, DirectX::XM_2PI);
				auto c = math::random_float(0.0f, 1.0f);
				auto d = math::random_float(0.0f, DirectX::XM_2PI);

				spread_values[i][0] = a;
				spread_values[i][1] = c;

				auto sin_b = 0.0f, cos_b = 0.0f;
				DirectX::XMScalarSinCos(&sin_b, &cos_b, b);

				auto sin_d = 0.0f, cos_d = 0.0f;
				DirectX::XMScalarSinCos(&sin_d, &cos_d, d);

				spread_values[i][2] = sin_b;
				spread_values[i][3] = cos_b;
				spread_values[i][4] = sin_d;
				spread_values[i][5] = cos_d;
			}
		}

		auto hits = 0;

		for (auto i = 0; i < 256; ++i)
		{
			auto inaccuracy = spread_values[i][0] * g_ctx.globals.inaccuracy;
			auto spread = spread_values[i][1] * g_ctx.globals.spread;

			auto spread_x = spread_values[i][3] * inaccuracy + spread_values[i][5] * spread;
			auto spread_y = spread_values[i][2] * inaccuracy + spread_values[i][4] * spread;

			auto direction = ZERO;

			direction.x = forward.x + right.x * spread_x + up.x * spread_y;
			direction.y = forward.y + right.y * spread_x + up.y * spread_y;
			direction.z = forward.z + right.z * spread_x + up.z * spread_y; //-V778

			auto end = g_ctx.globals.eye_pos + direction * weapon_info->flRange;

			if (hitbox_intersection(final_target.record->player, final_target.record->matrixes_data.main, final_target.data.hitbox, g_ctx.globals.eye_pos, end))
				++hits;
		}
		final_hitchance = (int)((float)hits / 2.56f);
	}
	
	if (g_ctx.globals.double_tap_aim)
		return final_hitchance;

	auto damage = 0;
	auto high_accuracy_weapon = g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_AWP || g_ctx.globals.weapon->m_iItemDefinitionIndex() == WEAPON_SSG08;

	auto spread = g_ctx.globals.spread + g_ctx.globals.inaccuracy;

	for (auto i = 1; i <= 6; ++i)
	{
		for (auto j = 0; j < 8; ++j)
		{
			auto current_spread = spread * ((float)i / 6.0f);

			auto direction_cos = 0.0f;
			auto direction_sin = 0.0f;

			DirectX::XMScalarSinCos(&direction_cos, &direction_sin, (float)j / 8.0f * DirectX::XM_2PI);

			auto spread_x = direction_cos * current_spread;
			auto spread_y = direction_sin * current_spread;

			auto direction = ZERO;

			direction.x = forward.x + spread_x * right.x + spread_y * up.x;
			direction.y = forward.y + spread_x * right.y + spread_y * up.y;
			direction.z = forward.z + spread_x * right.z + spread_y * up.z;

			auto end = g_ctx.globals.eye_pos + direction * weapon_info->flRange;

			if (hitbox_intersection(final_target.record->player, final_target.record->matrixes_data.main, final_target.data.hitbox, g_ctx.globals.eye_pos, end))
			{
				auto fire_data = autowall::get().wall_penetration(g_ctx.globals.eye_pos, end, final_target.record->player);
				auto valid_hitbox = true;

				if (final_target.data.hitbox == HITBOX_HEAD && fire_data.hitbox != HITBOX_HEAD)
					valid_hitbox = false;

				if (fire_data.valid && fire_data.damage >= 1 && valid_hitbox)
					damage += high_accuracy_weapon ? fire_data.damage : 1;
			}
		}
	}

	if (high_accuracy_weapon)
		return (float)damage / 48.0f >= get_minimum_damage(final_target.data.visible, final_target.health) ? final_hitchance : 0;

	return (float)damage / 48.0f >= (float)g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitchance_amount * 0.01f ? final_hitchance : 0;
}

static int clip_ray_to_hitbox(const Ray_t& ray, mstudiobbox_t* hitbox, matrix3x4_t& matrix, trace_t& trace)
{
	static auto fn = util::FindSignature(crypt_str("client.dll"), crypt_str("55 8B EC 83 E4 F8 F3 0F 10 42"));

	trace.fraction = 1.0f;
	trace.startsolid = false;

	return reinterpret_cast <int(__fastcall*)(const Ray_t&, mstudiobbox_t*, matrix3x4_t&, trace_t&)> (fn)(ray, hitbox, matrix, trace);
}

bool aim::hitbox_intersection(player_t* e, matrix3x4_t* matrix, int hitbox, const Vector& start, const Vector& end, float* safe)
{
	auto model = e->GetModel();

	if (!model)
		return false;

	auto studio_model = m_modelinfo()->GetStudioModel(model);

	if (!studio_model)
		return false;

	auto studio_set = studio_model->pHitboxSet(e->m_nHitboxSet());

	if (!studio_set)
		return false;

	auto studio_hitbox = studio_set->pHitbox(hitbox);

	if (!studio_hitbox)
		return false;

	trace_t trace;

	Ray_t ray;
	ray.Init(start, end);

	auto intersected = clip_ray_to_hitbox(ray, studio_hitbox, matrix[studio_hitbox->bone], trace) >= 0;

	if (!safe)
		return intersected;

	Vector min, max;

	math::vector_transform(studio_hitbox->bbmin, matrix[studio_hitbox->bone], min);
	math::vector_transform(studio_hitbox->bbmax, matrix[studio_hitbox->bone], max);

	auto center = (min + max) * 0.5f;
	auto distance = center.DistTo(end);

	if (distance > *safe)
		*safe = distance;

	return intersected;
}

static std::vector<std::tuple<float, float, float>> precomputed_seeds = { };
static const int total_seeds = 255;

void build_seed_table()
{
	if (!precomputed_seeds.empty())
		return;

	for (auto i = 0; i < total_seeds; i++) {
		math::random_seed(i + 1);

		const auto pi_seed = math::random_float(0.f, M_PI * 2);

		precomputed_seeds.emplace_back(math::random_float(0.f, 1.f),
			sin(pi_seed), cos(pi_seed));
	}
}

#define penetrate autowall::instance( )

bool aim::HitTraces(adjust_data* _animation, const Vector position, const float chance, int box)
{
	build_seed_table();

	float HITCHANCE_MAX = 100.f;
	const auto weapon = g_ctx.local()->m_hActiveWeapon();//g::local->get_active_weapon();

	if (!weapon)
		return false;

	const auto info = weapon->get_csweapon_info();

	if (!info)
		return false;

	const auto studio_model = _animation->player;

	if (!studio_model)
		return false;

	if (g_ctx.globals.weapon->can_fire(true))
		HITCHANCE_MAX += 25;

	// performance optimization.
	if ((g_ctx.globals.eye_pos - position).Length2D() > info->flRange)
		return false;

	// setup calculation parameters.
	const auto id = weapon->m_iItemDefinitionIndex();
	const auto round_acc = [](const float accuracy) { return roundf(accuracy * 1000.f) / 1000.f; };
	const auto crouched = g_ctx.local()->m_fFlags() & FL_DUCKING;

	// calculate inaccuracy.
	const auto weapon_inaccuracy = weapon->get_inaccuracy();

	if (id == 64)
		return weapon_inaccuracy < (crouched ? .0020f : .0055f);

	// calculate start and angle.
	auto start = g_ctx.globals.eye_pos;

	const auto aim_angle = math::calculate_angle(start, position);
	Vector forward, right, up;

	math::angle_vectors(aim_angle, &forward, &right, &up);
   //ang_vec_mult(aim_angle, forward, right, up);

	// keep track of all traces that hit the enemy.
	auto current = 0;
	int awalls_hit = 0;

	// setup calculation parameters.
	Vector total_spread, spread_angle, end;
	float inaccuracy, spread_x, spread_y;
	std::tuple<float, float, float>* seed;

	// use look-up-table to find average hit probability.
	for (auto i = 0u; i < total_seeds; i++)  // NOLINT(modernize-loop-convert)
	{
		// get seed.
		seed = &precomputed_seeds[i];

		// calculate spread.
		inaccuracy = std::get<0>(*seed) * weapon_inaccuracy;
		spread_x = std::get<2>(*seed) * inaccuracy;
		spread_y = std::get<1>(*seed) * inaccuracy;
		total_spread = (forward + right * spread_x + up * spread_y).Normalized();

		// calculate angle with spread applied.
		math::vector_angles(total_spread, spread_angle);

		// calculate end point of trace.
		math::angle_vectors(spread_angle, end);
		end = start + end.Normalized() * info->flRange;

		matrix3x4_t* matrix = 0;
		float* safe = 0;
		Vector point = nullptr;
		// did we hit the hitbox?
		if (g_cfg.ragebot.weapon[g_ctx.globals.current_weapon].hitchance_amount && box != (int)HITBOX_RIGHT_FOOT && box != (int)HITBOX_LEFT_FOOT)
		{
			if (aim::instance()->hitbox_intersection(studio_model, matrix, box, start, end, safe))
			{
				current++;

				if (g_cfg.ragebot.weapon[hooks::rage_weapon].accuracy_boost > 1.f && penetrate->wall_penetration(position, point, studio_model).damage > 1.f)
					awalls_hit++;
			}
		}
		else
		{
			CGameTrace tr;
			Ray_t ray;

			ray.Init(start, end);
			m_trace()->ClipRayToEntity(ray, MASK_SHOT | CONTENTS_GRATE, studio_model, &tr);

			if (auto ent = tr.hit_entity; ent)
			{
				if (ent == _animation->player)
				{
					current++;

					if (g_cfg.ragebot.weapon[hooks::rage_weapon].accuracy_boost > 1.f && penetrate->wall_penetration(position, point, studio_model).damage > 1.f)
						awalls_hit++;
				}
			}
		}

		// abort if hitchance is already sufficent.
		if (((static_cast<float>(current) / static_cast<float>(total_seeds)) >= (chance / HITCHANCE_MAX)))
		{
			if (((static_cast<float>(awalls_hit) / static_cast<float>(total_seeds)) >= (g_cfg.ragebot.weapon[hooks::rage_weapon].accuracy_boost / HITCHANCE_MAX)) || g_cfg.ragebot.weapon[hooks::rage_weapon].accuracy_boost <= 1.f)
				return true;
		}

		// abort if we can no longer reach hitchance.
		if (static_cast<float>(current + total_seeds - i) / static_cast<float>(total_seeds) < chance)
			return false;
	}

	return static_cast<float>(current) / static_cast<float>(total_seeds) >= chance;
}

/*
int BacktrackTicks()
{
	// получаем тикрейт сервера
	float timepertick = m_globals()->m_tickcount;//g_pGlobals->interval_per_tick; //g_pGlobals m_globals()->m_tickcount;
	if (timepertick == 0) timepertick += 0.001; //предотвращает краши игры
	float tickrate = 1 / timepertick;

	// вычисления
	int backticks = 200 / 1000 * tickrate; // поключение слайдера 1-200мс где 200 можно слайдер))
	if (backticks < 1) return 1; //предотвращение крашей
	else return backticks;
}
*/