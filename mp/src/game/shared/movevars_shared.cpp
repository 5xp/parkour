//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//


#include "cbase.h"
#include "movevars_shared.h"
#include "mom_shareddefs.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

float GetCurrentGravity( void )
{
#if defined( TF_CLIENT_DLL ) || defined( TF_DLL )
	if ( TFGameRules() )
	{
		return ( sv_gravity.GetFloat() * TFGameRules()->GetGravityMultiplier() );
	}
#endif 
	return sv_gravity.GetFloat();
}

inline void UpdatePhysicsGravity(const float gravity)
{
    if (physenv)
        physenv->SetGravity(Vector(0,0,-gravity));
}

#ifdef CLIENT_DLL
class CGravityChange : public CGameEventListener, public CAutoGameSystem
{
public:
    bool Init() OVERRIDE
    {
        ListenForGameEvent("gravity_change");
        return true;
    }
    void FireGameEvent(IGameEvent *event) OVERRIDE
    {
        UpdatePhysicsGravity(event->GetFloat("newgravity"));
    }
};
static CGravityChange s_GravityChange;
#else
static void GravityChanged_Callback(IConVar *var, const char *pOldString, float)
{
    ConVarRef grav(var);
    UpdatePhysicsGravity(grav.GetFloat());
    if (gpGlobals->mapname != NULL_STRING)
    {
        IGameEvent *event = gameeventmanager->CreateEvent("gravity_change");
        if (event)
        {
            event->SetFloat("newgravity", grav.GetFloat());
            gameeventmanager->FireEvent(event);
        }
    }
}
#endif

ConVar	sv_gravity("sv_gravity", "800", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_MAPPING, "World gravity."
#ifdef GAME_DLL
    , GravityChanged_Callback
#endif
    );

#if defined( DOD_DLL ) || defined( CSTRIKE_DLL ) || defined( HL1MP_DLL )
ConVar	sv_stopspeed	( "sv_stopspeed","100", FCVAR_NOTIFY | FCVAR_REPLICATED, "Minimum stopping speed when on ground." );
#else
ConVar	sv_stopspeed	( "sv_stopspeed","75", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY, "Minimum stopping speed when on ground." );
#endif // DOD_DLL || CSTRIKE_DLL

ConVar	sv_noclipaccelerate( "sv_noclipaccelerate", "5", FCVAR_NOTIFY | FCVAR_ARCHIVE | FCVAR_REPLICATED);
ConVar	sv_noclipspeed	( "sv_noclipspeed", "14", FCVAR_ARCHIVE | FCVAR_NOTIFY | FCVAR_REPLICATED);
ConVar sv_noclipspeed_vertical("sv_noclipspeed_vertical", "7", FCVAR_NOTIFY | FCVAR_ARCHIVE | FCVAR_REPLICATED);
ConVar sv_noclipspeed_duck_multiplier("sv_noclipspeed_duck_multiplier", "0.3", FCVAR_NOTIFY | FCVAR_ARCHIVE | FCVAR_REPLICATED);
ConVar sv_noclipspeed_sprint_multiplier("sv_noclipspeed_sprint_multiplier", "0.5", FCVAR_NOTIFY | FCVAR_ARCHIVE | FCVAR_REPLICATED);
ConVar	sv_specaccelerate( "sv_specaccelerate", "5", FCVAR_NOTIFY | FCVAR_ARCHIVE | FCVAR_REPLICATED);
ConVar	sv_specspeed	( "sv_specspeed", "3", FCVAR_ARCHIVE | FCVAR_NOTIFY | FCVAR_REPLICATED);
ConVar	sv_specnoclip	( "sv_specnoclip", "1", FCVAR_ARCHIVE | FCVAR_NOTIFY | FCVAR_REPLICATED);

ConVar	sv_maxspeed		( "sv_maxspeed", "320",  FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY);

ConVar	sv_accelerate	( "sv_accelerate", "5", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_MAPPING);

ConVar  sv_airspeed_cap("sv_airspeed_cap", "30.0", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_MAPPING);
ConVar	sv_airaccelerate(  "sv_airaccelerate", "150", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_MAPPING );    
ConVar	sv_wateraccelerate(  "sv_wateraccelerate", "10", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_MAPPING);
ConVar	sv_waterfriction(  "sv_waterfriction", "1", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_MAPPING);
ConVar	sv_footsteps	( "sv_footsteps", "1", FCVAR_NOTIFY | FCVAR_REPLICATED, "Play footstep sound for players" );
ConVar	sv_rollspeed	( "sv_rollspeed", "200", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY);
ConVar	sv_rollangle	( "sv_rollangle", "0", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY, "Max view roll angle");

ConVar sv_swimsound("sv_swimsound", "1", FCVAR_NOTIFY | FCVAR_REPLICATED, "Play swim sound for players");

ConVar	sv_friction		( "sv_friction","4", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_MAPPING, "World friction." );

#if defined( CSTRIKE_DLL ) || defined( HL1MP_DLL )
ConVar	sv_bounce		( "sv_bounce","0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Bounce multiplier for when physically simulated objects collide with other objects." );
ConVar	sv_maxvelocity	( "sv_maxvelocity","3500", FCVAR_REPLICATED, "Maximum speed any ballistically moving object is allowed to attain per axis." );
ConVar	sv_stepsize		( "sv_stepsize","18", FCVAR_NOTIFY | FCVAR_REPLICATED );
ConVar	sv_backspeed	( "sv_backspeed", "0.6", FCVAR_ARCHIVE | FCVAR_REPLICATED, "How much to slow down backwards motion" );
ConVar  sv_waterdist	( "sv_waterdist","12", FCVAR_REPLICATED, "Vertical view fixup when eyes are near water plane." );
#else
ConVar	sv_bounce		( "sv_bounce","0", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_MAPPING, "Bounce multiplier for when physically simulated objects collide with other objects." );
ConVar	sv_maxvelocity	( "sv_maxvelocity","3500", FCVAR_REPLICATED | FCVAR_MAPPING, "Maximum speed any ballistically moving object is allowed to attain per axis." );
ConVar	sv_stepsize		( "sv_stepsize","18", FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY );
ConVar	sv_backspeed	( "sv_backspeed", "0.6", FCVAR_ARCHIVE | FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY, "How much to slow down backwards motion" );
ConVar  sv_waterdist	( "sv_waterdist","12", FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY, "Vertical view fixup when eyes are near water plane." );
#endif // CSTRIKE_DLL

ConVar	sv_skyname		( "sv_skyname", "sky_urb01", FCVAR_ARCHIVE | FCVAR_REPLICATED, "Current name of the skybox texture" );

// Momentum convars
MAKE_CONVAR(sv_considered_on_ground, "1.0", FCVAR_MAPPING, "Amount of units you have to be above the ground to be considered on ground.\n", 0.0f, 5.f);
MAKE_TOGGLE_CONVAR(sv_duck_collision_fix, "1", FCVAR_MAPPING, "Fixes headbugs by updating the collision box after duck code instead of at the end of the tick. 1 = ON, 0 = OFF.\n");
MAKE_TOGGLE_CONVAR(sv_ground_trigger_fix, "1", FCVAR_MAPPING, "Fixes being able to jump off the ground if grounded with a trigger under the player (bounces and jumpbugs). 1 = ON, 0 = OFF.\n");
MAKE_TOGGLE_CONVAR(sv_edge_fix, "1", FCVAR_MAPPING, "Makes edgebugs more consistent and allows for bunnyhopping instead of edgebugging. 1 = ON, 0 = OFF.\n");

MAKE_TOGGLE_CONVAR(mom_pk_autosprint_enable, "1", FCVAR_ARCHIVE | FCVAR_USERINFO, "Enable autosprint.");

ConVar
sv_pk_jump_height(
	"sv_pk_jump_height",
	"60",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Regular jump height");
ConVar
sv_pk_airjump_max(
	"sv_pk_airjump_max",
	"1",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum number of airjumps allowed.");
ConVar
sv_pk_jump_buffer_ticks(
	"sv_pk_jump_buffer_ticks",
	"0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Number of ticks to buffer jump input for ground and wall jumps.");
ConVar
sv_pk_airjump_height(
	"sv_pk_airjump_height",
	"60",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Base airjump height.");
ConVar
sv_pk_airjump_min_height_fraction(
	"sv_pk_airjump_min_height_fraction",
	"0.25",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Minimum proportion of jump height attained even if moving upwards quickly");
ConVar
sv_pk_airjump_horz_speed(
	"sv_pk_airjump_horz_speed",
	"180",
	FCVAR_NOTIFY | FCVAR_REPLICATED);
ConVar
sv_pk_lurch_periodmin(
	"sv_pk_lurch_periodmin",
	"0.2",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Lurch strength will begin tapering off after this many seconds after jumping.");
ConVar
sv_pk_lurch_periodmax(
	"sv_pk_lurch_periodmax",
	"0.5",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Lurch strength tapers to zero after this many seconds after jumping.");
ConVar
sv_pk_lurch_strength(
	"sv_pk_lurch_strength",
	"0.7",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Fraction of change towards the desired lurch direction.");
ConVar
sv_pk_lurch_max(
	"sv_pk_lurch_max",
	"0.7",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum amount of velocity change toward desired lurch target as a fraction of sprinting speed.");
ConVar 
sv_pk_gravity_scale(
	"sv_pk_gravity_scale", 
	"0.75",
	FCVAR_NOTIFY | FCVAR_REPLICATED,    
	"How affected the player is by gravity.");
ConVar 
sv_pk_slide_required_start_speed(
	"sv_pk_slide_required_start_speed", 
	"200.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,    
	"Required speed to start a slide.");
ConVar 
sv_pk_slide_stop_speed(
	"sv_pk_slide_stop_speed", 
	"125.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,    
	"Falling under this speed will end a slide.");
ConVar 
sv_pk_slide_max_stop_speed(
	"sv_pk_slide_max_stop_speed", 
	"350.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,    
	"Slide may not end while speed is above this.");
ConVar 
sv_pk_slide_want_to_stop_decel(
	"sv_pk_slide_want_to_stop_decel", 
	"350.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,    
	"Deceleration is replaced with this when trying to stand up during a slide when over sv_pk_slide_max_stop_speed.");
ConVar 
sv_pk_slide_max_angle_dot(
	"sv_pk_slide_max_angle_dot", 
	"0.6",
	FCVAR_NOTIFY | FCVAR_REPLICATED,    
	"Cosine of max angle from forward that you can slide when sprinting.");
ConVar
sv_pk_slide_speed_boost(
	"sv_pk_slide_speed_boost",
	"150.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Speed boost for powerslide.");
ConVar
sv_pk_slide_speed_boost_cap(
	"sv_pk_slide_speed_boost_cap",
	"400.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"");
ConVar
sv_pk_slide_boost_cooldown(
	"sv_pk_slide_boost_cooldown",
	"2.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Amount of time between slides until a boost is allowed.");
ConVar
sv_pk_slide_jump_height(
	"sv_pk_slide_jump_height",
	"50.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Jump height while sliding.");
ConVar
sv_pk_slide_accel(
	"sv_pk_slide_accel",
	"20.0", // TODO: figure out this value
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Acceleration from input while sliding.");
ConVar
sv_pk_slide_decel(
	"sv_pk_slide_decel",
	"50.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Linear amount of deceleration while sliding.");
ConVar
sv_pk_slide_velocity_decay(
	"sv_pk_slide_velocity_decay",
	"0.7",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Proportion of velocity kept per second while sliding.");
ConVar
sv_pk_slide_step_velocity_reduction(
	"sv_pk_slide_step_velocity_reduction",
	"10.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Amount of speed to lose per unit stepped");
ConVar
sv_pk_slide_fov_scale(
	"sv_pk_slide_fov_scale",
	"1.1",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Max FOV scale while sliding");
ConVar
sv_pk_slide_fov_lerp_in_time(
	"sv_pk_slide_fov_lerp_in_time",
	"0.25",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Duration over which FOV will be lerped to max while sliding");
ConVar
sv_pk_slide_fov_lerp_out_time(
	"sv_pk_slide_fov_lerp_out_time",
	"0.25",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Duration over which FOV will be lerped to normal after sliding");
ConVar
sv_pk_slide_viewtilt_increase_speed(
	"sv_pk_slide_viewtilt_increase_speed",
	"5",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Speed at which viewtilt while sliding increases to full.");
ConVar
sv_pk_slide_viewtilt_decrease_speed(
	"sv_pk_slide_viewtilt_decrease_speed",
	"2.5",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Speed at which viewtilt while sliding decreases to zero.");
ConVar
sv_pk_slide_viewtilt_side(
	"sv_pk_slide_viewtilt_side",
	"15",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Max view tilt while looking to the side while sliding.");
ConVar
sv_pk_slide_viewtilt_player_speed(
	"sv_pk_slide_viewtilt_player_speed",
	"400",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"View tilt while sliding is at full at this speed.");
ConVar
sv_pk_skip_time(
	"sv_pk_skip_time",
	"0.1",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Player is considered skipping if they jump within this number of seconds after landing");
ConVar
sv_pk_skip_speed_reduce(
	"sv_pk_skip_speed_reduce",
	"12",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Speed lost when skipping");
ConVar
sv_pk_skip_jump_height_fraction(
	"sv_pk_skip_jump_height_fraction",
	"0.75",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Fraction of jump height when skipping");
ConVar
sv_pk_skip_speed_retain(
	"sv_pk_skip_speed_retain",
	"450",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Skipping will not drop speed below this");
ConVar
sv_pk_wallrun_jump_upspeed(
	"sv_pk_wallrun_jump_upspeed",
	"230",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Upward speed achieved from walljumping.");
ConVar
sv_pk_wallrun_jump_outwardspeed(
	"sv_pk_wallrun_jump_outwardspeed",
	"205",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum speed away from the wall achieved from walljumping.");
ConVar
sv_pk_wallrun_jump_inputdirspeed(
	"sv_pk_wallrun_jump_inputdirspeed",
	"75",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Speed achieved in the direction of input from walljumping.");
ConVar
sv_pk_wallrun_timelimit(
	"sv_pk_wallrun_timelimit",
	"1.75",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Wallrun max duration.");
ConVar
sv_pk_wallrun_out_time(
	"sv_pk_wallrun_out_time",
	"0.5",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Start easing out of the wallrun for the last N seconds.");
ConVar
sv_pk_wallrun_upwallboost(
	"sv_pk_wallrun_upwallboost",
	"250",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"The maximum amount of vertical impulse applied when beginning a wallrun.");
ConVar
sv_pk_wallrun_friction(
	"sv_pk_wallrun_friction",
	"4",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Friction while wallrunning.");
ConVar
sv_pk_wallrun_maxspeed_horizontal(
	"sv_pk_wallrun_maxspeed_horizontal",
	"340",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum horizontal speed while wallrunning.");
ConVar
sv_pk_wallrun_maxspeed_horizontal_backwards(
	"sv_pk_wallrun_maxspeed_horizontal_backwards",
	"50",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum horizontal speed while wallrunning backwards.");
ConVar
sv_pk_wallrun_maxspeed_vertical(
	"sv_pk_wallrun_maxspeed_vertical",
	"225",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum vertical speed while wallrunning.");
ConVar
sv_pk_wallrun_accel_horizontal(
	"sv_pk_wallrun_accel_horizontal",
	"1400",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Horizontal wallrun acceleration.");
ConVar
sv_pk_wallrun_accel_vertical(
	"sv_pk_wallrun_accel_vertical",
	"360",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Vertical wallrun acceleration.");
ConVar
sv_pk_wallrun_gravity_rampuptime(
	"sv_pk_wallrun_gravity_rampuptime",
	"1",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Wallrun gravity ramps up to 0 to full in this amount of seconds after beginning a wallrun.");
ConVar
sv_pk_wallrun_fallawayspeed(
	"sv_pk_wallrun_fallawayspeed",
	"70",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Impulse away from the wall when falling off.");
ConVar
sv_pk_wallrun_pushaway_fallofftime(
	"sv_pk_wallrun_pushaway_fallofftime",
	"0.05",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Time pushing away from the wall before falling off.");
ConVar
sv_pk_wallrun_noinput_slipfrac(
	"sv_pk_wallrun_noinput_slipfrac",
	"0.7",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Minimum fraction of slip behavior when no input is applied.");
ConVar
sv_pk_wallrun_upwardautopush(
	"sv_pk_wallrun_upwardautopush",
	"0.65",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Amount of automatic upward input when moving forward on a wall.");
ConVar
sv_pk_wallrun_anglechange_mincos(
	"sv_pk_wallrun_anglechange_mincos",
	"0.8",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Cosine threshold for maximum wall angle change away from you before falling off.");
ConVar
sv_pk_wallrun_rotate_maxrate(
	"sv_pk_wallrun_rotate_maxrate",
	"3",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum wallrun rotation rate in radians per second.");
ConVar
sv_pk_wallrun_avoid_top_wall_decel(
	"sv_pk_wallrun_avoid_top_wall_decel",
	"3000",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Deceleration applied to avoid running off the top edge of a wall.");
ConVar
sv_pk_wallrun_samewall_dot(
	"sv_pk_wallrun_samewall_dot",
	"0.9",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Dot product threshold for treating a wall as the same wall.");
ConVar
sv_pk_wallrun_samewall_height(
	"sv_pk_wallrun_samewall_height",
	"0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum height above the last wallrun start position for reattaching.");
ConVar
sv_pk_wallrun_allowed_wall_dist(
	"sv_pk_wallrun_allowed_wall_dist",
	"13",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Allowed distance the feet may be from the wall.");
ConVar
sv_pk_wallrun_slip_starttime(
	"sv_pk_wallrun_slip_starttime",
	"2",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Time wallrunning before slipping begins.");
ConVar
sv_pk_wallrun_slip_duration(
	"sv_pk_wallrun_slip_duration",
	"1",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Time for wallrun slipping to reach full effect.");
ConVar
sv_pk_wallrun_viewtilt_predict_time(
	"sv_pk_wallrun_viewtilt_predict_time",
	"0.25",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Prediction before wallrun to start view tilt.");
ConVar
sv_pk_wallrun_viewtilt_speed(
	"sv_pk_wallrun_viewtilt_speed",
	"6.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Speed at which wallrun view tilt increases in radians/sec.");
ConVar
sv_pk_wallrun_viewtilt_max(
	"sv_pk_wallrun_viewtilt_max",
	"15.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum amount of view tilt in degrees while wallrunning.");
ConVar
sv_pk_wallrun_viewcorrect_yaw_offset(
	"sv_pk_wallrun_viewcorrect_yaw_offset",
	"10.0",
	FCVAR_REPLICATED,
	"Moving forward while wallrunning will smooth the player's view to a minimum of this many degrees away from the wall parallel.");
ConVar
sv_pk_wallrun_viewcorrect_yaw_speed(
	"sv_pk_wallrun_viewcorrect_yaw_speed",
	"86.0",
	FCVAR_REPLICATED,
	"Max yaw correction speed in degrees per second.");
ConVar
sv_pk_wallrun_viewcorrect_yaw_decay(
	"sv_pk_wallrun_viewcorrect_yaw_decay",
	"3.44",
	FCVAR_REPLICATED,
	"Yaw correction decay coefficient.");
ConVar
sv_pk_wallrun_viewcorrect_pitch_min(
	"sv_pk_wallrun_viewcorrect_pitch_min",
	"30.0",
	FCVAR_REPLICATED,
	"Lower bound for pitch correction in degrees above or below the horizon.");
ConVar
sv_pk_wallrun_viewcorrect_pitch_max(
	"sv_pk_wallrun_viewcorrect_pitch_max",
	"50.0",
	FCVAR_REPLICATED,
	"Upper bound for pitch correction in degrees above or below the horizon.");
ConVar
sv_pk_wallrun_viewcorrect_pitch_speed(
	"sv_pk_wallrun_viewcorrect_pitch_speed",
	"35.0",
	FCVAR_REPLICATED,
	"Pitch correction speed in degrees per second while wallrunning.");
ConVar
sv_pk_sprinttilt_accel(
	"sv_pk_sprinttilt_accel",
	"35.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Acceleration of sprint view tilt fraction.");
ConVar
sv_pk_sprinttilt_max_vel(
	"sv_pk_sprinttilt_max_vel",
	"2.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum speed of sprint view tilt.");
ConVar
sv_pk_sprinttilt_turn_range(
	"sv_pk_sprinttilt_turn_range",
	"120.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Max turn rate that creates view tilt when sprinting.");
ConVar
sv_pk_sprinttilt_max_roll(
	"sv_pk_sprinttilt_max_roll",
	"2.0",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Maximum view roll while sprinting.");
ConVar
sv_pk_coyote_time(
	"sv_pk_coyote_time",
	"0.2",
	FCVAR_NOTIFY | FCVAR_REPLICATED,
	"Time after leaving a surface that jumps are still allowed.");
ConVar
sv_pk_slide_lock(
	"sv_pk_slide_lock",
	"1",
	FCVAR_NOTIFY | FCVAR_REPLICATED | FCVAR_ARCHIVE,
	"Locks your move direction when sliding");
ConVar
sv_pk_viewpunch_fall_distmin(
	"sv_pk_viewpunch_fall_distmin",
	"10",
	FCVAR_NOTIFY | FCVAR_REPLICATED);
ConVar
sv_pk_viewpunch_fall_distmax(
	"sv_pk_viewpunch_fall_distmax",
	"70",
	FCVAR_NOTIFY | FCVAR_REPLICATED);
ConVar
sv_pk_viewpunch_fall_distmaxscale(
	"sv_pk_viewpunch_fall_distmaxscale",
	"12",
	FCVAR_NOTIFY | FCVAR_REPLICATED);