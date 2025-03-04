// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#include "..\ragebot\aim.h"
#include "fakelag.h"
#include "misc.h"
#include "prediction_system.h"
#include "logs.h"

void fakelag::Fakelag(CUserCmd* m_pcmd)
{
	if (g_cfg.antiaim.fakelag && !globals.g.exploits)
	{
		static auto force_choke = false;

		if (force_choke)
		{
			force_choke = false;
			globals.send_packet = false;
			return;
		}

		if (globals.local()->m_fFlags() & FL_ONGROUND && !(engineprediction::get().backup_data.flags & FL_ONGROUND))
		{
			force_choke = true;
			globals.send_packet = false;
			return;
		}
	}

	static auto sv_maxusrcmdprocessticks = m_cvar()->FindVar("sv_maxusrcmdprocessticks");
	static auto fluctuate_ticks = 0;
	static auto switch_ticks = false;
	static auto random_factor = min(rand() % sv_maxusrcmdprocessticks->GetInt() + 1, g_cfg.antiaim.triggers_fakelag_amount);

	auto choked = m_clientstate()->iChokedCommands; //-V807
	auto flags = engineprediction::get().backup_data.flags; //-V807
	auto velocity = engineprediction::get().backup_data.velocity.Length(); //-V807
	auto velocity2d = engineprediction::get().backup_data.velocity.Length2D();

	auto max_speed = 260.0f;
	auto weapon_info = globals.g.weapon->get_csweapon_info();

	if (weapon_info)
		max_speed = globals.g.scoped ? weapon_info->flMaxPlayerSpeedAlt : weapon_info->flMaxPlayerSpeed;

	switch (g_cfg.antiaim.fakelag_type)
	{
	case 0:
		max_choke = g_cfg.antiaim.triggers_fakelag_amount;
		break;
	case 1:
		max_choke = random_factor;
		break;
	case 2:
		if (velocity2d >= 5.0f)
		{
			auto dynamic_factor = std::ceilf(64.0f / (velocity2d * m_globals()->m_intervalpertick));

			if (dynamic_factor > sv_maxusrcmdprocessticks->GetInt())
				dynamic_factor = g_cfg.antiaim.triggers_fakelag_amount;

			max_choke = dynamic_factor;
		}
		else
			max_choke = g_cfg.antiaim.triggers_fakelag_amount;
		break;
	case 3:
		max_choke = fluctuate_ticks;
		break;
	}

	if (m_gamerules()->m_bIsValveDS()) //-V807
		max_choke = m_engine()->IsVoiceRecording() ? 1 : min(max_choke, 6);

	if (misc::get().recharging_double_tap)
		max_choke = globals.g.weapon->get_max_tickbase_shift();

	if (globals.local()->m_fFlags() & FL_ONGROUND && engineprediction::get().backup_data.flags & FL_ONGROUND && !m_gamerules()->m_bIsValveDS() && key_binds::get().get_key_bind_state(20)) //-V807
	{
		max_choke = sv_maxusrcmdprocessticks->GetInt() - 2;

		if (choked < max_choke)
			globals.send_packet = false;
		else
			globals.send_packet = true;
	}
	else
	{
		if (g_cfg.ragebot.enable && globals.g.current_weapon != -1 && !globals.g.exploits && g_cfg.antiaim.fakelag && g_cfg.antiaim.fakelag_enablers[FAKELAG_PEEK] && g_cfg.antiaim.triggers_fakelag_amount > 6 && !started_peeking && velocity >= 5.0f)
		{
			auto predicted_eye_pos = globals.g.eye_pos + engineprediction::get().backup_data.velocity * m_globals()->m_intervalpertick * (float)g_cfg.antiaim.triggers_fakelag_amount * 0.5f;

			for (auto i = 1; i < m_globals()->m_maxclients; i++) // red
			{
				auto e = static_cast<player_t*>(m_entitylist()->GetClientEntity(i));

				if (!e->valid(true))
					continue;

				auto records = &player_records[i]; //-V826

				if (records->empty())
					continue;

				auto record = &records->front();

				if (!record->valid())
					continue;

				scan_data predicted_data;
				aim::get().scan(record, predicted_data, predicted_eye_pos, true);

				if (predicted_data.valid())
				{
					scan_data data;
					aim::get().scan(record, data, globals.g.eye_pos, true);

					if (!data.valid())
					{
						random_factor = min(rand() % sv_maxusrcmdprocessticks->GetInt() + 1, g_cfg.antiaim.triggers_fakelag_amount);
						switch_ticks = !switch_ticks;
						fluctuate_ticks = switch_ticks ? g_cfg.antiaim.triggers_fakelag_amount : max(g_cfg.antiaim.triggers_fakelag_amount - 2, 1);

						globals.send_packet = true;
						started_peeking = true;

						return;
					}
				}
			}
		}

		if (!globals.g.exploits && g_cfg.antiaim.fakelag && g_cfg.antiaim.fakelag_enablers[FAKELAG_PEEK] && started_peeking)
		{
			if (choked < max_choke)
				globals.send_packet = false;
			else
			{
				started_peeking = false;

				random_factor = min(rand() % sv_maxusrcmdprocessticks->GetInt() + 1, g_cfg.antiaim.triggers_fakelag_amount);
				switch_ticks = !switch_ticks;
				fluctuate_ticks = switch_ticks ? g_cfg.antiaim.triggers_fakelag_amount : max(g_cfg.antiaim.triggers_fakelag_amount - 2, 1);

				globals.send_packet = true;
			}
		}
		else if (!globals.g.exploits && g_cfg.antiaim.fakelag && velocity >= 5.0f && globals.g.slowwalking && g_cfg.antiaim.fakelag_enablers[FAKELAG_SLOW_WALK])
		{
			if (choked < max_choke)
				globals.send_packet = false;
			else
			{
				started_peeking = false;

				random_factor = min(rand() % sv_maxusrcmdprocessticks->GetInt() + 1, g_cfg.antiaim.triggers_fakelag_amount);
				switch_ticks = !switch_ticks;
				fluctuate_ticks = switch_ticks ? g_cfg.antiaim.triggers_fakelag_amount : max(g_cfg.antiaim.triggers_fakelag_amount - 2, 1);

				globals.send_packet = true;
			}
		}
		else if (!globals.g.exploits && g_cfg.antiaim.fakelag && velocity >= 5.0f && !globals.g.slowwalking && globals.local()->m_fFlags() & FL_ONGROUND && engineprediction::get().backup_data.flags & FL_ONGROUND && g_cfg.antiaim.fakelag_enablers[FAKELAG_MOVE])
		{
			if (choked < max_choke)
				globals.send_packet = false;
			else
			{
				started_peeking = false;

				random_factor = min(rand() % sv_maxusrcmdprocessticks->GetInt() + 1, g_cfg.antiaim.triggers_fakelag_amount);
				switch_ticks = !switch_ticks;
				fluctuate_ticks = switch_ticks ? g_cfg.antiaim.triggers_fakelag_amount : max(g_cfg.antiaim.triggers_fakelag_amount - 2, 1);

				globals.send_packet = true;
			}
		}
		else if (!globals.g.exploits && g_cfg.antiaim.fakelag && !globals.g.slowwalking && !(globals.local()->m_fFlags() & FL_ONGROUND && engineprediction::get().backup_data.flags & FL_ONGROUND) && g_cfg.antiaim.fakelag_enablers[FAKELAG_AIR])
		{
			if (choked < max_choke)
				globals.send_packet = false;
			else
			{
				started_peeking = false;

				random_factor = min(rand() % sv_maxusrcmdprocessticks->GetInt() + 1, g_cfg.antiaim.triggers_fakelag_amount);
				switch_ticks = !switch_ticks;
				fluctuate_ticks = switch_ticks ? g_cfg.antiaim.triggers_fakelag_amount : max(g_cfg.antiaim.triggers_fakelag_amount - 2, 1);

				globals.send_packet = true;
			}
		}
		else if (!globals.g.exploits && g_cfg.antiaim.fakelag)
		{
			max_choke = g_cfg.antiaim.fakelag_amount;

			if (m_gamerules()->m_bIsValveDS())
				max_choke = min(max_choke, 6);

			if (choked < max_choke)
				globals.send_packet = false;
			else
			{
				started_peeking = false;

				random_factor = min(rand() % sv_maxusrcmdprocessticks->GetInt() + 1, g_cfg.antiaim.fakelag_amount);
				switch_ticks = !switch_ticks;
				fluctuate_ticks = switch_ticks ? g_cfg.antiaim.fakelag_amount : max(g_cfg.antiaim.fakelag_amount - 2, 1);

				globals.send_packet = true;
			}
		}
		else if (globals.g.exploits || !antiaim::get().condition(m_pcmd, false) && (antiaim::get().type == ANTIAIM_LEGIT || g_cfg.antiaim.type[antiaim::get().type].desync)) //-V648
		{
			condition = true;
			started_peeking = false;

			if (choked < 1)
				globals.send_packet = false;
			else
				globals.send_packet = true;
		}
		else
			condition = true;
	}
}

void fakelag::Createmove()
{
	if (FakelagCondition(globals.get_command()))
		return;

	Fakelag(globals.get_command());

	static auto sv_maxusrcmdprocessticks = m_cvar()->FindVar("sv_maxusrcmdprocessticks");

	if (!m_gamerules()->m_bIsValveDS() && m_clientstate()->iChokedCommands <= sv_maxusrcmdprocessticks->GetInt())
	{
		static auto Fn = util::FindSignature(crypt_str("engine.dll"), crypt_str("B8 ? ? ? ? 3B F0 0F 4F F0 89 5D FC")) + 0x1;
		DWORD old = 0;

		VirtualProtect((void*)Fn, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &old);
		*(uint32_t*)Fn = 17;
		VirtualProtect((void*)Fn, sizeof(uint32_t), old, &old);
	}
}

bool fakelag::FakelagCondition(CUserCmd* m_pcmd)
{
	condition = false;

	if (globals.local()->m_bGunGameImmunity() || globals.local()->m_fFlags() & FL_FROZEN)
		condition = true;

	if (antiaim::get().freeze_check && !misc::get().double_tap_enabled && !misc::get().hide_shots_enabled)
		condition = true;

	return condition;
}