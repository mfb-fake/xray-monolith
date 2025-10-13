////////////////////////////////////////////////////////////////////////////
//	Module 		: smart_cover_planner_target_provider.cpp
//	Created 	: 18.09.2007
//	Author		: Alexander Dudin
//	Description : Target provider for target selector
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "smart_cover_planner_target_provider.h"
#include "script_game_object.h"
#include "smart_cover_animation_planner.h"
#include "ai/stalker/ai_stalker.h"
#include "agent_manager.h"
#include "agent_enemy_manager.h"
#include "weapon.h"
#include "enemy_manager.h"
#include "visual_memory_manager.h"
#include "stalker_movement_manager_smart_cover.h"

using smart_cover::animation_planner;
using smart_cover::target_provider;
using smart_cover::target_idle;
using smart_cover::target_fire;
using smart_cover::target_fire_no_lookout;

target_provider::target_provider(animation_planner* object, LPCSTR name,
                                 StalkerDecisionSpace::EWorldProperties const& world_property,
                                 u32 const& loophole_value) :
	inherited(object, name),
	m_world_property(world_property),
	m_loophole_value(loophole_value)
{
}

void target_provider::setup(animation_planner* object, CPropertyStorage* storage)
{
	inherited::setup(object, storage);
}

void target_provider::initialize()
{
	inherited::initialize();
	m_object->target(m_world_property);
	m_storage->set_property(m_world_property, true);
	m_object->decrease_loophole_value(m_loophole_value);
}

void target_provider::finalize()
{
	inherited::finalize();
}

////////////////////////////////////////////////////////////////////////////
//	class target_idle
////////////////////////////////////////////////////////////////////////////

target_idle::target_idle(animation_planner* object, LPCSTR name,
                         StalkerDecisionSpace::EWorldProperties const& world_property, u32 const& loophole_value) :
	inherited(object, name, world_property, loophole_value)
{
}

void target_idle::execute()
{
	inherited::execute();

	if (!completed())
		return;

	m_storage->set_property(StalkerDecisionSpace::eWorldPropertyLoopholeTooMuchTimeFiring, false);
}

////////////////////////////////////////////////////////////////////////////
//	class target_fire
////////////////////////////////////////////////////////////////////////////

target_fire::target_fire(animation_planner* object, LPCSTR name,
                         StalkerDecisionSpace::EWorldProperties const& world_property, u32 const& loophole_value) :
	inherited(object, name, world_property, loophole_value)
{
}

void target_fire::initialize()
{
	if (this->m_object->m_object->agent_manager().enemy().enemies().size() > 1)
		set_inertia_time(6000 + ::Random.randI(3000));
	else
		set_inertia_time(0);

	inherited::initialize();
}

void target_fire::execute()
{
	inherited::execute();

	if (!m_inertia_time)
		return;

	if (!completed())
		return;

	CAI_Stalker* stalker = this->m_object->m_object;

	typedef xr_vector<const CEntityAlive*> ENEMIES;
	const ENEMIES& enemies = stalker->memory().enemy().objects();

	bool has_visible_enemies = false;
	bool has_recent_enemies = false;
	u32 current_time = Device.dwTimeGlobal;

	for (ENEMIES::const_iterator it = enemies.begin(); it != enemies.end(); ++it) {
		const CEntityAlive* enemy = *it;
		if (!enemy || !enemy->g_Alive())
			continue;

		bool visible_now = stalker->memory().visual().visible_now(enemy);
		u32 last_seen = stalker->memory().visual().visible_object_time_last_seen(enemy);

		bool is_visible = visible_now;
		bool is_fresh = (last_seen != u32(-1) && (current_time - last_seen) < 2000);

		if (!is_visible && !is_fresh)
			continue;

		if (!stalker->movement().in_current_loophole_fov(enemy->Position()))
			continue;

		if (is_visible) {
			has_visible_enemies = true;
			break;
		}

		if (is_fresh) {
			has_recent_enemies = true;
		}
	}

	if (has_visible_enemies) {
		return;
	}

	if (has_recent_enemies) {
		return;
	}

	if (stalker->ready_to_kill())
	{
		CWeapon* weapon = smart_cast<CWeapon*>(stalker->m_best_item_to_kill);
		if (weapon)
		{
			u32 mag_size = weapon->GetAmmoMagSize();
			u32 ammo_remaining = weapon->GetAmmoElapsed();

			if (ammo_remaining <= mag_size / 6)
			{
				m_storage->set_property(StalkerDecisionSpace::eWorldPropertyLoopholeTooMuchTimeFiring, true);
				return;
			}
		}
	}

	m_storage->set_property(StalkerDecisionSpace::eWorldPropertyLoopholeTooMuchTimeFiring, true);
}

////////////////////////////////////////////////////////////////////////////
//	class target_fire_no_lookout
////////////////////////////////////////////////////////////////////////////

target_fire_no_lookout::target_fire_no_lookout(animation_planner* object, LPCSTR name,
                                               StalkerDecisionSpace::EWorldProperties const& world_property,
                                               u32 const& loophole_value) :
	inherited(object, name, world_property, loophole_value)
{
}

void target_fire_no_lookout::initialize()
{
	m_storage->set_property(StalkerDecisionSpace::eWorldPropertyLookedOut, false);
	inherited::initialize();
}