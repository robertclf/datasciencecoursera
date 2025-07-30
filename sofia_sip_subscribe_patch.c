/* 
 * Sofia SIP SUBSCRIBE Handler Patch for Transfer Scenarios
 * This patch modifies mod_sofia to properly handle SUBSCRIBE messages
 * during transfer scenarios and respond with appropriate dialog states
 */

#include "mod_sofia.h"

/* Add this to sofia.c around the SUBSCRIBE handling section */

static switch_status_t sofia_handle_subscribe_dialog_transfer(nua_handle_t *nh, 
                                                             sofia_profile_t *profile,
                                                             sip_t const *sip,
                                                             sofia_gateway_t *gateway,
                                                             int sub_state)
{
    const char *call_id = NULL;
    const char *event_header = NULL;
    const char *expires_str = NULL;
    switch_core_session_t *session = NULL;
    switch_channel_t *channel = NULL;
    int expires = 3600;  /* Default expiry */
    char *dialog_xml = NULL;
    char *contact_str = NULL;
    sip_contact_t *contact = NULL;
    
    if (!sip || !sip->sip_call_id || !sip->sip_event) {
        return SWITCH_STATUS_FALSE;
    }
    
    call_id = sip->sip_call_id->i_id;
    event_header = sip->sip_event->o_type;
    
    /* Only handle dialog events */
    if (strcasecmp(event_header, "dialog") != 0) {
        return SWITCH_STATUS_FALSE;
    }
    
    /* Get expires value */
    if (sip->sip_expires && sip->sip_expires->ex_delta > 0) {
        expires = sip->sip_expires->ex_delta;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, 
                     "SUBSCRIBE for dialog event, Call-ID: %s, Expires: %d\n", 
                     call_id, expires);
    
    /* Find existing session for this call-id */
    if ((session = switch_core_session_locate(call_id))) {
        channel = switch_core_session_get_channel(session);
        
        /* Check if this is a transfer scenario */
        const char *transfer_state = switch_channel_get_variable(channel, "transfer_state");
        const char *transfer_start_time = switch_channel_get_variable(channel, "transfer_start_time");
        
        if (transfer_state && transfer_start_time) {
            switch_time_t start_time = atoll(transfer_start_time);
            switch_time_t current_time = switch_time_now();
            int elapsed_seconds = (int)((current_time - start_time) / 1000000);
            
            /* Build appropriate dialog-info XML based on transfer state and timing */
            if (elapsed_seconds < 200) {  /* Still within reasonable time window */
                if (strcasecmp(transfer_state, "ringing") == 0 || 
                    strcasecmp(transfer_state, "trying") == 0) {
                    
                    /* Send 200 OK with "trying" state indicating transfer in progress */
                    dialog_xml = switch_mprintf(
                        "<?xml version=\"1.0\"?>\n"
                        "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" "
                        "version=\"1\" state=\"full\" entity=\"sip:%s@%s\">\n"
                        "  <dialog id=\"%s\" call-id=\"%s\" direction=\"initiator\">\n"
                        "    <state>trying</state>\n"
                        "    <local>\n"
                        "      <identity>sip:%s@%s</identity>\n"
                        "    </local>\n"
                        "    <remote>\n"
                        "      <identity>sip:%s@%s</identity>\n"
                        "    </remote>\n"
                        "  </dialog>\n"
                        "</dialog-info>\n",
                        profile->username, profile->domain,
                        call_id, call_id,
                        profile->username, profile->domain,
                        switch_channel_get_variable(channel, "sip_to_user"),
                        switch_channel_get_variable(channel, "sip_to_host")
                    );
                    
                } else if (strcasecmp(transfer_state, "answered") == 0) {
                    
                    /* Target answered - send confirmed state */
                    dialog_xml = switch_mprintf(
                        "<?xml version=\"1.0\"?>\n"
                        "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" "
                        "version=\"1\" state=\"full\" entity=\"sip:%s@%s\">\n"
                        "  <dialog id=\"%s\" call-id=\"%s\" direction=\"initiator\">\n"
                        "    <state>confirmed</state>\n"
                        "    <local>\n"
                        "      <identity>sip:%s@%s</identity>\n"
                        "    </local>\n"
                        "    <remote>\n"
                        "      <identity>sip:%s@%s</identity>\n"
                        "    </remote>\n"
                        "  </dialog>\n"
                        "</dialog-info>\n",
                        profile->username, profile->domain,
                        call_id, call_id,
                        profile->username, profile->domain,
                        switch_channel_get_variable(channel, "sip_to_user"),
                        switch_channel_get_variable(channel, "sip_to_host")
                    );
                }
            } else {
                /* Transfer took too long - send terminated state */
                dialog_xml = switch_mprintf(
                    "<?xml version=\"1.0\"?>\n"
                    "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" "
                    "version=\"1\" state=\"full\" entity=\"sip:%s@%s\">\n"
                    "  <dialog id=\"%s\" call-id=\"%s\" direction=\"initiator\">\n"
                    "    <state>terminated</state>\n"
                    "    <local>\n"
                    "      <identity>sip:%s@%s</identity>\n"
                    "    </local>\n"
                    "    <remote>\n"
                    "      <identity>sip:%s@%s</identity>\n"
                    "    </remote>\n"
                    "  </dialog>\n"
                    "</dialog-info>\n",
                    profile->username, profile->domain,
                    call_id, call_id,
                    profile->username, profile->domain,
                    switch_channel_get_variable(channel, "sip_to_user"),
                    switch_channel_get_variable(channel, "sip_to_host")
                );
            }
        } else {
            /* No transfer in progress - provide standard dialog state */
            switch_channel_state_t state = switch_channel_get_state(channel);
            const char *dialog_state = "trying";
            
            switch (state) {
                case CS_RING:
                case CS_EARLY:
                    dialog_state = "early";
                    break;
                case CS_EXECUTE:
                case CS_CONSUME_MEDIA:
                    dialog_state = "confirmed";
                    break;
                case CS_HANGUP:
                case CS_DESTROY:
                    dialog_state = "terminated";
                    break;
                default:
                    dialog_state = "trying";
                    break;
            }
            
            dialog_xml = switch_mprintf(
                "<?xml version=\"1.0\"?>\n"
                "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" "
                "version=\"1\" state=\"full\" entity=\"sip:%s@%s\">\n"
                "  <dialog id=\"%s\" call-id=\"%s\" direction=\"initiator\">\n"
                "    <state>%s</state>\n"
                "  </dialog>\n"
                "</dialog-info>\n",
                profile->username, profile->domain,
                call_id, call_id,
                dialog_state
            );
        }
        
        switch_core_session_rwunlock(session);
    } else {
        /* No active session - send terminated state */
        dialog_xml = switch_mprintf(
            "<?xml version=\"1.0\"?>\n"
            "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" "
            "version=\"1\" state=\"full\" entity=\"sip:%s@%s\">\n"
            "  <dialog id=\"%s\" call-id=\"%s\" direction=\"initiator\">\n"
            "    <state>terminated</state>\n"
            "  </dialog>\n"
            "</dialog-info>\n",
            profile->username, profile->domain,
            call_id, call_id
        );
    }
    
    /* Build contact header */
    if (sip->sip_contact && sip->sip_contact->m_url) {
        contact_str = switch_mprintf("<%s>", url_as_string(profile->home, sip->sip_contact->m_url));
    } else {
        contact_str = switch_mprintf("<sip:%s@%s:%d>", profile->username, profile->extsipip, profile->sip_port);
    }
    
    /* Send 200 OK response with dialog-info XML */
    if (expires > 0) {
        nua_respond(nh, 200, "OK",
                   SIPTAG_EXPIRES_STR(switch_mprintf("%d", expires)),
                   SIPTAG_CONTACT_STR(contact_str),
                   SIPTAG_CONTENT_TYPE_STR("application/dialog-info+xml"),
                   SIPTAG_PAYLOAD_STR(dialog_xml),
                   TAG_END());
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                         "Sent 200 OK for SUBSCRIBE with dialog state, Call-ID: %s\n", call_id);
    } else {
        /* Expires = 0, terminate subscription */
        nua_respond(nh, 200, "OK",
                   SIPTAG_EXPIRES_STR("0"),
                   SIPTAG_CONTACT_STR(contact_str),
                   TAG_END());
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                         "Terminated SUBSCRIBE for Call-ID: %s\n", call_id);
    }
    
    /* Cleanup */
    switch_safe_free(dialog_xml);
    switch_safe_free(contact_str);
    
    return SWITCH_STATUS_SUCCESS;
}

/* Modify the existing SUBSCRIBE handler in sofia.c */
/* Find the nua_i_subscribe case and add this call before the existing logic: */

case nua_i_subscribe:
    /* Add this call at the beginning of the nua_i_subscribe case */
    if (sofia_handle_subscribe_dialog_transfer(nh, profile, sip, gateway, sub_state) == SWITCH_STATUS_SUCCESS) {
        goto done;  /* Skip default SUBSCRIBE handling */
    }
    /* Continue with existing nua_i_subscribe logic... */
    break;