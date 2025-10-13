////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_movement_manager_smart_cover.cpp
//	Created 	: 27.12.2003
//	Modified	: 13.02.2008
//	Author		: Dmitriy Iassenev
//	Description : stalker movement manager class with smart covers
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "stalker_movement_manager_smart_cover.h"
#include "movement_manager_space.h"
#include "smart_cover_animation_selector.h"
#include "smart_cover_planner_target_selector.h"
#include "smart_cover_loophole.h"
#include "smart_cover.h"
#include "ai/stalker/ai_stalker.h"
#include "sight_manager.h"
#include "stalker_animation_manager.h"
#include "stalker_movement_params.h"
#include "smart_cover_transition.hpp"
#include "level_path_manager.h"
#include "detail_path_manager.h"
#include "script_game_object.h"
#include "smart_cover_transition_animation.hpp"
#include "CharacterPhysicsSupport.h"
#include "inventory.h"
#include "weapon.h"
#include "memory_manager.h"
#include "enemy_manager.h"
#include "animation_movement_controller.h"
#include "ai_object_location.h"
#include "visual_memory_manager.h"

namespace smart_cover
{
	shared_str transform_vertex(shared_str const& vertex_id, bool const& in);
} // namespace smart_cover

stalker_movement_manager_smart_cover::stalker_movement_manager_smart_cover(CAI_Stalker* object) :
	inherited(object),
	m_property_storage(0),
	m_current_transition(0),
	m_current_transition_animation(0),
	m_non_animated_loophole_change(false),
	m_apply_loophole_direction_distance(4.f),
	m_animation_selector(0),
	m_entering_smart_cover_with_animation(false),
	m_default_behaviour(false),
	m_enter_cover_id(""),
	m_enter_loophole_id(""),
	m_check_can_kill_enemy(false),
	m_combat_behaviour(false),
	m_alignment_in_progress(false),
	m_alignment_start_time(0),
	m_position_reached_time(0),
	m_cover_entry_time(0),
	m_alignment_timeout_forced(false)
{
	m_target.construct(this);
	m_target_selector = xr_new<target_selector_type>();
}

stalker_movement_manager_smart_cover::~stalker_movement_manager_smart_cover()
{
	xr_delete(m_animation_selector);
	xr_delete(m_target_selector);
}

void stalker_movement_manager_smart_cover::reinit()
{
	inherited::reinit();

	xr_delete(m_animation_selector);
	m_animation_selector = xr_new<animation_selector_type>(&object());
	m_animation_selector->setup(&object(), m_property_storage);

	m_target.construct(this);

	m_alignment_in_progress = false;
	m_alignment_start_time = 0;
	m_position_reached_time = 0;
	m_alignment_timeout_forced = false;
}

void stalker_movement_manager_smart_cover::update(u32 time_delta)
{
	if (object().getDestroy() || !object().g_Alive())
		return;

	if (!m_current.cover()) {
		if (m_target.cover())
			enter_smart_cover(time_delta);
		else
			inherited::update(time_delta);
		return;
	}

	const CEntityAlive* enemy = object().memory().enemy().selected();
	if (!enemy || !enemy->g_Alive()) {
		m_target.cover_id("");
		return;
	}

	if (m_non_animated_loophole_change)
		non_animated_change_loophole();

	VERIFY(m_current.cover_loophole());
	if (m_target.cover() && (m_current.cover_loophole() == m_target.cover_loophole()))
	{
		m_current.cover_fire_object(m_target.cover_fire_object());
		m_current.cover_fire_position(m_target.cover_fire_position());
	}

	u32 const grace_period = 300;
	if (Device.dwTimeGlobal - m_cover_entry_time > grace_period)
	{
		m_target_selector->update();
	}
}

void stalker_movement_manager_smart_cover::enter_smart_cover()
{
	VERIFY(m_target.cover());
	VERIFY(m_target.cover_loophole());

	smart_cover::loophole const& target_loophole = *m_target.cover_loophole();
	smart_cover::loophole const& loophole =
		target_loophole.enterable() ? target_loophole : nearest_enterable_loophole();

	if (!m_current.cover() && (m_enter_cover_id != "") && ((m_target.cover_id() != m_enter_cover_id) || (m_target.cover_loophole_id() != m_enter_loophole_id)))
	{
#ifdef DEBUG
		Msg("setting up cover: %s (%s)", m_enter_cover_id.c_str(), m_enter_loophole_id.c_str());
#endif
		m_current.cover_id(m_enter_cover_id);
		m_current.cover_loophole_id(m_enter_loophole_id);
	}
	else
	{
		go_next_loophole();
		VERIFY(m_current.cover_id()._get() == m_target.cover_id()._get());
		if (&loophole == m_target.cover_loophole())
		{
			m_current.cover_fire_object(m_target.cover_fire_object());
			m_current.cover_fire_position(m_target.cover_fire_position());
		}
	}

	if (!m_current.cover() || !m_current.cover_loophole()) {
		Msg("! [SmartCover] CRITICAL: %s failed to set cover in enter_smart_cover()!", object().cName().c_str());
		Msg("  m_current.cover() = %s", m_current.cover() ? "SET" : "NULL");
		Msg("  m_enter_cover_id = %s", m_enter_cover_id.c_str());
		Msg("  m_enter_loophole_id = %s", m_enter_loophole_id.c_str());
		m_entering_smart_cover_with_animation = false;
		return;
	}

	bind_global_selector();
	m_animation_selector->initialize();
	m_cover_entry_time = Device.dwTimeGlobal;
}

MotionID stalker_movement_manager_smart_cover::select_animation(bool& animation_movement_controller)
{
	if (!object().g_Alive())
		return (MotionID());

	animation_movement_controller = true;
	VERIFY(m_entering_smart_cover_with_animation || current_transition().animation().has_animation());
	return (m_enter_animation);
}

void stalker_movement_manager_smart_cover::on_animation_end()
{
	VERIFY(m_entering_smart_cover_with_animation);
	VERIFY(!m_current.cover());
	m_entering_smart_cover_with_animation = false;

	if (!m_target.cover()) {
		if (!m_current.cover())
			unbind_global_selector();
		return;
	}

	enter_smart_cover();
}

void stalker_movement_manager_smart_cover::on_frame(CPHMovementControl* movement_control, Fvector& dest_position)
{
	inherited::on_frame(movement_control, dest_position);
}

extern float g_smart_cover_animation_speed_factor;

void stalker_movement_manager_smart_cover::modify_animation(CBlend* blend)
{
	if (!blend)
		return;

	CMotionDef* motion_def = smart_cast<IKinematicsAnimated*>(object().Visual())->LL_GetMotionDef(blend->motionID);
	VERIFY(motion_def);
	blend->speed = motion_def->Speed() * g_smart_cover_animation_speed_factor;
}

bool show_restrictions(CRestrictedObject* object);

// MFB: Fixed alignment logic cancelling out too early along with other bugs.
// - 13.10.2025 -
void stalker_movement_manager_smart_cover::reach_enter_location(u32 const& time_delta)
{
	m_current.m_path_type = MovementManager::ePathTypeLevelPath;
	m_current.m_detail_path_type = DetailPathManager::eDetailPathTypeSmooth;
	m_current.m_mental_state = m_target.m_mental_state;
	m_current.m_body_state = m_target.m_body_state;
	m_current.m_movement_type = m_target.m_movement_type;

	VERIFY(m_target.cover());
	smart_cover::loophole const& target_loophole = *m_target.cover_loophole();
	smart_cover::loophole const& loophole = target_loophole.enterable() ?
		target_loophole : nearest_enterable_loophole();

	Fvector position;
	m_target.cover()->object().XFORM().transform_tiny(
		position,
		current_transition().animation().position()
	);

	u32 level_vertex_id = ai().level_graph().vertex(u32(-1), position);
	if (!accessible(level_vertex_id)) {
		level_vertex_id = restrictions().accessible_nearest(
			ai().level_graph().vertex_position(level_vertex_id),
			position
		);
		VERIFY(restrictions().accessible(level_vertex_id));
	}

	Fvector direction = m_target.cover()->enter_direction(loophole);

	CMovementManager::set_level_dest_vertex(level_vertex_id);
	m_current.desired_position(&position);
	m_current.desired_direction(&direction);

	float actual_distance = object().Position().distance_to(position);

	if (actual_distance > 0.15f) {
		m_alignment_in_progress = false;
		m_alignment_start_time = 0;
		m_alignment_timeout_forced = false;

		if (actual_distance <= m_apply_loophole_direction_distance) {
			object().sight().setup(CSightAction(SightManager::eSightTypeDirection, direction, true));
		}

		inherited::update(m_current);
		return;
	}

	// I have to find a better solution for aligning NPCs reliably.
	Fvector head_dir_current, head_dir_target;

	head_dir_current.setHP(-object().movement().m_head.current.yaw, -object().movement().m_head.current.pitch);
	head_dir_current.y = 0.f;
	head_dir_current.normalize_safe();

	head_dir_target.setHP(-object().movement().m_head.target.yaw, -object().movement().m_head.target.pitch);
	head_dir_target.y = 0.f;
	head_dir_target.normalize_safe();

	float alignment_progress = 0.0f;
	if (m_alignment_in_progress && m_alignment_start_time > 0) {
		u32 elapsed = Device.dwTimeGlobal - m_alignment_start_time;
		alignment_progress = _min(elapsed / 3000.0f, 1.0f);
	}

	// blend: early = 80% current, late = 80% target
	float current_weight = 0.8f - (alignment_progress * 0.6f);
	float target_weight = 1.0f - current_weight;

	Fvector head_dir;
	head_dir.x = head_dir_current.x * current_weight + head_dir_target.x * target_weight;
	head_dir.y = 0.f;
	head_dir.z = head_dir_current.z * current_weight + head_dir_target.z * target_weight;
	head_dir.normalize_safe();

	Fvector desired_dir = direction;
	desired_dir.y = 0.f;
	desired_dir.normalize_safe();

	float alignment_dot = desired_dir.dotproduct(head_dir);
	float required_alignment = INITIAL_ALIGNMENT_TOLERANCE;

	if (!m_alignment_timeout_forced && alignment_dot < required_alignment) {
		if (!m_alignment_in_progress) {
			m_alignment_in_progress = true;
			m_alignment_start_time = Device.dwTimeGlobal;

			Msg("[SmartCover] %s: Starting alignment (current=%.3f, need=%.3f)",
				object().cName().c_str(), alignment_dot, required_alignment);
		}

		u32 elapsed_time = Device.dwTimeGlobal - m_alignment_start_time;

		if (elapsed_time % 150 == 0) {
			Msg("[SmartCover] %s: Aligning... %.1f%% (%.3f/%.3f)",
				object().cName().c_str(),
				alignment_progress * 100.0f,
				alignment_dot,
				required_alignment);
		}

		if (elapsed_time > 1800) {
			Msg("[SmartCover] %s: Alignment timeout after 1.8 seconds (dot=%.3f, required=%.3f), forcing entry",
				object().cName().c_str(), alignment_dot, required_alignment);

			Fmatrix transform;
			transform.setXYZ(0.f, direction.getH(), 0.f);
			transform.c.set(object().Position());
			object().XFORM().set(transform);

			m_alignment_timeout_forced = true;
			m_alignment_in_progress = false;
		}
		else {
			EMovementType saved_type = m_current.m_movement_type;
			m_current.m_movement_type = eMovementTypeStand;

			object().sight().setup(CSightAction(SightManager::eSightTypeDirection, direction, true));

			inherited::update(m_current);
			m_current.m_movement_type = saved_type;
			return;
		}
	}
	else if (m_alignment_in_progress) {
		Msg("[SmartCover] %s: Successfully aligned (final_dot=%.3f)",
			object().cName().c_str(), alignment_dot);
		m_alignment_in_progress = false;
	}

	if (m_target.cover()->is_combat_cover()) {
		const CInventoryItem* item = object().inventory().ActiveItem();

		if (!item || item->BaseSlot() != INV_SLOT_3) {
			if (!m_alignment_timeout_forced) {
				if (object().CObjectHandler::goal_reached()) {
					object().set_goal(MonsterSpace::eObjectActionIdle, object().best_weapon());
				}
				inherited::update(m_current);
				return;
			}
			else {
				Msg("! [SmartCover] %s: Timeout forced, entering without proper weapon", object().cName().c_str());
			}
		}
	}

	m_alignment_timeout_forced = false;

	IKinematicsAnimated* kinematics = smart_cast<IKinematicsAnimated*>(object().Visual());

	if (kinematics) {
		shared_str anim_id = current_transition().animation().animation_id();

		if (anim_id.size()) {
			MotionID motion = kinematics->ID_Cycle(anim_id);

			if (motion != MotionID()) {
				m_enter_cover_id = m_target.cover_id();
				m_enter_loophole_id = loophole.id();
				VERIFY(m_enter_cover_id != "");
				VERIFY(m_enter_loophole_id != "");
				m_enter_animation = motion;

				object().animation().global().target_matrix(position, direction);
				object().sight().setup(CSightAction(SightManager::eSightTypeAnimationDirection, true, false));
				on_smart_cover_enter();
				m_entering_smart_cover_with_animation = true;

				CStalkerAnimationManager& animation = object().animation();
				animation.global_selector(CStalkerAnimationManager::AnimationSelector(this, &stalker_movement_manager_smart_cover::select_animation));
				animation.global_callback(CStalkerAnimationManager::AnimationCallback(this, &stalker_movement_manager_smart_cover::on_animation_end));
#ifdef DEBUG
				animation.global_modifier(CStalkerAnimationManager::AnimationModifier(this, &stalker_movement_manager_smart_cover::modify_animation));
#endif // #ifdef DEBUG
				return;
			}
		}
	}

	m_enter_cover_id = m_target.cover_id();
	m_enter_loophole_id = loophole.id();
	VERIFY(m_enter_cover_id != "");
	VERIFY(m_enter_loophole_id != "");

	enter_smart_cover();
}

void stalker_movement_manager_smart_cover::enter_smart_cover(u32 const& time_delta)
{
	VERIFY(!m_current.cover());

	if (m_entering_smart_cover_with_animation)
		return;

	reach_enter_location(time_delta);
}

void stalker_movement_manager_smart_cover::on_smart_cover_enter()
{
	VERIFY(object().character_physics_support());
	object().character_physics_support()->set_use_hit_anims(false);
}

void stalker_movement_manager_smart_cover::on_smart_cover_exit()
{
	VERIFY(!m_current.cover());

	VERIFY(object().character_physics_support());
	object().character_physics_support()->set_use_hit_anims(true);

	m_current_transition = 0;
	m_current_transition_animation = 0;
	m_non_animated_loophole_change = false;
	m_animation_selector->finalize();
	unbind_global_selector();
	m_alignment_in_progress = false;
	m_alignment_start_time = 0;
	m_position_reached_time = 0;
	
#ifdef DEBUG
	Msg("exiting from cover: %s", m_current.cover_id().c_str());
#endif // #ifdef DEBUG
	m_current.cover_id("");
	inherited::update(m_current);
}

bool stalker_movement_manager_smart_cover::target_approached(float const& distance) const
{
	if (!actual())
		return (false);

	if (!detail().actual())
		return (false);

	return (detail().distance_to_target() < distance);
}

namespace hash_fixed_vertex_manager
{
	IC u32 to_u32(shared_str const& string)
	{
		const str_value* get = string._get();
		return (*(u32 const*)&get);
	}
} // namespace hash_fixed_vertex_manager

void stalker_movement_manager_smart_cover::loophole_path(smart_cover::cover const& cover, shared_str const& source_raw,
	shared_str const& target_raw, LoopholePath& path) const
{
	shared_str source = smart_cover::transform_vertex(source_raw, true);
	shared_str target = smart_cover::transform_vertex(target_raw, false);

	typedef GraphEngineSpace::CBaseParameters CBaseParameters;
	CBaseParameters parameters(u32(-1), u32(-1), u32(-1));
	path.clear_not_free();
	R_ASSERT2(
		ai().graph_engine().search(
			cover.description()->transitions(),
			source,
			target,
			&path,
			parameters
		),
		make_string(
			"cannot build path via loopholes [%s] -> [%s] (cover %s)",
			source_raw.c_str(),
			target_raw.c_str(),
			cover.description()->table_id().c_str()
		)
	);
}


bool stalker_movement_manager_smart_cover::exit_transition()
{
	VERIFY(m_current.cover());

	try_actualize_path();

	VERIFY(!m_path.empty());
	VERIFY(m_path.size() > 1);

	return (m_path[1]._get() == smart_cover::transform_vertex("", false)._get());
}

void stalker_movement_manager_smart_cover::bind_global_selector()
{
	CStalkerAnimationManager& animation = object().animation();

	animation.global_selector(
		CStalkerAnimationManager::AnimationSelector(&animation_selector(),
			&smart_cover::animation_selector::select_animation));
	animation.global_callback(
		CStalkerAnimationManager::AnimationCallback(&animation_selector(),
			&smart_cover::animation_selector::on_animation_end));
#ifdef DEBUG
	animation.global_modifier(CStalkerAnimationManager::AnimationModifier(&animation_selector(), &smart_cover::animation_selector::modify_animation));
#endif // #ifdef DEBUG

	if (!m_current.cover())
		return;

	Fvector position = m_current.cover()->fov_position(*m_current.cover_loophole());
	Fvector direction = m_current.cover()->enter_direction(*m_current.cover_loophole());
	object().animation().global().target_matrix(position, direction);
}

void stalker_movement_manager_smart_cover::unbind_global_selector()
{
	CStalkerAnimationManager& animation = object().animation();

	animation.global_selector(CStalkerAnimationManager::AnimationSelector());
	animation.global_callback(CStalkerAnimationManager::AnimationCallback());
#ifdef DEBUG
	animation.global_modifier(CStalkerAnimationManager::AnimationModifier());
#endif // #ifdef DEBUG

	object().animation().global().target_matrix();
}

stalker_movement_manager_smart_cover::transition_action const& stalker_movement_manager_smart_cover::
current_transition()
{
#ifdef DEBUG
	Msg(
		"m_current_transition guard: [%s][%s] -> [%s][%s], [%d]",
		m_current.cover() ? m_current.cover()->id().c_str() : "<world>",
		m_current.cover() ? m_current.cover_loophole()->id().c_str() : "<no loophole>",
		m_target.cover() ? m_target.cover()->id().c_str() : "<world>",
		m_target.cover() ? m_target.cover_loophole()->id().c_str() : "<no loophole>",
		m_path.size()
	);
#endif // #ifdef DEBUG

	VERIFY(
		(m_current.cover() != m_target.cover()) ||
		!m_current.cover() ||
		(m_current.cover_loophole() != m_target.cover_loophole())
	);

	try_actualize_path();

	VERIFY2(
		m_current_transition,
		make_string(
			"[%s][%s] -> [%s][%s], [%d]",
			m_current.cover() ? m_current.cover()->id().c_str() : "<world>",
			m_current.cover() ? m_current.cover_loophole()->id().c_str() : "<no loophole>",
			m_target.cover() ? m_target.cover()->id().c_str() : "<world>",
			m_target.cover() ? m_target.cover_loophole()->id().c_str() : "<no loophole>",
			m_path.size()
		)
	);
	return (*m_current_transition);
}


void stalker_movement_manager_smart_cover::cleanup_after_animation_selector()
{
	level_path().make_inactual();
	detail().make_inactual();
}

void stalker_movement_manager_smart_cover::target_selector(CScriptCallbackEx<void> const& callback)
{
	VERIFY(m_target_selector);
	m_target_selector->callback(callback);
}

void stalker_movement_manager_smart_cover::target_idle()
{
	//	if (!m_current.cover()) {
	//		Msg								("! Cannot set target idle. Bad or absent smart_cover.");
	//		return;
	//	}

	//	if (!m_current.cover_loophole()->is_action_available("idle")) {
	//		Msg								("! Cannot set target idle. Loophole has no such action.");
	//		return;
	//	}

	m_target_selector->object().target(StalkerDecisionSpace::eWorldPropertyLoopholeIdle);
}

void stalker_movement_manager_smart_cover::target_lookout()
{
	//	if (!m_current.cover()) {
	//		Msg								("! Cannot set target lookout. Bad or absent smart_cover.");
	//		return;
	//	}

	//	if (!m_current.cover_loophole()->is_action_available("lookout")) {
	//		Msg								("! Cannot set target lookout. Loophole has no such action.");
	//		return;
	//	}

	m_target_selector->object().target(StalkerDecisionSpace::eWorldPropertyLookedOut);
}

void stalker_movement_manager_smart_cover::target_fire()
{
	//	if (!m_current.cover()) {
	//		Msg								("! Cannot set target fire. Bad or absent smart_cover.");
	//		return;
	//	}

	//	if (!m_current.cover_loophole()->is_action_available("fire")) {
	//		Msg								("! Cannot set target fire. Loophole has no such action.");
	//		return;
	//	}

	//	if (!enemy_in_fov()) {
	//		Msg								("! Cannot set target fire. Enemy is not in current loophole's fov.");
	//		return;
	//	}

	m_target_selector->object().target(StalkerDecisionSpace::eWorldPropertyLoopholeFire);
}

void stalker_movement_manager_smart_cover::target_fire_no_lookout()
{
	//	if (!current_params().cover()) {
	//		Msg								("! Cannot set target fire_no_lookout. Bad or absent smart_cover.");
	//		return;
	//	}

	//	if (!current_params().cover_loophole()->is_action_available("fire_no_lookout")) {
	//		Msg								("! Cannot set target fire_no_lookout. Loophole has no such action.");
	//		return;
	//	}

	m_target_selector->object().target(StalkerDecisionSpace::eWorldPropertyLoopholeFireNoLookout);
}

void stalker_movement_manager_smart_cover::target_default(bool const& value)
{
	//	if (!current_params().cover()) {
	//		Msg								("! Cannot set target fire_no_lookout. Bad or absent smart_cover.");
	//		return;
	//	}

	//	if (!current_params().cover_loophole()->is_action_available("fire_no_lookout")) {
	//		Msg								("! Cannot set target fire_no_lookout. Loophole has no such action.");
	//		return;
	//	}

	m_default_behaviour = value;
}

bool stalker_movement_manager_smart_cover::default_behaviour() const
{
	VERIFY(m_current.cover());
	VERIFY(m_current.cover_loophole());

	VERIFY(m_target_selector);
	if (m_target_selector->callback())
		return (m_default_behaviour);

	if (m_current.cover_fire_object())
		return (true);

	if (m_current.cover_fire_position())
		return (true);

	return (false);
}

bool stalker_movement_manager_smart_cover::in_smart_cover() const
{
	if (current_params().cover())
		return (true);

	if (entering_smart_cover_with_animation())
		return (true);

	return (false);
}

void stalker_movement_manager_smart_cover::remove_links(CObject* object)
{
	inherited::remove_links(object);

	if (m_target.cover_fire_object() == object)
		m_target.cover_fire_object(0);

	if (m_current.cover_fire_object() == object)
		m_current.cover_fire_object(0);
}
