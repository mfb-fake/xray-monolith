////////////////////////////////////////////////////////////////////////////
//	Module 		: smart_cover_evaluators.cpp
//	Created 	: 05.11.2007
//	Author		: Alexander Dudin
//	Description : Smart cover evaluators classes
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "smart_cover_evaluators.h"
#include "stalker_property_evaluators.h"
#include "script_game_object.h"
#include "stalker_decision_space.h"
#include "ai/stalker/ai_stalker.h"
#include "ai_space.h"
#include "stalker_movement_manager_smart_cover.h"
#include "memory_manager.h"
#include "memory_space.h"
#include "enemy_manager.h"
#include "visual_memory_manager.h"
#include "cover_point.h"
#include "smart_cover.h"
#include "smart_cover_animation_planner.h"
#include "smart_cover_planner_target_selector.h"
#include "smart_cover_loophole.h"
#include "smart_cover_transition.hpp"
#include "smart_cover_transition_animation.hpp"
#include "smart_cover_description.h"
#include "stalker_animation_manager.h"
#include "hit_memory_manager.h"

namespace smart_cover
{
	shared_str transform_vertex(shared_str const& vertex_id, bool const& in);
};

using namespace StalkerDecisionSpace;
using smart_cover::evaluators::in_cover_evaluator;
using smart_cover::evaluators::cover_actual_evaluator;
using smart_cover::evaluators::cover_entered_evaluator;
using smart_cover::evaluators::loophole_actual_evaluator;
using smart_cover::evaluators::loophole_hit_long_ago_evaluator;
using smart_cover::evaluators::is_action_available_evaluator;
using smart_cover::evaluators::loophole_planner_const_evaluator;
using smart_cover::evaluators::loophole_exitable_evaluator;
using smart_cover::evaluators::can_exit_loophole_with_animation;
using smart_cover::evaluators::default_behaviour_evaluator;
using smart_cover::evaluators::can_fire_at_enemy_evaluator;
using smart_cover::evaluators::idle_time_interval_passed_evaluator;
using smart_cover::evaluators::lookout_time_interval_passed_evaluator;
using smart_cover::evaluators::combat_enemy_evaluator;
using smart_cover::animation_planner;

typedef CStalkerPropertyEvaluator::_value_type _value_type;

//////////////////////////////////////////////////////////////////////////
// in_cover_evaluator
//////////////////////////////////////////////////////////////////////////

in_cover_evaluator::in_cover_evaluator(CAI_Stalker* object, LPCSTR evaluator_name) :
	inherited(object ? object->lua_game_object() : 0, evaluator_name)
{
}

_value_type in_cover_evaluator::evaluate()
{
	// FIXED: Once in smart cover, STAY in smart cover mode unless explicitly exited
	// This prevents the combat planner from yanking us out prematurely

	// Check if we're currently using a smart cover
	if (object().movement().current_params().cover())
		return (true);

	// Check if we're in the process of entering a smart cover
	if (object().movement().entering_smart_cover_with_animation())
		return (true);

	return (false);
}

//////////////////////////////////////////////////////////////////////////
// cover_actual_evaluator
//////////////////////////////////////////////////////////////////////////

cover_actual_evaluator::cover_actual_evaluator(CAI_Stalker* object, LPCSTR evaluator_name) :
	inherited(object ? object->lua_game_object() : 0, evaluator_name)
{
}

_value_type cover_actual_evaluator::evaluate()
{
	VERIFY(object().movement().current_params().cover());
	return (object().movement().current_params().cover() == object().movement().target_params().cover());
}

//////////////////////////////////////////////////////////////////////////
// cover_entered_evaluator
//////////////////////////////////////////////////////////////////////////

cover_entered_evaluator::cover_entered_evaluator(CAI_Stalker* object, LPCSTR evaluator_name) :
	inherited(object ? object->lua_game_object() : 0, evaluator_name)
{
}

_value_type cover_entered_evaluator::evaluate()
{
	return (!!object().movement().current_params().cover());
}

//////////////////////////////////////////////////////////////////////////
// loophole_actual_evaluator
//////////////////////////////////////////////////////////////////////////

loophole_actual_evaluator::loophole_actual_evaluator(CAI_Stalker* object, LPCSTR evaluator_name,
                                                     animation_planner* planner, u32 const& loophole_value) :
	inherited(object ? object->lua_game_object() : 0, evaluator_name),
	m_loophole_value(loophole_value),
	m_planner(planner)
{
}

_value_type loophole_actual_evaluator::evaluate()
{
	if (object().movement().current_params().cover() != object().movement().target_params().cover())
		return (false);

	if (object().movement().current_params().cover_loophole() != object().movement().target_params().cover_loophole())
		return (false);

	return (true);
}

//////////////////////////////////////////////////////////////////////////
// loophole_hit_long_ago_evaluator
//////////////////////////////////////////////////////////////////////////

loophole_hit_long_ago_evaluator::loophole_hit_long_ago_evaluator(animation_planner* object, LPCSTR evaluator_name,
                                                                 u32 const& time_to_wait) :
	inherited(object, evaluator_name),
	m_time_to_wait(time_to_wait)
{
}

_value_type loophole_hit_long_ago_evaluator::evaluate()
{
	return ((m_object->time_object_hit() + m_time_to_wait) < Device.dwTimeGlobal);
}

//////////////////////////////////////////////////////////////////////////
// is_action_available_evaluator
//////////////////////////////////////////////////////////////////////////

is_action_available_evaluator::is_action_available_evaluator(animation_planner* object, LPCSTR evaluator_name,
                                                             LPCSTR action_id) :
	inherited(object, evaluator_name),
	m_action_id(action_id)
{
}

_value_type is_action_available_evaluator::evaluate()
{
	if (!m_object->m_object->movement().current_params().cover())
		return (false);

	if (!m_object->m_object->movement().current_params().cover_loophole())
		return (false);

	return (m_object->m_object->movement().current_params().cover_loophole()->is_action_available(m_action_id));
}

//////////////////////////////////////////////////////////////////////////
// loophole_planner_const_evaluator
//////////////////////////////////////////////////////////////////////////

loophole_planner_const_evaluator::loophole_planner_const_evaluator(animation_planner* object, LPCSTR evaluator_name,
                                                                   bool const& value) :
	inherited(object, evaluator_name),
	m_value(value)
{
}

_value_type loophole_planner_const_evaluator::evaluate()
{
	return (m_value);
}

//////////////////////////////////////////////////////////////////////////
// loophole_exitable_evaluator
//////////////////////////////////////////////////////////////////////////

loophole_exitable_evaluator::loophole_exitable_evaluator(CAI_Stalker* object, LPCSTR evaluator_name) :
	inherited(object ? object->lua_game_object() : 0, evaluator_name)
{
}

_value_type loophole_exitable_evaluator::evaluate()
{
	if (!m_object->movement().current_params().cover_loophole())
		return (false);

	return (object().movement().current_params().cover_loophole()->exitable());
}

//////////////////////////////////////////////////////////////////////////
// can_exit_loophole_with_animation
//////////////////////////////////////////////////////////////////////////

can_exit_loophole_with_animation::can_exit_loophole_with_animation(CAI_Stalker* object, LPCSTR evaluator_name) :
	inherited(object ? object->lua_game_object() : 0, evaluator_name)
{
}

_value_type can_exit_loophole_with_animation::evaluate()
{
	stalker_movement_manager_smart_cover& movement = object().movement();
	stalker_movement_params const& current = movement.current_params();
	VERIFY(current.cover());

	stalker_movement_params const& target = object().movement().target_params();

	smart_cover::cover const* current_cover = current.cover();
	smart_cover::cover const* target_cover = target.cover();
	if (current_cover != target_cover)
	{
#ifdef DEBUG
		Msg						(
			"transition guard(cover): [%s] -> [%s]",
			current_cover ? current_cover->id().c_str() : "<world>",
			target_cover ? target_cover->id().c_str() : "<world>"
		);
#endif // #ifdef DEBUG
		return (movement.current_transition().animation().has_animation());
	}

	smart_cover::loophole const* current_loophole = current.cover_loophole();
	smart_cover::loophole const* target_loophole = target.cover_loophole();
	if (current_loophole != target_loophole)
	{
#ifdef DEBUG
		Msg						(
			"transition guard(loophole): [%s] -> [%s]",
			current_loophole ? current_loophole->id().c_str() : "<world>",
			target_loophole ? target_loophole->id().c_str() : "<world>"
		);
#endif // #ifdef DEBUG
		return (movement.current_transition().animation().has_animation());
	}

	return (false);
}

//////////////////////////////////////////////////////////////////////////
// default_behaviour_evaluator
//////////////////////////////////////////////////////////////////////////

default_behaviour_evaluator::default_behaviour_evaluator(animation_planner* object, LPCSTR evaluator_name) :
	inherited(object, evaluator_name)
{
}

_value_type default_behaviour_evaluator::evaluate()
{
	return (
		m_object->m_object->movement().default_behaviour() ||
		m_object->m_object->movement().combat_behaviour()
	);
}

//////////////////////////////////////////////////////////////////////////
// can_fire_at_enemy_evaluator
//////////////////////////////////////////////////////////////////////////

can_fire_at_enemy_evaluator::can_fire_at_enemy_evaluator(animation_planner* object, LPCSTR evaluator_name) :
	inherited(object, evaluator_name)
{
}

_value_type can_fire_at_enemy_evaluator::evaluate()
{
	CAI_Stalker* stalker = m_object->m_object;

	if (stalker->movement().current_params().cover_fire_position())
		return (true);

	if (stalker->movement().current_params().cover_fire_object())
		return (true);

	if (!stalker->movement().default_behaviour())
		return (true);

	typedef xr_vector<const CEntityAlive*> ENEMIES;
	const ENEMIES& enemies = stalker->memory().enemy().objects();

	u32 current_time = Device.dwTimeGlobal;
	bool has_valid_target = false;
	bool has_immediate_visible_threat = false;

	for (ENEMIES::const_iterator it = enemies.begin(); it != enemies.end(); ++it) {
		const CEntityAlive* enemy = *it;
		if (!enemy || !enemy->g_Alive())
			continue;

		bool visible_now = stalker->memory().visual().visible_now(enemy);

		if (!visible_now)
			continue;

		if (!stalker->movement().in_current_loophole_fov(enemy->Position()))
			continue;

		has_immediate_visible_threat = true;
		has_valid_target = true;
		break;
	}

	if (!has_immediate_visible_threat) {
		for (ENEMIES::const_iterator it = enemies.begin(); it != enemies.end(); ++it) {
			const CEntityAlive* enemy = *it;
			if (!enemy || !enemy->g_Alive())
				continue;

			u32 last_seen_time = stalker->memory().visual().visible_object_time_last_seen(enemy);

			bool position_is_fresh = (last_seen_time != u32(-1)) &&
				((current_time - last_seen_time) < 5000);

			if (!position_is_fresh)
				continue;

			if (!stalker->movement().in_current_loophole_fov(enemy->Position()))
				continue;

			has_valid_target = true;
			break;
		}
	}

	if (!has_valid_target)
		return (false);

	CWeapon* weapon = smart_cast<CWeapon*>(stalker->best_weapon());
	if (!weapon)
		return (false);

	if (weapon->GetAmmoElapsed() == 0)
		return (false);

	if (weapon->GetState() == CWeapon::eReload)
		return (false);

	return (true);
}

//////////////////////////////////////////////////////////////////////////
// idle_time_interval_passed_evaluator
//////////////////////////////////////////////////////////////////////////

idle_time_interval_passed_evaluator::idle_time_interval_passed_evaluator(
	animation_planner* object, LPCSTR evaluator_name, u32 const& time_interval) :
	inherited(object, evaluator_name),
	m_time_interval(time_interval)
{
}

_value_type idle_time_interval_passed_evaluator::evaluate()
{
	if (!m_object->stay_idle())
		return (false);

	u32 const& current_time = Device.dwTimeGlobal;
	u32 const& idle_start_time = m_object->last_idle_time();

	if (current_time < idle_start_time + m_time_interval)
	{
		return (true);
	}
	else
	{
		m_object->last_lookout_time(current_time);
		m_object->stay_idle(false);

		return (false);
	}
}

//////////////////////////////////////////////////////////////////////////
// lookout_time_interval_passed_evaluator
//////////////////////////////////////////////////////////////////////////

lookout_time_interval_passed_evaluator::lookout_time_interval_passed_evaluator(
	animation_planner* object, LPCSTR evaluator_name, u32 const& time_interval) :
	inherited(object, evaluator_name),
	m_time_interval(time_interval)
{
}

_value_type lookout_time_interval_passed_evaluator::evaluate()
{
	if (m_object->stay_idle())
		return (false);

	u32 const& current_time = Device.dwTimeGlobal;
	u32 const& lookout_start_time = m_object->last_lookout_time();

	if (current_time < lookout_start_time + m_time_interval)
	{
		return (true);
	}
	else
	{
		m_object->last_idle_time(current_time);
		m_object->stay_idle(true);

		return (false);
	}
}

//////////////////////////////////////////////////////////////////////////
// combat_enemy_evaluator
//////////////////////////////////////////////////////////////////////////

combat_enemy_evaluator::combat_enemy_evaluator(animation_planner* object, LPCSTR evaluator_name) :
	inherited(object, evaluator_name)
{
}

_value_type combat_enemy_evaluator::evaluate()
{
	CAI_Stalker* stalker = m_object->m_object;

	const CEntityAlive* current_enemy = stalker->memory().enemy().selected();
	if (!current_enemy)
		return (false);

	if (!current_enemy->g_Alive())
		return (false);

	bool current_enemy_visible = stalker->memory().visual().visible_now(current_enemy);
	u32 current_enemy_last_seen = stalker->memory().visual().visible_object_time_last_seen(current_enemy);

	if (!current_enemy_visible) {
		if (current_enemy_last_seen == u32(-1))
			return (false);

		if ((Device.dwTimeGlobal - current_enemy_last_seen) > 5000)
			return (false);
	}

	if (!stalker->movement().in_current_loophole_fov(current_enemy->Position()))
		return (false);

	bool current_enemy_firing = false;
	const CAI_Stalker* current_stalker = smart_cast<const CAI_Stalker*>(current_enemy);
	if (current_stalker && current_stalker->inventory().ActiveItem()) {
		CWeapon* current_weapon = smart_cast<CWeapon*>(current_stalker->inventory().ActiveItem());
		if (current_weapon) {
			u32 weapon_state = current_weapon->GetState();
			current_enemy_firing = (weapon_state == CWeapon::eFire || weapon_state == CWeapon::eFire2);
		}
	}

	bool current_enemy_targeting_us = false;
	u32 last_hit_time = stalker->memory().hit().last_hit_time();
	if (last_hit_time != 0 && (Device.dwTimeGlobal - last_hit_time) < 3000) {
		ALife::_OBJECT_ID hit_source = stalker->memory().hit().last_hit_object_id();
		if (hit_source == current_enemy->ID()) {
			current_enemy_targeting_us = true;
		}
	}

	typedef xr_vector<const CEntityAlive*> ENEMIES;
	const ENEMIES& enemies = stalker->memory().enemy().objects();

	float current_distance = stalker->Position().distance_to(current_enemy->Position());
	float current_threat_score = 0.0f;

	if (current_enemy_visible)
		current_threat_score += 100.0f;
	else
		current_threat_score += 30.0f;

	if (current_enemy_firing)
		current_threat_score += 120.0f;

	if (current_enemy_targeting_us)
		current_threat_score += 150.0f;

	if (current_distance > 0.1f)
		current_threat_score += (50.0f / current_distance);

	if (current_enemy_last_seen != u32(-1)) {
		u32 time_since_seen = Device.dwTimeGlobal - current_enemy_last_seen;
		if (time_since_seen < 2000) {
			float recency_factor = 1.0f - (time_since_seen / 2000.0f);
			current_threat_score += 40.0f * recency_factor;
		}
	}

	const CEntityAlive* better_enemy = nullptr;
	float best_threat_score = current_threat_score;

	bool in_combat_cover = stalker->movement().current_params().cover() != nullptr;

	for (ENEMIES::const_iterator it = enemies.begin(); it != enemies.end(); ++it) {
		const CEntityAlive* other_enemy = *it;

		if (other_enemy == current_enemy || !other_enemy || !other_enemy->g_Alive())
			continue;

		bool other_visible = stalker->memory().visual().visible_now(other_enemy);

		if (!other_visible) {
			u32 other_last_seen = stalker->memory().visual().visible_object_time_last_seen(other_enemy);
			if (other_last_seen == u32(-1))
				continue;

			if ((Device.dwTimeGlobal - other_last_seen) > 3000)
				continue;
		}

		if (!stalker->movement().in_current_loophole_fov(other_enemy->Position()))
			continue;

		bool other_enemy_firing = false;
		const CAI_Stalker* other_stalker = smart_cast<const CAI_Stalker*>(other_enemy);
		if (other_stalker && other_stalker->inventory().ActiveItem()) {
			CWeapon* other_weapon = smart_cast<CWeapon*>(other_stalker->inventory().ActiveItem());
			if (other_weapon) {
				u32 other_weapon_state = other_weapon->GetState();
				other_enemy_firing = (other_weapon_state == CWeapon::eFire || other_weapon_state == CWeapon::eFire2);
			}
		}

		bool other_enemy_targeting_us = false;
		if (last_hit_time != 0 && (Device.dwTimeGlobal - last_hit_time) < 3000) {
			ALife::_OBJECT_ID hit_source = stalker->memory().hit().last_hit_object_id();
			if (hit_source == other_enemy->ID()) {
				other_enemy_targeting_us = true;
			}
		}

		float other_distance = stalker->Position().distance_to(other_enemy->Position());
		float other_threat_score = 0.0f;

		if (other_visible)
			other_threat_score += 100.0f;
		else
			other_threat_score += 20.0f;

		if (other_enemy_firing)
			other_threat_score += 120.0f;

		if (other_enemy_targeting_us)
			other_threat_score += 150.0f;

		if (other_distance > 0.1f)
			other_threat_score += (50.0f / other_distance);

		if (other_distance < current_distance * 0.6f)
			other_threat_score += 80.0f;

		u32 other_last_seen = stalker->memory().visual().visible_object_time_last_seen(other_enemy);
		if (other_last_seen != u32(-1)) {
			u32 time_since_other_seen = Device.dwTimeGlobal - other_last_seen;
			if (time_since_other_seen < 2000) {
				float recency_factor = 1.0f - (time_since_other_seen / 2000.0f);
				other_threat_score += 40.0f * recency_factor;
			}
		}

		if (other_threat_score > best_threat_score) {
			best_threat_score = other_threat_score;
			better_enemy = other_enemy;
		}
	}

	float threat_multiplier = in_combat_cover ? 2.0f : 1.3f;

	if (current_enemy_targeting_us) {
		threat_multiplier = 1.5f;
	}

	if (in_combat_cover && current_enemy_visible && (current_enemy_firing || current_enemy_targeting_us)) {
		threat_multiplier = 3.0f;
	}

	if (better_enemy && (best_threat_score > current_threat_score * threat_multiplier)) {
		return (false);
	}

	return (true);
}