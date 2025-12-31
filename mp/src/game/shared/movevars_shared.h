//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef MOVEVARS_SHARED_H
#define MOVEVARS_SHARED_H
#ifdef _WIN32
#pragma once
#endif

#include "convar.h"

float GetCurrentGravity( void );

extern ConVar sv_gravity;
extern ConVar sv_stopspeed;
extern ConVar sv_noclipaccelerate;
extern ConVar sv_noclipspeed;
extern ConVar sv_noclipspeed_vertical;
extern ConVar sv_noclipspeed_duck_multiplier;
extern ConVar sv_noclipspeed_sprint_multiplier;
extern ConVar sv_maxspeed;
extern ConVar sv_accelerate;
extern ConVar sv_airspeed_cap;
extern ConVar sv_airaccelerate;
extern ConVar sv_wateraccelerate;
extern ConVar sv_waterfriction;
extern ConVar sv_footsteps;
extern ConVar sv_rollspeed;
extern ConVar sv_rollangle;
extern ConVar sv_friction;
extern ConVar sv_bounce;
extern ConVar sv_maxvelocity;
extern ConVar sv_stepsize;
extern ConVar sv_skyname;
extern ConVar sv_backspeed;
extern ConVar sv_waterdist;
extern ConVar sv_specaccelerate;
extern ConVar sv_specspeed;
extern ConVar sv_specnoclip;
extern ConVar sv_swimsound;

// Momentum convars
extern ConVar sv_considered_on_ground;
extern ConVar sv_duck_collision_fix;
extern ConVar sv_ground_trigger_fix;
extern ConVar sv_edge_fix;
// - - Parkour
extern ConVar mom_pk_autosprint_enable;
extern ConVar sv_pk_wallrun_jump_upspeed;
extern ConVar sv_pk_wallrun_jump_outwardspeed;
extern ConVar sv_pk_wallrun_jump_inputdirspeed;
extern ConVar sv_pk_wallrun_anticipation;
extern ConVar sv_pk_wallrun_time;
extern ConVar sv_pk_wallrun_speed;
extern ConVar sv_pk_wallrun_accel;
extern ConVar sv_pk_wallrun_boost;
extern ConVar sv_pk_wallrun_feet_z;
extern ConVar sv_pk_wallrun_roll;
extern ConVar sv_pk_wallrun_min_rise;
extern ConVar sv_pk_wallrun_max_rise;
extern ConVar sv_pk_wallrun_scramble_z;
extern ConVar sv_pk_wallrun_lookahead;
extern ConVar sv_pk_wallrun_inness;
extern ConVar sv_pk_wallrun_outness;
extern ConVar sv_pk_wallrun_lookness;
extern ConVar sv_pk_wallrun_stick_angle;
extern ConVar sv_pk_wallrun_corner_stick_angle;
extern ConVar sv_pk_coyote_time;
extern ConVar sv_pk_slide_lock;
extern ConVar sv_pk_gravity_scale;
extern ConVar sv_pk_jump_height;
extern ConVar sv_pk_airjump_height;
extern ConVar sv_pk_airjump_min_height_fraction;
extern ConVar sv_pk_airjump_horz_speed;
extern ConVar sv_pk_slide_required_start_speed;
extern ConVar sv_pk_slide_stop_speed;
extern ConVar sv_pk_slide_max_stop_speed;
extern ConVar sv_pk_slide_want_to_stop_decel;
extern ConVar sv_pk_slide_max_angle_dot;
extern ConVar sv_pk_slide_speed_boost;
extern ConVar sv_pk_slide_speed_boost_cap;
extern ConVar sv_pk_slide_boost_cooldown;
extern ConVar sv_pk_slide_jump_height;
extern ConVar sv_pk_slide_accel;
extern ConVar sv_pk_slide_decel;
extern ConVar sv_pk_slide_velocity_decay;
extern ConVar sv_pk_slide_step_velocity_reduction;
extern ConVar sv_pk_slide_fov_scale;
extern ConVar sv_pk_slide_fov_lerp_in_time;
extern ConVar sv_pk_slide_fov_lerp_out_time;
extern ConVar sv_pk_slide_viewtilt_increase_speed;
extern ConVar sv_pk_slide_viewtilt_decrease_speed;
extern ConVar sv_pk_slide_viewtilt_side;
extern ConVar sv_pk_slide_viewtilt_player_speed;
extern ConVar sv_pk_skip_time;
extern ConVar sv_pk_skip_speed_reduce;
extern ConVar sv_pk_skip_speed_retain;
extern ConVar sv_pk_skip_jump_height_fraction;

#endif // MOVEVARS_SHARED_H
