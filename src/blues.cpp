/**
 * @file blues.cpp
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Blues.IO NoteCard handler
 * @version 0.1
 * @date 2023-04-27
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "main.h"
#include <NoteTime.h>

#ifndef PRODUCT_UID
#define PRODUCT_UID "com.my-company.my-name:my-project"
#endif
#define myProductID PRODUCT_UID

/** Notecard instance */
Notecard notecard;

/** Callback for ATTN signal */
void blues_attn_cb(void);

/** JSON request to NoteCard */
J *req;

/** Buffer for response (if used beside of debug output) */
char blues_response[8192];

/** Buffer for request */
uint8_t fixed_req[8192] = {0};

volatile uint8_t *malloc_ptr = fixed_req;

volatile uint32_t used_malloc = 0;

/**
 * @brief Replace malloc with a fixed buffer address return
 * 
 * @param size unused
 * @return void* pointer to pre-allocated buffer
 */
void *fixed_alloc(size_t size)
{
	if (size >= 8192)
	{
		MYLOG("BLUES","Requested %d bytes", size);
		return (void *)NULL;
	}
	return (void *) fixed_req;
}

/**
 * @brief Replace free with a clean of the fixed buffer
 * 
 * @param ptr unused
 */
void fixed_free(void * ptr)
{
	memset(fixed_req,0,8182);
	return;
}

/** Flag to avoid multiple note requests sent to the NoteCard */
bool request_active = false;

/**
 * @brief Initialize Blues NoteCard
 *
 * @return true if NoteCard was found and setup was successful
 * @return false if NoteCard was not found or the setup failed
 */
bool init_blues(void)
{
	Wire.begin();

	notecard.begin();

	// NoteSetFn(fixed_alloc, fixed_free, noteDelay, noteMillis);

	// notecard.setDebugOutputStream(Serial);

	// Get the ProductUID from the saved settings
	// If no settings are found, use NoteCard internal settings!
	if (read_blues_settings())
	{
		MYLOG("BLUES", "Found saved settings, override NoteCard internal settings!");
		if (memcmp(g_blues_settings.product_uid, "com.my-company.my-name", 22) == 0)
		{
			MYLOG("BLUES", "No Product ID saved");
			AT_PRINTF(":EVT NO PUID");
			memcpy(g_blues_settings.product_uid, PRODUCT_UID, 33);
		}

		MYLOG("BLUES", "Set Product ID and connection mode");
		if (blues_start_req("hub.set"))
		{
			JAddStringToObject(req, "product", g_blues_settings.product_uid);
			if (g_blues_settings.conn_continous)
			{
				JAddStringToObject(req, "mode", "continuous");
			}
			else
			{
				JAddStringToObject(req, "mode", "minimum");
			}
			// Set sync time to 20 times the sensor read time
			JAddNumberToObject(req, "seconds", (g_lorawan_settings.send_repeat_time * 20 / 1000));
			JAddBoolToObject(req, "heartbeat", true);

			if (!blues_send_req())
			{
				MYLOG("BLUES", "hub.set request failed");
				return false;
			}
		}
		else
		{
			MYLOG("BLUES", "hub.set request failed");
			return false;
		}

#if USE_GNSS == 1
		MYLOG("BLUES", "Set location mode");
		if (blues_start_req("card.location.mode"))
		{
			// Continous GNSS mode
			// JAddStringToObject(req, "mode", "continous");

			// Periodic GNSS mode
			JAddStringToObject(req, "mode", "periodic");

			// Set location acquisition time to the sensor read time
			JAddNumberToObject(req, "seconds", (g_lorawan_settings.send_repeat_time / 2000));
			JAddBoolToObject(req, "heartbeat", true);
			if (!blues_send_req())
			{
				MYLOG("BLUES", "card.location.mode request failed");
				return false;
			}
		}
		else
		{
			MYLOG("BLUES", "card.location.mode request failed");
			return false;
		}
#else
		MYLOG("BLUES", "Stop location mode");
		if (blues_start_req("card.location.mode"))
		{
			// GNSS mode off
			JAddStringToObject(req, "mode", "off");
			if (!blues_send_req())
			{
				MYLOG("BLUES", "card.location.mode request failed");
				return false;
			}
		}
		else
		{
			MYLOG("BLUES", "card.location.mode request failed");
			return false;
		}
#endif

		/// \todo reset attn signal needs rework
		// pinMode(WB_IO5, INPUT);
		// if (g_blues_settings.motion_trigger)
		// {
		// 	if (blues_start_req("card.attn"))
		// 	{
		// 		JAddStringToObject(req, "mode", "disarm");
		// 		if (!blues_send_req())
		// 		{
		// 			MYLOG("BLUES", "card.attn request failed");
		// 		}

		// 		if (!blues_enable_attn())
		// 		{
		// 			return false;
		// 		}
		// 	}
		// }
		// else
		// {
		// 	MYLOG("BLUES", "card.attn request failed");
		// 	return false;
		// }

		MYLOG("BLUES", "Set APN");
		// {“req”:”card.wireless”}
		if (blues_start_req("card.wireless"))
		{
			JAddStringToObject(req, "mode", "auto");

			if (g_blues_settings.use_ext_sim)
			{
				// USING EXTERNAL SIM CARD
				JAddStringToObject(req, "apn", g_blues_settings.ext_sim_apn);
				JAddStringToObject(req, "method", "dual-secondary-primary");
			}
			else
			{
				// USING BLUES eSIM CARD
				JAddStringToObject(req, "method", "primary");
			}
			if (!blues_send_req())
			{
				MYLOG("BLUES", "card.wireless request failed");
				return false;
			}
		}
		else
		{
			MYLOG("BLUES", "card.wireless request failed");
			return false;
		}

#if IS_V2 == 1
		// Only for V2 cards, setup the WiFi network
		MYLOG("BLUES", "Set WiFi");
		if (blues_start_req("card.wifi"))
		{
			JAddStringToObject(req, "ssid", "-");
			JAddStringToObject(req, "password", "-");
			JAddStringToObject(req, "name", "RAK-");
			JAddStringToObject(req, "org", "RAK-PH");
			JAddBoolToObject(req, "start", false);

			if (!blues_send_req())
			{
				MYLOG("BLUES", "card.wifi request failed");
			}
		}
		else
		{
			MYLOG("BLUES", "card.wifi request failed");
			return false;
		}
#endif
	}

	// {"req": "card.version"}
	if (blues_start_req("card.version"))
	{
		if (!blues_send_req())
		{
			MYLOG("BLUES", "card.version request failed");
		}
	}
	return true;
}

/**
 * @brief Send a data packet to NoteHub.IO
 *
 * @param data Payload as byte array (CayenneLPP formatted)
 * @param data_len Length of payload
 * @return true if note could be sent to NoteCard
 * @return false if note send failed
 */
bool blues_send_payload(uint8_t *data, uint16_t data_len)
{
	if (blues_start_req("note.add"))
	{
		JAddStringToObject(req, "file", "data.qo");
		JAddBoolToObject(req, "sync", true);
		J *body = JCreateObject();
		if (body != NULL)
		{
			char node_id[24];
			sprintf(node_id, "%02x%02x%02x%02x%02x%02x%02x%02x",
					g_lorawan_settings.node_device_eui[0], g_lorawan_settings.node_device_eui[1],
					g_lorawan_settings.node_device_eui[2], g_lorawan_settings.node_device_eui[3],
					g_lorawan_settings.node_device_eui[4], g_lorawan_settings.node_device_eui[5],
					g_lorawan_settings.node_device_eui[6], g_lorawan_settings.node_device_eui[7]);
			JAddStringToObject(body, "dev_eui", node_id);

			JAddItemToObject(req, "body", body);

			JAddBinaryToObject(req, "payload", data, data_len);

			MYLOG("BLUES", "Finished parsing");
			if (!blues_send_req())
			{
				MYLOG("PABLUESRSE", "Send request failed");
				return false;
			}
			return true;
		}
		else
		{
			request_active = false;
			MYLOG("BLUES", "Error creating body");
		}
	}
	return false;
}

/**
 * @brief Create a request structure to be sent to the NoteCard
 *
 * @param request_name name of request, e.g. card.wireless
 * @return true if request could be created
 * @return false if request could not be created
 */
bool blues_start_req(String request_name)
{
	if (request_active)
	{
		MYLOG("BLUES", "A request already exists");
		return false;
	}

	req = notecard.newRequest(request_name.c_str());
	if (req != NULL)
	{
		request_active = true;
		return true;
	}
	MYLOG("BLUES", "Create request failed");

	return false;
}

/**
 * @brief Send a completed request to the NoteCard.
 *
 * @return true if request could be sent and the response does not have "err"
 * @return false if request could not be sent or the response did have "err"
 */
bool blues_send_req(void)
{
	char *json = JPrintUnformatted(req);
	MYLOG("BLUES", "Card request = %s", json);

	sprintf(blues_response, "Req failed");
	J *rsp;
	rsp = notecard.requestAndResponse(req);

	// Mark request as finished
	request_active = false;

	if (rsp == NULL)
	{
		request_active = false;
		return false;
	}
	// json = JPrintUnformatted(rsp);
	JPrintPreallocated(rsp, blues_response, 8192, false);

	MYLOG("BLUES", "Card response = %s", blues_response);

	if (strlen(blues_response) >= 8192)
	{
		MYLOG("BLUES", "Returned string of JPrintPreallocated is larger than buffer");
		request_active = false;
		notecard.deleteResponse(rsp);

		MYLOG("BLUES", "Restart WisBlock");
		api_reset();

		// notecard.begin();
		// NoteSetFnDefault(fixed_alloc, fixed_free, noteDelay, noteMillis);

		// blues_start_req("card.restart");
		// blues_send_req();

		 return false;
	}

	if (JIsPresent(rsp, "err"))
	{
		MYLOG("BLUES", "Card error response = %s", blues_response);
		char * error_type = JGetString(rsp, "err");
		notecard.deleteResponse(rsp);
		char *found_memory_fail = NULL;
		found_memory_fail = strstr(error_type,"insufficient");
		if (found_memory_fail)
		{
			MYLOG("BLUES", "Out of memory, restart WisBlock");
			api_reset();
			// notecard.begin();
			// NoteSetFnDefault(fixed_alloc, fixed_free, noteDelay, noteMillis);
		}
		request_active = false;
		return false;
	}
	// MYLOG("BLUES", "Card response = %s", blues_response);
	notecard.deleteResponse(rsp);
	request_active = false;
	return true;
}

/**
 * @brief Request NoteHub status, mainly for debug purposes
 *
 */
void blues_hub_status(void)
{
	if (blues_start_req("hub.status"))
	{
		blues_send_req();
	}
}

/**
 * @brief Get the location information from the NoteCard
 *
 * @return true if a location could be acquired
 * @return false if request failed or no location is available
 */
bool blues_get_location(void)
{
	bool result = false;
	if (blues_start_req("card.location"))
	{
		J *rsp;
		rsp = notecard.requestAndResponse(req);
		if (rsp == NULL)
		{
			MYLOG("BLUES", "card.location failed, report no location");
			request_active = false;
			return false;
		}
		char *json = JPrintUnformatted(rsp);
		MYLOG("BLUES", "Card response = %s", json);

		if (JIsPresent(rsp, "err"))
		{
			MYLOG("BLUES", "Card error response = %s", blues_response);
			char *error_type = JGetString(rsp, "err");
			notecard.deleteResponse(rsp);
			char *found_memory_fail = NULL;
			found_memory_fail = strstr(error_type, "insufficient");
			if (found_memory_fail)
			{
				MYLOG("BLUES", "Out of memory, restart WisBlock");
				api_reset();
				// notecard.begin();
				// NoteSetFnDefault(fixed_alloc, fixed_free, noteDelay, noteMillis);
			}
			request_active = false;
			return false;
		}
		if (JHasObjectItem(rsp, "lat") && JHasObjectItem(rsp, "lat"))
		{
			float blues_latitude = JGetNumber(rsp, "lat");
			float blues_longitude = JGetNumber(rsp, "lon");
			float blues_altitude = 0;

			if ((blues_latitude == 0.0) && (blues_longitude == 0.0))
			{
				MYLOG("BLUES", "No valid GPS data, report no location");
			}
			else
			{
				MYLOG("BLUES", "Got location Lat %.6f Long %0.6f", blues_latitude, blues_longitude);
				g_solution_data.addGNSS_6(LPP_CHANNEL_GPS, (uint32_t)(blues_latitude * 10000000), (uint32_t)(blues_longitude * 10000000), blues_altitude);
				result = true;
			}
		}

		notecard.deleteResponse(rsp);
		request_active = false;
	}
	else
	{
		MYLOG("BLUES", "card.location request failed");
	}

	if (!result)
	{
		// No GPS coordinates, get last tower location
		if (blues_start_req("card.time"))
		{
			J *rsp;
			rsp = notecard.requestAndResponse(req);
			if (rsp == NULL)
			{
				MYLOG("BLUES", "card.time failed, report no location");
				request_active = false;
				return false;
			}
			char *json = JPrintUnformatted(rsp);
			MYLOG("BLUES", "Card response = %s", json);

			if (JHasObjectItem(rsp, "lat") && JHasObjectItem(rsp, "lat"))
			{
				float blues_latitude = JGetNumber(rsp, "lat");
				float blues_longitude = JGetNumber(rsp, "lon");
				float blues_altitude = 0;

				if ((blues_latitude == 0.0) && (blues_longitude == 0.0))
				{
					MYLOG("BLUES", "No valid GPS data, report no location");
				}
				else
				{
					MYLOG("BLUES", "Got tower location Lat %.6f Long %0.6f", blues_latitude, blues_longitude);
					g_solution_data.addGNSS_6(LPP_CHANNEL_GPS, (uint32_t)(blues_latitude * 10000000), (uint32_t)(blues_longitude * 10000000), blues_altitude);
					result = true;
				}
			}

			notecard.deleteResponse(rsp);
			request_active = false;
		}
	}

	// Clear last GPS location
	if (blues_start_req("card.location.mode"))
	{
		JAddBoolToObject(req, "delete", true);
		J *rsp;
		rsp = notecard.requestAndResponse(req);
		if (rsp == NULL)
		{
			MYLOG("BLUES", "card.location.mode");
		}
		char *json = JPrintUnformatted(rsp);
		MYLOG("BLUES", "Card response = %s", json);
		notecard.deleteResponse(rsp);

		request_active = false;
	}
	return result;
}

/**
 * @brief Enable ATTN interrupt
 * 		At the moment enables only the alarm on motion
 *
 * @return true if ATTN could be enabled
 * @return false if ATTN could not be enabled
 */
bool blues_enable_attn(void)
{
	MYLOG("BLUES", "Enable ATTN on motion");
	if (blues_start_req("card.attn"))
	{
		JAddStringToObject(req, "mode", "motion");
		if (!blues_send_req())
		{
			MYLOG("BLUES", "card.attn request failed");
			return false;
		}
	}
	else
	{
		MYLOG("BLUES", "Request creation failed");
	}
	attachInterrupt(WB_IO5, blues_attn_cb, RISING);

	MYLOG("BLUES", "Arm ATTN on motion");
	if (blues_start_req("card.attn"))
	{
		JAddStringToObject(req, "mode", "arm");
		if (!blues_send_req())
		{
			MYLOG("BLUES", "card.attn request failed");
			return false;
		}
	}
	else
	{
		MYLOG("BLUES", "Request creation failed");
	}
	return true;
}

/**
 * @brief Disable ATTN interrupt
 *
 * @return true if ATTN could be disabled
 * @return false if ATTN could not be disabled
 */
bool blues_disable_attn(void)
{
	MYLOG("BLUES", "Disable ATTN on motion");
	detachInterrupt(WB_IO5);

	if (blues_start_req("card.attn"))
	{
		JAddStringToObject(req, "mode", "disarm");
		if (!blues_send_req())
		{
			MYLOG("BLUES", "card.attn request failed");
		}
	}
	else
	{
		MYLOG("BLUES", "Request creation failed");
	}
	if (blues_start_req("card.attn"))
	{
		JAddStringToObject(req, "mode", "-motion");
		if (!blues_send_req())
		{
			MYLOG("BLUES", "card.attn request failed");
		}
	}
	else
	{
		MYLOG("BLUES", "Request creation failed");
	}

	return true;
}

/**
 * @brief Get the reason for the ATTN interrup
 *  /// \todo work in progress
 * @return String reason /// \todo return value not final yet
 */
String blues_attn_reason(void)
{
	return "";
}

/**
 * @brief Callback for ATTN interrupt
 *       Wakes up the app_handler with an BLUES_ATTN event
 *
 */
void blues_attn_cb(void)
{
	api_wake_loop(BLUES_ATTN);
}