/**
 * @brief Simple logger.
 *
 * Writes the output into file inside the current directory.
 */

// Windows stuff.

#ifdef _WIN32
#  define WINVER 0x0500
#  define _WIN32_WINNT 0x0500
#  include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <serial.h>

#include <unordered_map>

// SDK

#include "scssdk_telemetry.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"

#define UNUSED(x)

/**
 * @brief Logging support.
 */
FILE *log_file = NULL;

// FD for serial port
int serial_port = 0;

/**
 * @brief Tracking of paused state of the game.
 */
bool output_paused = true;

/**
 * @brief Should we print the data header next time
 * we are printing the data?
 */
bool print_header = true;

/**
 * @brief Last timestamp we received.
 */
scs_timestamp_t last_timestamp = static_cast<scs_timestamp_t>(-1);


std::unordered_map<std::string, std::string> metrics;


/**
 * @brief Combined telemetry data.
 */
struct telemetry_state_t
{
	scs_timestamp_t timestamp;
	scs_timestamp_t raw_rendering_timestamp;
	scs_timestamp_t raw_simulation_timestamp;
	scs_timestamp_t raw_paused_simulation_timestamp;

	float speed;
	float rpm;
	int gear;

	bool wipers;

	bool light_lblinker;
	bool light_rblinker;
	bool light_parking;
	bool light_beam_low;
	bool light_beam_high;
	bool light_aux_front;
	bool light_aux_roof;
	bool light_beacon;
	bool light_brake;
	bool light_reverse;

} telemetry;

/**
 * @brief Function writting message to the game internal log.
 */
scs_log_t game_log = NULL;

// Management of the log file.

bool init_log(void)
{
	if (log_file) {
		return true;
	}
	log_file = fopen("telemetry.csv", "wt");
	if (! log_file) {
		return false;
	}
	fprintf(log_file, "# Log opened\n");
	return true;
}

void finish_log(void)
{
	if (! log_file) {
		return;
	}
	fprintf(log_file, "# Log ended\n");
	fclose(log_file);
	log_file = NULL;
}

void log_print(const char *const text, ...)
{
	if (! log_file) {
		return;
	}
	va_list args;
	va_start(args, text);
	vfprintf(log_file, text, args);
	va_end(args);
}

void log_line(const char *const text, ...)
{
	if (! log_file) {
		return;
	}
	va_list args;
	va_start(args, text);
	vfprintf(log_file, text, args);
	fprintf(log_file, "\n");
	va_end(args);
}

// Handling of individual events.

SCSAPI_VOID telemetry_frame_start(const scs_event_t UNUSED(event), const void *const event_info, const scs_context_t UNUSED(context))
{
	const struct scs_telemetry_frame_start_t *const info = static_cast<const scs_telemetry_frame_start_t *>(event_info);

	// The following processing of the timestamps is done so the output
	// from this plugin has continuous time, it is not necessary otherwise.

	// When we just initialized itself, assume that the time started
	// just now.

	if (last_timestamp == static_cast<scs_timestamp_t>(-1)) {
		last_timestamp = info->paused_simulation_time;
	}

	// The timer might be sometimes restarted (e.g. after load) while
	// we want to provide continuous time on our output.

	if (info->flags & SCS_TELEMETRY_FRAME_START_FLAG_timer_restart) {
		last_timestamp = 0;
	}

	// Advance the timestamp by delta since last frame.

	telemetry.timestamp += (info->paused_simulation_time - last_timestamp);
	last_timestamp = info->paused_simulation_time;

	// The raw values.

	telemetry.raw_rendering_timestamp = info->render_time;
	telemetry.raw_simulation_timestamp = info->simulation_time;
	telemetry.raw_paused_simulation_timestamp = info->paused_simulation_time;
}

SCSAPI_VOID telemetry_frame_end(const scs_event_t UNUSED(event), const void *const UNUSED(event_info), const scs_context_t UNUSED(context))
{
	if (output_paused) {
		return;
	}
	fflush(log_file);
	tcdrain(serial_port);
}

SCSAPI_VOID telemetry_pause(const scs_event_t event, const void *const UNUSED(event_info), const scs_context_t UNUSED(context))
{
	output_paused = (event == SCS_TELEMETRY_EVENT_paused);
	if (output_paused) {
		log_line("# Telemetry paused");
	}
	else {
		log_line("# Telemetry unpaused");
	}
	print_header = true;
}

SCSAPI_VOID log_value(const scs_named_value_t *current) {
	switch (current->value.type) {
		case SCS_VALUE_TYPE_INVALID: {
			log_line("none");
			break;
		}
		case SCS_VALUE_TYPE_bool: {
			log_line("bool = %s", current->value.value_bool.value ? "true" : "false");
			break;
		}
		case SCS_VALUE_TYPE_s32: {
			log_line("s32 = %d", static_cast<int>(current->value.value_s32.value));
			break;
		}
		case SCS_VALUE_TYPE_u32: {
			log_line("u32 = %u", static_cast<unsigned>(current->value.value_u32.value));
				break;
		}
		case SCS_VALUE_TYPE_s64: {
			log_line("s64 = %" SCS_PF_S64, current->value.value_s64.value);
			break;
		}
		case SCS_VALUE_TYPE_u64: {
			log_line("u64 = %" SCS_PF_U64, current->value.value_u64.value);
			break;
		}
		case SCS_VALUE_TYPE_float: {
			log_line("float = %f", current->value.value_float.value);
			break;
		}
		case SCS_VALUE_TYPE_double: {
			log_line("double = %f", current->value.value_double.value);
			break;
		}
		case SCS_VALUE_TYPE_fvector: {
			log_line(
				"fvector = (%f,%f,%f)",
				current->value.value_fvector.x,
				current->value.value_fvector.y,
				current->value.value_fvector.z
			);
			break;
		}
		case SCS_VALUE_TYPE_dvector: {
			log_line(
				"dvector = (%f,%f,%f)",
				current->value.value_dvector.x,
				current->value.value_dvector.y,
				current->value.value_dvector.z
			);
			break;
		}
		case SCS_VALUE_TYPE_euler: {
			log_line(
				"euler = h:%f p:%f r:%f",
				current->value.value_euler.heading * 360.0f,
				current->value.value_euler.pitch * 360.0f,
				current->value.value_euler.roll * 360.0f
			);
			break;
		}
		case SCS_VALUE_TYPE_fplacement: {
			log_line(
				"fplacement = (%f,%f,%f) h:%f p:%f r:%f",
				current->value.value_fplacement.position.x,
				current->value.value_fplacement.position.y,
				current->value.value_fplacement.position.z,
				current->value.value_fplacement.orientation.heading * 360.0f,
				current->value.value_fplacement.orientation.pitch * 360.0f,
				current->value.value_fplacement.orientation.roll * 360.0f
			);
			break;
		}
		case SCS_VALUE_TYPE_dplacement: {
			log_line(
				"dplacement = (%f,%f,%f) h:%f p:%f r:%f",
				current->value.value_dplacement.position.x,
				current->value.value_dplacement.position.y,
				current->value.value_dplacement.position.z,
				current->value.value_dplacement.orientation.heading * 360.0f,
				current->value.value_dplacement.orientation.pitch * 360.0f,
				current->value.value_dplacement.orientation.roll * 360.0f
			);
			break;
		}
		case SCS_VALUE_TYPE_string: {
			log_line("string = %s", current->value.value_string.value);
			break;
		}
		default: {
			log_line("unknown");
			break;
		}
	}
}

SCSAPI_VOID telemetry_configuration(const scs_event_t event, const void *const event_info, const scs_context_t UNUSED(context))
{
	// Here we just print the configuration info.

	const struct scs_telemetry_configuration_t *const info = static_cast<const scs_telemetry_configuration_t *>(event_info);
	log_line("# Configuration: %s", info->id);

	for (const scs_named_value_t *current = info->attributes; current->name; ++current) {
		log_print("  %s", current->name);
		if (current->index != SCS_U32_NIL) {
			log_print("[%u]", static_cast<unsigned>(current->index));
		}
		log_print(" : ");
		log_value(current);
	}

	print_header = true;
}

// Handling of individual channels.
/*
SCSAPI_VOID telemetry_store_orientation(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value, const scs_context_t context)
{
	assert(context);
	telemetry_state_t *const state = static_cast<telemetry_state_t *>(context);

	// This callback was registered with the SCS_TELEMETRY_CHANNEL_FLAG_no_value flag
	// so it is called even when the value is not available.

	if (! value) {
		state->orientation_available = false;
		return;
	}

	assert(value);
	assert(value->type == SCS_VALUE_TYPE_euler);
	state->orientation_available = true;
	state->heading = value->value_euler.heading * 360.0f;
	state->pitch = value->value_euler.pitch * 360.0f;
	state->roll = value->value_euler.roll * 360.0f;
}
*/

void update_metric(const scs_string_t name, const scs_string_t value) {
	char s [30];
	int l;

	if (metrics.count(name)) {
		if (metrics[name] != value) {
			l = sprintf(s, "%s=%s\n", name, value);
			write(serial_port, s, l);
			log_print(s);
		}
	}
	metrics[name] = value;
}

SCSAPI_VOID telemetry_log(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value, const scs_context_t context)
{
	char s[30];

	switch (value->type) {
		case SCS_VALUE_TYPE_INVALID: {
			log_line("_");
			break;
		}
		case SCS_VALUE_TYPE_bool: {
			sprintf(s, "%d", value->value_bool.value);
			update_metric(name, s);
			break;
		}
		case SCS_VALUE_TYPE_s32: {
			sprintf(s, "%d", static_cast<int>(value->value_s32.value));
			update_metric(name, s);
			break;
		}
		case SCS_VALUE_TYPE_u32: {
			if (strcmp(name, "game.time") == 0) {
				const unsigned int v = static_cast<unsigned>(value->value_u32.value);
				sprintf(s, "%02d%02d", (v / 60) % 24, v % 60);
			} else {
				sprintf(s, "%u", static_cast<unsigned>(value->value_u32.value));
			};
			update_metric(name, s);
			break;
		}
		case SCS_VALUE_TYPE_u64: {
			sprintf(s, "%lu", value->value_u64.value);
			update_metric(name, s);
			break;
		}
		case SCS_VALUE_TYPE_float: {
			float v = value->value_float.value;
			sprintf(s, "%.0f", v);
			update_metric(name, s);
			break;
		}
		case SCS_VALUE_TYPE_double: {
			sprintf(s, "%.0f", value->value_double.value);
			update_metric(name, s);
			break;
		}
		case SCS_VALUE_TYPE_fvector: {
			log_line(
				"fvector %f,%f,%f",
				value->value_fvector.x,
				value->value_fvector.y,
				value->value_fvector.z
			);
			break;
		}
		case SCS_VALUE_TYPE_dvector: {
			log_line(
				"dvector %f,%f,%f",
				value->value_dvector.x,
				value->value_dvector.y,
				value->value_dvector.z
			);
			break;
		}
		case SCS_VALUE_TYPE_euler: {
			log_line(
				"euler h:%f,p:%f,r:%f",
				value->value_euler.heading * 360.0f,
				value->value_euler.pitch * 360.0f,
				value->value_euler.roll * 360.0f
			);
			break;
		}
		case SCS_VALUE_TYPE_fplacement: {
			log_line(
				"fplacement %f,%f,%f,%f,%f,%f",
				value->value_fplacement.position.x,
				value->value_fplacement.position.y,
				value->value_fplacement.position.z,
				value->value_fplacement.orientation.heading * 360.0f,
				value->value_fplacement.orientation.pitch * 360.0f,
				value->value_fplacement.orientation.roll * 360.0f
			);
			break;
		}
		case SCS_VALUE_TYPE_dplacement: {
			log_line(
				"dplacement %f,%f,%f,%f,%f,%f",
				value->value_dplacement.position.x,
				value->value_dplacement.position.y,
				value->value_dplacement.position.z,
				value->value_dplacement.orientation.heading * 360.0f,
				value->value_dplacement.orientation.pitch * 360.0f,
				value->value_dplacement.orientation.roll * 360.0f
			);
			break;
		}
		case SCS_VALUE_TYPE_string: {
			sprintf(s, "%s", value->value_string.value);
			update_metric(name, s);
			break;
		}
		default: {
			log_line("_");
			break;
		}
	}
}

/**
 * @brief Telemetry API initialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t *const params)
{
	// We currently support only one version.

	if (version != SCS_TELEMETRY_VERSION_1_00) {
		return SCS_RESULT_unsupported;
	}

	const scs_telemetry_init_params_v100_t *const version_params = static_cast<const scs_telemetry_init_params_v100_t *>(params);
	if (! init_log()) {
		version_params->common.log(SCS_LOG_TYPE_error, "Unable to initialize the log file");
		return SCS_RESULT_generic_error;
	}

	// Check application version. Note that this example uses fairly basic channels which are likely to be supported
	// by any future SCS trucking game however more advanced application might want to at least warn the user if there
	// is game or version they do not support.

	log_line("Game '%s' %u.%u", version_params->common.game_id, SCS_GET_MAJOR_VERSION(version_params->common.game_version), SCS_GET_MINOR_VERSION(version_params->common.game_version));

	if (strcmp(version_params->common.game_id, SCS_GAME_ID_EUT2) == 0) {

		// Bellow the minimum version there might be some missing features (only minor change) or
		// incompatible values (major change).

		const scs_u32_t MINIMAL_VERSION = SCS_TELEMETRY_EUT2_GAME_VERSION_1_00;
		if (version_params->common.game_version < MINIMAL_VERSION) {
			log_line("# WARNING: Too old version of the game, some features might behave incorrectly");
		}

		// Future versions are fine as long the major version is not changed.

		const scs_u32_t IMPLEMENTED_VERSION = SCS_TELEMETRY_EUT2_GAME_VERSION_CURRENT;
		if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION)) {
			log_line("# WARNING: Too new major version of the game, some features might behave incorrectly");
		}
	}
	else if (strcmp(version_params->common.game_id, SCS_GAME_ID_ATS) == 0) {

		// Bellow the minimum version there might be some missing features (only minor change) or
		// incompatible values (major change).

		const scs_u32_t MINIMAL_VERSION = SCS_TELEMETRY_ATS_GAME_VERSION_1_00;
		if (version_params->common.game_version < MINIMAL_VERSION) {
			log_line("# WARNING: Too old version of the game, some features might behave incorrectly");
		}

		// Future versions are fine as long the major version is not changed.

		const scs_u32_t IMPLEMENTED_VERSION = SCS_TELEMETRY_ATS_GAME_VERSION_CURRENT;
		if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION)) {
			log_line("WARNING: Too new major version of the game, some features might behave incorrectly");
		}
	}
	else {
		log_line("# WARNING: Unsupported game, some features or values might behave incorrectly");
	}

	// Register for events. Note that failure to register those basic events
	// likely indicates invalid usage of the api or some critical problem. As the
	// example requires all of them, we can not continue if the registration fails.

	const bool events_registered =
		//(version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_start, telemetry_frame_start, NULL) == SCS_RESULT_ok) &&
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_end, telemetry_frame_end, NULL) == SCS_RESULT_ok) &&
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_paused, telemetry_pause, NULL) == SCS_RESULT_ok) &&
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_started, telemetry_pause, NULL) == SCS_RESULT_ok)
	;
	if (! events_registered) {

		// Registrations created by unsuccessfull initialization are
		// cleared automatically so we can simply exit.

		version_params->common.log(SCS_LOG_TYPE_error, "# Unable to register event callbacks");
		return SCS_RESULT_generic_error;
	}

	// Register for the configuration info. As this example only prints the retrieved
	// data, it can operate even if that fails.

	version_params->register_for_event(SCS_TELEMETRY_EVENT_configuration, telemetry_configuration, NULL);

	// Register for channels. The channel might be missing if the game does not support
	// it (SCS_RESULT_not_found) or if does not support the requested type
	// (SCS_RESULT_unsupported_type). For purpose of this example we ignore the failues
	// so the unsupported channels will remain at theirs default value->

	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL, SCS_VALUE_TYPE_euler, SCS_TELEMETRY_CHANNEL_FLAG_no_value, telemetry_store_orientation, &telemetry);


    /* Floating point channels */
	//version_params->register_for_channel(SCS_TELEMETRY_CHANNEL_local_scale, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_input_steering, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_input_throttle, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_input_brake, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_input_clutch, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_effective_brake, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_effective_clutch, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_brake_air_pressure, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_brake_temperature, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_fuel, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_fuel_average_consumption, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_fuel_range, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_adblue, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_adblue_average_consumption, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_oil_pressure, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_oil_temperature, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_water_temperature, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_battery_voltage, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_dashboard_backlight, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_wear_engine, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_wear_transmission, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_wear_cabin, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_wear_chassis, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_wear_wheels, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_odometer, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_navigation_distance, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_navigation_time, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_navigation_speed_limit, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);


	/* Unsigned 32-bit integer channels */
	version_params->register_for_channel(SCS_TELEMETRY_CHANNEL_game_time, SCS_U32_NIL, SCS_VALUE_TYPE_u32, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_hshifter_slot, SCS_U32_NIL, SCS_VALUE_TYPE_u32, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_retarder_level, SCS_U32_NIL, SCS_VALUE_TYPE_u32, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_aux_front, SCS_U32_NIL, SCS_VALUE_TYPE_u32, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_aux_roof, SCS_U32_NIL, SCS_VALUE_TYPE_u32, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);


	/* Signed 32-bit integer channels */
	//version_params->register_for_channel(SCS_TELEMETRY_CHANNEL_next_rest_stop, SCS_U32_NIL, SCS_VALUE_TYPE_s32, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	//version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear, SCS_U32_NIL, SCS_VALUE_TYPE_s32, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_displayed_gear, SCS_U32_NIL, SCS_VALUE_TYPE_s32, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);


	/* Boolean channels */
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_parking_brake, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_motor_brake, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_brake_air_pressure_warning, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_brake_air_pressure_emergency, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_fuel_warning, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_adblue_warning, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_oil_pressure_warning        , SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_water_temperature_warning, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_battery_voltage_warning, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_electric_enabled, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_engine_enabled, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_lblinker, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_rblinker, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_lblinker, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_rblinker, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_parking, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_low_beam, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_high_beam, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_beacon, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_brake, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_light_reverse, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_wipers, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_log, NULL);


	// Remember the function we will use for logging.
	game_log = version_params->common.log;
	game_log(SCS_LOG_TYPE_message, "TELEMETRY: Initializing telemetry log example");

	// Set the structure with defaults.
	memset(&telemetry, 0, sizeof(telemetry));
	print_header = true;
	last_timestamp = static_cast<scs_timestamp_t>(-1);

	// Connect serial portname
	serial_port = serial_connect("/dev/ttyACM1");
	game_log(SCS_LOG_TYPE_message, "TELEMETRY: Serial port connected");

	// Initially the game is paused.
	output_paused = true;
	return SCS_RESULT_ok;
}

/**
 * @brief Telemetry API deinitialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_VOID scs_telemetry_shutdown(void)
{
	// Any cleanup needed. The registrations will be removed automatically
	// so there is no need to do that manually.

	game_log = NULL;
	finish_log();
}

// Cleanup

#ifdef _WIN32
BOOL APIENTRY DllMain(
	HMODULE module,
	DWORD  reason_for_call,
	LPVOID reseved
)
{
	if (reason_for_call == DLL_PROCESS_DETACH) {
		finish_log();
	}
	return TRUE;
}
#endif

#ifdef __linux__
void __attribute__ ((destructor)) unload(void)
{
	finish_log();
}
#endif
