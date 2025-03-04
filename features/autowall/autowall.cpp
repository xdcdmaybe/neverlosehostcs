// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#include "autowall.h"

bool autowall::is_breakable_entity(IClientEntity* e)
{
	if (!e || e->EntIndex() == 0)
		return false;

	//m_takeDamage isn't properly set when using the signature.
	//Back it up, set it to true, then restore.
	static auto is_breakable = util::FindSignature(crypt_str("client.dll"), crypt_str("55 8B EC 51 56 8B F1 85 F6 74 68"));

	auto take_damage = *(uintptr_t*)((uintptr_t)is_breakable + 0x26);
	auto takeDamageBackup = *(uint8_t*)((uintptr_t)e + take_damage);

	auto pClass = e->GetClientClass();

	//				 '       ''     '      '   '
	//			    01234567890123456     012345678
	//Check against CBreakableSurface and CBaseDoor

	//Windows etc. are CBrekableSurface
	//Large garage door in Office is CBaseDoor and it get's reported as a breakable when it is not one
	//This is seperate from "CPropDoorRotating", which is a normal door.
	//Normally you would also check for "CFuncBrush" but it was acting oddly so I removed it. It's below if interested.
	//((clientClass->m_pNetworkName[1]) != 'F' || (clientClass->m_pNetworkName[4]) != 'c' || (clientClass->m_pNetworkName[5]) != 'B' || (clientClass->m_pNetworkName[9]) != 'h')

	if ((pClass->m_pNetworkName[1] == 'B' && pClass->m_pNetworkName[9] == 'e' && pClass->m_pNetworkName[10] == 'S' && pClass->m_pNetworkName[16] == 'e')
		|| (pClass->m_pNetworkName[1] != 'B' || pClass->m_pNetworkName[5] != 'D'))
		*(uint8_t*)((uintptr_t)e + take_damage) = DAMAGE_YES;

	bool breakable = is_breakable;

	return breakable;
}

void autowall::scale_damage(player_t* e, CGameTrace& enterTrace, weapon_info_t* weaponData, float& currentDamage)
{
	if (!e->is_player())
		return;

	//Cred. to N0xius for reversing this.
	//TODO: _xAE^; look into reversing this yourself sometime
	bool hasHeavyArmor = e->m_bHasHeavyArmor();
	int armorValue = e->m_ArmorValue();
	int hitGroup = enterTrace.hitgroup;

	//Fuck making a new function, lambda beste. ~ Does the person have armor on for the hitbox checked?
	auto IsArmored = [&]()->bool
	{
		switch (hitGroup)
		{
		case HITGROUP_HEAD:
			return !!e->m_bHasHelmet(); //Fuck compiler errors - force-convert it to a bool via (!!)
		case HITGROUP_GENERIC:
		case HITGROUP_CHEST:
		case HITGROUP_STOMACH:
		case HITGROUP_LEFTARM:
		case HITGROUP_RIGHTARM:
			return true;
		default:
			return false;
		}
	};

	switch (hitGroup)
	{
	case HITGROUP_HEAD:
		currentDamage *= hasHeavyArmor ? 2.f : 4.f; //Heavy Armor does 1/2 damage
		break;
	case HITGROUP_STOMACH:
		currentDamage *= 1.25f;
		break;
	case HITGROUP_LEFTLEG:
	case HITGROUP_RIGHTLEG:
		currentDamage *= 0.75f;
		break;
	default:
		break;
	}

	if (armorValue > 0 && IsArmored())
	{
		float bonusValue = 1.f, armorBonusRatio = 0.5f, armorRatio = weaponData->flArmorRatio / 2.f;

		//Damage gets modified for heavy armor users
		if (hasHeavyArmor)
		{
			armorBonusRatio = 0.33f;
			armorRatio *= 0.5f;
			bonusValue = 0.33f;
		}

		auto NewDamage = currentDamage * armorRatio;

		if (hasHeavyArmor)
			NewDamage *= 0.85f;

		if (((currentDamage - (currentDamage * armorRatio)) * (bonusValue * armorBonusRatio)) > armorValue)
			NewDamage = currentDamage - (armorValue / armorBonusRatio);

		currentDamage = NewDamage;
	}
}

bool autowall::trace_to_exit(CGameTrace& enterTrace, CGameTrace& exitTrace, Vector startPosition, const Vector& direction)
{
	Vector start, end;
	float maxDistance = 90.f, rayExtension = 4.f, currentDistance = 0;
	int firstContents = 0;

	while (currentDistance <= maxDistance)
	{
		//Add extra distance to our ray
		currentDistance += rayExtension;

		//Multiply the direction vector to the distance so we go outwards, add our position to it.
		start = startPosition + direction * currentDistance;

		if (!firstContents)
			firstContents = m_trace()->GetPointContents(start, MASK_SHOT_HULL | CONTENTS_HITBOX, nullptr);

		int pointContents = m_trace()->GetPointContents(start, MASK_SHOT_HULL | CONTENTS_HITBOX, nullptr);

		if (!(pointContents & MASK_SHOT_HULL) || pointContents & CONTENTS_HITBOX && pointContents != firstContents)
		{
			//Let's setup our end position by deducting the direction by the extra added distance
			end = start - (direction * rayExtension);

			//Let's cast a ray from our start pos to the end pos
			util::trace_line(start, end, MASK_SHOT_HULL | CONTENTS_HITBOX, nullptr, &exitTrace);

			//Let's check if a hitbox is in-front of our enemy and if they are behind a solid wall
			if (exitTrace.startsolid && exitTrace.surface.flags & SURF_HITBOX)
			{
				CTraceFilter filter;
				filter.pSkip = exitTrace.hit_entity;

				util::trace_line(end, startPosition, MASK_SHOT_HULL, &filter, &exitTrace);

				if (exitTrace.DidHit() && !exitTrace.startsolid)
				{
					start = exitTrace.endpos;
					return true;
				}

				continue;
			}

			//Can we hit? Is the wall solid?
			if (exitTrace.DidHit() && !exitTrace.startsolid)
			{

				//Is the wall a breakable? If so, let's shoot through it.
				if (is_breakable_entity(enterTrace.hit_entity) && is_breakable_entity(exitTrace.hit_entity))
					return true;

				if (enterTrace.surface.flags & SURF_NODRAW || !(exitTrace.surface.flags & SURF_NODRAW) && (exitTrace.plane.normal.Dot(direction) <= 1.f))
				{
					float multAmount = exitTrace.fraction * 4.f;
					start -= direction * multAmount;
					return true;
				}

				continue;
			}

			if (!exitTrace.DidHit() || exitTrace.startsolid)
			{
				if (enterTrace.hit_entity && enterTrace.hit_entity->EntIndex() && is_breakable_entity(enterTrace.hit_entity))
				{
					exitTrace = enterTrace;
					exitTrace.endpos = start + direction;
					return true;
				}
			}
		}
	}
	return false;
}

bool autowall::handle_bullet_penetration(weapon_info_t* weaponData, CGameTrace& enterTrace, Vector& eyePosition, const Vector& direction, int& possibleHitsRemaining, float& currentDamage, float penetrationPower, float ff_damage_reduction_bullets, float ff_damage_bullet_penetration, bool draw_impact)
{

	trace_t exitTrace;
	player_t* pEnemy = (player_t*)enterTrace.hit_entity;
	surfacedata_t* enterSurfaceData = m_physsurface()->GetSurfaceData(enterTrace.surface.surfaceProps);
	int enterMaterial = enterSurfaceData->game.material;

	float enterSurfPenetrationModifier = enterSurfaceData->game.flPenetrationModifier;
	float enterDamageModifier = enterSurfaceData->game.flDamageModifier;
	float thickness, modifier, lostDamage, finalDamageModifier, combinedPenetrationModifier;
	bool isSolidSurf = ((enterTrace.contents >> 3) & CONTENTS_SOLID);
	bool isLightSurf = ((enterTrace.surface.flags >> 7) & SURF_LIGHT);

	if (possibleHitsRemaining <= 0
		//Test for "DE_CACHE/DE_CACHE_TELA_03" as the entering surface and "CS_ITALY/CR_MISCWOOD2B" as the exiting surface.
		//Fixes a wall in de_cache which seems to be broken in some way. Although bullet penetration is not recorded to go through this wall
		//Decals can be seen of bullets within the glass behind of the enemy. Hacky method, but works.
		//You might want to have a check for this to only be activated on de_cache.
		|| (enterTrace.surface.name == (const char*)0x2227c261 && exitTrace.surface.name == (const char*)0x2227c868)
		|| (!possibleHitsRemaining && !isLightSurf && !isSolidSurf && enterMaterial != CHAR_TEX_GRATE && enterMaterial != CHAR_TEX_GLASS)
		|| weaponData->flPenetration <= 0.f
		|| !trace_to_exit(enterTrace, exitTrace, enterTrace.endpos, direction)
		&& !(m_trace()->GetPointContents(enterTrace.endpos, MASK_SHOT_HULL, nullptr) & MASK_SHOT_HULL))
		return false;

	surfacedata_t* exitSurfaceData = m_physsurface()->GetSurfaceData(exitTrace.surface.surfaceProps);
	int exitMaterial = exitSurfaceData->game.material;
	float exitSurfPenetrationModifier = exitSurfaceData->game.flPenetrationModifier;
	float exitDamageModifier = exitSurfaceData->game.flDamageModifier;

	if (enterMaterial == CHAR_TEX_GRATE || enterMaterial == CHAR_TEX_GLASS)
	{
		combinedPenetrationModifier = 3.f;
		finalDamageModifier = 0.05f;
	}

	else if (isSolidSurf || isLightSurf)
	{
		combinedPenetrationModifier = 1.f;
		finalDamageModifier = 0.16f;
	}
	else if (enterMaterial == CHAR_TEX_FLESH && (pEnemy->m_iTeamNum() == globals.local()->m_iTeamNum() && ff_damage_reduction_bullets == 0.f)) //TODO: Team check config
	{
		//Look's like you aren't shooting through your teammate today
		if (ff_damage_bullet_penetration == 0.f)
			return false;

		//Let's shoot through teammates and get kicked for teamdmg! Whatever, atleast we did damage to the enemy. I call that a win.
		combinedPenetrationModifier = ff_damage_bullet_penetration;
		finalDamageModifier = 0.16f;
	}
	else
	{
		combinedPenetrationModifier = (enterSurfPenetrationModifier + exitSurfPenetrationModifier) / 2.f;
		finalDamageModifier = 0.16f;
	}

	//Do our materials line up?
	if (enterMaterial == exitMaterial)
	{
		if (exitMaterial == CHAR_TEX_CARDBOARD || exitMaterial == CHAR_TEX_WOOD)
			combinedPenetrationModifier = 3.f;
		else if (exitMaterial == CHAR_TEX_PLASTIC)
			combinedPenetrationModifier = 2.f;
	}


	//Calculate thickness of the wall by getting the length of the range of the trace and squaring
	thickness = (exitTrace.endpos - enterTrace.endpos).LengthSqr();
	modifier = fmaxf(1.f / combinedPenetrationModifier, 0.f);

	//This calculates how much damage we've lost depending on thickness of the wall, our penetration, damage, and the modifiers set earlier
	lostDamage = fmaxf(
		((modifier * thickness) / 24.f)
		+ ((currentDamage * finalDamageModifier)
			+ (fmaxf(3.75f / penetrationPower, 0.f) * 3.f * modifier)), 0.f);

	//Did we loose too much damage?
	if (lostDamage > currentDamage)
		return false;

	//We can't use any of the damage that we've lost
	if (lostDamage > 0.f)
		currentDamage -= lostDamage;

	//Do we still have enough damage to deal?
	if (currentDamage < 1.f)
		return false;

	eyePosition = exitTrace.endpos;
	--possibleHitsRemaining;

	return true;
}

bool autowall::fire_bullet(weapon_t* pWeapon, Vector& direction, bool& visible, float& currentDamage, int& hitbox, IClientEntity* e, float length, const Vector& pos)
{
	if (!pWeapon)
		return false;

	float currentDistance = 0.f, penetrationPower, penetrationDistance, maxRange, ff_damage_reduction_bullets, ff_damage_bullet_penetration, rayExtension = 40.f;
	Vector eyePosition = pos;

	static auto damageReductionBullets = m_cvar()->FindVar(crypt_str("ff_damage_reduction_bullets"));
	static auto damageBulletPenetration = m_cvar()->FindVar(crypt_str("ff_damage_bullet_penetration"));
	ff_damage_reduction_bullets = damageReductionBullets->GetFloat();
	ff_damage_bullet_penetration = damageBulletPenetration->GetFloat();

	auto weaponData = pWeapon->get_csweapon_info();

	CGameTrace enterTrace;

	//We should be skipping g_pLocalPlayer when casting a ray to players.
	CTraceFilter filter;
	filter.pSkip = globals.local();

	if (!weaponData)
		return false;

	maxRange = weaponData->flRange;
	penetrationDistance = 3000.0f;
	penetrationPower = weaponData->flPenetration;

	//This gets set in FX_Firebullets to 4 as a pass-through value.
	//CS:GO has a maximum of 4 surfaces a bullet can pass-through before it 100% stops.
	//Excerpt from Valve: https://steamcommunity.com/sharedfiles/filedetails/?id=275573090
	//"The total number of surfaces any bullet can penetrate in a single flight is capped at 4." -CS:GO Official
	int possibleHitsRemaining = 4;

	//Set our current damage to what our gun's initial damage reports it will do
	currentDamage = weaponData->iDamage;

	//If our damage is greater than (or equal to) 1, and we can shoot, let's shoot.
	while (possibleHitsRemaining > 0 && currentDamage >= 1.f)
	{
		//Calculate max bullet range
		maxRange -= currentDistance;

		//Create endpoint of bullet
		Vector end = eyePosition + direction * maxRange;

		util::trace_line(eyePosition, end, MASK_SHOT_HULL | CONTENTS_HITBOX, &filter, &enterTrace);
		util::clip_trace_to_players(e, eyePosition, end + direction * rayExtension, MASK_SHOT_HULL | CONTENTS_HITBOX, &filter, &enterTrace);

		//We have to do this *after* tracing to the player.
		surfacedata_t* enterSurfaceData = m_physsurface()->GetSurfaceData(enterTrace.surface.surfaceProps);
		float enterSurfPenetrationModifier = enterSurfaceData->game.flPenetrationModifier;
		int enterMaterial = enterSurfaceData->game.material;

		//"Fraction == 1" means that we didn't hit anything. We don't want that- so let's break on it.
		if (enterTrace.fraction >= 1.f)	// == 1.f
		{
			break;
		}

		//calculate the damage based on the distance the bullet traveled.
		currentDistance += enterTrace.fraction * maxRange;

		//Let's make our damage drops off the further away the bullet is.
		currentDamage *= pow(weaponData->flRangeModifier, currentDistance / 500.0f);

		//Sanity checking / Can we actually shoot through?
		if (currentDistance > penetrationDistance && weaponData->flPenetration > 0.f || enterSurfPenetrationModifier < 0.1f)
			break;

		//This looks gay as fuck if we put it into 1 long line of code.
		bool canDoDamage = (enterTrace.hitgroup != HITGROUP_GEAR && enterTrace.hitgroup != HITGROUP_GENERIC);
		auto isPlayer = ((player_t*)enterTrace.hit_entity)->is_player();
		auto isEnemy = ((player_t*)enterTrace.hit_entity)->m_iTeamNum() != globals.local()->m_iTeamNum();

		//TODO: Team check config
		if (canDoDamage && isPlayer && isEnemy)
		{
			scale_damage((player_t*)enterTrace.hit_entity, enterTrace, weaponData, currentDamage);
			hitbox = enterTrace.hitbox;
			return true;
		}

		if (isPlayer && (player_t*)enterTrace.hit_entity == globals.local())
		{
			enterTrace.hitgroup = 1;
			//scale_damage( g_pLocalPlayer, enterTrace, weaponData, currentDamage );
			return true;
		}

		//auto pos = eyePosition;

		//Calling HandleBulletPenetration here reduces our penetrationCounter, and if it returns true, we can't shoot through it.
		if (!handle_bullet_penetration(weaponData, enterTrace, eyePosition, direction, possibleHitsRemaining, currentDamage, penetrationPower, ff_damage_reduction_bullets, ff_damage_bullet_penetration, !e))
			break;

		visible = false;
	}
	return false;
}

autowall::returninfo_t autowall::wall_penetration(const Vector& eye_pos, Vector& point, IClientEntity* e)
{
	globals.g.autowalling = true;
	auto tmp = point - eye_pos;

	auto angles = ZERO;
	math::vector_angles(tmp, angles);

	auto direction = ZERO;
	math::angle_vectors(angles, direction);

	direction.NormalizeInPlace();

	auto visible = true;
	auto damage = -1.0f;
	auto hitbox = -1;

	auto weapon = globals.local()->m_hActiveWeapon().Get();

	if (fire_bullet(weapon, direction, visible, damage, hitbox, e, 0.0f, eye_pos))
	{
		globals.g.autowalling = false;
		return returninfo_t(visible, (int)damage, hitbox);
	}
	else
	{
		globals.g.autowalling = false;
		return returninfo_t(false, -1, -1);
	}
}