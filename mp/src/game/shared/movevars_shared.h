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
extern ConVar sv_pk_acceleration;
extern ConVar sv_pk_deceleration;
extern ConVar sv_pk_wallrun_jump_upspeed;
extern ConVar sv_pk_wallrun_jump_outwardspeed;
extern ConVar sv_pk_wallrun_jump_inputdirspeed;
extern ConVar sv_pk_wallrun_timelimit;
extern ConVar sv_pk_wallrun_out_time;
extern ConVar sv_pk_wallrun_upwallboost;
extern ConVar sv_pk_wallrun_friction;
extern ConVar sv_pk_wallrun_maxspeed_horizontal;
extern ConVar sv_pk_wallrun_maxspeed_horizontal_backwards;
extern ConVar sv_pk_wallrun_maxspeed_vertical;
extern ConVar sv_pk_wallrun_accel_horizontal;
extern ConVar sv_pk_wallrun_accel_vertical;
extern ConVar sv_pk_wallrun_gravity_rampuptime;
extern ConVar sv_pk_wallrun_fallawayspeed;
extern ConVar sv_pk_wallrun_pushaway_fallofftime;
extern ConVar sv_pk_wallrun_noinput_slipfrac;
extern ConVar sv_pk_wallrun_upwardautopush;
extern ConVar sv_pk_wallrun_anglechange_mincos;
extern ConVar sv_pk_wallrun_rotate_maxrate;
extern ConVar sv_pk_wallrun_avoid_top_wall_decel;
extern ConVar sv_pk_wallrun_samewall_dot;
extern ConVar sv_pk_wallrun_samewall_height;
extern ConVar sv_pk_wallrun_allowed_wall_dist;
extern ConVar sv_pk_wallrun_slip_starttime;
extern ConVar sv_pk_wallrun_slip_duration;
extern ConVar sv_pk_wallrun_viewtilt_predict_time;
extern ConVar sv_pk_wallrun_viewtilt_speed;
extern ConVar sv_pk_wallrun_viewtilt_max;
extern ConVar sv_pk_wallrun_viewcorrect_yaw_offset;
extern ConVar sv_pk_wallrun_viewcorrect_yaw_speed;
extern ConVar sv_pk_wallrun_viewcorrect_yaw_decay;
extern ConVar sv_pk_wallrun_viewcorrect_pitch_min;
extern ConVar sv_pk_wallrun_viewcorrect_pitch_max;
extern ConVar sv_pk_wallrun_viewcorrect_pitch_speed;
extern ConVar sv_pk_sprinttilt_accel;
extern ConVar sv_pk_sprinttilt_max_vel;
extern ConVar sv_pk_sprinttilt_turn_range;
extern ConVar sv_pk_sprinttilt_max_roll;
extern ConVar sv_pk_coyote_time;
extern ConVar sv_pk_gravity_scale;
extern ConVar sv_pk_jump_height;
extern ConVar sv_pk_airjump_max;
extern ConVar sv_pk_jump_buffer_ticks;
extern ConVar sv_pk_airjump_height;
extern ConVar sv_pk_airjump_min_height_fraction;
extern ConVar sv_pk_airjump_horz_speed;
extern ConVar sv_pk_lurch_periodmin;
extern ConVar sv_pk_lurch_periodmax;
extern ConVar sv_pk_lurch_strength;
extern ConVar sv_pk_lurch_max;
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
extern ConVar sv_pk_viewpunch_fall_distmin;
extern ConVar sv_pk_viewpunch_fall_distmax;
extern ConVar sv_pk_viewpunch_fall_distmaxscale;

#endif // MOVEVARS_SHARED_H
